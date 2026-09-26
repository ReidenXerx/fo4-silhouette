// Offline tests for everything in the plugin with no game in it: the catalog reader, OBody's rule
// priority as the runtime applies it (S-23), the bodies the plugin writes (variety S-17/S-21, never the
// shaft S-29), ORefit's keyword floors (S-40..S-42, S-48), the co-save bytes (S-43, S-47), and the whole
// director -- driven through a fake bridge that does to a fake LooksMenu exactly what Silhouette:Bridge
// does to the real one. The fake keeps LooksMenu's three kinds of layer (unkeyed, Silhouette's refit
// keyword, another mod's keyword) and shows the MAXIMUM over them per morph, as LooksMenu does, so each
// test checks what the woman would look like, not just the orders on the way. It can also stop an order
// half-way, the way a save that lands between two bridge calls does, lose an actor from memory, and keep
// one busy in another mod's scene.
//
//     build\Release\SilhouetteTests.exe                -> "N passed, 0 failed", exit code 0
//     build\Release\SilhouetteTests.exe --check <data> -> the generated files, read by the plugin's parser

#include "PCH.h"

#include "Catalog.h"
#include "Crosshair.h"
#include "Director.h"
#include "Plan.h"
#include "Presets.h"
#include "Registry.h"
#include "Rules.h"

#include <iostream>
#include <sstream>

namespace
{
	int g_failed = 0;
	int g_passed = 0;

	void Check(bool a_ok, std::string_view a_what)
	{
		if (a_ok) {
			++g_passed;
		} else {
			++g_failed;
			std::cout << "FAIL: " << a_what << "\n";
		}
	}

	constexpr auto kUrgent = SH::Lane::kUrgent;
	constexpr auto kNormal = SH::Lane::kNormal;
	constexpr auto kBackground = SH::Lane::kBackground;

	nlohmann::json BaseCatalog()
	{
		return nlohmann::json::parse(R"json({
			"schema": 1, "build": "abc123", "stamp": 1234, "mode": "absolute", "rulesHash": "r0",
			"states": {"female": ["VaginaPenetrate"], "male": ["Erection"]},
			"neverInBody": {"female": ["VaginaPenetrate"], "male": ["Erection", "Penis Width"]},
			"presets": [
				{"name": "Curvy", "sex": "female", "marker": "Silhouette_Curvy", "values": {"Breasts": 0.8, "Butt": 0.5},
				 "random": true, "menu": true, "zeroed": false, "fit": "full", "family": "CBBE"},
				{"name": "Slim", "sex": "female", "marker": "Silhouette_Slim", "values": {"Breasts": 0.2, "NippleSize": 0.9},
				 "random": true, "menu": true, "zeroed": false, "fit": "full", "family": "CBBE"},
				{"name": "Athletic", "sex": "female", "marker": "Silhouette_Athletic", "values": {"Waist": -0.3},
				 "random": true, "menu": true, "zeroed": false, "fit": "full", "family": "CBBE"},
				{"name": "BT - Average", "sex": "male", "marker": "Silhouette_BT_Average", "values": {"BTChest": 0.4},
				 "random": true, "menu": true, "zeroed": false, "fit": "full", "family": "BodyTalk"}
			],
			"player": {"female": "Slim", "male": "BT - Average"},
			"variety": {
				"female": [{"morph": "NippleSize", "low": 0.0, "high": 0.5, "group": "nipples"},
				           {"morph": "VaginaSize", "low": -0.3, "high": 0.3, "group": "genitals"}],
				"male": [{"morph": "BTBallSize", "low": 0.1, "high": 0.4, "group": "genitals"}]
			},
			"rules": {
				"races": ["HumanRace"],
				"npcFormID": {"female": [{"plugin": "Fallout4.esm", "id": 1000}], "male": []},
				"blacklistedNpcsFormID": [{"plugin": "Fallout4.esm", "id": 2000}],
				"blacklistedPlugins": {"female": ["Blocked.esp"], "male": []},
				"blacklistedRaces": {"female": ["GhoulRace"], "male": []},
				"npcName": [{"name": "Piper", "sex": "female", "presets": ["Curvy"]},
				            {"name": "Blocked Name Rule", "sex": "female", "presets": ["Slim"]}],
				"blacklistedNpcNames": ["Mama Murphy"],
				"faction": [{"plugin": "Fallout4.esm", "id": 3000, "editorID": "BoSFaction", "sex": "female", "presets": ["Athletic"]},
				            {"plugin": "Fallout4.esm", "id": 3001, "editorID": "RaiderFaction", "sex": "female", "presets": ["Slim", "Curvy"]}]
			},
			"orefit": {
				"slots": [33, 36, 41],
				"blacklist": [], "blacklistNames": ["Sheer Dress"], "blacklistPlugins": [],
				"force": [], "forceNames": [],
				"outfits": [{"name": "Vault 111 Jumpsuit", "sex": "female", "set": "Jumpsuit-Refit"}],
				"sets": [
					{"name": "builtin:female", "sex": "female", "floors": [
						{"morph": "BreastsTogether", "value": 0.3, "heavyOnly": false},
						{"morph": "PushUp", "value": 0.2, "heavyOnly": false},
						{"morph": "NipBGone", "value": 1.0, "heavyOnly": true}]},
					{"name": "Curvy-Refit", "sex": "female", "floors": [{"morph": "Breasts", "value": 0.95, "heavyOnly": false}]},
					{"name": "Jumpsuit-Refit", "sex": "female", "floors": [{"morph": "BreastsTogether", "value": 0.6, "heavyOnly": false}]}
				],
				"heavy": {"words": ["armor", "armour", "armored", "chest piece", "jacket", "coat"], "items": [], "names": ["Heavy Robe"]},
				"light": {"items": [], "names": ["Sexy Armor Bikini"]}
			}
		})json");
	}

	SH::ActorFacts Npc()
	{
		SH::ActorFacts a;
		a.female = true;
		a.baseName = "Somebody";
		a.bases = { { "Fallout4.esm", 0x12345 } };
		a.originPlugin = "Fallout4.esm";
		a.race = "HumanRace";
		a.seed = 0xFF000801;
		return a;
	}

	std::shared_ptr<const SH::Catalog> Cat(const nlohmann::json& a_doc)
	{
		std::string error;
		auto        c = SH::ParseCatalog(a_doc, error);
		if (!c) {
			Check(false, std::format("test catalog: {}", error));
			return nullptr;
		}
		// An older build's manifest (stamp 99): its bodies carried what no body may carry now.
		std::unordered_map<std::string, SH::ManifestEntry> old{
			{ "Silhouette_BT_Old", { "BT - Average", false, { "BTChest", "Penis Width", "Erection" } } },
			{ "Silhouette_Curvy", { "Curvy", true, { "Breasts", "Butt" } } },
		};
		c->AddManifest(99, std::move(old));
		return std::make_shared<const SH::Catalog>(std::move(*c));
	}

	// ------------------------------------------------------------------ catalog

	void TestCatalog()
	{
		std::string error;
		auto        c = SH::ParseCatalog(BaseCatalog(), error);
		Check(c.has_value(), std::format("the base catalog parses ({})", error));
		if (!c) {
			return;
		}
		Check(c->presets.size() == 4, "four presets");
		Check(c->Find("Curvy", true) != nullptr && c->Find("Curvy", false) == nullptr, "presets are per sex");
		Check(c->Find("cURVY", true) && c->Find("cURVY", true)->name == "Curvy", "a preset is found whatever the case of the name asked for");
		Check(c->FindByMarker("silhouette_slim") && c->FindByMarker("Silhouette_Slim")->name == "Slim", "marker -> preset, any case");
		Check(c->playerDefault[1] == "Slim" && c->playerDefault[0] == "BT - Average", "player defaults");
		Check(c->NeverInBody(false, "penis width") && !c->NeverInBody(true, "Penis Width"), "never-in-body is per sex");
		Check(SH::KindOf("Silhouette_Refit") == SH::MarkerKind::kRefit && SH::KindOf("Silhouette_Curvy") == SH::MarkerKind::kBody &&
				  SH::KindOf("silhouette_chosen") == SH::MarkerKind::kChoice && SH::KindOf("Breasts") == SH::MarkerKind::kNone,
			"marker kinds");
		Check(c->HeavyWord("Combat Armor Chest Piece") == "armor" && c->HeavyWord("Leather chest-piece") == "chest piece" &&
				  c->HeavyWord("Armored Vault Suit") == "armored" && c->HeavyWord("Minuteman Coat") == "coat",
			"heavy by a whole word or phrase of the name, any case, any separator (S-48)");
		Check(c->HeavyWord("Sexy Bra").empty() && c->HeavyWord("Armorsmith's Apron").empty() && c->HeavyWord("Coated Dress").empty() &&
				  c->HeavyWord("").empty(),
			"a word inside another word is not the word: bras, aprons and coated things stay light");

		struct Broken
		{
			std::function<void(nlohmann::json&)> edit;
			std::string_view                     what;
		};
		const std::vector<Broken> broken{
			{ [](auto& d) { d["rules"]["npcName"][0]["presets"] = { "Nobody" }; }, "a rule naming a preset the catalog lacks" },
			{ [](auto& d) { d["rules"]["faction"][0]["id"] = 0x01003000; }, "a form id with a load-order byte" },
			{ [](auto& d) { d["presets"][0]["sex"] = "Female"; }, "sex that is not exactly female or male" },
			{ [](auto& d) { d["schema"] = 2; }, "another schema" },
			{ [](auto& d) { d["stamp"] = 1 << 24; }, "a stamp a float32 cannot hold exactly" },
			{ [](auto& d) { d["presets"][0]["marker"] = "Curvy"; }, "a marker without the Silhouette_ prefix" },
			{ [](auto& d) { d["presets"][1]["marker"] = "Silhouette_curvy"; }, "two presets sharing a marker" },
			{ [](auto& d) { d["presets"][0]["marker"] = "Silhouette_Blacklisted"; }, "a preset using the reserved blacklist marker" },
			{ [](auto& d) { d["presets"][0]["marker"] = "Silhouette_Refit"; }, "a preset using the reserved refit marker" },
			{ [](auto& d) { d["presets"][0]["marker"] = "Silhouette_Chosen"; }, "a preset using the reserved choice marker (S-51)" },
			{ [](auto& d) { d["presets"][1]["name"] = "curvy"; }, "two presets whose names differ only in case" },
			{ [](auto& d) { d["variety"]["male"][0]["morph"] = "Penis Width"; }, "variety on a morph never part of a body (S-29)" },
			{ [](auto& d) { d["presets"][0]["values"]["Breasts"] = 1e39; }, "a value a float cannot hold" },
			{ [](auto& d) { d["presets"][0]["values"] = nlohmann::json::array({ 0.1, 0.2 }); }, "values given as a list" },
			{ [](auto& d) { d["rules"]["races"] = nullptr; }, "null where a list belongs" },
			{ [](auto& d) { d["orefit"]["sets"][1]["name"] = "BUILTIN:female"; }, "a refit set listed twice" },
			{ [](auto& d) { d["neverInBody"]["female"] = nlohmann::json::array(); }, "a runtime state missing from never-in-body" },
			{ [](auto& d) { d["presets"][3]["values"]["Penis Width"] = 1.0; }, "a preset carrying a shaft value (S-29)" },
			{ [](auto& d) { d["orefit"]["sets"][0]["floors"][0]["morph"] = "Silhouette_Curvy"; }, "a refit floor on a marker" },
			{ [](auto& d) { d["orefit"]["sets"][0]["floors"][0]["value"] = -0.2; }, "a floor below 0: a refit only raises (S-40)" },
			{ [](auto& d) { d["orefit"]["sets"][0]["floors"][0]["value"] = 0.0; }, "a floor of 0" },
			{ [](auto& d) { d["orefit"]["outfits"][0]["set"] = "Nope-Refit"; }, "an outfit naming a set that does not exist" },
			{ [](auto& d) { d.erase("rulesHash"); }, "no rules hash" },
			{ [](auto& d) { d["orefit"]["heavy"]["words"] = { "--" }; }, "a heavy word with no word in it" },
			{ [](auto& d) { d["orefit"]["heavy"].erase("words"); }, "no heavy words" },
			{ [](auto& d) { d["schema"] = 4294967297ull; }, "a schema that an int would wrap to 1 (wave 3, L4 F4)" },
			{ [](auto& d) { d["schema"] = true; }, "a schema that is not a number" },
			{ [](auto& d) { d["orefit"]["slots"][0] = 4294967329ull; }, "a slot that an int would wrap to 33" },
			{ [](auto& d) { d["orefit"]["slots"][0] = -3; }, "a negative slot" },
		};
		for (const auto& b : broken) {
			auto doc = BaseCatalog();
			b.edit(doc);
			Check(!SH::ParseCatalog(doc, error), std::format("refused: {}", b.what));
		}

		{
			std::istringstream both("# Silhouette - generated\r\n# 43 female, 8 male templates. Build 3198337fbaec, marker stamp 3250227 (absolute), rules 0a1b2c3d4e5f.\r\n");
			const auto h = SH::ParseFilesHeader(both);
			Check(h && h->build == "3198337fbaec" && h->stamp == 3250227 && h->rules == "0a1b2c3d4e5f", "a header names its build, stamp and rules");
			std::istringstream old("# 43 female, 8 male templates. Build 3198337fbaec, marker stamp 3250227 (absolute).\r\n");
			const auto o = SH::ParseFilesHeader(old);
			Check(o && o->stamp == 3250227 && o->rules.empty(), "an older header states no rules");
			std::istringstream none("# nothing here\n#\n");
			Check(!SH::ParseFilesHeader(none), "a header without a build is not one");
		}

		const auto manifest = nlohmann::json::parse(R"json({"stamp": 99, "templates": {
			"Silhouette_Old": {"preset": "Old Preset (v1)", "gender": "male", "values": {"BTChest": 1.0, "Penis Width": 1.0}}}})json");
		auto m = SH::ParseManifest(manifest, error);
		Check(m.has_value(), "a manifest parses");
		if (m) {
			c->AddManifest(m->first, m->second);
			Check(c->PresetForMarker("Silhouette_Old", 99) == "Old Preset (v1)", "an older build's marker names its preset");
			Check(c->PresetForMarker("SILHOUETTE_OLD", 99) == "Old Preset (v1)", "a marker is looked up whatever its case (the engine's string pool)");
			Check(!c->PresetForMarker("Silhouette_Old", 1234).has_value(), "a marker is read against ITS stamp only");
			Check(c->PresetForMarker("Silhouette_Curvy", 1234) == "Curvy", "the current build's markers fall back to the catalog");
			const auto heal = c->HealFor("silhouette_old", 99);
			Check(heal.size() == 1 && heal[0] == "Penis Width", "HealFor names exactly what that template held that no body may hold");
			Check(c->HealFor("Silhouette_Curvy", 1234).empty(), "this build's bodies need no heal");
		}
	}

	// ------------------------------------------------------------------ rules

	void TestRules()
	{
		std::string error;
		const auto  c = SH::ParseCatalog(BaseCatalog(), error);
		if (!c) {
			Check(false, "catalog for the rules");
			return;
		}
		auto a = Npc();
		Check(SH::Decide(*c, a).tier == SH::Tier::kNone && !SH::Decide(*c, a).blacklisted, "a plain NPC keeps BodyGen's roll");
		a.baseName = "piper";
		auto v = SH::Decide(*c, a);
		Check(v.tier == SH::Tier::kName && v.preset == "Curvy" && v.options == std::vector<std::string>{ "Curvy" }, "npc by name, case-insensitive, with its list");
		a = Npc();
		a.baseName = "Piper";
		a.female = false;
		Check(SH::Decide(*c, a).tier == SH::Tier::kNone, "a name rule is for its own sex only");
		a = Npc();
		a.baseName = "Mama Murphy";
		v = SH::Decide(*c, a);
		Check(v.tier == SH::Tier::kNameBlacklist && v.blacklisted, "name blacklist");
		a = Npc();
		a.baseName = "Mama Murphy";
		a.bases = { { "Fallout4.esm", 2000 } };
		v = SH::Decide(*c, a);
		Check(v.tier == SH::Tier::kNone && v.blacklisted, "form id blacklist is BodyGen's, outranks everything, and counts as blacklisted");
		a = Npc();
		a.baseName = "Piper";
		a.bases = { { "Fallout4.esm", 0x12345 }, { "fallout4.ESM", 1000 } };
		Check(SH::Decide(*c, a).tier == SH::Tier::kNone, "a per-NPC form id preset on a template outranks a name rule");
		a = Npc();
		a.factions = { { "Fallout4.esm", 3000 } };
		v = SH::Decide(*c, a);
		Check(v.tier == SH::Tier::kFaction && v.preset == "Athletic", "faction rule");
		Check(SH::Decide(*c, a, false).tier == SH::Tier::kFaction,
			"S-73: the switch leaves out only Silhouette's own pools: a rule the catalog does not mark as one stays");
		{
			auto doc = BaseCatalog();
			doc["rules"]["faction"][0]["pool"] = true;
			const auto pools = Cat(doc);
			Check(SH::Decide(*pools, a).tier == SH::Tier::kFaction, "S-72: a faction's own pool draws like any faction rule");
			v = SH::Decide(*pools, a, false);
			Check(v.tier == SH::Tier::kNone && !v.blacklisted,
				"S-73: faction bodies switched off, the faction is drawn from the random pool (BodyGen's roll)");
			auto raider = Npc();
			raider.factions = { { "Fallout4.esm", 3001 } };
			Check(SH::Decide(*pools, raider, false).tier == SH::Tier::kFaction, "... while another faction rule still applies");
		}
		a.baseName = "Piper";
		Check(SH::Decide(*c, a).preset == "Curvy", "a name rule outranks a faction rule");
		a = Npc();
		a.factions = { { "Fallout4.esm", 3000 } };
		a.originPlugin = "Blocked.esp";
		v = SH::Decide(*c, a);
		Check(v.tier == SH::Tier::kNone && v.blacklisted, "a plugin blacklist outranks a faction rule and counts as blacklisted");
		a.baseName = "Piper";
		v = SH::Decide(*c, a);
		Check(v.tier == SH::Tier::kName && !v.blacklisted, "a name rule outranks a plugin blacklist (OBody's order): not blacklisted");
		a = Npc();
		a.race = "GhoulRace";
		Check(SH::Decide(*c, a).tier == SH::Tier::kNone, "an undistributed race is never ours");
		a = Npc();
		a.factions = { { "Fallout4.esm", 3001 } };
		const auto first = SH::Decide(*c, a).preset;
		bool       same = true;
		for (int i = 0; i < 20; ++i) {
			same &= SH::Decide(*c, a).preset == first;
		}
		Check(same, "a rule with several presets draws the same one for the same person");
		Check(SH::Decide(*c, a).options.size() == 2, "the verdict carries every preset the rule lists (S-52)");
		std::set<std::string> drawn;
		for (std::uint32_t id = 0xFF000800; id < 0xFF000840; ++id) {
			a.seed = id;
			drawn.insert(SH::Decide(*c, a).preset);
		}
		Check(drawn.size() == 2, "a rule with several presets spreads them across people");
	}

	// ------------------------------------------------------------------ plans

	std::optional<float> Value(const SH::Morphs& a_m, std::string_view a_k)
	{
		const auto it = std::ranges::find_if(a_m, [&](const auto& p) { return p.first == a_k; });
		return it == a_m.end() ? std::optional<float>{} : std::optional<float>{ it->second };
	}

	void TestPlan()
	{
		const auto c = Cat(BaseCatalog());
		if (!c) {
			return;
		}
		const auto* curvy = c->Find("Curvy", true);
		const auto  a = SH::BodyFor(*c, *curvy, 0xFF000801, {});
		Check(a == SH::BodyFor(*c, *curvy, 0xFF000801, {}), "the same person gets the same body every time");
		Check(Value(a, "Breasts") == 0.8F && Value(a, "Butt") == 0.5F, "the preset's own values");
		Check(a.back().first == "Silhouette_Curvy" && a.back().second == 1234.0F, "the marker, last, holding the stamp");
		const auto nip = Value(a, "NippleSize");
		const auto vag = Value(a, "VaginaSize");
		Check(nip && *nip >= 0.0F && *nip < 0.5F && vag && *vag >= -0.3F && *vag < 0.3F, "every range drawn, inside its range");

		// S-21: a range REPLACES the preset's own value. Slim sets NippleSize 0.9, outside 0..0.5.
		const auto* slim = c->Find("Slim", true);
		const auto  s = SH::BodyFor(*c, *slim, 0xFF000801, {});
		Check(Value(s, "NippleSize") && *Value(s, "NippleSize") < 0.5F &&
				  *Value(s, "NippleSize") == SH::Draw(0xFF000801, "NippleSize", 0.0F, 0.5F),
			"a preset's own value for a ranged morph is replaced by the draw");
		const auto off = SH::BodyFor(*c, *slim, 0xFF000801, { .nipples = false, .genitals = true });
		Check(Value(off, "NippleSize") == 0.9F, "with nipple variety off, the preset's own value stays");

		// never the shaft (S-29), never a state (S-16), whatever the preset says
		auto withNever = *c;
		for (auto& p : withNever.presets) {
			if (p.name == "BT - Average") {
				p.values.emplace_back("Penis Width", 1.0F);
				p.values.emplace_back("Erection", 1.0F);
			}
		}
		const auto m = SH::BodyFor(withNever, *withNever.Find("BT - Average", false), 7, {});
		Check(!Value(m, "Penis Width") && !Value(m, "Erection") && Value(m, "BTBallSize"), "never the shaft, never a state; balls rolled");

		// keep: what they already hold, inside its range, stays; outside or absent, drawn
		std::unordered_map<std::string, float> keep{ { "NippleSize", 0.33F }, { "VaginaSize", 0.9F } };
		const auto k = SH::BodyFor(*c, *curvy, 0xFF000801, {}, &keep);
		Check(Value(k, "NippleSize") == 0.33F, "a rolled value in range is kept");
		Check(Value(k, "VaginaSize") == SH::Draw(0xFF000801, "VaginaSize", -0.3F, 0.3F), "a value out of range is drawn again");

		// top-up: only what is missing from her own layer
		const auto t = SH::TopUp(*c, true, 5, {}, { "Breasts", "nipplesize" });
		Check(t.size() == 1 && t[0].first == "VaginaSize", "top-up adds exactly the missing ranges (names compared in any case)");
		Check(SH::TopUp(*c, true, 5, { .nipples = true, .genitals = false }, { "Breasts" }).size() == 1, "top-up follows the switches");

		// refit floors, and the marker that names them (S-40, S-42, S-50)
		const auto* builtin = c->FindRefit("builtin:female", true);
		const auto  light = SH::RefitFloors(*builtin, false);
		const auto  heavy = SH::RefitFloors(*builtin, true);
		Check(!Value(light, "Silhouette_Refit") && !Value(light, "NipBGone") && Value(heavy, "NipBGone") == 1.0F && Value(light, "BreastsTogether") == 0.3F,
			"floors only, no marker; NipBGone under heavy clothes only");
		const auto ml = SH::RefitMarker(*builtin, false);
		const auto mh = SH::RefitMarker(*builtin, true);
		Check(ml >= 1.0F && ml < 16777216.0F && static_cast<int>(ml) % 2 == 1 && static_cast<int>(mh) % 2 == 0,
			"the refit marker fits a float exactly, odd for light clothes, even for heavy");
		Check(ml == SH::RefitMarker(*builtin, false), "the same set gives the same marker");
		auto changed = *builtin;
		changed.floors[0].value = 0.5F;
		Check(SH::RefitMarker(changed, false) != ml, "a set whose floors changed gives another marker: the old refit reads as stale");
		auto renamed = *builtin;
		renamed.name = "Female-Refit";
		Check(SH::RefitMarker(renamed, false) != ml, "another set with the same floors gives another marker");

		// the touch-up's key: what this build wants of a body
		const auto key = SH::TouchKey(*c, "Silhouette_BT_Old", 99, false, {});
		Check(key == SH::TouchKey(*c, "silhouette_bt_old", 99, false, {}), "the touch key ignores the marker's case");
		Check(key != SH::TouchKey(*c, "Silhouette_BT_Old", 99, false, { .nipples = true, .genitals = false }), "switching a range group changes the key");
		auto more = *c;
		more.variety[0].push_back({ "BTNippleSize", 0.0F, 0.5F, "nipples" });
		Check(key != SH::TouchKey(more, "Silhouette_BT_Old", 99, false, {}), "a build with a new range changes the key");
		auto healer = *c;
		healer.neverInBody[0].push_back("BTChest");
		Check(key != SH::TouchKey(healer, "Silhouette_BT_Old", 99, false, {}), "a build that heals something new changes the key");

		float lo = 1.0F;
		float hi = -1.0F;
		for (std::uint32_t id = 0xFF000800; id < 0xFF000900; ++id) {
			const auto v = SH::Draw(id, "VaginaSize", -0.3F, 0.3F);
			lo = std::min(lo, v);
			hi = std::max(hi, v);
		}
		Check(lo < -0.25F && hi > 0.25F, "draws spread across the whole range");
		Check(SH::BodyHash("Silhouette_Curvy", 1234) != SH::BodyHash("Silhouette_Curvy", 1235) &&
				  SH::BodyHash("silhouette_curvy", 1234) == SH::BodyHash("Silhouette_Curvy", 1234) && SH::BodyHash("x", 0) != 0,
			"the body hash tells builds apart, ignores case, and is never 0");
	}

	// ------------------------------------------------------------------ the co-save

	void TestRegistry()
	{
		SH::Registry r;
		auto&        a = r.Get(0x0001A4F2);
		a.base = 0x0002F1E5;
		a.source = SH::Source::kPicker;
		a.preset = "Curvy";
		a.stamp = 1234;
		a.announced = 77;
		a.touched = 88;
		r.Get(0xFF000900).announced = 5;
		r.Get(0x00000777).source = SH::Source::kReset;
		(void)r.Get(0x00000001);  // empty: never written
		r.Keep(SH::PickerSave{ .ref = 0x0001A4F2, .base = 0x0002F1E5, .female = true,
			.snapshot = { { "Breasts", 0.33F }, { "Silhouette_Slim", 1234.0F } }, .before = a });
		r.Keep(SH::PickerSave{ .ref = 0x00000888, .base = 0x00012345, .female = true, .snapshot = { { "Butt", 0.5F } } });

		const auto   bytes = r.Serialize([](std::uint32_t, std::uint32_t, bool) { return true; });
		std::vector<std::pair<std::uint32_t, std::uint32_t>> asked;
		(void)r.Serialize([&](std::uint32_t a_ref, std::uint32_t a_base, bool) {
			asked.emplace_back(a_ref, a_base);
			return true;
		});
		Check(std::ranges::find(asked, std::pair<std::uint32_t, std::uint32_t>{ 0x0001A4F2, 0x0002F1E5 }) != asked.end() &&
				  std::ranges::find(asked, std::pair<std::uint32_t, std::uint32_t>{ 0x00000888, 0x00012345 }) != asked.end(),
			"the keep predicate is told whose each record and picking is: a created id handed to someone else is not written");
		SH::Registry back;
		std::string  error;
		Check(back.Deserialize(bytes, SH::Registry::kVersion, [](std::uint32_t id) { return id; }, error) == SH::Registry::Loaded::kOk,
			std::format("records read back ({})", error));
		const auto* b = back.Find(0x0001A4F2);
		Check(b && b->base == 0x0002F1E5 && b->source == SH::Source::kPicker && b->preset == "Curvy" && b->stamp == 1234 &&
				  b->announced == 77 && b->touched == 88,
			"a record survives the co-save exactly");
		Check(back.Size() == 3 && back.Find(0x777) && back.Find(0x777)->source == SH::Source::kReset, "an empty record is not written; a reset is");
		Check(back.pickings.size() == 2 && back.pickings.at(0x0001A4F2).snapshot == r.pickings.at(0x0001A4F2).snapshot &&
				  back.pickings.at(0x0001A4F2).before && back.pickings.at(0x0001A4F2).before->preset == "Curvy",
			"every picking in progress survives the co-save, one per actor (S-47)");

		SH::Registry moved;
		Check(moved.Deserialize(bytes, SH::Registry::kVersion,
				  [](std::uint32_t id) -> std::uint32_t { return id == 0xFF000900 ? 0 : (id & 0x00FFFFFF) | 0x05000000; }, error) == SH::Registry::Loaded::kOk,
			"records read back with a new load order");
		Check(moved.Size() == 2 && moved.Find(0x0501A4F2) && moved.Find(0x0501A4F2)->base == 0x0502F1E5 && moved.pickings.contains(0x0501A4F2),
			"ids resolved, the gone one dropped");

		// A later version appends a field to a record: this one skips it and reads the rest.
		{
			auto grown = bytes;
			// the first record's u16 length sits right after the u32 count
			std::uint16_t length = 0;
			std::memcpy(&length, grown.data() + 4, 2);
			const auto longer = static_cast<std::uint16_t>(length + 3);
			std::memcpy(grown.data() + 4, &longer, 2);
			grown.insert(grown.begin() + 6 + length, { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } });
			SH::Registry later;
			Check(later.Deserialize(grown, SH::Registry::kVersion, nullptr, error) == SH::Registry::Loaded::kOk && later.Size() == 3,
				std::format("a record with fields this version does not know is read, the new fields skipped ({})", error));
		}
		// A source a later version added is nobody's choice here.
		{
			auto odd = bytes;
			// count(4) + length(2) + ref(4) + base(4): the source byte of the first record (0x00000777 sorts first)
			odd[4 + 2 + 4 + 4] = std::byte{ 42 };
			SH::Registry later;
			Check(later.Deserialize(odd, SH::Registry::kVersion, nullptr, error) == SH::Registry::Loaded::kOk && later.Find(0x777) &&
					  later.Find(0x777)->source == SH::Source::kNone,
				"an unknown source reads as none, the rest kept");
		}

		SH::Registry cut;
		cut.Get(42).announced = 1;
		std::vector<std::byte> shorter(bytes.begin(), bytes.end() - 3);
		Check(cut.Deserialize(shorter, SH::Registry::kVersion, nullptr, error) == SH::Registry::Loaded::kRefused && cut.Find(42),
			"cut-short bytes are refused, nothing replaced");
		Check(cut.Deserialize(bytes, 1, nullptr, error) == SH::Registry::Loaded::kRefused, "the never-released version 1 is refused");
		Check(cut.Deserialize(bytes, SH::Registry::kVersion + 1, nullptr, error) == SH::Registry::Loaded::kNewer && cut.Find(42),
			"a newer version is not read -- and says so, so its bytes are kept");
		const auto   kept = r.Serialize([](std::uint32_t id, std::uint32_t, bool) { return id != 0xFF000900; });
		SH::Registry k;
		Check(k.Deserialize(kept, SH::Registry::kVersion, nullptr, error) == SH::Registry::Loaded::kOk && k.Size() == 2,
			"a deleted reference's record is not saved");

		SH::Registry cap;
		for (std::uint32_t i = 1; i <= SH::Registry::kMaxPickings + 5; ++i) {
			cap.Keep(SH::PickerSave{ .ref = i });
		}
		Check(cap.pickings.size() == SH::Registry::kMaxPickings && !cap.pickings.contains(1) && cap.pickings.contains(SH::Registry::kMaxPickings + 5),
			"past the cap the oldest picking goes");
	}

	// ------------------------------------------------------------------ the fake game

	using Layer = std::map<std::string, float>;

	struct FakeActor
	{
		Layer                 unkeyed;
		Layer                 refit;  // Silhouette's keyword
		Layer                 other;  // another mod's keyword (AAF, the anatomy arousal layer)
		std::set<std::string> listed;  // names LooksMenu lists until a load, emptied ones too

		[[nodiscard]] float Effective(const std::string& a_morph) const
		{
			std::optional<float> best;
			for (const auto* l : { &unkeyed, &refit, &other }) {
				if (const auto it = l->find(a_morph); it != l->end()) {
					best = best ? std::max(*best, it->second) : it->second;
				}
			}
			return best.value_or(0.0F);
		}
	};

	struct FakeGame
	{
		std::unordered_map<std::uint32_t, FakeActor> actors;
		std::string                                  rollsTo = "Silhouette_Slim";  // what BodyGen's regenerate gives
		float                                        stamp = 1234.0F;
		std::set<std::uint32_t>                      away;    // not in memory: Game.GetForm gives None
		std::set<std::uint32_t>                      busy;    // in another mod's scene (AAF_ActorBusy)
		std::set<std::uint32_t>                      busyMidRoll;  // a scene starts while a roll reads the keyed values
		bool                                         keyword = true;  // Silhouette.esp's refit keyword resolves

		// A body as BodyGen writes it: a template's values, its ranges drawn, the marker.
		void Roll(std::uint32_t a_ref, const SH::Catalog& a_c, std::string_view a_marker, float a_stamp)
		{
			auto&       a = actors[a_ref];
			const auto* p = a_c.FindByMarker(a_marker);
			a.unkeyed.clear();
			if (p) {
				for (const auto& [m, v] : SH::BodyFor(a_c, *p, a_ref * 7 + 3, {})) {
					a.unkeyed[m] = v;
				}
			}
			a.unkeyed[std::string{ a_marker }] = a_stamp;
			for (const auto& [m, v] : a.unkeyed) {
				a.listed.insert(m);
			}
		}

		// A save loaded: LooksMenu drops what is empty; names listed are what is there.
		void Load()
		{
			for (auto& [ref, a] : actors) {
				a.listed.clear();
				for (const auto* l : { &a.unkeyed, &a.refit, &a.other }) {
					for (const auto& [m, v] : *l) {
						a.listed.insert(m);
					}
				}
			}
		}
	};

	// Exactly what Silhouette:Bridge does with one order (protocol 3). a_stopAfter: the writes that land
	// before a save cuts the order short (-1: all of them, and Done).
	void RunOrder(SH::Director& d, std::uint32_t a_id, FakeGame& g, int a_stopAfter = -1)
	{
		const auto o = d.Peek(a_id);
		const auto who = d.OrderActor(a_id);
		if (!o || who != o->ref) {
			return;
		}
		if (g.away.contains(who)) {
			d.Gone(a_id);
			return;
		}
		auto& a = g.actors[who];
		if (o->kind == SH::OrderKind::kTouch && g.busy.contains(who)) {
			d.Defer(a_id);  // a touch-up waits out the scene
			return;
		}
		if (o->regenerate) {
			if (g.busy.contains(who)) {
				d.Defer(a_id);
				return;
			}
			// The keyed values are read, a frame each: a scene can start meanwhile. Asked again before the roll.
			if (g.busyMidRoll.erase(who) != 0) {
				g.busy.insert(who);
			}
			if (d.OrderActor(a_id) != who) {
				return;
			}
			if (g.busy.contains(who)) {
				d.Defer(a_id);
				return;
			}
			// RegenerateMorphs clears every key; the bridge puts the keyed values back.
			const auto refit = a.refit;
			const auto other = a.other;
			a = {};
			a.refit = refit;
			a.other = other;
			a.unkeyed[g.rollsTo] = g.stamp;
			for (const auto* l : { &a.unkeyed, &a.refit, &a.other }) {
				for (const auto& [m, v] : *l) {
					a.listed.insert(m);
				}
			}
		}
		const auto unkeyed = [&](const std::string& a_m) {
			const auto it = a.unkeyed.find(a_m);
			return it == a.unkeyed.end() ? 0.0F : it->second;
		};
		if (o->probe || o->readAll) {
			for (const auto& name : a.listed) {
				auto kind = SH::MarkerKind::kNone;
				if (o->probe) {
					d.NoteName(a_id, name);
					kind = SH::KindOf(name);
				}
				if (kind == SH::MarkerKind::kRefit) {
					if (g.keyword) {
						d.NoteMarker(a_id, name, a.refit.contains(name) ? a.refit.at(name) : 0.0F);
					}
				} else if (kind == SH::MarkerKind::kBody || kind == SH::MarkerKind::kChoice || o->readAll) {
					const auto v = unkeyed(name);
					if (kind != SH::MarkerKind::kNone) {
						d.NoteMarker(a_id, name, v);
					}
					if (o->readAll && v != 0.0F) {
						d.NoteLayer(a_id, name, v);
					}
				}
			}
		}
		const auto n = d.ReadCount(a_id);
		for (std::int32_t i = 0; i < n && !d.ReadsDone(a_id); ++i) {
			d.NoteRead(a_id, i, unkeyed(d.ReadMorph(a_id, i)));
		}
		if (!d.Prepare(a_id)) {
			d.Done(a_id, false);
			return;
		}
		if (d.OrderActor(a_id) != who) {
			return;
		}
		const auto w = d.WriteCount(a_id);
		if (!g.keyword) {
			for (std::int32_t i = 0; i < w; ++i) {
				if (d.WriteLayer(a_id, i) == SH::Layer::kRefit) {
					d.Done(a_id, false);
					return;
				}
			}
		}
		if (d.ClearsUnkeyed(a_id)) {
			a.unkeyed.clear();
		}
		if (g.keyword && d.ClearsRefit(a_id)) {
			a.refit.clear();
		}
		for (std::int32_t i = 0; i < w; ++i) {
			if (a_stopAfter >= 0 && i >= a_stopAfter) {
				return;  // the save landed here: no Done, and the order is forgotten with the session
			}
			const auto m = d.WriteMorph(a_id, i);
			const auto v = d.WriteValue(a_id, i);
			auto&      layer = d.WriteLayer(a_id, i) == SH::Layer::kRefit ? a.refit : a.unkeyed;
			if (v == 0.0F) {
				layer.erase(m);  // LooksMenu: SetMorph(0) erases the entry
			} else {
				layer[m] = v;
			}
			a.listed.insert(m);
		}
		if (a_stopAfter >= 0) {
			return;
		}
		d.Done(a_id, true);
	}

	int Drain(SH::Director& d, FakeGame& g)
	{
		int n = 0;
		while (const auto id = d.NextOrder()) {
			RunOrder(d, id, g);
			if (++n > 1000) {
				Check(false, "the director keeps handing out orders: a loop");
				break;
			}
		}
		return n;
	}

	// The bridge raises each event and says so.
	std::vector<SH::Event> Events(SH::Director& d)
	{
		std::vector<SH::Event> out;
		while (const auto id = d.NextEvent()) {
			out.push_back(*d.EventAt(id));
			d.EventDone(id);
		}
		return out;
	}

	// One poll's RaiseEvents, exactly as Silhouette:Bridge does it: an actor not in memory (Game.GetForm
	// gives None) is skipped -- not raised, no EventDone -- and at most 64 are taken.
	std::vector<SH::Event> Poll(SH::Director& d, const FakeGame& g)
	{
		std::vector<SH::Event> out;
		int                    raised = 0;
		auto                   id = d.NextEvent();
		while (id != 0) {
			const auto e = d.EventAt(id);
			if (e && !g.away.contains(e->ref)) {
				out.push_back(*e);
				d.EventDone(id);
			}
			raised += 1;
			id = raised < 64 ? d.NextEvent() : 0;
		}
		return out;
	}

	bool Has(const std::vector<SH::Event>& a_events, SH::EventKind a_kind, std::string_view a_preset = {})
	{
		return std::ranges::any_of(a_events, [&](const SH::Event& e) { return e.kind == a_kind && (a_preset.empty() || e.preset == a_preset); });
	}

	std::size_t Count(const std::vector<SH::Event>& a_events, SH::EventKind a_kind)
	{
		return static_cast<std::size_t>(std::ranges::count_if(a_events, [&](const SH::Event& e) { return e.kind == a_kind; }));
	}

	SH::Sighting See(std::uint32_t a_ref, std::string a_name, bool a_clothed = false, bool a_heavy = false, std::string a_outfit = {})
	{
		SH::Sighting s;
		s.ref = a_ref;
		s.base = 0x00012345;
		s.facts = Npc();
		s.facts.baseName = std::move(a_name);
		s.facts.seed = a_ref;
		s.clothed = a_clothed;
		s.heavy = a_heavy;
		s.outfitSet = std::move(a_outfit);
		return s;
	}

	// A new session over the same saved game: the director's records go through the co-save bytes.
	void Reload(SH::Director& d, FakeGame& g, const nlohmann::json& a_doc = BaseCatalog())
	{
		const auto bytes = d.SaveRecords(nullptr);
		const auto reset = d.SaveReset();  // S-68: its own record, only once pressed
		d.ForgetWorld();
		d.RevertRecords();
		std::string error;
		Check(d.LoadRecords(bytes, SH::Registry::kVersion, [](std::uint32_t id) { return id; }, error) == SH::Registry::Loaded::kOk,
			std::format("reload ({})", error));
		if (!reset.empty()) {
			Check(d.LoadReset(reset, SH::Registry::kResetVersion, error) == SH::Registry::Loaded::kOk, std::format("reload the reset ({})", error));
		}
		d.SetCatalog(Cat(a_doc));
		g.Load();
	}

	// A save made while Silhouette.dll was not loaded: F4SE keeps no chunk of a plugin that is not there.
	void ReloadWithoutRecords(SH::Director& d, FakeGame& g, const nlohmann::json& a_doc = BaseCatalog())
	{
		d.ForgetWorld();
		d.RevertRecords();
		d.SetCatalog(Cat(a_doc));
		g.Load();
	}

	// ------------------------------------------------------------------ the director: bodies

	void TestBodies()
	{
		SH::Director d;
		FakeGame     g;
		const auto   cat = Cat(BaseCatalog());
		d.SetCatalog(cat);
		std::string why;

		// A plain NPC: BodyGen gave them Slim. One probe, one OnActorGenerated, nothing else.
		g.Roll(0x100, *cat, "Silhouette_Slim", 1234.0F);
		const auto before = g.actors[0x100].unkeyed;
		d.Seen(See(0x100, "Somebody"));
		Check(Drain(d, g) == 1, "a plain NPC with a whole body costs one probe");
		Check(Has(Events(d), SH::EventKind::kGenerated, "Slim"), "OnActorGenerated names BodyGen's preset");
		Check(g.actors[0x100].unkeyed == before, "a probe changes nothing");
		d.Seen(See(0x100, "Somebody"));
		Check(Drain(d, g) == 0, "seen again this session: nothing to do");
		Reload(d, g);
		d.Seen(See(0x100, "Somebody"));
		(void)Drain(d, g);
		Check(!Has(Events(d), SH::EventKind::kGenerated), "a new session probes again but announces a body only once");

		// An announcement handed out but not raised before a save is made again after the load.
		g.Roll(0x110, *cat, "Silhouette_Athletic", 1234.0F);
		d.Seen(See(0x110, "Somebody"));
		(void)Drain(d, g);
		Check(d.NextEvent() != 0, "(set-up) the event handed out; the save lands before the bridge raises it");
		Reload(d, g);
		d.Seen(See(0x110, "Somebody"));
		(void)Drain(d, g);
		Check(Has(Events(d), SH::EventKind::kGenerated, "Athletic"), "an announcement lost to a save is made again");

		// A name rule: decided once LooksMenu is read, the intent written before the body order runs.
		g.Roll(0x200, *cat, "Silhouette_Slim", 1234.0F);
		d.Seen(See(0x200, "Piper"));
		const auto probe = d.NextOrder();
		RunOrder(d, probe, g);
		const auto intent = d.RecordOf(0x200);
		Check(intent && intent->source == SH::Source::kNameRule && intent->preset == "Curvy", "the rule's intent is recorded before the bridge acts (S-43)");
		(void)Drain(d, g);
		Check(g.actors[0x200].unkeyed.contains("Silhouette_Curvy") && !g.actors[0x200].unkeyed.contains("Silhouette_Slim") &&
				  g.actors[0x200].unkeyed.at("Breasts") == 0.8F,
			"the name rule's preset replaced BodyGen's body, marker and all");
		auto ev = Events(d);
		Check(Has(ev, SH::EventKind::kGenerated, "Curvy") && !Has(ev, SH::EventKind::kGenerated, "Slim"),
			"OnActorGenerated for the rule's body, not for the one it replaced");
		d.Seen(See(0x200, "Piper"));
		Check(Drain(d, g) == 0, "a rule already applied is not applied again");

		// LooksMenu forgot her and BodyGen rolled again: reality differs from intent, the rule's body goes back.
		Reload(d, g);
		g.Roll(0x200, *cat, "Silhouette_Athletic", 1234.0F);
		d.Seen(See(0x200, "Piper"));
		(void)Drain(d, g);
		Check(g.actors[0x200].unkeyed.contains("Silhouette_Curvy"), "a body that is not the intended one is given again (S-43)");

		// An API choice accepted and saved before the bridge ran it survives the save.
		g.Roll(0x300, *cat, "Silhouette_Slim", 1234.0F);
		Check(d.RequestPreset(0x300, true, 0x00012345, "Athletic", SH::Source::kAPI, kNormal, why), "an API choice is accepted");
		Reload(d, g);
		d.Seen(See(0x300, "Somebody Else"));
		(void)Drain(d, g);
		Check(g.actors[0x300].unkeyed.contains("Silhouette_Athletic"), "a choice queued before a save lands after the load");
		Check(g.actors[0x300].unkeyed.contains("Silhouette_Chosen") && g.actors[0x300].unkeyed.at("Silhouette_Chosen") == 4.0F,
			"an API body carries the choice beside it (S-51)");

		// A save that cuts a body order short leaves half a body, its marker still "pending" (S-58); the load repairs it.
		Check(d.RequestPreset(0x310, true, 0x00012345, "Curvy", SH::Source::kPicker, kUrgent, why), "picked");
		g.Roll(0x310, *cat, "Silhouette_Slim", 1234.0F);
		const auto cut = d.NextOrder();
		RunOrder(d, cut, g, 1);  // the clear and one write, then the save
		Check((g.actors[0x310].unkeyed == Layer{ { "Silhouette_Curvy", 0.25F } }), "(set-up) the clear, then the marker first, as pending");
		Reload(d, g);
		d.Seen(See(0x310, "Somebody"));
		(void)Drain(d, g);
		auto  want = SH::BodyFor(*cat, *cat->Find("Curvy", true), 0x310, {});
		Layer whole(want.begin(), want.end());
		whole["Silhouette_Chosen"] = 3.0F;
		Check(g.actors[0x310].unkeyed == whole, "the next session gives the whole body, its choice marker beside it");

		// A new build re-gives a picked preset with its values; its own build leaves it be.
		auto next = BaseCatalog();
		next["stamp"] = 5678;
		Reload(d, g, next);
		d.Seen(See(0x310, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0x310].unkeyed.contains("Silhouette_Curvy") && g.actors[0x310].unkeyed.at("Silhouette_Curvy") == 5678.0F,
			"a new build re-gives the choice, stamped with the new build");

		// The re-give decided, then a save before it was written: the record has the new stamp, LooksMenu the old.
		auto third = BaseCatalog();
		third["stamp"] = 9999;
		Reload(d, g, third);
		d.Seen(See(0x310, "Somebody"));
		RunOrder(d, d.NextOrder(), g);  // the probe: the re-give is decided and recorded
		Check(d.RecordOf(0x310) && d.RecordOf(0x310)->stamp == 9999 && g.actors[0x310].unkeyed.at("Silhouette_Curvy") == 5678.0F,
			"(set-up) the record says 9999, the body is still 5678");
		Reload(d, g, third);
		d.Seen(See(0x310, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0x310].unkeyed.at("Silhouette_Curvy") == 9999.0F, "a re-give a save cut short is given at the next load");
		Reload(d, g);

		// Name blacklist: bare with the marker; kept bare after LooksMenu forgets; lifted, BodyGen rolls.
		g.Roll(0x400, *cat, "Silhouette_Slim", 1234.0F);
		d.Seen(See(0x400, "Mama Murphy"));
		(void)Drain(d, g);
		Check((g.actors[0x400].unkeyed == Layer{ { "Silhouette_Blacklisted", 1234.0F } }), "a name-blacklisted NPC is bare but for the blacklist marker");
		Check(!Has(Events(d), SH::EventKind::kGenerated), "no body, no OnActorGenerated");
		Reload(d, g);
		g.Roll(0x400, *cat, "Silhouette_Slim", 1234.0F);
		d.Seen(See(0x400, "Mama Murphy"));
		(void)Drain(d, g);
		Check((g.actors[0x400].unkeyed == Layer{ { "Silhouette_Blacklisted", 1234.0F } }), "blacklisted again after BodyGen rolled her behind our back");
		auto lifted = BaseCatalog();
		lifted["rules"]["blacklistedNpcNames"] = nlohmann::json::array();
		Reload(d, g, lifted);
		g.rollsTo = "Silhouette_Athletic";
		d.Seen(See(0x400, "Mama Murphy"));
		(void)Drain(d, g);
		Check(g.actors[0x400].unkeyed.contains("Silhouette_Athletic") && !g.actors[0x400].unkeyed.contains("Silhouette_Blacklisted"),
			"a lifted blacklist lets BodyGen roll them");
		Check(Has(Events(d), SH::EventKind::kGenerated, "Athletic"), "and their new body is announced");
		Reload(d, g);

		// Not ours: the player and the dummies, and races Silhouette does not distribute to.
		auto player = See(0x14, "Piper");
		player.eligible = false;
		g.Roll(0x14, *cat, "Silhouette_Slim", 1234.0F);
		g.actors[0x14].unkeyed.erase("VaginaSize");  // a default body lacking a range the build rolls
		const auto playerBody = g.actors[0x14].unkeyed;
		d.Seen(player);
		d.Dressed(player, false);
		auto ghoul = See(0x500, "Piper");
		ghoul.facts.race = "GhoulRace";
		d.Seen(ghoul);
		Check(Drain(d, g) == 0, "the player, the dummies and undistributed races are left alone");
		// ...and stay alone when ORefit is switched, though they are known this session (wave 2, L1 F1).
		d.Configure({ .orefit = false });
		d.Configure({ .orefit = true });
		d.SetSwitch(SH::Switch::kORefit, false);
		d.SetSwitch(SH::Switch::kORefit, true);
		Check(Drain(d, g) == 0 && g.actors[0x14].unkeyed == playerBody && !Has(Events(d), SH::EventKind::kGenerated),
			"switching ORefit probes nobody Silhouette never shapes: the player is never touched (S-45)");

		// A created reference's id reused by someone else: the record is forgotten.
		Check(d.RequestPreset(0xFF000A00, true, 0x00011111, "Curvy", SH::Source::kPicker, kUrgent, why), "picked");
		(void)Drain(d, g);
		auto stranger = See(0xFF000A00, "Somebody");
		stranger.base = 0x00022222;
		d.Seen(stranger);
		(void)Drain(d, g);
		Check(!d.RecordOf(0xFF000A00) || d.RecordOf(0xFF000A00)->source != SH::Source::kPicker, "a record for a reused id is dropped");
		// LooksMenu kept the old owner's morphs on the id: the body stays, the choice beside it goes.
		Check(g.actors[0xFF000A00].unkeyed.contains("Silhouette_Curvy") && !g.actors[0xFF000A00].unkeyed.contains("Silhouette_Chosen"),
			"a choice that came with a reused id is not rebuilt, and its marker is taken off");

		// A placed leveled NPC respawned: a new temporary base, the same person to LooksMenu. The choice stays.
		Check(d.RequestPreset(0x0A00, true, 0xFF00B000, "Curvy", SH::Source::kPicker, kUrgent, why), "picked");
		(void)Drain(d, g);
		auto respawned = See(0x0A00, "Somebody");
		respawned.base = 0xFF00B001;
		d.Seen(respawned);
		(void)Drain(d, g);
		Check(d.RecordOf(0x0A00) && d.RecordOf(0x0A00)->source == SH::Source::kPicker && g.actors[0x0A00].unkeyed.contains("Silhouette_Curvy"),
			"a placed reference keeps its record when its leveled base is replaced");

		Check(!d.RequestPreset(0x600, true, 1, "Nobody", SH::Source::kAPI, kNormal, why) && why.find("Nobody") != std::string::npos,
			"an unknown preset is refused with a reason");
		Check(!d.RequestPreset(0x600, false, 1, "Curvy", SH::Source::kAPI, kNormal, why), "a preset of the other sex is refused");
		Check(d.RequestPreset(0x601, true, 1, "cURVY", SH::Source::kAPI, kNormal, why) && d.AssignedPreset(0x601) == "Curvy",
			"a preset asked for in another case is the catalog's (the engine's string pool)");
		(void)Drain(d, g);

		// The latest decision wins while an actor waits; one order per actor at a time.
		(void)d.RequestPreset(0x700, true, 1, "Curvy", SH::Source::kAPI, kNormal, why);
		(void)d.RequestPreset(0x700, true, 1, "Athletic", SH::Source::kAPI, kNormal, why);
		const auto first = d.NextOrder();
		Check(d.Peek(first) && d.Peek(first)->ref == 0x700 && d.Peek(first)->body.preset == "Athletic", "two requests before the bridge came: the second wins");
		(void)d.RequestPreset(0x700, true, 1, "Slim", SH::Source::kAPI, kNormal, why);
		Check(d.NextOrder() == 0, "an actor with an order in flight gets no second one");
		Check(d.AssignedPreset(0x700) == "Slim", "AssignedPreset answers with the intent");
		RunOrder(d, first, g);
		(void)Drain(d, g);
		Check(g.actors[0x700].unkeyed.contains("Silhouette_Slim"), "and the last decision is the one that lands");

		// Regenerate: BodyGen's roll, keyed layers kept, announced even when it rolls the same preset.
		g.actors[0x700].other["Erection"] = 1.0F;
		g.rollsTo = "Silhouette_Slim";
		(void)Events(d);
		Check(d.RequestRegenerate(0x700, true, 1, kUrgent, why), "regenerate accepted");
		(void)Drain(d, g);
		Check(g.actors[0x700].unkeyed.contains("Silhouette_Slim") && g.actors[0x700].other.at("Erection") == 1.0F &&
				  Has(Events(d), SH::EventKind::kGenerated, "Slim"),
			"Regenerate: BodyGen's roll, another mod's keyed morph kept, announced though it is the same preset (S-46)");
		(void)d.RequestReapply(0x700, true, 1, "Slim", kNormal, why);
		(void)Drain(d, g);
		Check(Has(Events(d), SH::EventKind::kGenerated, "Slim"), "Reapply is announced too");
		Check(!d.RecordOf(0x700) || d.RecordOf(0x700)->preset.empty(), "reapplying a body BodyGen gave pins nobody's choice on it");

		// A stale order from another launch is nobody's.
		SH::Director other;
		other.SetCatalog(cat);
		Check(other.OrderActor(first) == 0 && other.Peek(first) == std::nullopt, "an order id from another launch names nothing");
		Check(d.NextOrder() == 0, "(idle)");

		// Lanes (S-55): the player's actions, then decisions and other mods, then probes and bulk work.
		for (std::uint32_t r = 0x800; r < 0x805; ++r) {
			g.Roll(r, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(r, "Somebody"));
		}
		(void)d.RequestRegenerate(0x806, true, 1, kBackground, why);  // "Give the people around me new bodies"
		(void)d.RequestPreset(0x807, true, 1, "Athletic", SH::Source::kAPI, kNormal, why);
		d.Dressed(See(0x808, "Somebody", true), false);  // first contact while dressing
		(void)d.RequestPreset(0x810, true, 1, "Curvy", SH::Source::kPicker, kUrgent, why);
		std::vector<std::uint32_t> order;
		while (const auto id = d.NextOrder()) {
			order.push_back(d.Peek(id)->ref);
			RunOrder(d, id, g);
			if (order.size() > 64) {
				break;
			}
		}
		Check(order.size() >= 3 && order[0] == 0x810, "the NPC page / picker goes first");
		Check(order.size() >= 3 && ((order[1] == 0x807 && order[2] == 0x808) || (order[1] == 0x808 && order[2] == 0x807)),
			"then other mods' calls and first contacts while dressing");
		Check(std::ranges::find(order, 0x806) > std::ranges::find(order, 0x808), "bulk work waits behind them with the probes");
	}

	// ------------------------------------------------------------------ the director: decisions of wave 2

	void TestDecisions()
	{
		std::string why;

		// S-52: a met NPC keeps the preset a rule drew while the rule still lists it.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			std::vector<std::uint32_t> raiders;
			for (std::uint32_t r = 0x900; r < 0x920; ++r) {
				auto s = See(r, "Raider");
				s.facts.factions = { { "Fallout4.esm", 3001 } };
				g.Roll(r, *cat, "Silhouette_Athletic", 1234.0F);
				d.Seen(s);
				raiders.push_back(r);
			}
			(void)Drain(d, g);
			std::map<std::uint32_t, std::string> drawn;
			for (const auto r : raiders) {
				drawn[r] = d.AssignedPreset(r);
			}
			auto more = BaseCatalog();
			more["rules"]["faction"][1]["presets"] = { "Slim", "Curvy", "Athletic" };
			Reload(d, g, more);
			for (const auto r : raiders) {
				auto s = See(r, "Raider");
				s.facts.factions = { { "Fallout4.esm", 3001 } };
				d.Seen(s);
			}
			(void)Drain(d, g);
			const auto kept = std::ranges::count_if(raiders, [&](std::uint32_t r) { return d.AssignedPreset(r) == drawn[r]; });
			Check(kept == static_cast<std::ptrdiff_t>(raiders.size()),
				std::format("adding a preset to a rule re-bodies nobody already met ({} of {} kept)", kept, raiders.size()));
			auto fewer = BaseCatalog();
			fewer["rules"]["faction"][1]["presets"] = { "Slim" };
			Reload(d, g, fewer);
			for (const auto r : raiders) {
				auto s = See(r, "Raider");
				s.facts.factions = { { "Fallout4.esm", 3001 } };
				d.Seen(s);
			}
			(void)Drain(d, g);
			Check(std::ranges::all_of(raiders, [&](std::uint32_t r) { return d.AssignedPreset(r) == "Slim" && g.actors[r].unkeyed.contains("Silhouette_Slim"); }),
				"a preset taken out of the rule is drawn again from what is left");
		}

		// S-51: a choice survives a save made while the plugin was not loaded.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			auto bos = See(0x930, "Scribe");
			bos.facts.factions = { { "Fallout4.esm", 3000 } };  // the faction rule gives Athletic
			g.Roll(0x930, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(bos);
			(void)Drain(d, g);
			Check(g.actors[0x930].unkeyed.contains("Silhouette_Athletic"), "(set-up) the faction rule's body");
			Check(d.RequestPreset(0x930, true, 0x00012345, "Curvy", SH::Source::kAPI, kNormal, why), "another mod chooses Curvy");
			(void)Drain(d, g);
			ReloadWithoutRecords(d, g);
			d.Seen(bos);
			(void)Drain(d, g);
			const auto rec = d.RecordOf(0x930);
			Check(g.actors[0x930].unkeyed.contains("Silhouette_Curvy") && rec && rec->source == SH::Source::kAPI && rec->preset == "Curvy",
				"the choice is rebuilt from LooksMenu: the faction rule does not take her back");

			// Without the marker (a body from before S-51) the rule wins, as it must.
			g.actors[0x930].unkeyed.erase("Silhouette_Chosen");
			ReloadWithoutRecords(d, g);
			d.Seen(bos);
			(void)Drain(d, g);
			Check(g.actors[0x930].unkeyed.contains("Silhouette_Athletic"), "no marker, no record: the rule decides");

			// A record the co-save kept but LooksMenu's marker missing: the marker is put back beside the body.
			Check(d.RequestPreset(0x931, true, 0x00012345, "Slim", SH::Source::kPicker, kUrgent, why), "picked");
			(void)Drain(d, g);
			g.actors[0x931].unkeyed.erase("Silhouette_Chosen");
			Reload(d, g);
			d.Seen(See(0x931, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0x931].unkeyed.contains("Silhouette_Chosen") && g.actors[0x931].unkeyed.at("Silhouette_Chosen") == 3.0F &&
					  g.actors[0x931].unkeyed.contains("Silhouette_Slim"),
				"a choice without its marker gets it, the body untouched");
		}

		// S-53: Reset means a new body at the next load, even when another mod keeps a morph on them.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0x940, *cat, "Silhouette_Slim", 1234.0F);
			g.actors[0x940].other["AnatomyArousal"] = 0.4F;
			d.Seen(See(0x940, "Somebody", true));
			(void)Drain(d, g);
			Check(d.RequestReset(0x940, true, 0x00012345, kNormal, why) && d.AssignedPreset(0x940).empty(), "Reset: the intent is cleared at once");
			(void)Drain(d, g);
			Check(g.actors[0x940].unkeyed.empty() && g.actors[0x940].refit.empty() && d.RecordOf(0x940) &&
					  d.RecordOf(0x940)->source == SH::Source::kReset,
				"bare now, no refit, and the reset remembered");
			std::string no;
			Check(!d.RequestAdopt(0x940, true, 0x00012345, no), "the regeneration window leaves a reset alone");
			Reload(d, g);  // LooksMenu keeps her map (the anatomy layer): BodyGen does not run
			g.rollsTo = "Silhouette_Athletic";
			d.Seen(See(0x940, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0x940].unkeyed.contains("Silhouette_Athletic") && g.actors[0x940].other.at("AnatomyArousal") == 0.4F &&
					  (!d.RecordOf(0x940) || d.RecordOf(0x940)->source == SH::Source::kNone),
				"the next load: still bare, so the plugin rolls her a body, the other mod's morph kept");

			g.Roll(0x941, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0x941, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestReset(0x941, true, 0x00012345, kNormal, why);
			(void)Drain(d, g);
			Reload(d, g);
			g.Roll(0x941, *cat, "Silhouette_Curvy", 1234.0F);  // LooksMenu dropped her empty map; BodyGen rolled her
			d.Seen(See(0x941, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0x941].unkeyed.contains("Silhouette_Curvy") && (!d.RecordOf(0x941) || d.RecordOf(0x941)->source == SH::Source::kNone),
				"BodyGen gave her a body at the load: the reset is over, her new body stays");
		}

		// The regeneration window (S-15) through the plugin.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.actors[0x950].other["AnatomyArousal"] = 0.2F;
			g.Load();
			Check(d.RequestAdopt(0x950, true, 0x00012345, why), "someone with only another mod's morph is adopted");
			(void)d.RequestPreset(0x951, true, 1, "Curvy", SH::Source::kAPI, kNormal, why);
			(void)Drain(d, g);
			Check(!d.RequestAdopt(0x951, true, 1, why), "not someone whose body another mod chose");
			(void)d.PickerStart(0x952, true, 1, "Cait");
			Check(!d.RequestAdopt(0x952, true, 1, why), "not someone being picked");
			(void)d.RequestPreset(0x953, true, 1, "Slim", SH::Source::kAPI, kNormal, why);
			Check(!d.RequestAdopt(0x953, true, 1, why), "not someone with a change on its way");
			(void)Drain(d, g);
			Check(g.actors[0x950].unkeyed.contains("Silhouette_Slim") && g.actors[0x950].other.at("AnatomyArousal") == 0.2F, "the adopted one rolled, the morph kept");
		}

		// Out of reach and busy: the work waits instead of being lost.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0x960, *cat, "Silhouette_Slim", 1234.0F);
			(void)d.RequestPreset(0x960, true, 0x00012345, "Curvy", SH::Source::kAPI, kNormal, why);
			g.away.insert(0x960);
			(void)Drain(d, g);
			Check(d.Pending() == 0 && g.actors[0x960].unkeyed.contains("Silhouette_Slim"), "an actor out of memory: the order waits, off the queue");
			g.away.erase(0x960);
			d.Seen(See(0x960, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0x960].unkeyed.contains("Silhouette_Curvy"), "seen again: the waiting change lands");

			// A roll has no intent to fall back on: only the parked order brings it (the Adopter's hand-off, L3 F7a).
			g.actors[0x962].other["AnatomyArousal"] = 0.2F;
			g.Load();
			g.away.insert(0x962);
			Check(d.RequestAdopt(0x962, true, 0x00012345, why), "(set-up) adopted");
			(void)Drain(d, g);
			g.away.erase(0x962);
			d.Seen(See(0x962, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0x962].unkeyed.contains("Silhouette_Slim") && g.actors[0x962].other.at("AnatomyArousal") == 0.2F,
				"a roll that could not reach them lands when they are seen again");

			auto now = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);
			d.SetClock([&] { return now; });
			g.Roll(0x961, *cat, "Silhouette_Slim", 1234.0F);
			g.actors[0x961].other["Erection"] = 1.0F;
			g.busy.insert(0x961);
			(void)d.RequestRegenerate(0x961, true, 0x00012345, kUrgent, why);
			Check(Drain(d, g) == 1 && d.Pending() == 0 && g.actors[0x961].unkeyed.contains("Silhouette_Slim"),
				"in another mod's scene: the roll is handed out once and waits, nothing captured -- the drain ends instead of spinning, "
				"and the bridge does not poll faster for it");
			{
				const auto told = d.NextNotice();
				Check(told.contains("waits until it ends") && d.NextNotice().empty(),
					std::format("S-71: the player is told the change they asked for waits ({})", told));
			}
			now += SH::Director::kDeferWait - std::chrono::seconds(1);
			Check(d.NextOrder() == 0, "not tried again before the wait is over");
			now += std::chrono::seconds(1);
			Check(d.Pending() == 1, "due again once the wait is over");
			Check(Drain(d, g) == 1 && d.Pending() == 0, "tried again after the wait, still busy: deferred again");
			Check(d.NextNotice().empty(), "... and told once, not at every try");
			g.busy.erase(0x961);
			g.rollsTo = "Silhouette_Athletic";
			now += SH::Director::kDeferWait;
			(void)Drain(d, g);
			Check(g.actors[0x961].unkeyed.contains("Silhouette_Athletic") && g.actors[0x961].other.at("Erection") == 1.0F, "the scene over: the roll lands");
			{
				const auto told = d.NextNotice();
				Check(told.contains("is done") && d.NextNotice().empty(), std::format("... and the player is told it is done ({})", told));
			}

			// A new decision made while a deferred one waits is tried at once.
			g.busy.insert(0x961);
			(void)d.RequestRegenerate(0x961, true, 0x00012345, kNormal, why);
			(void)Drain(d, g);
			g.busy.erase(0x961);
			Check(d.RequestPreset(0x961, true, 0x00012345, "Curvy", SH::Source::kAPI, kNormal, why) && Drain(d, g) == 1 &&
					  g.actors[0x961].unkeyed.contains("Silhouette_Curvy"),
				"a new decision replaces the deferred one and does not wait out its time");
			Check(d.NextNotice().empty(), "another mod's request that waits is not the player's to be told about");

			// Bulk work (Reset everyone's rolls, the regeneration window) waits without a word.
			g.busy.insert(0x961);
			(void)d.RequestRegenerate(0x961, true, 0x00012345, kBackground, why);
			(void)Drain(d, g);
			g.busy.erase(0x961);
			now += SH::Director::kDeferWait;
			(void)Drain(d, g);
			Check(d.NextNotice().empty(), "bulk work that waited for a scene is not said, before or after");

			// ... nor when something urgent the player did not ask for is queued beside it: undressing takes the
			// refit off in the urgent lane, and the actor's work takes the most urgent lane of what it holds.
			d.Seen(See(0x961, "Somebody", true));
			(void)Drain(d, g);
			Check(g.actors[0x961].refit.contains("Silhouette_Refit"), "(set-up) dressed: the refit is on");
			g.busy.insert(0x961);
			(void)d.RequestRegenerate(0x961, true, 0x00012345, kBackground, why);
			d.Seen(See(0x961, "Somebody", false));
			(void)Drain(d, g);
			(void)Drain(d, g);
			g.busy.erase(0x961);
			now += SH::Director::kDeferWait;
			(void)Drain(d, g);
			Check(d.NextNotice().empty(), "a bulk roll that shares its turn with an urgent refit is still not the player's to be told about");

			// The player's own change replaced by another mod's before it lands: no "done" for the other mod's change.
			g.busy.insert(0x961);
			(void)d.RequestRegenerate(0x961, true, 0x00012345, kUrgent, why);
			(void)Drain(d, g);
			Check(d.NextNotice().contains("waits until it ends"), "(set-up) the player's change waits");
			g.busy.erase(0x961);
			Check(d.RequestPreset(0x961, true, 0x00012345, "Slim", SH::Source::kAPI, kNormal, why), "(set-up) another mod's choice takes its place");
			(void)Drain(d, g);
			Check(d.NextNotice().empty(), "another mod's change landing is not the player's change being done");
		}

		// The summary line says what the bridge did.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			Check(d.TakeSummary().empty(), "nothing done, nothing waiting: no summary");
			g.Roll(0x970, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0x970, "Somebody"));
			(void)Drain(d, g);
			const auto line = d.TakeSummary();
			Check(line.find("1 probe(s)") != std::string::npos && d.TakeSummary().empty(), std::format("the summary counts and resets ({})", line));
		}
	}

	// ------------------------------------------------------------------ the director: ORefit

	void TestRefit()
	{
		SH::Director d;
		FakeGame     g;
		const auto   cat = Cat(BaseCatalog());
		d.SetCatalog(cat);
		std::string why;

		// Dressed with a body: floors under the keyword, her own layer untouched, the clothed shape her
		// body raised to the floors -- in sync by construction (S-40).
		g.Roll(0x100, *cat, "Silhouette_Slim", 1234.0F);
		g.actors[0x100].unkeyed["BreastsTogether"] = 0.1F;
		const auto naked = g.actors[0x100].unkeyed;
		d.Seen(See(0x100, "Somebody", true));
		(void)Drain(d, g);
		auto& a = g.actors[0x100];
		Check(a.unkeyed == naked, "the refit never touches her own layer");
		Check(a.Effective("BreastsTogether") == 0.3F && a.Effective("PushUp") == 0.2F && a.Effective("NipBGone") == 0.0F,
			"dressed lightly: breasts together and pushed up, nipples as they are");
		Check(a.refit.contains("Silhouette_Refit") && static_cast<int>(a.refit.at("Silhouette_Refit")) % 2 == 1 && d.RefitApplied(0x100),
			"the refit marker says a light refit is on (odd)");
		auto ev = Events(d);
		Check(Has(ev, SH::EventKind::kORefitChanged) && std::ranges::find_if(ev, [](auto& e) { return e.kind == SH::EventKind::kORefitChanged; })->flag,
			"OnORefitChanged(applied)");

		// Heavy clothes flatten the nipples; back to light, they come back.
		d.Dressed(See(0x100, "Somebody", true, true), false);
		(void)Drain(d, g);
		Check(a.Effective("NipBGone") == 1.0F && static_cast<int>(a.refit.at("Silhouette_Refit")) % 2 == 0,
			"under heavy clothes, nipples flat, and the marker even: another mod can see it (S-49, S-50)");
		d.Dressed(See(0x100, "Somebody", true, false), false);
		(void)Drain(d, g);
		Check(a.Effective("NipBGone") == 0.0F && a.Effective("BreastsTogether") == 0.3F, "under light clothes again, nipples back");

		// A body that grows above the floor shows through: dressed and undressed in sync.
		g.actors[0x100].unkeyed["BreastsTogether"] = 0.7F;
		Check(a.Effective("BreastsTogether") == 0.7F, "a body above the floor keeps its own value while dressed");
		g.actors[0x100].unkeyed["BreastsTogether"] = 0.1F;

		// Undressing takes the keyword layer off: exactly her body.
		d.Dressed(See(0x100, "Somebody", false), true);
		ev = Events(d);
		Check(Has(ev, SH::EventKind::kRemovingClothes) && Has(ev, SH::EventKind::kNaked), "OnActorRemovingClothes and OnActorNaked");
		(void)Drain(d, g);
		Check(a.refit.empty() && a.unkeyed == naked && a.Effective("BreastsTogether") == 0.1F, "naked: exactly her body, nothing of the refit left");

		// A preset's own refit, and an outfit's own set outranking both.
		g.Roll(0x200, *cat, "Silhouette_Curvy", 1234.0F);
		d.Seen(See(0x200, "Somebody", true));
		(void)Drain(d, g);
		Check(g.actors[0x200].Effective("Breasts") == 0.95F && !g.actors[0x200].refit.contains("BreastsTogether"), "Curvy-Refit, not the built-in set");
		d.Dressed(See(0x200, "Somebody", true, false, "Jumpsuit-Refit"), false);
		(void)Drain(d, g);
		Check(g.actors[0x200].Effective("BreastsTogether") == 0.6F && !g.actors[0x200].refit.contains("Breasts"), "another outfit's set replaces the refit layer");

		// Who is never refit (S-41) -- each with a body of her own, so only the rule can keep it off.
		const auto refitOf = [&](std::uint32_t ref) { return !g.actors[ref].refit.empty(); };
		g.Roll(0x300, *cat, "Silhouette_Slim", 1234.0F);
		d.Seen(See(0x300, "Mama Murphy", true));
		(void)Drain(d, g);
		Check(!refitOf(0x300), "a name-blacklisted woman stays bare, dressed or not");
		auto formBlack = See(0x301, "Heather", true);
		formBlack.facts.bases = { { "Fallout4.esm", 2000 } };
		g.actors[0x301].unkeyed = { { "Breasts", 0.6F }, { "Waist", 0.2F } };
		g.actors[0x301].listed = { "Breasts", "Waist" };
		d.Seen(formBlack);
		(void)Drain(d, g);
		Check(!refitOf(0x301), "a woman blacklisted by form id is not refit, though she has a body of her own");
		auto pluginBlack = See(0x309, "Heather", true);
		pluginBlack.facts.originPlugin = "Blocked.esp";
		g.actors[0x309].unkeyed = { { "Breasts", 0.6F } };
		g.actors[0x309].listed = { "Breasts" };
		d.Seen(pluginBlack);
		(void)Drain(d, g);
		Check(!refitOf(0x309), "a woman of a blacklisted plugin is not refit");
		g.actors[0x302].other["VaginaSize"] = 0.4F;  // only another mod's keyed morph
		g.Load();
		d.Seen(See(0x302, "Somebody", true));
		(void)Drain(d, g);
		Check(!refitOf(0x302), "a woman with only other mods' morphs has no body: not refit, the regeneration window can still roll her");
		d.Seen(See(0x303, "Somebody", true));
		(void)Drain(d, g);
		Check(!refitOf(0x303) && g.actors[0x303].listed.empty(), "a woman with nothing stored gets nothing stored: BodyGen can still roll her");
		g.Roll(0x304, *cat, "Silhouette_Slim", 1234.0F);
		d.Seen(See(0x304, "Somebody", true));
		(void)Drain(d, g);
		Check(refitOf(0x304), "(set-up) refit on");
		Check(d.RequestReset(0x304, true, 0x00012345, kNormal, why), "reset accepted");
		(void)Drain(d, g);
		d.Dressed(See(0x304, "Somebody", true), false);
		(void)Drain(d, g);
		Check(g.actors[0x304].unkeyed.empty() && g.actors[0x304].refit.empty(), "reset while dressed: bare, no refit, nothing to keep BodyGen away");

		// A custom follower: no Silhouette marker, a body of her own -- found past other mods' names (L1 F11).
		g.actors[0x305].other = { { "AnatomyArousal", 0.3F }, { "AnatomyWet", 0.1F }, { "AnatomyA", 0.1F }, { "AnatomyB", 0.1F },
			{ "AnatomyC", 0.1F }, { "AnatomyD", 0.1F } };
		g.actors[0x305].unkeyed = { { "Waist", 0.2F } };
		g.Load();
		d.Seen(See(0x305, "Heather", true));
		(void)Drain(d, g);
		Check(refitOf(0x305) && g.actors[0x305].Effective("BreastsTogether") == 0.3F,
			"a custom follower with her own body is refit, however many other mods' morphs are listed first");

		// A new body while dressed: the refit stays, and the clothed shape follows the new body.
		g.Roll(0x306, *cat, "Silhouette_Slim", 1234.0F);
		d.Seen(See(0x306, "Somebody", true));
		(void)Drain(d, g);
		Check(d.RequestPreset(0x306, true, 0x00012345, "Curvy", SH::Source::kPicker, kUrgent, why), "picked");
		(void)Drain(d, g);
		Check(g.actors[0x306].unkeyed.contains("Silhouette_Curvy") && g.actors[0x306].Effective("Breasts") == 0.95F,
			"a new body arrives under the refit it should have (Curvy-Refit)");

		// Men: no set, no refit.
		auto man = See(0x500, "Somebody", true);
		man.facts.female = false;
		g.Roll(0x500, *cat, "Silhouette_BT_Average", 1234.0F);
		d.Seen(man);
		(void)Drain(d, g);
		Check(!refitOf(0x500), "no male set: a clothed man is left as he is");

		// Switched off: everyone refit comes off at once; on: refit again.
		d.Configure({ .orefit = false });
		(void)Drain(d, g);
		Check(!refitOf(0x200) && !refitOf(0x305) && !refitOf(0x306), "ORefit off: every refit comes off");
		d.SetSwitch(SH::Switch::kORefit, true);
		(void)Drain(d, g);
		Check(refitOf(0x200) && refitOf(0x305), "ORefit on (through the API's switch): refit again");

		// A build that changes the floors reaches a woman who never undresses (wave 2, L6 F3).
		auto firmer = BaseCatalog();
		firmer["orefit"]["sets"][0]["floors"][0]["value"] = 0.5;
		firmer["stamp"] = 4321;
		Reload(d, g, firmer);
		d.Seen(See(0x305, "Heather", true));
		(void)Drain(d, g);
		Check(g.actors[0x305].Effective("BreastsTogether") == 0.5F, "new floors land on a still-dressed woman at the next sighting");
		Reload(d, g);

		// A save that cuts a refit short: the pending marker tells the next session.
		SH::Director e;
		FakeGame     h;
		e.SetCatalog(cat);
		h.Roll(0x100, *cat, "Silhouette_Slim", 1234.0F);
		const auto bare = h.actors[0x100].unkeyed;
		e.Seen(See(0x100, "Somebody", true));
		RunOrder(e, e.NextOrder(), h);  // the probe
		auto refitOrder = e.NextOrder();
		Check(e.Peek(refitOrder) && e.Peek(refitOrder)->kind == SH::OrderKind::kRefit, "(set-up) the refit order");
		RunOrder(e, refitOrder, h, 2);  // the pending marker and one floor, then the save
		Reload(e, h);
		e.Seen(See(0x100, "Somebody", false));  // she undressed in the loaded save
		(void)Drain(e, h);
		Check(h.actors[0x100].refit.empty() && h.actors[0x100].unkeyed == bare, "an unfinished refit found naked after a load comes off entirely");
		e.Seen(See(0x100, "Somebody", true));
		(void)Drain(e, h);
		Reload(e, h);
		e.Seen(See(0x100, "Somebody", true));
		RunOrder(e, e.NextOrder(), h);  // the probe: the refit is whole
		Check(e.NextOrder() == 0, "(set-up) a whole refit found dressed needs nothing");
		Reload(e, h);
		h.actors[0x100].refit.clear();
		e.Seen(See(0x100, "Somebody", true));
		RunOrder(e, e.NextOrder(), h);
		refitOrder = e.NextOrder();
		RunOrder(e, refitOrder, h, 1);  // only the pending marker lands, then the save
		Check(h.actors[0x100].refit.size() == 1 && h.actors[0x100].refit.at("Silhouette_Refit") == 0.25F, "(set-up) the marker is written first, as pending");
		Reload(e, h);
		e.Seen(See(0x100, "Somebody", true));
		(void)Drain(e, h);
		Check(h.actors[0x100].Effective("PushUp") == 0.2F && h.actors[0x100].refit.at("Silhouette_Refit") >= 1.0F,
			"an unfinished refit found dressed is written whole");

		// Silhouette.esp removed: LooksMenu drops the keyword's values at load. Put back when dressed.
		Reload(e, h);
		h.actors[0x100].refit.clear();
		e.Seen(See(0x100, "Somebody", true));
		(void)Drain(e, h);
		Check(h.actors[0x100].Effective("BreastsTogether") == 0.3F, "a refit LooksMenu dropped is put back on the next sighting");

		// Without the keyword (an older Silhouette.esp) nothing of a refit reaches her own layer.
		SH::Director f;
		FakeGame     k;
		k.keyword = false;
		f.SetCatalog(cat);
		k.Roll(0x100, *cat, "Silhouette_Slim", 1234.0F);
		const auto own = k.actors[0x100].unkeyed;
		f.Seen(See(0x100, "Somebody", true));
		(void)Drain(f, k);
		Check(k.actors[0x100].unkeyed == own && k.actors[0x100].refit.empty(), "no refit keyword: the refit is refused, her body untouched");
	}

	// ------------------------------------------------------------------ the director: touch-up

	void TestTouchUp()
	{
		SH::Director d;
		FakeGame     g;
		const auto   cat = Cat(BaseCatalog());
		d.SetCatalog(cat);

		// A man an older build shaped: the shaft value goes (S-29), the ball size he lacks comes (S-44).
		auto& m = g.actors[0x100];
		m.unkeyed = { { "BTChest", 0.4F }, { "Penis Width", 1.0F }, { "Silhouette_BT_Old", 99.0F } };
		m.listed = { "BTChest", "Penis Width", "Silhouette_BT_Old" };
		auto man = See(0x100, "Somebody");
		man.facts.female = false;
		d.Seen(man);
		(void)Drain(d, g);
		Check(!m.unkeyed.contains("Penis Width") && m.unkeyed.at("BTChest") == 0.4F && m.unkeyed.contains("BTBallSize") &&
				  m.unkeyed.at("BTBallSize") == SH::Draw(0x100, "BTBallSize", 0.1F, 0.4F),
			"an old body: shaft removed, balls rolled, the rest untouched");
		const auto after = m.unkeyed;
		Reload(d, g);
		d.Seen(man);
		Check(Drain(d, g) == 1 && m.unkeyed == after, "touched once: the next session only probes");

		// A build that forbids something new of that old body heals it again (wave 2, L6 F2).
		auto stricter = BaseCatalog();
		stricter["neverInBody"]["male"].push_back("BTChest");
		stricter["presets"][3]["values"] = { { "BTArms", 0.2 } };
		Reload(d, g, stricter);
		d.Seen(man);
		(void)Drain(d, g);
		Check(!m.unkeyed.contains("BTChest") && m.unkeyed.contains("BTBallSize"), "a later build's heal reaches a body already touched");
		Reload(d, g);

		// A woman from before the variety existed gets it; what she has stays.
		auto& w = g.actors[0x200];
		w.unkeyed = { { "Breasts", 0.8F }, { "Butt", 0.5F }, { "NippleSize", 0.2F }, { "Silhouette_Curvy", 99.0F } };
		w.listed = { "Breasts", "Butt", "NippleSize", "Silhouette_Curvy" };
		d.Seen(See(0x200, "Somebody"));
		(void)Drain(d, g);
		Check(w.unkeyed.at("NippleSize") == 0.2F && w.unkeyed.contains("VaginaSize") && w.unkeyed.at("Breasts") == 0.8F,
			"top-up: the missing genital range drawn, her nipples kept");

		// Touched once per body: a value the player took off afterwards (LooksMenu's own sliders) stays off.
		w.unkeyed.erase("VaginaSize");
		Reload(d, g);
		d.Seen(See(0x200, "Somebody"));
		(void)Drain(d, g);
		Check(!w.unkeyed.contains("VaginaSize"), "a topped-up value the player removed later is not put back");

		// ...and so is a body that never needed a top-up (wave 2, L1 F6).
		g.Roll(0x210, *cat, "Silhouette_Curvy", 1234.0F);
		d.Seen(See(0x210, "Somebody"));
		(void)Drain(d, g);
		g.actors[0x210].unkeyed.erase("NippleSize");
		Reload(d, g);
		d.Seen(See(0x210, "Somebody"));
		(void)Drain(d, g);
		Check(!g.actors[0x210].unkeyed.contains("NippleSize"), "a value removed from a body that needed nothing is not put back either");

		// ...nor from a body Silhouette gave or rolled itself.
		std::string why;
		(void)d.RequestPreset(0x211, true, 0x00012345, "Curvy", SH::Source::kAPI, kNormal, why);
		(void)d.RequestRegenerate(0x212, true, 0x00012345, kNormal, why);
		(void)Drain(d, g);
		g.actors[0x211].unkeyed.erase("NippleSize");
		Reload(d, g);
		d.Seen(See(0x211, "Somebody"));
		d.Seen(See(0x212, "Somebody"));
		(void)Drain(d, g);
		Check(!g.actors[0x211].unkeyed.contains("NippleSize") && g.actors[0x211].unkeyed.contains("Silhouette_Curvy"),
			"a value removed from a body Silhouette gave is not put back");
		Check(!g.actors[0x212].unkeyed.contains("NippleSize") && !g.actors[0x212].unkeyed.contains("VaginaSize"),
			"a body a roll gave is taken as it came: nothing topped up after");

		// Another mod's keyed value of a variety slider is not hers: she still gets her own (L5 #6).
		auto& x = g.actors[0x220];
		x.unkeyed = { { "Breasts", 0.8F }, { "VaginaSize", 0.1F }, { "Silhouette_Curvy", 1234.0F } };
		x.other = { { "NippleSize", 0.3F } };
		g.Load();
		d.Seen(See(0x220, "Somebody"));
		(void)Drain(d, g);
		Check(x.unkeyed.contains("NippleSize") && x.other.at("NippleSize") == 0.3F, "top-up weighs her own layer, not the anatomy layer's value");

		// Variety switched off: no top-up of that group.
		auto& y = g.actors[0x300];
		y.unkeyed = { { "Breasts", 0.8F }, { "Silhouette_Curvy", 99.0F } };
		y.listed = { "Breasts", "Silhouette_Curvy" };
		d.Configure({ .orefit = true, .variety = { .nipples = false, .genitals = true } });
		d.Seen(See(0x300, "Somebody"));
		(void)Drain(d, g);
		Check(!y.unkeyed.contains("NippleSize") && y.unkeyed.contains("VaginaSize"), "the variety switches apply to the top-up");
	}

	// ------------------------------------------------------------------ the director: the picker

	void TestPicker()
	{
		SH::Director d;
		FakeGame     g;
		const auto   cat = Cat(BaseCatalog());
		d.SetCatalog(cat);
		std::string why;
		g.Roll(0x100, *cat, "Silhouette_Slim", 1234.0F);
		g.actors[0x100].unkeyed["Waist"] = -0.4F;  // a slider she was given by hand
		g.Load();
		const auto before = g.actors[0x100].unkeyed;
		d.Seen(See(0x100, "Cait"));
		(void)Drain(d, g);
		(void)Events(d);

		auto msg = d.PickerStart(0x100, true, 0x00012345, "Cait");
		Check(msg.find("Cait") != std::string::npos, "Pick names who was picked");
		Check(d.PickerStep(1).find("Still reading") != std::string::npos, "Next waits for the snapshot");
		(void)Drain(d, g);
		Check(d.PickerReady(), "the snapshot is in");
		msg = d.PickerStep(1);
		Check(msg == "Cait: Athletic (3/3)", std::format("Next starts from the preset she has (Slim is 2/3): {}", msg));
		(void)Drain(d, g);
		Check(g.actors[0x100].unkeyed.contains("Silhouette_Athletic") &&
				  g.actors[0x100].unkeyed.at("NippleSize") == before.at("NippleSize") && g.actors[0x100].unkeyed.at("VaginaSize") == before.at("VaginaSize"),
			"the preview is on her, with her own variety (L6 F12)");
		Check(!d.RecordOf(0x100) || d.RecordOf(0x100)->source != SH::Source::kPicker, "a preview is not a choice");
		msg = d.PickerCancel();
		(void)Drain(d, g);
		Check(g.actors[0x100].unkeyed == before, std::format("Cancel puts back exactly what she had ({})", msg));
		Check(d.PickerTarget() == 0 && !d.HasPicking(0x100), "and ends the picking");

		// Next then Previous back to her own preset, then Keep: exactly her body, hand edit included (L1 F9).
		(void)d.PickerStart(0x100, true, 0x00012345, "Cait");
		(void)Drain(d, g);
		(void)d.PickerStep(1);
		(void)Drain(d, g);
		(void)d.PickerStep(-1);
		(void)Drain(d, g);
		msg = d.PickerKeep();
		(void)Drain(d, g);
		Check(msg == "Cait keeps the body they had." && g.actors[0x100].unkeyed == before, std::format("Keep on her own preset restores it exactly ({})", msg));

		(void)d.PickerStart(0x100, true, 0x00012345, "Cait");
		(void)Drain(d, g);
		(void)d.PickerStep(-1);
		(void)Drain(d, g);
		(void)Events(d);
		msg = d.PickerKeep();
		(void)Drain(d, g);
		Check(msg == "Cait keeps Curvy.", msg);
		const auto rec = d.RecordOf(0x100);
		Check(rec && rec->source == SH::Source::kPicker && rec->preset == "Curvy", "Keep records a picker choice");
		Check(g.actors[0x100].unkeyed.contains("Silhouette_Chosen") && g.actors[0x100].unkeyed.at("Silhouette_Chosen") == 3.0F,
			"and marks it in LooksMenu (S-51)");
		Check(Count(Events(d), SH::EventKind::kGenerated) == 1, "Keep announces the body once");

		// Keep while the preview is still queued: it is written as the choice, announced once.
		g.Roll(0x110, *cat, "Silhouette_Slim", 1234.0F);
		d.Seen(See(0x110, "Curie"));
		(void)Drain(d, g);
		(void)Events(d);
		(void)d.PickerStart(0x110, true, 0x00012345, "Curie");
		(void)Drain(d, g);
		(void)d.PickerStep(1);
		msg = d.PickerKeep();
		(void)Drain(d, g);
		Check(g.actors[0x110].unkeyed.contains("Silhouette_Athletic") && g.actors[0x110].unkeyed.contains("Silhouette_Chosen") &&
				  Count(Events(d), SH::EventKind::kGenerated) == 1,
			std::format("a Keep before the preview landed makes it the choice ({})", msg));

		// Keep while the preview is being written: announced once, when it lands, the choice marked after it.
		g.Roll(0x120, *cat, "Silhouette_Slim", 1234.0F);
		d.Seen(See(0x120, "Nora"));
		(void)Drain(d, g);
		(void)Events(d);
		(void)d.PickerStart(0x120, true, 0x00012345, "Nora");
		(void)Drain(d, g);
		(void)d.PickerStep(1);
		const auto writing = d.NextOrder();
		Check(d.Peek(writing) && d.Peek(writing)->kind == SH::OrderKind::kBody && d.Peek(writing)->body.preview, "(set-up) the preview is in flight");
		msg = d.PickerKeep();
		Check(Count(Events(d), SH::EventKind::kGenerated) == 0, std::format("nothing announced before the body is written ({})", msg));
		RunOrder(d, writing, g);
		(void)Drain(d, g);
		Check(Count(Events(d), SH::EventKind::kGenerated) == 1 && g.actors[0x120].unkeyed.contains("Silhouette_Athletic") &&
				  g.actors[0x120].unkeyed.contains("Silhouette_Chosen") && g.actors[0x120].unkeyed.at("Silhouette_Chosen") == 3.0F,
			"Keep while the preview is being written: announced once when it lands, the choice marked beside it");

		// Picking refused while a body is on its way; a request made while picking ends it.
		g.Roll(0x200, *cat, "Silhouette_Slim", 1234.0F);
		(void)d.RequestPreset(0x200, true, 0x00012345, "Athletic", SH::Source::kAPI, kNormal, why);
		Check(d.PickerStart(0x200, true, 0x00012345, "Curie").find("still changing") != std::string::npos, "no picking while a body is on its way");
		(void)Drain(d, g);
		(void)d.PickerStart(0x200, true, 0x00012345, "Curie");
		(void)Drain(d, g);
		(void)d.RequestPreset(0x200, true, 0x00012345, "Curvy", SH::Source::kAPI, kNormal, why);
		Check(d.PickerTarget() == 0 && !d.HasPicking(0x200), "a decision made elsewhere ends the picking");
		(void)Drain(d, g);
		Check(g.actors[0x200].unkeyed.contains("Silhouette_Curvy"), "and is what she gets");

		// A save in the middle of picking loads as a Cancel (S-47).
		g.Roll(0x300, *cat, "Silhouette_Slim", 1234.0F);
		const auto hers = g.actors[0x300].unkeyed;
		d.Seen(See(0x300, "Cait"));
		(void)Drain(d, g);
		(void)d.PickerStart(0x300, true, 0x00012345, "Cait");
		(void)Drain(d, g);
		(void)d.PickerStep(1);
		(void)Drain(d, g);
		Check(!(g.actors[0x300].unkeyed == hers), "(set-up) a preview on her");
		Reload(d, g);
		d.Seen(See(0x300, "Cait"));
		(void)Drain(d, g);
		Check(g.actors[0x300].unkeyed == hers && !d.HasPicking(0x300), "a picking saved mid-preview loads as a Cancel");
		Reload(d, g);
		d.Seen(See(0x300, "Cait"));
		(void)Drain(d, g);
		Check(g.actors[0x300].unkeyed == hers, "and only once");

		// One saved picking per actor (L6 F6): picking B does not lose A's.
		g.Roll(0x400, *cat, "Silhouette_Slim", 1234.0F);
		g.Roll(0x401, *cat, "Silhouette_Slim", 1234.0F);
		const auto herA = g.actors[0x400].unkeyed;
		d.Seen(See(0x400, "A"));
		d.Seen(See(0x401, "B"));
		(void)Drain(d, g);
		(void)d.PickerStart(0x400, true, 0x00012345, "A");
		(void)Drain(d, g);
		(void)d.PickerStep(1);
		(void)Drain(d, g);
		g.away.insert(0x400);  // A leaves memory
		msg = d.PickerStart(0x401, true, 0x00012345, "B");
		(void)Drain(d, g);
		Check(d.HasPicking(0x400) && d.PickerTarget() == 0x401, std::format("A's Cancel could not reach her, and is kept ({})", msg));
		(void)d.PickerCancel();
		(void)Drain(d, g);
		g.away.erase(0x400);
		d.Seen(See(0x400, "A"));
		(void)Drain(d, g);
		Check(g.actors[0x400].unkeyed == herA && !d.HasPicking(0x400), "seen again, A gets exactly her body back");

		// A load, then A picked again before she is seen: the picking carries on from her real body (L6 F6c).
		(void)d.PickerStart(0x400, true, 0x00012345, "A");
		(void)Drain(d, g);
		(void)d.PickerStep(1);
		(void)Drain(d, g);
		Reload(d, g);
		msg = d.PickerStart(0x400, true, 0x00012345, "A");
		Check(msg.find("again") != std::string::npos && d.PickerReady(), std::format("the saved picking is taken up at once ({})", msg));
		(void)d.PickerCancel();
		(void)Drain(d, g);
		Check(g.actors[0x400].unkeyed == herA, "Cancel then puts back her body, not the preview a save left on her");

		// A decision made while a restore waits retires it (L6 F8).
		(void)d.PickerStart(0x400, true, 0x00012345, "A");
		(void)Drain(d, g);
		(void)d.PickerStep(1);
		(void)Drain(d, g);
		Reload(d, g);
		g.rollsTo = "Silhouette_Curvy";
		(void)d.RequestRegenerate(0x400, true, 0x00012345, kNormal, why);
		d.Seen(See(0x400, "A"));
		(void)Drain(d, g);
		Check(g.actors[0x400].unkeyed.contains("Silhouette_Curvy") && !d.HasPicking(0x400), "the new roll stays: the old picking is not restored over it");

		// A saved picking of a created reference whose id now names somebody else is dropped (L6 F9).
		g.Roll(0xFF000B00, *cat, "Silhouette_Slim", 1234.0F);
		auto made = See(0xFF000B00, "Made");
		made.base = 0x00031111;
		d.Seen(made);
		(void)Drain(d, g);
		(void)d.PickerStart(0xFF000B00, true, 0x00031111, "Made");
		(void)Drain(d, g);
		(void)d.PickerStep(1);
		(void)Drain(d, g);
		Reload(d, g);
		g.Roll(0xFF000B00, *cat, "Silhouette_Curvy", 1234.0F);  // the id reused by someone else
		const auto strangerBody = g.actors[0xFF000B00].unkeyed;
		auto       stranger = See(0xFF000B00, "Stranger");
		stranger.base = 0x00032222;
		d.Seen(stranger);
		(void)Drain(d, g);
		Check(g.actors[0xFF000B00].unkeyed == strangerBody && !d.HasPicking(0xFF000B00), "somebody else's picking is not restored on a stranger");

		// A restore never puts back what no body may hold (L1 F8).
		auto& old = g.actors[0x500];
		old.unkeyed = { { "BTChest", 0.4F }, { "Penis Width", 1.0F }, { "Silhouette_BT_Old", 99.0F } };
		g.Load();
		(void)d.PickerStart(0x500, false, 0x00012345, "Old");
		(void)Drain(d, g);
		(void)d.PickerStep(1);
		(void)Drain(d, g);
		(void)d.PickerCancel();
		(void)Drain(d, g);
		Check(!old.unkeyed.contains("Penis Width") && old.unkeyed.at("BTChest") == 0.4F, "Cancel restores the body, not the shaft value a heal takes out");

		// A failed snapshot ends the picking instead of leaving it waiting.
		msg = d.PickerStart(0x300, true, 0x00012345, "Curie");
		std::uint32_t snap = 0;
		while (const auto id = d.NextOrder()) {
			if (d.Peek(id)->kind == SH::OrderKind::kSnapshot) {
				snap = id;
				break;
			}
			RunOrder(d, id, g);
		}
		Check(snap != 0, "the snapshot is asked for");
		d.Done(snap, false);
		(void)Drain(d, g);
		Check(d.PickerTarget() == 0, "a snapshot that failed ends the picking");
	}

	// ------------------------------------------------------------------ wave 3

	// A new session over the same saved game, the co-save keeping what a_keep keeps -- the game's rule
	// for references not in memory at the save.
	void ReloadKeeping(SH::Director& d, FakeGame& g, const SH::Registry::KeepFn& a_keep, std::shared_ptr<const SH::Catalog> a_cat)
	{
		const auto bytes = d.SaveRecords(a_keep);
		d.ForgetWorld();
		d.RevertRecords();
		std::string error;
		Check(d.LoadRecords(bytes, SH::Registry::kVersion, [](std::uint32_t id) { return id; }, error) == SH::Registry::Loaded::kOk,
			std::format("reload ({})", error));
		d.SetCatalog(std::move(a_cat));
		g.Load();
	}

	SH::Sighting Raider(std::uint32_t a_ref)
	{
		auto s = See(a_ref, "Raider");
		s.facts.factions = { { "Fallout4.esm", 3001 } };  // RaiderFaction: Slim or Curvy
		return s;
	}

	void TestWave3()
	{
		std::string why;
		using namespace std::chrono_literals;

		// L1 M1: a roll waiting out a scene holds nothing else back -- the refit comes off when she undresses in it.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			auto now = std::chrono::steady_clock::time_point{} + 1h;
			d.SetClock([&] { return now; });
			g.Roll(0xA00, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xA00, "Somebody", true));
			(void)Drain(d, g);
			Check(!g.actors[0xA00].refit.empty(), "(set-up) dressed, refit on");
			g.busy.insert(0xA00);
			Check(d.RequestRegenerate(0xA00, true, 0x00012345, kUrgent, why) && Drain(d, g) == 1, "(set-up) the roll is deferred");
			d.Dressed(See(0xA00, "Somebody", false), true);
			Check(Drain(d, g) == 1 && g.actors[0xA00].refit.empty(), "undressed in the scene while a roll waits: the refit comes off at once");
			Check(d.Pending() == 0, "the waiting roll does not make the bridge poll faster");
			const auto line = d.TakeSummary();
			Check(line.find("1 held") != std::string::npos, std::format("the summary says what is held back ({})", line));
			g.busy.erase(0xA00);
			g.rollsTo = "Silhouette_Athletic";
			now += SH::Director::kDeferWait;
			(void)Drain(d, g);
			Check(g.actors[0xA00].unkeyed.contains("Silhouette_Athletic"), "the scene over: the roll lands");

			// A scene that starts while the roll reads the keyed values: asked again before the roll (L3 L3).
			g.busyMidRoll.insert(0xA00);
			g.actors[0xA00].other["Erection"] = 1.0F;
			g.rollsTo = "Silhouette_Curvy";
			(void)d.RequestRegenerate(0xA00, true, 0x00012345, kUrgent, why);
			(void)Drain(d, g);
			Check(g.actors[0xA00].unkeyed.contains("Silhouette_Athletic") && g.actors[0xA00].other.at("Erection") == 1.0F,
				"a scene that began while the keyed values were read: nothing rolled, the order waits");
			g.busy.erase(0xA00);
			now += SH::Director::kDeferWait;
			(void)Drain(d, g);
			Check(g.actors[0xA00].unkeyed.contains("Silhouette_Curvy"), "and lands after it");

			// A touch-up waits out a scene too (L5 L7).
			auto& m = g.actors[0xA01];
			m.unkeyed = { { "BTChest", 0.4F }, { "Penis Width", 1.0F }, { "Silhouette_BT_Old", 99.0F } };
			m.listed = { "BTChest", "Penis Width", "Silhouette_BT_Old" };
			auto man = See(0xA01, "Somebody");
			man.facts.female = false;
			g.busy.insert(0xA01);
			d.Seen(man);
			(void)Drain(d, g);
			Check(m.unkeyed.contains("Penis Width"), "in a scene: the touch-up waits, the shaft value stays for now");
			g.busy.erase(0xA01);
			now += SH::Director::kDeferWait;
			(void)Drain(d, g);
			Check(!m.unkeyed.contains("Penis Width") && m.unkeyed.contains("BTBallSize"), "the scene over: healed and topped up");
		}

		// L1 M2: Back to random, the regeneration window's hand-off and Reset are owed across a save (S-59).
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xA10, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xA10, "Somebody"));
			(void)Drain(d, g);
			Check(d.RequestRegenerate(0xA10, true, 0x00012345, kUrgent, why) && d.RecordOf(0xA10) && d.RecordOf(0xA10)->source == SH::Source::kRoll,
				"Back to random: the roll is recorded as owed");
			Check(d.Describe(0xA10).find("on its way") != std::string::npos, d.Describe(0xA10));
			Reload(d, g);  // the queued roll went with the session
			g.rollsTo = "Silhouette_Athletic";
			d.Seen(See(0xA10, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0xA10].unkeyed.contains("Silhouette_Athletic") && (!d.RecordOf(0xA10) || d.RecordOf(0xA10)->source == SH::Source::kNone),
				"a Back to random saved before it ran lands after the load, and is owed no more");

			g.actors[0xA11].other["AnatomyArousal"] = 0.2F;
			g.Load();
			Check(d.RequestAdopt(0xA11, true, 0x00012345, why), "(set-up) the window hands someone over");
			Check(!d.RequestAdopt(0xA11, true, 0x00012345, why) && why.find("on its way") != std::string::npos, std::format("and not twice ({})", why));
			Reload(d, g);
			g.rollsTo = "Silhouette_Curvy";
			d.Seen(See(0xA11, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0xA11].unkeyed.contains("Silhouette_Curvy") && g.actors[0xA11].other.at("AnatomyArousal") == 0.2F,
				"the window's hand-off saved before it ran lands after the load, the other mod's morph kept");

			g.Roll(0xA12, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xA12, "Somebody"));
			(void)Drain(d, g);
			(void)Events(d);
			Check(d.RequestReset(0xA12, true, 0x00012345, kNormal, why) && d.RecordOf(0xA12)->stamp == 0, "Reset: owed until it lands");
			Check(!d.RequestReapply(0xA12, true, 0x00012345, "Slim", kNormal, why) && why.find("reset") != std::string::npos,
				std::format("a Reapply does not undo a reset on its way ({})", why));
			Reload(d, g);
			d.Seen(See(0xA12, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0xA12].unkeyed.empty() && d.RecordOf(0xA12) && d.RecordOf(0xA12)->source == SH::Source::kReset && d.RecordOf(0xA12)->stamp == 1234,
				"a Reset saved before it ran is carried out after the load");
			Reload(d, g);
			g.Roll(0xA12, *cat, "Silhouette_Slim", 1234.0F);  // LooksMenu dropped the empty map; BodyGen rolled the same preset
			d.Seen(See(0xA12, "Somebody"));
			(void)Drain(d, g);
			Check(Has(Events(d), SH::EventKind::kGenerated, "Slim"), "the body after a reset is announced, the same preset as before included (L1 L4)");
		}

		// L1 M3: what the co-save keeps of a created reference.
		{
			Check(SH::KeepInCoSave(0x0001A4F2, 5, false, std::nullopt), "a placed reference is kept, its cell loaded or not");
			Check(!SH::KeepInCoSave(0xFF000900, 5, false, std::nullopt) && !SH::KeepInCoSave(0xFF000900, 5, false, 6u) &&
					  SH::KeepInCoSave(0xFF000900, 5, false, 5u),
				"a created one's bookkeeping only while it is still that NPC in memory");
			Check(SH::KeepInCoSave(0xFF000900, 5, true, std::nullopt) && SH::KeepInCoSave(0xFF000900, 5, true, 6u),
				"intent always: an unloaded created NPC is not gone, and a reused id is caught at the next sighting (S-57)");
			SH::Registry r;
			r.Get(0xFF000901) = SH::Record{ .base = 1, .source = SH::Source::kPicker, .preset = "Curvy", .stamp = 1 };
			r.Get(0xFF000902).announced = 5;
			r.Get(0xFF000903).source = SH::Source::kRoll;
			r.Get(0xFF000904) = SH::Record{ .base = 1, .source = SH::Source::kFactionRule, .preset = "Slim", .stamp = 1, .salt = 2 };
			r.Get(0xFF000905) = SH::Record{ .base = 1, .source = SH::Source::kFactionRule, .preset = "Slim", .stamp = 1 };
			r.Keep(SH::PickerSave{ .ref = 0xFF000906, .base = 1 });
			std::map<std::uint32_t, bool> told;
			(void)r.Serialize([&](std::uint32_t a_ref, std::uint32_t, bool a_intent) {
				told[a_ref] = a_intent;
				return true;
			});
			Check(told[0xFF000901] && !told[0xFF000902] && told[0xFF000903] && told[0xFF000904] && !told[0xFF000905] && told[0xFF000906],
				"intent: a choice, a roll owed, a rule drawn again, a picking -- not a rule's plain draw or what was announced");

			// A picked created NPC out of memory at the save keeps the choice; a stranger with the id later does not get it.
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			Check(d.RequestPreset(0xFF000C00, true, 0x00011111, "Curvy", SH::Source::kPicker, kUrgent, why), "picked");
			(void)Drain(d, g);
			const auto unloaded = [](std::uint32_t a_ref, std::uint32_t a_base, bool a_intent) { return SH::KeepInCoSave(a_ref, a_base, a_intent, std::nullopt); };
			ReloadKeeping(d, g, unloaded, cat);
			Check(d.RecordOf(0xFF000C00) && d.RecordOf(0xFF000C00)->source == SH::Source::kPicker, "a choice on a created NPC whose cell was unloaded survives the save");
			auto stranger = See(0xFF000C00, "Somebody");
			stranger.base = 0x00022222;
			d.Seen(stranger);
			(void)Drain(d, g);
			Check((!d.RecordOf(0xFF000C00) || d.RecordOf(0xFF000C00)->source != SH::Source::kPicker) &&
					  !g.actors[0xFF000C00].unkeyed.contains("Silhouette_Chosen"),
				"the id went to someone else: the choice is not theirs, its marker comes off");
		}

		// L1 L1: a preview is not their body -- nothing gives it again.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xA20, *cat, "Silhouette_Slim", 1234.0F);
			const auto hers = g.actors[0xA20].unkeyed;
			d.Seen(See(0xA20, "Cait"));
			(void)Drain(d, g);
			(void)d.PickerStart(0xA20, true, 0x00012345, "Cait");
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			Check(!d.RequestReapply(0xA20, true, 0x00012345, "Athletic", kBackground, why) && why.find("picked") != std::string::npos,
				std::format("a Refresh during a picking is refused ({})", why));
			Reload(d, g);  // the picking is saved: still not their body
			Check(!d.RequestReapply(0xA20, true, 0x00012345, "Athletic", kBackground, why), "and after a load, before the Cancel lands");
			d.Seen(See(0xA20, "Cait"));
			(void)Drain(d, g);
			Check(g.actors[0xA20].unkeyed == hers, "the picking loads as a Cancel: her own body");
		}

		// L1 L2: a picked body keeps the variety it was picked with when a new build gives it again.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xA30, *cat, "Silhouette_Slim", 1234.0F);
			const auto nip = g.actors[0xA30].unkeyed.at("NippleSize");
			Check(nip != SH::Draw(0xA30, "NippleSize", 0.0F, 0.5F), "(set-up) BodyGen's roll is not the plugin's draw");
			d.Seen(See(0xA30, "Cait"));
			(void)Drain(d, g);
			(void)d.PickerStart(0xA30, true, 0x00012345, "Cait");
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			(void)d.PickerKeep();
			(void)Drain(d, g);
			Check(g.actors[0xA30].unkeyed.contains("Silhouette_Athletic") && g.actors[0xA30].unkeyed.at("NippleSize") == nip, "(set-up) picked, her variety kept");
			auto next = BaseCatalog();
			next["stamp"] = 5678;
			Reload(d, g, next);
			d.Seen(See(0xA30, "Cait"));
			(void)Drain(d, g);
			Check(g.actors[0xA30].unkeyed.at("Silhouette_Athletic") == 5678.0F && g.actors[0xA30].unkeyed.at("NippleSize") == nip,
				"a new build gives a picked body again with the variety it was picked with");
		}

		// L1 L2 again: a re-give decided in one session and cut off by a save is made in the next (Reconcile) -- same variety.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xAD0, *cat, "Silhouette_Slim", 1234.0F);
			const auto nip = g.actors[0xAD0].unkeyed.at("NippleSize");
			d.Seen(See(0xAD0, "Cait"));
			(void)Drain(d, g);
			(void)d.PickerStart(0xAD0, true, 0x00012345, "Cait");
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			(void)d.PickerKeep();
			(void)Drain(d, g);
			auto next = BaseCatalog();
			next["stamp"] = 5678;
			Reload(d, g, next);
			d.Seen(See(0xAD0, "Cait"));
			RunOrder(d, d.NextOrder(), g);  // the probe: the re-give is decided and recorded
			Reload(d, g, next);             // and a save lands before it is written
			d.Seen(See(0xAD0, "Cait"));
			(void)Drain(d, g);
			const auto& b = g.actors[0xAD0].unkeyed;
			Check(b.contains("Silhouette_Athletic") && b.at("Silhouette_Athletic") == 5678.0F && b.contains("NippleSize") && b.at("NippleSize") == nip,
				"a re-give a save put off keeps her variety as well");
		}

		// W3: a probe that lands after they stopped being someone Silhouette shapes changes nothing.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xB50, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xB50, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestReset(0xB50, true, 0x00012345, kNormal, why);
			Reload(d, g);  // the reset is owed
			d.Seen(See(0xB50, "Somebody"));  // the probe is queued
			auto ghoul = See(0xB50, "Somebody");
			ghoul.facts.race = "GhoulRace";
			d.Seen(ghoul);  // and before it runs, they are of a race Silhouette does not shape
			(void)Drain(d, g);
			Check(g.actors[0xB50].unkeyed.contains("Silhouette_Slim"), "nothing owed is carried out on someone Silhouette no longer shapes");
		}

		// S-60 with S-52: the salt is read at every sighting -- a rule that drops the preset drawn draws again with it.
		{
			bool found = false;
			for (std::uint32_t r = 0xB60; r < 0xB80 && !found; ++r) {
				SH::Director d;
				FakeGame     g;
				const auto   cat = Cat(BaseCatalog());
				d.SetCatalog(cat);
				g.Roll(r, *cat, "Silhouette_Athletic", 1234.0F);
				d.Seen(Raider(r));
				(void)Drain(d, g);
				(void)d.RequestRegenerate(r, true, 0x00012345, kUrgent, why);
				(void)Drain(d, g);
				const auto pressed = d.RecordOf(r);
				if (!pressed || pressed->salt == 0) {
					continue;
				}
				// The next build's rule no longer lists what they drew: they draw again, from what is left.
				auto without = BaseCatalog();
				std::vector<std::string> left;
				for (const auto* p : { "Slim", "Curvy", "Athletic" }) {
					if (!SH::IEquals(p, pressed->preset)) {
						left.emplace_back(p);
					}
				}
				without["rules"]["faction"][1]["presets"] = left;
				without["stamp"] = 5678;
				const auto newer = Cat(without);
				auto       facts = Raider(r).facts;
				const auto unsalted = SH::Decide(*newer, facts).preset;
				facts.salt = pressed->salt;
				const auto salted = SH::Decide(*newer, facts).preset;
				if (salted == unsalted) {
					continue;  // this one cannot tell the two apart
				}
				found = true;
				Reload(d, g, without);
				d.Seen(Raider(r));
				(void)Drain(d, g);
				Check(d.AssignedPreset(r) == salted, std::format("drawn again with the salt their presses left ({} expected, {} got)", salted, d.AssignedPreset(r)));
			}
			Check(found, "(set-up) a raider whose salted draw differs from the draw by id");
		}

		// L1 L3: Back to random under a rule announces the rule's body, not the roll it replaced.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xA40, *cat, "Silhouette_Curvy", 1234.0F);
			d.Seen(See(0xA40, "Piper"));
			(void)Drain(d, g);
			(void)Events(d);
			g.rollsTo = "Silhouette_Athletic";
			(void)d.RequestRegenerate(0xA40, true, 0x00012345, kUrgent, why);
			(void)Drain(d, g);
			auto ev = Events(d);
			Check(Has(ev, SH::EventKind::kGenerated, "Curvy") && !Has(ev, SH::EventKind::kGenerated, "Athletic"),
				"only the body she ends with is announced");
			Check(!d.RecordOf(0xA40) || d.RecordOf(0xA40)->salt == 0, "a rule with one preset is not drawn again: nothing to draw");
			g.Roll(0xA41, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xA41, "Mama Murphy"));
			(void)Drain(d, g);
			(void)Events(d);
			(void)d.RequestRegenerate(0xA41, true, 0x00012345, kUrgent, why);
			(void)Drain(d, g);
			Check(!Has(Events(d), SH::EventKind::kGenerated) && (g.actors[0xA41].unkeyed == Layer{ { "Silhouette_Blacklisted", 1234.0F } }),
				"a blacklisted woman rolled is bared again, and no body is announced");
		}

		// L1 L5: a Reapply before the first probe keeps a choice the co-save lost.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			Check(d.RequestPreset(0xA60, true, 0x00012345, "Curvy", SH::Source::kPicker, kUrgent, why), "picked");
			(void)Drain(d, g);
			ReloadWithoutRecords(d, g);
			Check(d.RequestReapply(0xA60, true, 0x00012345, "Curvy", kBackground, why), "(set-up) Refresh asks for her body again, before any probe");
			(void)Drain(d, g);
			const auto rec = d.RecordOf(0xA60);
			Check(g.actors[0xA60].unkeyed.contains("Silhouette_Chosen") && g.actors[0xA60].unkeyed.at("Silhouette_Chosen") == 3.0F && rec &&
					  rec->source == SH::Source::kPicker && rec->preset == "Curvy",
				"the choice marker stays beside the body given again, and the choice is recorded again");
		}

		// L1 L6: a chosen preset gone from the build: the body stays, the choice comes off, the rules decide.
		{
			SH::Director d;
			FakeGame     g;
			d.SetCatalog(Cat(BaseCatalog()));
			Check(d.RequestPreset(0xA70, true, 0x00012345, "Athletic", SH::Source::kPicker, kUrgent, why), "picked");
			(void)Drain(d, g);
			auto gone = BaseCatalog();
			gone["stamp"] = 5678;
			gone["presets"].erase(2);  // Athletic
			gone["rules"]["faction"][0]["presets"] = { "Slim" };
			auto withOld = *Cat(gone);
			withOld.AddManifest(1234, { { "Silhouette_Athletic", { "Athletic", true, { "Waist" } } } });
			const auto newer = std::make_shared<const SH::Catalog>(std::move(withOld));
			ReloadKeeping(d, g, nullptr, newer);
			d.Seen(See(0xA70, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0xA70].unkeyed.contains("Silhouette_Athletic") && !g.actors[0xA70].unkeyed.contains("Silhouette_Chosen") &&
					  (!d.RecordOf(0xA70) || d.RecordOf(0xA70)->source == SH::Source::kNone),
				"the body stays, the choice and its marker go");
			ReloadKeeping(d, g, [](std::uint32_t, std::uint32_t, bool) { return false; }, newer);  // even with no records at all
			d.Seen(See(0xA70, "Somebody"));
			(void)Drain(d, g);
			Check(!d.RecordOf(0xA70) || d.RecordOf(0xA70)->source == SH::Source::kNone, "and the choice is not rebuilt from LooksMenu next time");
		}

		// L1 L7: picked before anyone probed them -- the session's probe was the snapshot.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xA80, *cat, "Silhouette_Slim", 1234.0F);
			g.actors[0xA80].unkeyed["Silhouette_Chosen"] = 4.0F;  // another mod's choice; the co-save lost it
			g.Load();
			(void)d.PickerStart(0xA80, true, 0x00012345, "Nora");
			(void)Drain(d, g);
			(void)d.PickerCancel();
			(void)Drain(d, g);
			d.Seen(See(0xA80, "Somebody"));
			(void)Drain(d, g);
			const auto rec = d.RecordOf(0xA80);
			Check(rec && rec->source == SH::Source::kAPI && rec->preset == "Slim", "the choice is rebuilt at their first sighting");
			Check(Has(Events(d), SH::EventKind::kGenerated, "Slim"), "and their body announced");
		}

		// L1 L8: out of memory -- said in the summary, not polled for.
		{
			SH::Director d;
			FakeGame     g;
			d.SetCatalog(Cat(BaseCatalog()));
			g.away.insert(0xA90);
			(void)d.RequestPreset(0xA90, true, 0x00012345, "Curvy", SH::Source::kAPI, kNormal, why);
			(void)Drain(d, g);
			const auto line = d.TakeSummary();
			Check(d.Pending() == 0 && line.find("1 for people out of memory") != std::string::npos, std::format("parked work is said ({})", line));
		}

		// L1 L9: an announcement handed to the bridge and not raised yet is not made twice.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xAA0, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xAA0, "Somebody"));
			(void)Drain(d, g);
			const auto first = d.NextEvent();
			Check(first != 0, "(set-up) the bridge took the announcement");
			g.rollsTo = "Silhouette_Slim";
			(void)d.RequestRegenerate(0xAA0, true, 0x00012345, kUrgent, why);
			(void)Drain(d, g);
			Check(d.NextEvent() == 0, "the same body announced again while the first is being raised: once");
			d.EventDone(first);
		}

		// L1 L10: the oldest picking goes at the cap, after a load too.
		{
			SH::Registry r;
			r.Keep(SH::PickerSave{ .ref = 0x2F0 });
			r.Keep(SH::PickerSave{ .ref = 0x2E0 });
			r.Keep(SH::PickerSave{ .ref = 0x2D0 });
			SH::Registry back;
			std::string  error;
			(void)back.Deserialize(r.Serialize(nullptr), SH::Registry::kVersion, nullptr, error);
			for (std::uint32_t i = 0; i < SH::Registry::kMaxPickings - 2; ++i) {
				back.Keep(SH::PickerSave{ .ref = 0x1000 + i });
			}
			Check(!back.pickings.contains(0x2F0) && back.pickings.contains(0x2D0), "the first picking made goes first, not the lowest id");
		}

		// L5 L1: a refit already right is not written again.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xAB0, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xAB0, "Somebody", true));
			(void)Drain(d, g);
			(void)Events(d);
			d.Dressed(See(0xAB0, "Somebody", true, true), false);
			d.Dressed(See(0xAB0, "Somebody", true, false), false);
			Check(Drain(d, g) == 0 && !Has(Events(d), SH::EventKind::kORefitChanged), "heavy and back before the bridge came: nothing written, nothing said");
		}

		// S-58: a Refresh of a BodyGen body that a save cut short is given whole at the next load.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xAC0, *cat, "Silhouette_Slim", 1234.0F);
			const auto hers = g.actors[0xAC0].unkeyed;
			d.Seen(See(0xAC0, "Somebody"));
			(void)Drain(d, g);
			Check(d.RequestReapply(0xAC0, true, 0x00012345, "Slim", kBackground, why), "Refresh");
			RunOrder(d, d.NextOrder(), g, 4);  // the pending marker and the values; the save lands before the marker
			Check(g.actors[0xAC0].unkeyed.contains("Silhouette_Slim") && g.actors[0xAC0].unkeyed.at("Silhouette_Slim") == 0.25F,
				"(set-up) the marker says pending");
			Reload(d, g);
			d.Seen(See(0xAC0, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0xAC0].unkeyed == hers, "given again whole at the next load, her variety kept");
			Check(!d.RecordOf(0xAC0) || d.RecordOf(0xAC0)->source == SH::Source::kNone, "and still nobody's choice: BodyGen's body");

			Check(d.RequestReapply(0xAC0, true, 0x00012345, "Slim", kBackground, why), "Refresh again");
			RunOrder(d, d.NextOrder(), g, 1);  // only the pending marker
			Reload(d, g);
			d.Seen(See(0xAC0, "Somebody"));
			(void)Drain(d, g);
			const auto& half = g.actors[0xAC0].unkeyed;
			Check(half.contains("Silhouette_Slim") && half.at("Silhouette_Slim") == 1234.0F && half.contains("Breasts") && half.at("Breasts") == 0.2F,
				"cut right after the marker: still given whole");
		}

		// Owner Q1 (S-60): Back to random under a rule with several presets draws again, somewhere new, and keeps it.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			std::map<std::uint32_t, std::string> first;
			for (std::uint32_t r = 0xB00; r < 0xB08; ++r) {
				g.Roll(r, *cat, "Silhouette_Athletic", 1234.0F);
				d.Seen(Raider(r));
			}
			(void)Drain(d, g);
			for (std::uint32_t r = 0xB00; r < 0xB08; ++r) {
				first[r] = d.AssignedPreset(r);
				(void)d.RequestRegenerate(r, true, 0x00012345, kUrgent, why);
			}
			(void)Drain(d, g);
			std::size_t moved = 0;
			for (std::uint32_t r = 0xB00; r < 0xB08; ++r) {
				const auto now = d.AssignedPreset(r);
				const auto marker = now == "Slim" ? "Silhouette_Slim" : "Silhouette_Curvy";
				moved += now != first[r] && d.RecordOf(r)->salt != 0 && g.actors[r].unkeyed.contains(marker) ? 1 : 0;
			}
			Check(moved == 8, std::format("every raider pressed lands on the other preset of the two ({} of 8)", moved));
			Reload(d, g);
			for (std::uint32_t r = 0xB00; r < 0xB08; ++r) {
				d.Seen(Raider(r));
			}
			(void)Drain(d, g);
			const auto kept = std::ranges::count_if(first, [&](const auto& p) { return d.AssignedPreset(p.first) != p.second; });
			Check(kept == 8, "the new draw survives the save and stays (S-52)");
			(void)d.RequestRegenerate(0xB00, true, 0x00012345, kUrgent, why);
			(void)Drain(d, g);
			Check(d.AssignedPreset(0xB00) == first[0xB00], "pressed again: back to the other one");
		}

		// While picked, the body on them may be a preview: the probe that comes after it announces nothing,
		// and a roll owed waits for the picking to end.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xB30, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xB30, "Somebody"));  // the probe is queued, not run
			(void)d.PickerStart(0xB30, true, 0x00012345, "Somebody");
			RunOrder(d, d.NextOrder(), g);  // the snapshot
			(void)d.PickerStep(1);           // Athletic, before the probe comes round
			(void)Drain(d, g);
			Check(g.actors[0xB30].unkeyed.contains("Silhouette_Athletic") && !Has(Events(d), SH::EventKind::kGenerated, "Athletic"),
				"a preview on her when the probe comes is not announced as her body");
			(void)d.PickerCancel();
			(void)Drain(d, g);

			g.Roll(0xB31, *cat, "Silhouette_Slim", 1234.0F);
			const auto hers = g.actors[0xB31].unkeyed;
			d.Seen(See(0xB31, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestRegenerate(0xB31, true, 0x00012345, kUrgent, why);
			Reload(d, g);  // the roll is owed
			g.rollsTo = "Silhouette_Athletic";
			(void)d.PickerStart(0xB31, true, 0x00012345, "Somebody");
			(void)Drain(d, g);
			d.Seen(See(0xB31, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0xB31].unkeyed == hers && d.PickerTarget() == 0xB31, "a roll owed waits while the player is picking her");
			(void)d.PickerCancel();
			(void)Drain(d, g);
		}

		// Power armour is not clothing: getting in says nothing of undressing (L2).
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xB20, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xB20, "Somebody", true));
			(void)Drain(d, g);
			(void)Events(d);
			auto frame = See(0xB20, "Somebody", false);
			frame.powerArmor = true;
			d.Dressed(frame, true);
			const auto ev = Events(d);
			Check(!Has(ev, SH::EventKind::kNaked) && !Has(ev, SH::EventKind::kRemovingClothes), "climbing into power armour: no OnActorNaked, no OnActorRemovingClothes");
		}

		// The co-save's appended fields: read when there, absent in an older build's bytes.
		{
			SH::Registry r;
			r.Get(0x777) = SH::Record{ .base = 3, .source = SH::Source::kFactionRule, .preset = "Slim", .stamp = 9, .salt = 7 };
			r.Keep(SH::PickerSave{ .ref = 0x888, .base = 4, .before = SH::Record{ .source = SH::Source::kNameRule, .preset = "Curvy", .salt = 5 } });
			r.Get(0x999).source = SH::Source::kRoll;
			const auto   bytes = r.Serialize(nullptr);
			SH::Registry back;
			std::string  error;
			Check(back.Deserialize(bytes, SH::Registry::kVersion, nullptr, error) == SH::Registry::Loaded::kOk && back.Find(0x777)->salt == 7 &&
					  back.pickings.at(0x888).before->salt == 5 && back.Find(0x999)->source == SH::Source::kRoll,
				std::format("the salt, a picking's record's salt and a roll owed read back ({})", error));
			// The same bytes as a build before the salt wrote them: the first record's item 4 bytes shorter.
			auto          older = bytes;
			std::uint16_t length = 0;
			std::memcpy(&length, older.data() + 4, 2);
			const auto shorter = static_cast<std::uint16_t>(length - 4);
			std::memcpy(older.data() + 4, &shorter, 2);
			older.erase(older.begin() + 6 + shorter, older.begin() + 6 + length);
			SH::Registry old;
			Check(old.Deserialize(older, SH::Registry::kVersion, nullptr, error) == SH::Registry::Loaded::kOk && old.Find(0x777) &&
					  old.Find(0x777)->salt == 0 && old.Find(0x777)->preset == "Slim",
				std::format("a record without the salt reads as salt 0 ({})", error));
		}
	}

	// ------------------------------------------------------------------ wave 4

	void TestWave4()
	{
		std::string why;
		using namespace std::chrono_literals;

		// M1: a picking holds back what a probe settles; when it ends, what was held back is done -- not at
		// the next load. Here an owed Back to random, and the picker's snapshot is the session's first probe.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xC00, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xC00, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestRegenerate(0xC00, true, 0x00012345, kUrgent, why);
			Reload(d, g);  // the roll is owed
			g.rollsTo = "Silhouette_Athletic";
			d.Seen(See(0xC00, "Somebody"));  // a probe is queued...
			(void)d.PickerStart(0xC00, true, 0x00012345, "Somebody");  // ...and the snapshot goes first
			(void)Drain(d, g);
			Check(g.actors[0xC00].unkeyed.contains("Silhouette_Slim"), "(set-up) while she is picked, the owed roll waits");
			(void)d.PickerCancel();  // nothing tried on
			(void)Drain(d, g);
			Check(g.actors[0xC00].unkeyed.contains("Silhouette_Athletic"), "the picking over, the owed roll lands this session");

			// The same through a Cancel that puts a preview back.
			g.Roll(0xC01, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xC01, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestRegenerate(0xC01, true, 0x00012345, kUrgent, why);
			Reload(d, g);
			d.Seen(See(0xC01, "Somebody"));
			(void)d.PickerStart(0xC01, true, 0x00012345, "Somebody");
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			(void)d.PickerCancel();
			(void)Drain(d, g);
			Check(g.actors[0xC01].unkeyed.contains("Silhouette_Athletic"), "the owed roll lands once the restore after a preview is done");

			// And when the picking ends because the bridge could not take the snapshot: probed under the
			// picker, the owed roll lands once the picker lets go.
			g.Roll(0xC02, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xC02, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestRegenerate(0xC02, true, 0x00012345, kUrgent, why);
			Reload(d, g);
			d.Seen(See(0xC02, "Somebody"));
			const auto probe = d.NextOrder();  // in flight when the player picks her
			(void)d.PickerStart(0xC02, true, 0x00012345, "Somebody");
			RunOrder(d, probe, g);
			Check(g.actors[0xC02].unkeyed.contains("Silhouette_Slim"), "(set-up) probed under the picker, the owed roll waits");
			const auto snapshot = d.NextOrder();
			Check(d.Peek(snapshot) && d.Peek(snapshot)->kind == SH::OrderKind::kSnapshot, "(set-up) the snapshot is handed out next");
			d.Done(snapshot, false);  // the bridge could not take it
			(void)Drain(d, g);
			Check(!d.PickerReady() && g.actors[0xC02].unkeyed.contains("Silhouette_Athletic"), "a failed snapshot ends the picking, and the owed roll lands");
		}

		// M2: a Reset asked while a Back to random is in flight is not erased when the roll lands.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xC10, *cat, "Silhouette_Slim", 1234.0F);
			g.actors[0xC10].other["AnatomyArousal"] = 0.3F;  // LooksMenu keeps her map: BodyGen never runs for her
			g.Load();
			d.Seen(See(0xC10, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestRegenerate(0xC10, true, 0x00012345, kUrgent, why);
			const auto roll = d.NextOrder();  // in flight
			Check(d.RequestReset(0xC10, true, 0x00012345, kNormal, why), "(set-up) Reset asked while the roll is in flight");
			RunOrder(d, roll, g);
			(void)Drain(d, g);
			Check(g.actors[0xC10].unkeyed.empty() && d.RecordOf(0xC10) && d.RecordOf(0xC10)->source == SH::Source::kReset,
				"the reset lands after the roll, and is remembered");
			Reload(d, g);
			d.Seen(See(0xC10, "Somebody"));
			(void)Drain(d, g);
			Check(!g.actors[0xC10].unkeyed.empty(), "the next load gives her a body (S-53): she is not left bare for good");

			// Two Back to random in a row, the second still queued when the first lands, then a save.
			g.Roll(0xC11, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xC11, "Somebody"));
			(void)Drain(d, g);
			g.rollsTo = "Silhouette_Athletic";
			(void)d.RequestRegenerate(0xC11, true, 0x00012345, kUrgent, why);
			const auto first = d.NextOrder();
			(void)d.RequestRegenerate(0xC11, true, 0x00012345, kUrgent, why);
			RunOrder(d, first, g);
			Check(d.RecordOf(0xC11) && d.RecordOf(0xC11)->source == SH::Source::kRoll, "the second roll is still owed after the first lands");
			Reload(d, g);  // the queued second roll went with the session
			g.rollsTo = "Silhouette_Curvy";
			d.Seen(See(0xC11, "Somebody"));
			(void)Drain(d, g);
			Check(g.actors[0xC11].unkeyed.contains("Silhouette_Curvy"), "and lands after the load");
		}

		// L1: a Cancel after a preview keeps a choice rebuilt from LooksMenu during the picking.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xC20, *cat, "Silhouette_Athletic", 1234.0F);
			g.actors[0xC20].unkeyed["Silhouette_Chosen"] = 3.0F;  // picked; a save without the plugin lost the record
			g.Load();
			(void)d.PickerStart(0xC20, true, 0x00012345, "Raider");  // picked before anyone saw her
			(void)Drain(d, g);
			d.Seen(Raider(0xC20));  // seen during the picking: the choice is rebuilt
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			(void)d.PickerCancel();
			Check(d.RecordOf(0xC20) && d.RecordOf(0xC20)->source == SH::Source::kPicker,
				"the choice goes back at once, before the restore lands (S-47)");
			(void)Drain(d, g);
			d.Seen(Raider(0xC20));
			(void)Drain(d, g);
			const auto rec = d.RecordOf(0xC20);
			Check(g.actors[0xC20].unkeyed.contains("Silhouette_Athletic") && rec && rec->source == SH::Source::kPicker,
				"Cancel puts back her pick, and the faction rule does not take her");
		}

		// L2: the roll after a name leaves the blacklist is owed across a save.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xC30, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xC30, "Mama Murphy"));
			(void)Drain(d, g);
			auto lifted = BaseCatalog();
			lifted["rules"]["blacklistedNpcNames"] = nlohmann::json::array();
			Reload(d, g, lifted);
			d.Seen(See(0xC30, "Mama Murphy"));
			RunOrder(d, d.NextOrder(), g);  // the probe decides the roll; a save comes before it runs
			Reload(d, g, lifted);
			g.rollsTo = "Silhouette_Curvy";
			d.Seen(See(0xC30, "Mama Murphy"));
			(void)Drain(d, g);
			Check(g.actors[0xC30].unkeyed.contains("Silhouette_Curvy") && !g.actors[0xC30].unkeyed.contains("Silhouette_Blacklisted"),
				"a lifted blacklist's roll lands after a save cut it off");
		}

		// L3: a choice rebuilt by the probe goes with a body asked for while that probe was in flight.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			Check(d.RequestPreset(0xC40, true, 0x00012345, "Athletic", SH::Source::kPicker, kUrgent, why), "picked");
			(void)Drain(d, g);
			ReloadWithoutRecords(d, g);
			d.Seen(See(0xC40, "Somebody"));
			const auto probe = d.NextOrder();  // in flight
			Check(d.RequestReapply(0xC40, true, 0x00012345, "Athletic", kBackground, why), "(set-up) Refresh asked meanwhile");
			RunOrder(d, probe, g);  // rebuilds the choice
			(void)Drain(d, g);
			Check(g.actors[0xC40].unkeyed.contains("Silhouette_Chosen") && g.actors[0xC40].unkeyed.at("Silhouette_Chosen") == 3.0F,
				"the body given again carries the choice beside it");
		}

		// L4: an announcement the bridge skipped (the actor was not in memory) blocks nothing after it.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xC50, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xC50, "Somebody"));
			(void)Drain(d, g);
			g.away.insert(0xC50);
			Check(Poll(d, g).empty(), "(set-up) the announcement is skipped: she is not in memory when it is raised");
			g.away.erase(0xC50);
			(void)Poll(d, g);
			(void)d.RequestReapply(0xC50, true, 0x00012345, "Slim", kNormal, why);
			(void)Drain(d, g);
			Check(Has(Poll(d, g), SH::EventKind::kGenerated, "Slim"), "a later body of the same preset is announced");
		}

		// L6: a Refresh does not give a body to someone blacklisted by name.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xC60, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xC60, "Mama Murphy"));
			RunOrder(d, d.NextOrder(), g);  // the probe decides the blacklist; the bare body is on its way
			Check(!d.RequestReapply(0xC60, true, 0x00012345, "Slim", kBackground, why) && why.find("blacklisted") != std::string::npos,
				std::format("refused ({})", why));
			(void)Drain(d, g);
			Check((g.actors[0xC60].unkeyed == Layer{ { "Silhouette_Blacklisted", 1234.0F } }), "and she is bare");
		}

		// L7: a Cancel that puts back a half-written body leaves it saying so; S-58 gives it whole.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xC70, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(0xC70, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestReapply(0xC70, true, 0x00012345, "Slim", kBackground, why);
			RunOrder(d, d.NextOrder(), g, 2);  // the pending marker and one value, then a save
			Reload(d, g);
			(void)d.PickerStart(0xC70, true, 0x00012345, "Somebody");  // before anyone saw her: the snapshot holds the half body
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			(void)d.PickerCancel();
			(void)Drain(d, g);
			d.Seen(See(0xC70, "Somebody"));
			(void)Drain(d, g);
			const auto& b = g.actors[0xC70].unkeyed;
			Check(b.contains("Silhouette_Slim") && b.at("Silhouette_Slim") == 1234.0F && b.contains("Breasts"), "the half body is given whole this session");
		}

		// L8: when the first thing seen after a load is an equip event, an unfinished picking is still put back.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			g.Roll(0xC80, *cat, "Silhouette_Slim", 1234.0F);
			const auto hers = g.actors[0xC80].unkeyed;
			d.Seen(See(0xC80, "Somebody"));
			(void)Drain(d, g);
			(void)d.PickerStart(0xC80, true, 0x00012345, "Somebody");
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			Reload(d, g);
			d.Dressed(See(0xC80, "Somebody", true), false);
			(void)Drain(d, g);
			Check(g.actors[0xC80].unkeyed == hers && !d.HasPicking(0xC80), "the picking loads as a Cancel from the first equip event too");
		}

		// L9: a new build gives a rule's body again with the variety she has.
		{
			bool found = false;
			for (std::uint32_t r = 0xC90; r < 0xCB0 && !found; ++r) {
				const auto cat = Cat(BaseCatalog());
				auto       facts = Raider(r).facts;
				if (SH::Decide(*cat, facts).preset != "Slim") {
					continue;  // the rule must draw the preset BodyGen gave her, so her body is not replaced
				}
				found = true;
				SH::Director d;
				FakeGame     g;
				d.SetCatalog(cat);
				g.Roll(r, *cat, "Silhouette_Slim", 1234.0F);  // BodyGen's variety, not the plugin's draw
				const auto nip = g.actors[r].unkeyed.at("NippleSize");
				d.Seen(Raider(r));
				(void)Drain(d, g);
				auto next = BaseCatalog();
				next["stamp"] = 5678;
				Reload(d, g, next);
				d.Seen(Raider(r));
				(void)Drain(d, g);
				const auto& b = g.actors[r].unkeyed;
				Check(b.contains("Silhouette_Slim") && b.at("Silhouette_Slim") == 5678.0F && b.contains("NippleSize") && b.at("NippleSize") == nip,
					"a new build's values, her own variety kept");
			}
			Check(found, "(set-up) a raider the rule gives Slim");
		}

		// Lens 2 L6: a scene that never ends does not fill the log: deferring again says nothing new.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			auto now = std::chrono::steady_clock::time_point{} + 1h;
			d.SetClock([&] { return now; });
			auto& m = g.actors[0xCC0];
			m.unkeyed = { { "BTChest", 0.4F }, { "Penis Width", 1.0F }, { "Silhouette_BT_Old", 99.0F } };
			m.listed = { "BTChest", "Penis Width", "Silhouette_BT_Old" };
			auto man = See(0xCC0, "Somebody");
			man.facts.female = false;
			g.busy.insert(0xCC0);
			d.Seen(man);
			(void)Drain(d, g);
			(void)d.TakeSummary();
			now += SH::Director::kDeferWait;
			(void)Drain(d, g);  // handed out again, deferred again
			Check(d.TakeSummary().empty(), "a touch-up deferred again, and nothing else: no summary line");
		}
	}

	void TestWave5()
	{
		std::string why;
		const auto  generated = [](const std::vector<SH::Event>& a_events, std::uint32_t a_ref, std::string_view a_preset) {
			return std::ranges::count_if(a_events, [&](const SH::Event& e) { return e.kind == SH::EventKind::kGenerated && e.ref == a_ref && e.preset == a_preset; });
		};
		const auto append = [](std::vector<SH::Event>& a_all, const std::vector<SH::Event>& a_more) { a_all.insert(a_all.end(), a_more.begin(), a_more.end()); };

		// Lens 1 M1: a landed Reset on someone another mod's keyed morph keeps in LooksMenu's map; picked before she
		// is seen, a preview, Cancel -- and she is read before the restore lands. The preview is no body BodyGen gave
		// her: the reset stays owed, and she gets her new body. (She was left bare for good.)
		for (int when = 0; when < 3; ++when) {
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xD00;
			g.Roll(A, *cat, "Silhouette_Slim", 1234.0F);
			g.actors[A].other["AnatomyArousal"] = 0.3F;  // LooksMenu keeps her map: BodyGen never runs for her
			g.Load();
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestReset(A, true, 0x00012345, kNormal, why);
			(void)Drain(d, g);  // bare; the reset has landed: a new body at the next load (S-53)
			Reload(d, g);
			(void)d.PickerStart(A, true, 0x00012345, "Somebody");
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			(void)d.PickerCancel();
			if (when == 0) {
				d.Seen(See(A, "Somebody"));  // while the restore waits
			} else {
				const auto restore = d.NextOrder();  // being written
				if (when == 1) {
					d.Seen(See(A, "Somebody"));
				} else {
					d.Dressed(See(A, "Somebody", true), false);
				}
				RunOrder(d, restore, g);
			}
			(void)Drain(d, g);
			const auto label = when == 0 ? "seen while the restore waits" : when == 1 ? "seen while it is written" : "dressing while it is written";
			Check(!g.actors[A].unkeyed.empty(), std::format("a landed reset, a cancelled preview, {}: she gets her new body this session", label));
			Reload(d, g);
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			Check(!g.actors[A].unkeyed.empty(), std::format("({}) and has a body after the next load", label));
		}

		// And with her probe queued when she is picked: the reset's new body does not start under the picker, and
		// nothing is announced while she is picked (S-46); it lands when the picking ends.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xD04;
			g.Roll(A, *cat, "Silhouette_Slim", 1234.0F);
			g.actors[A].other["AnatomyArousal"] = 0.3F;
			g.Load();
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestReset(A, true, 0x00012345, kNormal, why);
			(void)Drain(d, g);
			Reload(d, g);
			g.rollsTo = "Silhouette_Athletic";
			d.Seen(See(A, "Somebody"));                            // her probe is queued...
			(void)d.PickerStart(A, true, 0x00012345, "Somebody");  // ...and the snapshot goes first
			(void)Drain(d, g);
			const auto during = Poll(d, g);
			Check(g.actors[A].unkeyed.empty() && !Has(during, SH::EventKind::kGenerated), "while she is picked, the reset's new body waits, and nothing is announced");
			(void)d.PickerCancel();
			(void)Drain(d, g);
			Check(g.actors[A].unkeyed.contains("Silhouette_Athletic"), "the picking over, the reset's new body lands");
		}

		// Lens 1 L2: picked again while the Cancel's restore is written, and ended again. The first restore to land
		// does not end the picking the second belongs to: what the picking held back runs this session.
		for (int end = 0; end < 3; ++end) {
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xD10, B = 0xD11;
			g.Roll(A, *cat, "Silhouette_Slim", 1234.0F);
			g.Roll(B, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestRegenerate(A, true, 0x00012345, kUrgent, why);
			Reload(d, g);  // the roll is owed (S-59)
			g.rollsTo = "Silhouette_Athletic";
			d.Seen(See(A, "Somebody"));
			d.Seen(See(B, "Somebody"));
			(void)d.PickerStart(A, true, 0x00012345, "Somebody");
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			(void)d.PickerCancel();
			const auto first = d.NextOrder();                      // the restore, being written
			(void)d.PickerStart(A, true, 0x00012345, "Somebody");  // picked again meanwhile
			if (end == 0) {
				(void)d.PickerCancel();
			} else if (end == 1) {
				(void)d.PickerKeep();  // on the preset they had: a Cancel
			} else {
				(void)d.PickerStart(B, true, 0x00012345, "Somebody");
			}
			RunOrder(d, first, g);
			(void)Drain(d, g);
			if (end == 2) {
				(void)d.PickerCancel();
				(void)Drain(d, g);
			}
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			const auto label = end == 0 ? "cancelled again" : end == 1 ? "kept on the preset they had" : "left for somebody else";
			Check(g.actors[A].unkeyed.contains("Silhouette_Athletic"), std::format("two restores of one picking ({}): the owed roll lands this session", label));
		}

		// Lens 1 L4: Keep while the preview is being written, and she leaves memory before it lands: the body comes
		// back as the choice -- not her old body with the choice's marker beside it.
		for (const bool dllLess : { false, true }) {
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xD20;
			g.Roll(A, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			(void)d.PickerStart(A, true, 0x00012345, "Somebody");
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			const auto preview = d.NextOrder();  // being written
			(void)d.PickerKeep();
			g.away.insert(A);
			RunOrder(d, preview, g);  // gone before it landed
			g.away.erase(A);
			d.Seen(See(A, "Somebody"));
			const auto back = d.NextOrder();
			Check(d.Peek(back) && d.Peek(back)->kind == SH::OrderKind::kBody && d.Peek(back)->body.what == SH::BodyRequest::What::kPreset &&
					  !d.Peek(back)->body.preview && d.Peek(back)->body.choice == SH::Source::kPicker,
				"the body coming back goes out as the choice, its marker with it (a save right after keeps it)");
			RunOrder(d, back, g);
			(void)Drain(d, g);
			const auto  rec = d.RecordOf(A);
			const auto* kept = rec ? cat->Find(rec->preset, true) : nullptr;
			Check(rec && rec->source == SH::Source::kPicker && kept && kept->name != "Slim" && g.actors[A].unkeyed.contains(kept->marker) &&
					  !g.actors[A].unkeyed.contains("Silhouette_Slim"),
				"Keep while the preview was written, then gone: the kept body lands, with its choice");
			if (dllLess) {
				ReloadWithoutRecords(d, g);  // saved without the plugin (S-51's case)
				d.Seen(See(A, "Somebody"));
				(void)Drain(d, g);
				const auto again = d.RecordOf(A);
				Check(again && kept && again->source == SH::Source::kPicker && again->preset == kept->name,
					"and a save without the plugin rebuilds that choice, not the old body's");
			}
		}

		// Lens 1 L3: an announcement raised while she was picked is not forgotten by the Cancel: one body, one
		// OnActorGenerated -- this session and at the next load.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xD30;
			g.Roll(A, *cat, "Silhouette_Slim", 1234.0F);
			std::vector<SH::Event> raised;
			(void)d.RequestPreset(A, true, 0x00012345, "Curvy", SH::Source::kAPI, kNormal, why);  // another mod, before she is seen
			(void)Drain(d, g);
			(void)d.PickerStart(A, true, 0x00012345, "Somebody");  // picked within that second
			(void)Drain(d, g);
			append(raised, Poll(d, g));  // raised during the picking
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			(void)d.PickerStep(1);
			(void)Drain(d, g);
			append(raised, Poll(d, g));
			(void)d.PickerCancel();
			(void)Drain(d, g);
			append(raised, Poll(d, g));
			append(raised, Poll(d, g));
			Check(generated(raised, A, "Curvy") == 1, "announced once, though the picking around it was cancelled");
			Reload(d, g);
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			append(raised, Poll(d, g));
			Check(generated(raised, A, "Curvy") == 1, "and not again at the next load");
		}

		// Lens 1 L5: a choice marker on its way is not a body on its way: the top-up of a chosen body is not held
		// back by it, and happens this session (the with-marker case is the control).
		for (const bool withoutMarker : { false, true }) {
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xD40;
			(void)d.RequestPreset(A, true, 0x00012345, "Curvy", SH::Source::kPicker, kUrgent, why);
			(void)Drain(d, g);
			auto& a = g.actors[A];
			a.unkeyed.erase("NippleSize");  // a variety the body lacks: the top-up gives it (S-44)
			if (withoutMarker) {
				a.unkeyed.erase("Silhouette_Chosen");  // picked before S-51: no marker beside the body
			}
			Reload(d, g);
			{
				// ...in a session that wants the touch-up, as for a body an older plugin gave
				const auto   bytes = d.SaveRecords(nullptr);
				SH::Registry r;
				std::string  error;
				(void)r.Deserialize(bytes, SH::Registry::kVersion, [](std::uint32_t a_id) { return a_id; }, error);
				r.Get(A).touched = 0;
				const auto again = r.Serialize(nullptr);
				d.ForgetWorld();
				d.RevertRecords();
				(void)d.LoadRecords(again, SH::Registry::kVersion, [](std::uint32_t a_id) { return a_id; }, error);
				d.SetCatalog(cat);
				g.Load();
			}
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			for (int i = 0; i < 3; ++i) {
				d.Seen(See(A, "Somebody"));
				d.Dressed(See(A, "Somebody", i % 2 == 0), false);
				(void)Drain(d, g);
			}
			Check(a.unkeyed.contains("NippleSize"), std::format("a chosen body {} its marker is topped up this session", withoutMarker ? "without" : "with"));
		}

		// ...and the first announcement (S-46): a chosen body whose marker is being written back is announced this
		// session -- the marker leaves the body as it is, and nothing announces it later.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xD44;
			(void)d.RequestPreset(A, true, 0x00012345, "Curvy", SH::Source::kPicker, kUrgent, why);
			(void)Drain(d, g);                        // not raised before the save: nothing announced yet
			g.actors[A].unkeyed.erase("Silhouette_Chosen");  // picked before S-51: no marker beside the body
			Reload(d, g);
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			Check(Has(Poll(d, g), SH::EventKind::kGenerated, "Curvy") && g.actors[A].unkeyed.contains("Silhouette_Chosen"),
				"a chosen body whose marker is written back is announced this session, and gets its marker");
		}

		// ...and the half body (S-58): a reused created id whose previous owner's choice marker is being taken off
		// still gets the half body a save cut short given again whole this session.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xFF000D70;
			(void)d.RequestPreset(A, true, 0x00099999, "Curvy", SH::Source::kAPI, kNormal, why);  // the id's previous owner
			(void)Drain(d, g);
			Reload(d, g);
			// The id given to somebody new since, with half a body a save cut short and the old owner's choice
			// marker beside it.
			g.actors[A].unkeyed = { { "Silhouette_Slim", 0.25F }, { "Breasts", 0.2F }, { "Silhouette_Chosen", 4.0F } };
			g.Load();
			d.Seen(See(A, "Somebody"));  // another NPC record than the one the co-save knew: a stranger (S-57)
			(void)Drain(d, g);
			const auto& b = g.actors[A].unkeyed;
			std::string layer;
			for (const auto& [m, v] : b) {
				layer += std::format(" {}={}", m, v);
			}
			for (const auto& line : d.TakeLog()) {
				layer += " | " + line;
			}
			Check(b.contains("Silhouette_Slim") && b.at("Silhouette_Slim") == 1234.0F && !b.contains("Silhouette_Chosen"),
				std::format("a reused id's half body is given again whole this session, and the old owner's choice marker taken off ({})", layer));
		}

		// Lens 1 N9: the picker's snapshot comes back gone (she left memory): the picking ends, and what the probe
		// under the picker held back runs as soon as she is back -- not at her next sighting.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xD50;
			g.Roll(A, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestRegenerate(A, true, 0x00012345, kUrgent, why);
			Reload(d, g);  // the roll is owed
			g.rollsTo = "Silhouette_Athletic";
			d.Seen(See(A, "Somebody"));
			const auto probe = d.NextOrder();
			(void)d.PickerStart(A, true, 0x00012345, "Somebody");
			RunOrder(d, probe, g);  // probed under the picker: the roll waits
			const auto snapshot = d.NextOrder();
			g.away.insert(A);
			RunOrder(d, snapshot, g);  // gone
			g.away.erase(A);
			(void)Drain(d, g);
			Check(!d.PickerReady() && g.actors[A].unkeyed.contains("Silhouette_Athletic"), "a snapshot come back gone ends the picking, and the owed roll lands");
		}

		// Wave 6 lens 1: nobody is picked without an NPC record. A picking stored with base 0 was never found
		// stale when its created id went to somebody else (S-57), and a Cancel then put the previous owner's
		// choice and body on the newcomer.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xFF000D80;
			g.Roll(A, *cat, "Silhouette_Slim", 1234.0F);
			const auto said = d.PickerStart(A, true, 0, "Somebody");
			(void)Drain(d, g);
			Check(!d.PickerReady() && d.PickerTarget() == 0 && !d.HasPicking(A), std::format("an actor with no NPC record is not picked ({})", said));
		}

		// Lens 1 N8: someone Silhouette no longer shapes (their race left the build) keeps what was asked for
		// (S-59) -- and "Which body" says it waits for that, not that it is on its way.
		{
			SH::Director d;
			FakeGame     g;
			const auto   cat = Cat(BaseCatalog());
			d.SetCatalog(cat);
			constexpr std::uint32_t A = 0xD60;
			g.Roll(A, *cat, "Silhouette_Slim", 1234.0F);
			d.Seen(See(A, "Somebody"));
			(void)Drain(d, g);
			(void)d.RequestRegenerate(A, true, 0x00012345, kUrgent, why);
			Reload(d, g);  // the roll is owed
			auto notShaped = See(A, "Somebody");
			notShaped.eligible = false;
			d.Seen(notShaped);
			(void)Drain(d, g);
			const auto text = d.Describe(A);
			Check(text.find("once Silhouette shapes them again") != std::string::npos && d.RecordOf(A) && d.RecordOf(A)->source == SH::Source::kRoll,
				std::format("an owed roll on someone not shaped stays owed, and says it waits ({})", text));
		}
	}
}

// The generated files themselves, read by the plugin's own parser: what the game would refuse at load
// is refused here, before anything is deployed (L4F1). a_root is a Data folder or a mod folder.
int CheckData(const std::filesystem::path& a_root)
{
	const auto  folder = a_root / "F4SE/Plugins/Silhouette";
	std::string error;
	std::ifstream in(folder / "catalog.json", std::ios::binary);
	if (!in) {
		std::cout << "CHECK FAIL: no " << (folder / "catalog.json").string() << "\n";
		return 1;
	}
	std::optional<SH::Catalog> catalog;
	try {
		catalog = SH::ParseCatalog(nlohmann::json::parse(in), error);
	} catch (const std::exception& e) {
		error = e.what();
	}
	if (!catalog) {
		std::cout << "CHECK FAIL: the plugin would refuse catalog.json: " << error << "\n";
		return 1;
	}
	int             failed = 0;
	std::size_t     manifests = 0;
	bool            own = false;
	std::error_code ec;
	// As the plugin names and compares them: path::string() throws for a name the ANSI code page cannot hold.
	const auto nameOf = [](const std::filesystem::path& a_path) {
		const auto u = a_path.filename().generic_u8string();
		return std::string{ reinterpret_cast<const char*>(u.data()), u.size() };
	};
	for (std::filesystem::directory_iterator it{ folder / "manifests", ec }, end; !ec && it != end; it.increment(ec)) {
		const auto& path = it->path();
		if (path.extension() != ".json") {
			continue;
		}
		std::ifstream m(path, std::ios::binary);
		std::optional<std::pair<std::uint32_t, std::unordered_map<std::string, SH::ManifestEntry>>> parsed;
		try {
			parsed = SH::ParseManifest(nlohmann::json::parse(m), error);
		} catch (const std::exception& e) {
			error = e.what();
		}
		if (!parsed) {
			std::cout << "CHECK FAIL: manifest " << nameOf(path) << ": " << error << "\n";
			++failed;
			continue;
		}
		if (path.stem().wstring() != std::to_wstring(parsed->first)) {
			std::cout << std::format("CHECK FAIL: manifest {} says it is stamp {}: the plugin reads a manifest only under its own stamp's name\n",
				nameOf(path), parsed->first);
			++failed;
			continue;
		}
		if (parsed->first == catalog->stamp) {
			own = true;
			// Every preset of this build must be named by its manifest: a body of it is named through the
			// catalog today, but only the manifest names it once a later build is installed.
			for (const auto& p : catalog->presets) {
				const auto named = std::ranges::find_if(parsed->second, [&](const auto& e) { return SH::IEquals(e.first, p.marker); });
				if (named == parsed->second.end() || named->second.preset != p.name) {
					std::cout << std::format("CHECK FAIL: manifest {} does not name {} as {}\n", nameOf(path), p.marker, p.name);
					++failed;
				}
			}
		}
		++manifests;
	}
	if (ec) {
		std::cout << "CHECK FAIL: manifests: " << ec.message() << "\n";
		++failed;
	}
	if (!own) {
		std::cout << "CHECK FAIL: no manifest for this build's stamp " << catalog->stamp << "\n";
		++failed;
	}
	for (const auto* file : { "Silhouette_templates.ini", "Silhouette_morphs.ini" }) {
		std::ifstream h(a_root / "F4SE/Plugins/F4EE/BodyGen/Loose" / file);
		const auto    header = h ? SH::ParseFilesHeader(h) : std::nullopt;
		if (!header) {
			std::cout << "CHECK FAIL: " << file << " names no build in its header\n";
			++failed;
		} else if (header->build != catalog->build || header->stamp != catalog->stamp || header->rules != catalog->rulesHash) {
			std::cout << std::format("CHECK FAIL: {} is build {} stamp {} rules {}, the catalog build {} stamp {} rules {}\n", file,
				header->build, header->stamp, header->rules, catalog->build, catalog->stamp, catalog->rulesHash);
			++failed;
		}
	}
	if (failed == 0) {
		std::cout << std::format("CHECK OK: build {}, stamp {}, rules {}: {} presets, {} refit set(s), {} manifest(s), both BodyGen headers agree\n",
			catalog->build, catalog->stamp, catalog->rulesHash, catalog->presets.size(), catalog->refitSets.size(), manifests);
	}
	return failed;
}

// S-68, MCM's "Reset everyone": a fresh start -- everyone seen now, every choice on record wherever they
// are, and anyone met later whose Silhouette body an older build made; picks included (owner poll). The
// catalog's build is stamp 1234; stamp 99 is an older build's (Cat's manifest names its Curvy).
static void TestResetEveryone()
{
	using Layer = std::map<std::string, float>;
	std::string why;
	std::string said;
	{
		SH::Director d;
		FakeGame     g;
		const auto   cat = Cat(BaseCatalog());
		d.SetCatalog(cat);
		g.Roll(0xA00, *cat, "Silhouette_Curvy", 99.0F);   // an older build's roll, seen this session
		g.Roll(0xA10, *cat, "Silhouette_Slim", 1234.0F);  // this build's roll, seen this session
		g.Roll(0xA20, *cat, "Silhouette_Slim", 1234.0F);  // picked, then out of sight: a record only
		g.Roll(0xA30, *cat, "Silhouette_Curvy", 99.0F);   // an older build's roll, met in an earlier session
		g.Roll(0xA40, *cat, "Silhouette_Slim", 1234.0F);  // this build's roll, met in an earlier session
		g.Roll(0xA50, *cat, "Silhouette_Slim", 1234.0F);  // blacklisted by name
		d.Seen(See(0xA20, "Somebody"));
		d.Seen(See(0xA50, "Mama Murphy"));
		(void)Drain(d, g);
		Check(d.RequestPreset(0xA20, true, 0x00012345, "Curvy", SH::Source::kPicker, kUrgent, why), "(set-up) picked");
		(void)Drain(d, g);
		Check(g.actors[0xA20].unkeyed.contains("Silhouette_Chosen"), "(set-up) the pick carries its marker");
		Reload(d, g);
		d.Seen(See(0xA00, "Somebody"));
		d.Seen(See(0xA10, "Somebody"));
		d.Seen(See(0xA50, "Mama Murphy"));
		(void)Drain(d, g);
		Check(g.actors[0xA00].unkeyed.contains("Silhouette_Curvy") && g.actors[0xA00].unkeyed.at("Silhouette_Curvy") == 99.0F,
			"(set-up) before the press an older build's body is left as it is");

		g.rollsTo = "Silhouette_Athletic";
		Check(d.RequestResetEveryone(said), "Reset everyone is accepted");
		Check(said.starts_with("2 around you"), std::format("the answer counts who changes now ({})", said));
		(void)Drain(d, g);
		Check(g.actors[0xA00].unkeyed.contains("Silhouette_Athletic") && !g.actors[0xA00].unkeyed.contains("Silhouette_Curvy"),
			"someone seen this session gets a new body at once");
		Check(g.actors[0xA10].unkeyed.contains("Silhouette_Athletic"), "... a body this build made too: everyone around");
		Check((g.actors[0xA50].unkeyed == Layer{ { "Silhouette_Blacklisted", 1234.0F } }), "someone blacklisted by name stays bare");

		d.Seen(See(0xA20, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0xA20].unkeyed.contains("Silhouette_Athletic") && !g.actors[0xA20].unkeyed.contains("Silhouette_Chosen") &&
				  !d.RecordOf(0xA20).value_or(SH::Record{}).Intent(),
			"a pick made before the press is forgotten wherever they were, marker and all (owner: picks too)");
		d.Seen(See(0xA30, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0xA30].unkeyed.contains("Silhouette_Athletic"), "someone met later with an older build's body is decided again when met");
		d.Seen(See(0xA40, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0xA40].unkeyed.contains("Silhouette_Slim"), "someone met later with this build's body keeps it: the rules of now made it");

		// Once, and across a save; a newer build's bodies came after the press.
		g.rollsTo = "Silhouette_Slim";
		g.Roll(0xA60, *cat, "Silhouette_Curvy", 99.0F);
		auto next = BaseCatalog();
		next["stamp"] = 5678;
		Reload(d, g, next);
		g.Roll(0xA70, *cat, "Silhouette_Curvy", 5678.0F);
		for (const std::uint32_t r : { 0xA00u, 0xA30u, 0xA60u, 0xA70u }) {
			d.Seen(See(r, "Somebody"));
		}
		(void)Drain(d, g);
		Check(g.actors[0xA00].unkeyed.contains("Silhouette_Athletic") && g.actors[0xA30].unkeyed.contains("Silhouette_Athletic"),
			"decided again once: the next session leaves the new bodies be");
		Check(g.actors[0xA60].unkeyed.contains("Silhouette_Slim"), "the press survives a save: an older build's body met after it is decided again");
		Check(g.actors[0xA70].unkeyed.contains("Silhouette_Curvy") && g.actors[0xA70].unkeyed.at("Silhouette_Curvy") == 5678.0F,
			"a body a newer build made came after the press: kept");

		// Blacklisted by name, wearing another mod's choice (a choice beats the blacklist): the fresh start
		// forgets the choice, and the blacklist has its say -- bare, in this session, not at the next.
		g.Roll(0xA90, *cat, "Silhouette_Slim", 5678.0F);
		d.Seen(See(0xA90, "Mama Murphy"));
		(void)Drain(d, g);
		Check(d.RequestPreset(0xA90, true, 0x00012345, "Curvy", SH::Source::kAPI, kNormal, why), "(set-up) another mod's choice");
		(void)Drain(d, g);
		Check(g.actors[0xA90].unkeyed.contains("Silhouette_Chosen"), "(set-up) the choice is on her, beside Curvy");
		Check(d.RequestResetEveryone(said), "pressed again");
		(void)Drain(d, g);
		Check((g.actors[0xA90].unkeyed == Layer{ { "Silhouette_Blacklisted", 5678.0F } }),
			"someone blacklisted by name who wore a choice is bare after the press, in the same session");

		// A choice another mod asked for before the press, for someone never seen, not written yet: it does not
		// land after the press -- the roll owed takes its place.
		g.Roll(0xAA0, *cat, "Silhouette_Slim", 5678.0F);
		Check(d.RequestPreset(0xAA0, true, 0x00012345, "Curvy", SH::Source::kAPI, kNormal, why), "(set-up) another mod asks; nothing written yet");
		Check(d.RequestResetEveryone(said), "pressed with that choice still queued");
		(void)Drain(d, g);
		Check(!g.actors[0xAA0].unkeyed.contains("Silhouette_Chosen") && !g.actors[0xAA0].unkeyed.contains("Silhouette_Curvy"),
			"a choice queued before the press does not land after it");

		// A save made without the plugin carries no reset: the next session follows none.
		g.Roll(0xA80, *cat, "Silhouette_Curvy", 99.0F);
		ReloadWithoutRecords(d, g, next);
		d.Seen(See(0xA80, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0xA80].unkeyed.contains("Silhouette_Curvy"), "a save without the reset's record decides nobody again");
	}
	{
		SH::Director d;
		FakeGame     g;
		const auto   cat = Cat(BaseCatalog());
		d.SetCatalog(cat);
		g.Roll(0xB00, *cat, "Silhouette_Slim", 1234.0F);
		d.Seen(See(0xB00, "Somebody"));
		(void)Drain(d, g);
		(void)d.PickerStart(0xB00, true, 0x00012345, "Somebody");
		(void)Drain(d, g);
		Check(!d.RequestResetEveryone(said) && said.contains("picked"), "refused while the picker is open");
		(void)d.PickerCancel();
		(void)Drain(d, g);

		// A race Silhouette does not distribute to is not its at all (S-11): whatever body they have is left be,
		// a Silhouette one from an older build included.
		auto ghoul = See(0xB10, "Somebody");
		ghoul.facts.race = "GhoulRace";
		g.Roll(0xB10, *cat, "Silhouette_Curvy", 99.0F);
		auto other = See(0xB20, "Somebody");
		other.facts.race = "GhoulRace";
		g.actors[0xB20].unkeyed = { { "Breasts", 0.4F } };
		g.actors[0xB20].listed = { "Breasts" };
		d.Seen(ghoul);
		d.Seen(other);
		(void)Drain(d, g);
		g.rollsTo = "Silhouette_Athletic";
		Check(d.RequestResetEveryone(said), "accepted once the picking is over");
		(void)Drain(d, g);
		Check(g.actors[0xB10].unkeyed.contains("Silhouette_Curvy"), "a race Silhouette does not distribute to keeps even a Silhouette body (S-11)");
		Check((g.actors[0xB20].unkeyed == Layer{ { "Breasts", 0.4F } }), "... and another mod's body");

		// Someone a blacklist keeps from BodyGen, wearing another mod's body: nothing of Silhouette's, nothing
		// for BodyGen to give -- not touched.
		auto blocked = See(0xB40, "Somebody");
		blocked.facts.originPlugin = "Blocked.esp";
		g.actors[0xB40].unkeyed = { { "Breasts", 0.6F } };
		g.actors[0xB40].listed = { "Breasts" };
		d.Seen(blocked);
		(void)Drain(d, g);
		Check(d.RequestResetEveryone(said), "pressed again");
		(void)Drain(d, g);
		Check((g.actors[0xB40].unkeyed == Layer{ { "Breasts", 0.6F } }), "a blacklisted NPC's body from another mod is left alone");

		// A named character (a form-id rule: BodyGen's own line for them) met after the press.
		auto named = See(0xB30, "Piper Wright");
		named.facts.bases = { { "Fallout4.esm", 1000 } };
		g.Roll(0xB30, *cat, "Silhouette_Curvy", 99.0F);
		d.Seen(named);
		(void)Drain(d, g);
		Check(g.actors[0xB30].unkeyed.contains("Silhouette_Athletic"), "a named character met after the press is rolled again: their own line gives their body");
	}
	{
		// S-70: bodies Silhouette did not make -- from before it was installed, or another mod's. They cannot be
		// dated, so the press reaches each of them once: at the first sighting since it, wherever they are.
		SH::Director d;
		FakeGame     g;
		const auto   cat = Cat(BaseCatalog());
		d.SetCatalog(cat);
		const auto foreign = [&](std::uint32_t a_ref, float a_value) {
			g.actors[a_ref].unkeyed = { { "Breasts", a_value } };
			g.actors[a_ref].listed = { "Breasts" };
		};
		foreign(0xC00, 0.4F);  // seen before the press
		foreign(0xC10, 0.5F);  // met after it, in the same session
		foreign(0xC20, 0.6F);  // met after it, in a later session
		foreign(0xC30, 0.3F);  // met after it, but no BodyGen line gives them a body
		d.Seen(See(0xC00, "Somebody"));
		(void)Drain(d, g);
		Check((g.actors[0xC00].unkeyed == Layer{ { "Breasts", 0.4F } }), "(set-up) never pressed: a body Silhouette did not make is left alone");

		g.rollsTo = "Silhouette_Athletic";
		Check(d.RequestResetEveryone(said), "pressed");
		(void)Drain(d, g);
		Check(g.actors[0xC00].unkeyed.contains("Silhouette_Athletic") && !g.actors[0xC00].unkeyed.contains("Breasts"),
			"seen at the press: the body from before Silhouette is replaced at once");
		d.Seen(See(0xC10, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0xC10].unkeyed.contains("Silhouette_Athletic"), "met after the press: a body Silhouette did not make is decided again");

		// After that sighting, what is put on them is theirs: sliders set by hand, another mod's body.
		g.actors[0xC10].unkeyed = { { "Breasts", 0.9F } };
		g.actors[0xC10].listed = { "Breasts" };
		auto blocked = See(0xC30, "Somebody");
		blocked.facts.originPlugin = "Blocked.esp";
		Reload(d, g);
		d.Seen(See(0xC10, "Somebody"));
		d.Seen(See(0xC20, "Somebody"));
		d.Seen(blocked);
		(void)Drain(d, g);
		Check((g.actors[0xC10].unkeyed == Layer{ { "Breasts", 0.9F } }),
			"a body put on someone after their first sighting since the press stays theirs, across a save");
		Check(g.actors[0xC20].unkeyed.contains("Silhouette_Athletic"), "first met in a later session: decided again then");
		Check((g.actors[0xC30].unkeyed == Layer{ { "Breasts", 0.3F } }), "no BodyGen line gives them a body: left alone");

		g.actors[0xC20].unkeyed = { { "Breasts", 0.2F } };
		g.actors[0xC20].listed = { "Breasts" };
		d.Seen(See(0xC20, "Somebody"));
		(void)Drain(d, g);
		Check((g.actors[0xC20].unkeyed == Layer{ { "Breasts", 0.2F } }), "... and in the same session: only the first sighting counts");

		// Seen at the press, given a body by it, then sliders set by hand: the press already looked at them.
		g.actors[0xC00].unkeyed = { { "Breasts", 0.8F } };
		g.actors[0xC00].listed = { "Breasts" };
		// Only other mods' keyed morphs: BodyGen never rolls someone who holds any morph at all.
		g.actors[0xC50].other = { { "Erection", 1.0F } };
		g.actors[0xC50].listed = { "Erection" };
		Reload(d, g);
		d.Seen(See(0xC00, "Somebody"));
		d.Seen(See(0xC50, "Somebody"));
		(void)Drain(d, g);
		Check((g.actors[0xC00].unkeyed == Layer{ { "Breasts", 0.8F } }), "someone the press itself reached keeps what was put on them after it");
		Check(g.actors[0xC50].unkeyed.contains("Silhouette_Athletic"), "someone holding only another mod's keyed morphs gets a body when met");

		// Pressed again, with nobody seen yet this session: a new start, the list with it.
		foreign(0xC40, 0.7F);
		Reload(d, g);
		Check(d.RequestResetEveryone(said), "pressed again");
		d.Seen(See(0xC20, "Somebody"));
		d.Seen(See(0xC40, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0xC20].unkeyed.contains("Silhouette_Athletic") && g.actors[0xC40].unkeyed.contains("Silhouette_Athletic"),
			"a new press reaches everyone's body again when met, a hand-set one looked at after the last press too");

		// A created reference's id handed to somebody new (S-57) is a first sighting, across a save too; the same
		// person seen again is not.
		foreign(0xFF000C60, 0.4F);
		d.Seen(See(0xFF000C60, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0xFF000C60].unkeyed.contains("Silhouette_Athletic"), "(set-up) a created actor met since the press is decided again");
		Reload(d, g);
		foreign(0xFF000C60, 0.6F);
		auto newcomer = See(0xFF000C60, "Somebody Else");
		newcomer.base = 0x00054321;
		d.Seen(newcomer);
		(void)Drain(d, g);
		Check(g.actors[0xFF000C60].unkeyed.contains("Silhouette_Athletic"), "the id handed to somebody new: their body is decided again, the list notwithstanding");
		foreign(0xFF000C60, 0.7F);
		Reload(d, g);
		d.Seen(newcomer);
		(void)Drain(d, g);
		Check((g.actors[0xFF000C60].unkeyed == Layer{ { "Breasts", 0.7F } }), "... and once: the same newcomer seen again keeps what was put on them");

		// A NEW GAME started after that save, in the same session: main.cpp's kNewGame forgets the world and
		// reverts the records (CoSave::Revert). Nothing of the pressed save may reach the new one.
		d.ForgetWorld();
		d.RevertRecords();
		foreign(0xC70, 0.5F);
		d.Seen(See(0xC70, "Somebody"));
		(void)Drain(d, g);
		Check((g.actors[0xC70].unkeyed == Layer{ { "Breasts", 0.5F } }) && d.SaveReset().empty(),
			"a new game carries no press of the save played before it");

		// S-70 on a new game: the regeneration window's quest starts with the game and presses Reset everyone
		// before anyone is seen -- during character creation. Another new game, so nobody is seen yet.
		d.ForgetWorld();
		d.RevertRecords();
		said.clear();
		const bool freshStart = d.RequestResetEveryone(said);
		Check(freshStart && said.starts_with("0 around you"),
			std::format("a new game's fresh start is accepted with nobody seen yet ({}: {})", freshStart, said));
		// The player and the character-creation dummies (whose body LooksMenu clones onto the player) are
		// never shaped: not by the press, not by what follows it (S-13, S-45).
		auto player = See(0x14, "Player");
		player.eligible = false;
		auto dummy = See(0x0A7D35, "MQ101PlayerSpouseFemale");
		dummy.eligible = false;
		g.actors[0x14].unkeyed = { { "Breasts", 0.3F } };
		g.actors[0x14].listed = { "Breasts" };
		g.actors[0x0A7D35].unkeyed = { { "Breasts", 0.3F } };
		g.actors[0x0A7D35].listed = { "Breasts" };
		d.Seen(player);
		d.Seen(dummy);
		const auto drained = Drain(d, g);
		Check(drained == 0 && (g.actors[0x14].unkeyed == Layer{ { "Breasts", 0.3F } }) &&
				  (g.actors[0x0A7D35].unkeyed == Layer{ { "Breasts", 0.3F } }),
			std::format("on a new game the fresh start never reaches the player nor a character-creation dummy ({} orders; player {}, dummy {})",
				drained, g.actors[0x14].unkeyed.size(), g.actors[0x0A7D35].unkeyed.size()));
		// ...while the first NPC met after it, wearing a body Silhouette did not make, is decided as for any save.
		foreign(0xC80, 0.6F);
		d.Seen(See(0xC80, "Somebody"));
		(void)Drain(d, g);
		Check(g.actors[0xC80].unkeyed.contains("Silhouette_Athletic"), "a new game's fresh start reaches the people met afterwards");
	}
	{
		SH::Registry r;
		std::string  error;
		Check(r.SerializeReset().empty(), "never pressed: no reset record");
		r.resetStamps = { 1234, 5678 };
		const auto   bytes = r.SerializeReset();
		SH::Registry back;
		Check(back.DeserializeReset(bytes, SH::Registry::kResetVersion, error) == SH::Registry::Loaded::kOk && back.resetStamps == r.resetStamps,
			"the reset record reads back");
		auto longer = bytes;
		longer.insert(longer.end(), { std::byte{ 7 }, std::byte{ 7 } });
		Check(back.DeserializeReset(longer, SH::Registry::kResetVersion, error) == SH::Registry::Loaded::kOk && back.resetStamps == r.resetStamps,
			"fields a later version appends are skipped");
		Check(bytes.size() == 2 + 2 * 4 + 4 + 4, "the reset record is a count and its stamps, then who was met since: the tag, and nobody");

		// S-70: who was met since the press follows the stamps, in form ids resolved for this session.
		r.resetMet = { { 0x0A000001, 0x0A000800 }, { 0x0B000002, 0x0B000801 }, { 0x00000C03, 0x00000C04 } };
		const auto withMet = r.SerializeReset();
		SH::Registry met;
		const auto   moved = [](std::uint32_t a_id) -> std::uint32_t {
			if (a_id >> 24 == 0x0A) {
				return (a_id & 0xFFFFFF) | 0x0C000000;  // the plugin moved in the load order
			}
			return a_id >> 24 == 0x0B ? 0 : a_id;  // that plugin is gone
		};
		Check(met.DeserializeReset(withMet, SH::Registry::kResetVersion, error, moved) == SH::Registry::Loaded::kOk &&
				  met.resetStamps == r.resetStamps && (met.resetMet == std::unordered_map<std::uint32_t, std::uint32_t>{ { 0x0C000001, 0x0C000800 }, { 0x00000C03, 0x00000C04 } }),
			"who was met since the press reads back, resolved: moved ids follow, gone ones drop");
		SH::Registry old;
		old.resetMet = { { 99, 1 } };
		Check(old.DeserializeReset(std::span{ withMet }.first(2 + 2 * 4), SH::Registry::kResetVersion, error) == SH::Registry::Loaded::kOk &&
				  old.resetStamps == r.resetStamps && old.resetMet.empty(),
			"a record from before S-70 (the stamps alone): nobody met since the press yet");
		{
			// The first build of S-70 wrote references alone after the stamps, no tag: read, with no record known.
			auto refsOnly = std::vector<std::byte>(withMet.begin(), withMet.begin() + 2 + 2 * 4);
			for (const std::uint32_t v : { 2u, 0x00000C03u, 0x00000C05u }) {
				for (int i = 0; i < 4; ++i) {
					refsOnly.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
				}
			}
			SH::Registry early;
			Check(early.DeserializeReset(refsOnly, SH::Registry::kResetVersion, error) == SH::Registry::Loaded::kOk &&
					  (early.resetMet == std::unordered_map<std::uint32_t, std::uint32_t>{ { 0x00000C03, 0 }, { 0x00000C05, 0 } }),
				"the first S-70 build's list (references alone) is read, each with its record unknown");
		}
		r.resetMet.clear();
		SH::Registry cut;
		cut.resetStamps = { 42 };
		Check(bytes.size() >= 2 &&
				  cut.DeserializeReset(std::span{ bytes }.first(bytes.size() - 2), SH::Registry::kResetVersion, error) == SH::Registry::Loaded::kRefused &&
				  cut.resetStamps == std::vector<std::uint32_t>{ 42 },
			"cut short: refused, nothing replaced");
		Check(cut.DeserializeReset(bytes, SH::Registry::kResetVersion + 1, error) == SH::Registry::Loaded::kNewer, "a newer version is not read");

		SH::Registry f;
		f.Get(1).source = SH::Source::kPicker;
		f.Get(1).preset = "Curvy";
		f.Get(2).source = SH::Source::kNameRule;
		f.Get(2).preset = "Slim";
		f.Get(2).salt = 3;
		f.Get(3).source = SH::Source::kNameBlacklist;
		f.Get(4).source = SH::Source::kReset;
		f.Keep(SH::PickerSave{ .ref = 5, .base = 77 });
		const auto changed = f.ForgetChoices();
		Check(f.Find(1)->source == SH::Source::kRoll && f.Find(1)->preset.empty(), "a pick on record becomes a roll owed");
		Check(f.Find(2)->source == SH::Source::kNameRule && f.Find(2)->preset.empty() && f.Find(2)->salt == 0,
			"a rule's kept draw goes back to the draw by id alone");
		Check(f.Find(3)->source == SH::Source::kNameBlacklist && f.Find(4)->source == SH::Source::kReset, "a name blacklist and a reset owed stay as they are");
		Check(f.pickings.empty() && f.Find(5) && f.Find(5)->source == SH::Source::kRoll && f.Find(5)->base == 77,
			"a picking in progress becomes a roll owed");
		Check(changed == 3, std::format("the count is the pick, the rule's draw and the picking ({})", changed));
	}
}

// The crosshair (the Pick hotkey, the menu's target): the picks the sink saw, chosen on the main thread.
// Handles 0x100-0x1FF stand for NPCs Silhouette shapes, anything else for a door or a chair.
static void TestCrosshairTrail()
{
	using SH::CrosshairTrail;
	const auto npc = [](std::uint32_t a_handle) { return a_handle >= 0x100 && a_handle < 0x200; };
	{
		CrosshairTrail t;
		Check(t.Choose(0, 1000, npc) == 0, "crosshair: nothing seen, nobody picked");
		t.Note(0x101, 1000);
		Check(t.Choose(0, 1000, npc) == 0x101, "crosshair: the NPC under the crosshair is picked");
		t.Note(0x050, 2000);
		Check(t.Choose(0, 2000, npc) == 0, "crosshair: Pick takes only what is under the crosshair now; a door is nobody");
		Check(t.Choose(30'000, 2000, npc) == 0x101, "crosshair: the menu takes the NPC the crosshair left, within the window");
		Check(t.Choose(30'000, 32'000, npc) == 0x101, "crosshair: ... to the window's last millisecond");
		Check(t.Choose(30'000, 32'001, npc) == 0, "crosshair: ... and not after it");
	}
	{
		CrosshairTrail t;
		t.Note(0x101, 1000);
		t.Note(0x101, 50'000);  // the view caster reports every update: the same pick changes nothing
		t.Note(0, 60'000);
		Check(t.Choose(30'000, 89'000, npc) == 0x101, "crosshair: a long look counts from when the crosshair left them, not from the first sight");
	}
	{
		CrosshairTrail t;
		t.Note(0x101, 1000);
		for (std::int64_t frame = 0; frame < 1000; ++frame) {
			t.Note(0x050, 2000 + frame);  // a door, reported on every update
		}
		Check(t.Choose(30'000, 3000, npc) == 0x101, "crosshair: a pick reported on every update is one pick, and does not push the NPC out");
	}
	{
		CrosshairTrail t;
		t.Note(0x101, 1000);
		t.Note(0x102, 2000);
		t.Note(0x050, 3000);
		t.Note(0, 4000);
		Check(t.Choose(30'000, 5000, npc) == 0x102, "crosshair: the newest NPC left wins over an older one");
		Check(t.Choose(30'000, 5000, [](std::uint32_t a_handle) { return a_handle == 0x101; }) == 0x101,
			"crosshair: an older pick is found when the newer ones are not wanted");
		Check(t.Current() == 0, "crosshair: nothing under it now");
	}
	{
		CrosshairTrail t;
		t.Note(0x101, 1000);
		for (std::uint32_t i = 0; i < CrosshairTrail::kKept; ++i) {
			t.Note(0x1000 + i, 2000 + i);  // the first moves the NPC into the trail, each next one a door
		}
		Check(t.Choose(30'000, 3000, npc) == 0x101, "crosshair: the NPC is still found behind kKept - 1 doors");
		t.Note(0x2000, 3001);
		Check(t.Choose(30'000, 3001, npc) == 0, "crosshair: past kKept picks the oldest is forgotten");
	}
	{
		CrosshairTrail t;
		t.Note(0x101, 1000);
		t.Note(0x102, 2000);  // 0x101 is in the trail, 0x102 under the crosshair
		t.Forget();
		Check(t.Current() == 0 && t.Choose(30'000, 2000, npc) == 0, "crosshair: a load forgets the pick and the trail");
	}
}

	// ------------------------------------------------------------------ S-76: the player's own presets

	// A BodySlide .tri: one shape per entry, each morph moving one vertex.
	std::vector<std::byte> Tri(const std::vector<std::pair<std::string, std::vector<std::string>>>& a_shapes)
	{
		std::string raw = "PIRT";
		const auto  u16 = [&](std::uint16_t v) { raw.append(reinterpret_cast<const char*>(&v), 2); };
		u16(static_cast<std::uint16_t>(a_shapes.size()));
		for (const auto& [shape, morphs] : a_shapes) {
			raw += static_cast<char>(shape.size());
			raw += shape;
			u16(static_cast<std::uint16_t>(morphs.size()));
			for (const auto& m : morphs) {
				raw += static_cast<char>(m.size());
				raw += m;
				const float mult = 0.01F;
				raw.append(reinterpret_cast<const char*>(&mult), 4);
				u16(1);
				raw.append(8, '\0');  // vertex 0, no move
			}
		}
		std::vector<std::byte> out(raw.size());
		std::memcpy(out.data(), raw.data(), raw.size());
		return out;
	}

	bool Near(float a_lhs, float a_rhs) { return std::abs(a_lhs - a_rhs) < 1e-5F; }

	const SH::Preset* Added(const SH::Presets::Installed& a_result, std::string_view a_name)
	{
		const auto it = std::ranges::find(a_result.added, a_name, &SH::Preset::name);
		return it == a_result.added.end() ? nullptr : &*it;
	}

	float ValueOf(const SH::Preset& a_preset, std::string_view a_morph)
	{
		const auto it = std::ranges::find(a_preset.values, a_morph, &std::pair<std::string, float>::first);
		return it == a_preset.values.end() ? -99.0F : it->second;
	}

	void TestInstalledPresets()
	{
		namespace P = SH::Presets;
		// ---- reading a SliderPresets file, as BodySlide's SliderPresets.cpp does
		const auto ps = P::ParseXml(R"(<?xml version="1.0" encoding="utf-8"?>
<SliderPresets>
	<!-- <Preset name="Commented out"/> -->
	<Preset name="Mine &amp; Yours" set="CBBE Body">
		<Group name="CBBE"/>
		<Group name="CBBE Outfits"/>
		<SetSlider name="Breasts" size="big" value="40"/>
		<SetSlider name="Breasts" size="small" value="10"/>
		<SetSlider name="Waist" size="both" value="-20"/>
		<SetSlider name="Butt" value="90"/>
		<SetSlider name="Waist" size="big" value="30"/>
	</Preset>
	<Preset name='Empty'/>
</SliderPresets>)");
		Check(ps.size() == 2 && ps[0].name == "Mine & Yours" && ps[1].name == "Empty" && ps[1].big.empty(),
			"presets: a file's presets are read, an entity decoded, a comment skipped, a self-closed preset kept");
		Check(ps.size() == 2 && ps[0].families == std::vector<std::string>{ "CBBE" }, "presets: a group naming outfits is not a body family");
		Check(ps.size() == 2 && ps[0].big.size() == 2 && ps[0].big[0].first == "Breasts" && Near(static_cast<float>(ps[0].big[0].second), 0.4F) &&
				  ps[0].big[1].first == "Waist" && Near(static_cast<float>(ps[0].big[1].second), 0.3F),
			"presets: big and both count, small and no size do not, the later value of a slider wins");

		// ---- markers: the generator's plain_marker, character for character
		Check(P::PlainMarker("CBBE Curvy") == "Silhouette_CBBE_Curvy" &&
				  P::PlainMarker("The Rocket Bomb Body CBBE Extra") == "Silhouette_The_Rocket_Bomb_Body_CBBE_Extra" &&
				  P::PlainMarker("Josie CBBE Body 2 (Nude)") == "Silhouette_Josie_CBBE_Body_2_Nude" &&
				  P::PlainMarker("xy - Type 3DCG (Blessed)(2)(a)") == "Silhouette_xy_Type_3DCG_Blessed_2_a" &&
				  P::PlainMarker("A=B/C,D|E@F") == "Silhouette_A_B_C_D_E_F" && P::PlainMarker("!!!").empty(),
			"presets: a marker is named exactly as the generator names it");

		// ---- the body's morphs, from its .tri
		const auto tri = Tri({ { "CBBE", { "Breasts", "Waist" } }, { "AnatomyGenitals", { "VaginaPenetrate" } } });
		const auto morphs = P::TriMorphs(tri);
		Check(morphs == std::unordered_set<std::string>{ "Breasts", "Waist", "VaginaPenetrate" }, "presets: every shape's morphs are read from a .tri");
		auto cut = tri;
		cut.resize(cut.size() - 3);
		Check(P::TriMorphs(cut).empty() && P::TriMorphs(std::vector<std::byte>(8)).empty(), "presets: a cut or foreign .tri reads as no body");

		// ---- resolving: the generator's classify, band and resolve
		auto doc = BaseCatalog();
		doc["sliderSets"] = nlohmann::json::parse(R"({"female": {"set": "CBBE Body", "sliders": {
			"Breasts": [0.0, false], "Waist": [0.5, false], "Butt": [0.0, true], "VaginaPenetrate": [0.3, false], "Arms": [0.0, false]}}})");
		const auto cat = Cat(doc);
		const std::unordered_set<std::string> bodies[2] = { { "BTChest" }, { "Breasts", "Waist", "Butt", "VaginaPenetrate" } };
		const auto one = [](std::string a_name, std::vector<std::string> a_families, std::vector<std::pair<std::string, double>> a_big) {
			return P::SliderPreset{ std::move(a_name), std::move(a_families), std::move(a_big) };
		};
		const std::vector<P::SliderPreset> files{
			one("Mine", { "CBBE" }, { { "Breasts", 0.4F }, { "Butt", 0.2F } }),
			one("mine", { "CBBE" }, { { "Breasts", 0.9F } }),
			one("Curvy", { "CBBE" }, { { "Breasts", 0.1F } }),
			one("Mine (Outfit)", { "CBBE" }, { { "Breasts", 0.3F } }),
			one("Half", { "Fusion Girl" }, { { "Breasts", 0.5F }, { "Zzz", 0.5F } }),
			one("HalfCBBE", { "CBBE" }, { { "Breasts", 0.5F }, { "Zzz", 0.5F } }),
			one("Low", {}, { { "Breasts", 0.5F }, { "Zzz", 0.5F }, { "Yyy", 0.5F } }),
			one("Man", {}, { { "BTChest", 0.3F } }),
			one("Curvy!", { "CBBE" }, { { "Breasts", 0.6F } }),
			one("Mine-Refit", { "CBBE" }, { { "Breasts", 0.6F } }),
			one("Fine Digits", { "CBBE" }, { { "Breasts", 0.123456 }, { "Butt", 1.00004 } }),
		};
		const auto result = P::Resolve(*cat, files, bodies);
		const auto* mine = Added(result, "Mine");
		Check(mine && mine->female && mine->marker == "Silhouette_Mine" && mine->installed && mine->menu && !mine->random &&
				  mine->fit == "full" && mine->family == "CBBE",
			"presets: a full fit joins the pickers, never random, named and marked as the generator would");
		Check(mine && Near(ValueOf(*mine, "Breasts"), 0.4F) && Near(ValueOf(*mine, "Waist"), 0.5F) && Near(ValueOf(*mine, "Butt"), 0.8F),
			"presets: a slider the preset leaves out takes the set's default, an inverted one is turned");
		Check(mine && ValueOf(*mine, "VaginaPenetrate") == -99.0F && ValueOf(*mine, "Arms") == -99.0F,
			"presets: never a runtime state, and never a morph the body does not carry");
		Check(!Added(result, "mine") && !Added(result, "Curvy") && !Added(result, "Mine (Outfit)") && !Added(result, "Mine-Refit"),
			"presets: the first of a name wins, the catalog's own stays, outfit copies and refit sets are left out");
		Check(!Added(result, "Half") && Added(result, "HalfCBBE") && Added(result, "HalfCBBE")->fit == "partial",
			"presets: a partial fit counts only for the body's own family");
		Check(!Added(result, "Low") && !Added(result, "Man") && !Added(result, "Curvy!"),
			"presets: too little fit, no slider set for that body, or a marker another body uses: left out");
		const auto* digits = Added(result, "Fine Digits");
		Check(digits && ValueOf(*digits, "Breasts") == 0.1235F && ValueOf(*digits, "Butt") == -99.0F,
			"presets: a value is the number the template would carry (4 decimals), and one that rounds to 0 is left out");
		Check(result.added.size() == 3 && result.notes.size() == 4, std::format("presets: 3 added, 4 said why ({} / {})", result.added.size(), result.notes.size()));
		std::unordered_set<std::string> none[2];
		Check(P::Resolve(*cat, files, none).added.empty(), "presets: no body read, nothing added");
	}

// --presets <Data> <catalog.json>: what the plugin's own code (Presets::ReadInstalled) adds to the pickers from a
// Data folder, as JSON -- compared with the generator's reading of the same presets (tools/tests/test_presets_parity.py).
int DumpPresets(const std::filesystem::path& a_data, const std::filesystem::path& a_catalog)
{
	std::ifstream  file{ a_catalog };
	nlohmann::json doc;
	try {
		file >> doc;
	} catch (const std::exception& e) {
		std::cerr << "catalog: " << e.what() << "\n";
		return 2;
	}
	std::string error;
	auto        catalog = SH::ParseCatalog(doc, error);
	if (!catalog) {
		std::cerr << "catalog refused: " << error << "\n";
		return 2;
	}
	const auto     read = SH::Presets::ReadInstalled(*catalog, a_data);
	nlohmann::json out{ { "files", read.files }, { "bodies", { read.body[0], read.body[1] } }, { "added", nlohmann::json::array() },
		{ "notes", read.installed.notes } };
	for (const auto& p : read.installed.added) {
		nlohmann::json values = nlohmann::json::object();
		for (const auto& [m, v] : p.values) {
			values[m] = v;
		}
		out["added"].push_back({ { "name", p.name }, { "sex", p.female ? "female" : "male" }, { "fit", p.fit }, { "marker", p.marker },
			{ "family", p.family }, { "values", values } });
	}
	std::cout << out.dump(1) << "\n";
	return 0;
}

int main(int argc, char** argv)
{
	if (argc == 3 && std::string_view{ argv[1] } == "--check") {
		return CheckData(argv[2]);
	}
	if (argc == 4 && std::string_view{ argv[1] } == "--presets") {
		return DumpPresets(argv[2], argv[3]);
	}
	TestCatalog();
	TestRules();
	TestPlan();
	TestRegistry();
	TestBodies();
	TestDecisions();
	TestRefit();
	TestTouchUp();
	TestPicker();
	TestWave3();
	TestWave4();
	TestWave5();
	TestCrosshairTrail();
	TestResetEveryone();
	TestInstalledPresets();
	std::cout << g_passed << " passed, " << g_failed << " failed\n";
	return g_failed;
}
