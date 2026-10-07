#include "Catalog.h"

namespace SH
{
	namespace
	{
		using json = nlohmann::json;

		// Every reader throws on a wrong shape; ParseCatalog turns the first throw into the error and
		// refuses the whole document.
		struct Bad : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		const json& At(const json& a_obj, std::string_view a_key, std::string_view a_where)
		{
			if (!a_obj.is_object()) {
				throw Bad(std::format("{}: expected an object", a_where));
			}
			const auto it = a_obj.find(a_key);
			if (it == a_obj.end()) {
				throw Bad(std::format("{}: missing \"{}\"", a_where, a_key));
			}
			return *it;
		}

		const json& Object(const json& a_v, std::string_view a_where)
		{
			if (!a_v.is_object()) {
				throw Bad(std::format("{}: expected an object", a_where));
			}
			return a_v;
		}

		const json& List(const json& a_v, std::string_view a_where)
		{
			if (!a_v.is_array()) {
				throw Bad(std::format("{}: expected a list", a_where));
			}
			return a_v;
		}

		std::string Str(const json& a_v, std::string_view a_where)
		{
			if (!a_v.is_string()) {
				throw Bad(std::format("{}: expected a string", a_where));
			}
			return a_v.get<std::string>();
		}

		bool Bool(const json& a_v, std::string_view a_where)
		{
			if (!a_v.is_boolean()) {
				throw Bad(std::format("{}: expected true or false", a_where));
			}
			return a_v.get<bool>();
		}

		// A number that is finite as a double AND as the float the plugin keeps (1e39 is not).
		float Num(const json& a_v, std::string_view a_where)
		{
			if (!a_v.is_number()) {
				throw Bad(std::format("{}: expected a number", a_where));
			}
			const auto v = a_v.get<double>();
			const auto f = static_cast<float>(v);
			if (!std::isfinite(v) || !std::isfinite(f)) {
				throw Bad(std::format("{}: not a finite number", a_where));
			}
			return f;
		}

		// A non-negative integer, however the JSON holds it: a parser reads 12 as unsigned, a program
		// that builds the document may have written it signed.
		std::uint64_t Unsigned(const json& a_v, std::string_view a_where)
		{
			if (!a_v.is_number_integer() || (!a_v.is_number_unsigned() && a_v.get<std::int64_t>() < 0)) {
				throw Bad(std::format("{} must be a non-negative integer", a_where));
			}
			return a_v.get<std::uint64_t>();
		}

		bool Sex(const json& a_v, std::string_view a_where)
		{
			const auto s = Str(a_v, a_where);
			if (s == "female") {
				return true;
			}
			if (s == "male") {
				return false;
			}
			throw Bad(std::format("{}: sex must be \"female\" or \"male\", not \"{}\"", a_where, s));
		}

		std::vector<std::string> Strings(const json& a_v, std::string_view a_where)
		{
			std::vector<std::string> out;
			for (const auto& e : List(a_v, a_where)) {
				out.push_back(Str(e, a_where));
			}
			return out;
		}

		FormRef Ref(const json& a_v, std::string_view a_where)
		{
			FormRef r;
			r.plugin = Str(At(a_v, "plugin", a_where), a_where);
			const auto value = Unsigned(At(a_v, "id", a_where), std::format("{}: id", a_where));
			// Without the load-order byte: at most 24 bits for a full plugin, 12 for a light one.
			if (value > 0xFFFFFF) {
				throw Bad(std::format("{}: id {:X} carries a load-order byte", a_where, value));
			}
			r.id = static_cast<std::uint32_t>(value);
			if (r.plugin.empty()) {
				throw Bad(std::format("{}: empty plugin name", a_where));
			}
			return r;
		}

		std::vector<FormRef> Refs(const json& a_v, std::string_view a_where)
		{
			std::vector<FormRef> out;
			for (const auto& e : List(a_v, a_where)) {
				out.push_back(Ref(e, a_where));
			}
			return out;
		}

		// {"female": ..., "male": ...}: both sexes, nothing else.
		template <class F>
		void PerSex(const json& a_v, std::string_view a_where, F&& a_each)
		{
			Object(a_v, a_where);
			for (const auto& [key, value] : a_v.items()) {
				if (key == "female") {
					a_each(true, value);
				} else if (key == "male") {
					a_each(false, value);
				} else {
					throw Bad(std::format("{}: unknown key \"{}\"", a_where, key));
				}
			}
		}

		bool Listed(const std::vector<std::string>& a_list, std::string_view a_name)
		{
			return std::ranges::any_of(a_list, [&](const std::string& s) { return IEquals(s, a_name); });
		}
	}

	bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
	{
		return a_lhs.size() == a_rhs.size() &&
		       std::ranges::equal(a_lhs, a_rhs, [](char a, char b) {
				   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			   });
	}

	std::optional<FilesHeader> ParseFilesHeader(std::istream& a_in)
	{
		std::string line;
		for (int i = 0; i < 12 && std::getline(a_in, line); ++i) {
			const auto b = line.find("Build ");
			const auto s = line.find("marker stamp ");
			if (b == std::string::npos || s == std::string::npos) {
				continue;
			}
			FilesHeader h;
			h.build = line.substr(b + 6);
			h.build = h.build.substr(0, h.build.find(','));
			const auto* digits = line.c_str() + s + 13;
			const auto [end, ec] = std::from_chars(digits, line.c_str() + line.size(), h.stamp);
			if (ec != std::errc{} || h.build.empty()) {
				continue;
			}
			if (const auto r = line.find(", rules "); r != std::string::npos) {
				h.rules = line.substr(r + 8);
				h.rules = h.rules.substr(0, h.rules.find_first_of(",.; \r"));
			}
			return h;
		}
		return std::nullopt;
	}

	MarkerKind KindOf(std::string_view a_morph)
	{
		if (IEquals(a_morph, kRefitMarker)) {
			return MarkerKind::kRefit;
		}
		if (IEquals(a_morph, kChoiceMarker)) {
			return MarkerKind::kChoice;
		}
		return a_morph.size() > 11 && IEquals(a_morph.substr(0, 11), "Silhouette_") ? MarkerKind::kBody : MarkerKind::kNone;
	}

	bool FormRef::Is(std::string_view a_plugin, std::uint32_t a_id) const
	{
		return id == a_id && IEquals(plugin, a_plugin);
	}

	const Preset* Catalog::Find(std::string_view a_name, bool a_female) const
	{
		for (const auto& p : presets) {
			if (p.female == a_female && IEquals(p.name, a_name)) {
				return &p;
			}
		}
		return nullptr;
	}

	const Preset* Catalog::FindByMarker(std::string_view a_marker) const
	{
		for (const auto& p : presets) {
			if (IEquals(p.marker, a_marker)) {
				return &p;
			}
		}
		return nullptr;
	}

	const RacePool* Catalog::RacePoolOf(std::string_view a_race) const
	{
		for (const auto& p : racePools) {
			if (IEquals(p.race, a_race)) {
				return &p;
			}
		}
		return nullptr;
	}

	std::optional<bool> Catalog::BodyFemaleOf(std::string_view a_race) const
	{
		const auto* p = RacePoolOf(a_race);
		return p ? std::optional<bool>{ p->female } : std::nullopt;
	}

	std::vector<const Preset*> Catalog::MenuPresets(bool a_female) const
	{
		std::vector<const Preset*> out;
		for (const auto& p : presets) {
			if (p.female == a_female && p.menu) {
				out.push_back(&p);
			}
		}
		return out;
	}

	const RefitSet* Catalog::FindRefit(std::string_view a_name, bool a_female) const
	{
		for (const auto& s : refitSets) {
			if (s.female == a_female && IEquals(s.name, a_name)) {
				return &s;
			}
		}
		return nullptr;
	}

	bool Catalog::NeverInBody(bool a_female, std::string_view a_morph) const
	{
		return Listed(neverInBody[a_female ? 1 : 0], a_morph);
	}

	namespace
	{
		// Lower-case ASCII words of a name: "Combat Armor Chest-Piece" -> combat armor chest piece.
		std::vector<std::string> Words(std::string_view a_text)
		{
			std::vector<std::string> out;
			std::string               word;
			for (const char ch : a_text) {
				const auto c = static_cast<unsigned char>(ch);
				if (std::isalnum(c) || c >= 0x80) {
					word.push_back(static_cast<char>(std::tolower(c)));
				} else if (!word.empty()) {
					out.push_back(std::move(word));
					word.clear();
				}
			}
			if (!word.empty()) {
				out.push_back(std::move(word));
			}
			return out;
		}

		std::string LowerAscii(std::string_view a_text)
		{
			std::string out{ a_text };
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}
	}

	std::string Catalog::HeavyWord(std::string_view a_itemName) const
	{
		const auto name = Words(a_itemName);
		for (const auto& phrase : heavyWords) {
			const auto words = Words(phrase);
			if (words.empty() || words.size() > name.size()) {
				continue;
			}
			for (std::size_t i = 0; i + words.size() <= name.size(); ++i) {
				if (std::equal(words.begin(), words.end(), name.begin() + static_cast<std::ptrdiff_t>(i))) {
					return phrase;
				}
			}
		}
		return {};
	}

	std::string Catalog::OutfitRefitSet(std::string_view a_outfitName, bool a_female) const
	{
		if (a_outfitName.empty()) {
			return {};
		}
		for (const auto& o : outfitRefits) {
			if (o.female == a_female && IEquals(o.outfit, a_outfitName)) {
				return o.refitSet;
			}
		}
		return {};
	}

	const RefitSet* Catalog::RefitFor(std::string_view a_preset, bool a_female, std::string_view a_outfitSet) const
	{
		if (!a_outfitSet.empty()) {
			if (const auto* s = FindRefit(a_outfitSet, a_female)) {
				return s;
			}
		}
		if (!a_preset.empty()) {
			if (const auto* s = FindRefit(std::format("{}-Refit", a_preset), a_female)) {
				return s;
			}
		}
		if (const auto* s = FindRefit(a_female ? "Female-Refit"sv : "Male-Refit"sv, a_female)) {
			return s;
		}
		return FindRefit(a_female ? "builtin:female"sv : "builtin:male"sv, a_female);
	}

	void Catalog::AddManifest(std::uint32_t a_stamp, std::unordered_map<std::string, ManifestEntry> a_markers)
	{
		// Markers arrive from LooksMenu through the engine's case-insensitive string pool: keyed in
		// lower case, looked up in lower case.
		std::unordered_map<std::string, ManifestEntry> lowered;
		for (auto& [marker, entry] : a_markers) {
			lowered.emplace(LowerAscii(marker), std::move(entry));
		}
		_manifests[a_stamp] = std::move(lowered);
	}

	std::optional<std::string> Catalog::PresetForMarker(std::string_view a_marker, std::uint32_t a_stamp) const
	{
		if (const auto it = _manifests.find(a_stamp); it != _manifests.end()) {
			if (const auto m = it->second.find(LowerAscii(a_marker)); m != it->second.end()) {
				return m->second.preset;
			}
		}
		// A stamp with no manifest (deleted by hand, or a build we never saw): the current build's
		// marker still names a preset of the same name when it exists.
		if (a_stamp == stamp) {
			if (const auto* p = FindByMarker(a_marker)) {
				return p->name;
			}
		}
		return std::nullopt;
	}

	std::vector<std::string> Catalog::HealFor(std::string_view a_marker, std::uint32_t a_stamp) const
	{
		std::vector<std::string> out;
		const auto               it = _manifests.find(a_stamp);
		if (it == _manifests.end()) {
			return out;
		}
		const auto m = it->second.find(LowerAscii(a_marker));
		if (m == it->second.end()) {
			return out;
		}
		for (const auto& morph : m->second.morphs) {
			if (NeverInBody(m->second.female, morph)) {
				out.push_back(morph);
			}
		}
		return out;
	}

	std::optional<Catalog> ParseCatalog(const nlohmann::json& a_doc, std::string& a_error)
	{
		try {
			Catalog c;
			// Read whole, never through an int: 2^32+1 would wrap to 1 and pass.
			const auto& schema = At(a_doc, "schema", "catalog");
			if (!schema.is_number_integer() || Unsigned(schema, "catalog.schema") != 1) {
				throw Bad("catalog: schema must be 1 (this plugin reads format 1 only)");
			}
			c.schema = 1;
			c.build = Str(At(a_doc, "build", "catalog"), "catalog.build");
			const auto stamp = Unsigned(At(a_doc, "stamp", "catalog"), "catalog.stamp");
			if (stamp == 0 || stamp >= (1ull << 24)) {
				throw Bad("catalog.stamp: must be 1 .. 2^24-1 (a marker holds it as a float32, exactly)");
			}
			c.stamp = static_cast<std::uint32_t>(stamp);
			c.mode = Str(At(a_doc, "mode", "catalog"), "catalog.mode");
			c.rulesHash = Str(At(a_doc, "rulesHash", "catalog"), "catalog.rulesHash");
			if (c.rulesHash.empty()) {
				throw Bad("catalog.rulesHash: empty");
			}

			PerSex(At(a_doc, "states", "catalog"), "catalog.states", [&](bool a_female, const json& a_v) {
				c.states[a_female ? 1 : 0] = Strings(a_v, "catalog.states");
			});
			PerSex(At(a_doc, "neverInBody", "catalog"), "catalog.neverInBody", [&](bool a_female, const json& a_v) {
				c.neverInBody[a_female ? 1 : 0] = Strings(a_v, "catalog.neverInBody");
			});
			for (const bool female : { false, true }) {
				for (const auto& s : c.states[female ? 1 : 0]) {
					if (!c.NeverInBody(female, s)) {
						throw Bad(std::format("catalog.neverInBody: the runtime state \"{}\" is missing (S-16)", s));
					}
				}
			}
			// S-76, optional: a catalog from before has none, and the player's own presets are then not read.
			if (const auto it = a_doc.find("sliderSets"); it != a_doc.end()) {
				for (const auto& [sex, v] : Object(*it, "catalog.sliderSets").items()) {
					if (sex != "female" && sex != "male") {
						throw Bad(std::format("catalog.sliderSets: \"{}\" is not a sex", sex));
					}
					const auto    where = std::format("catalog.sliderSets.{}", sex);
					BodySliderSet set;
					set.name = Str(At(v, "set", where), where);
					for (const auto& [name, pair] : Object(At(v, "sliders", where), where).items()) {
						if (!pair.is_array() || pair.size() != 2) {
							throw Bad(std::format("{}.{}: expected [default, invert]", where, name));
						}
						set.sliders.push_back({ name, Num(pair[0], where), Bool(pair[1], where) });
					}
					c.sliderSets[sex == "female" ? 1 : 0] = std::move(set);
				}
			}

			std::unordered_set<std::string> markers;
			for (const auto& p : List(At(a_doc, "presets", "catalog"), "catalog.presets")) {
				Preset preset;
				preset.name = Str(At(p, "name", "preset"), "preset.name");
				const auto where = std::format("preset \"{}\"", preset.name);
				preset.female = Sex(At(p, "sex", where), where);
				preset.marker = Str(At(p, "marker", where), where);
				if (KindOf(preset.marker) != MarkerKind::kBody || IEquals(preset.marker, kBlacklistMarker)) {
					throw Bad(std::format("{}: marker \"{}\" is not a body marker (\"Silhouette_...\", not a reserved one)", where, preset.marker));
				}
				std::string lower = preset.marker;
				std::ranges::transform(lower, lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
				if (!markers.insert(lower).second) {
					throw Bad(std::format("{}: marker \"{}\" is used by another preset too", where, preset.marker));
				}
				for (const auto& [morph, value] : Object(At(p, "values", where), where + ".values").items()) {
					if (c.NeverInBody(preset.female, morph)) {
						throw Bad(std::format("{}: \"{}\" is never part of a body (S-16, S-29)", where, morph));
					}
					preset.values.emplace_back(morph, Num(value, where));
				}
				preset.random = Bool(At(p, "random", where), where);
				preset.menu = Bool(At(p, "menu", where), where);
				preset.zeroed = Bool(At(p, "zeroed", where), where);
				preset.fit = Str(At(p, "fit", where), where);
				preset.family = Str(At(p, "family", where), where);
				if (c.Find(preset.name, preset.female)) {
					throw Bad(std::format("{}: listed twice for one sex", where));
				}
				c.presets.push_back(std::move(preset));
			}

			PerSex(At(a_doc, "player", "catalog"), "catalog.player", [&](bool a_female, const json& a_v) {
				c.playerDefault[a_female ? 1 : 0] = Str(a_v, "catalog.player");
			});
			PerSex(At(a_doc, "variety", "catalog"), "catalog.variety", [&](bool a_female, const json& a_v) {
				for (const auto& e : List(a_v, "catalog.variety")) {
					VarietyRange r;
					r.morph = Str(At(e, "morph", "variety"), "variety.morph");
					const auto where = std::format("variety \"{}\"", r.morph);
					r.low = Num(At(e, "low", where), where);
					r.high = Num(At(e, "high", where), where);
					r.group = Str(At(e, "group", where), where);
					if (r.low > r.high) {
						throw Bad(std::format("{}: low {} is above high {}", where, r.low, r.high));
					}
					if (r.group != "nipples" && r.group != "genitals") {
						throw Bad(std::format("{}: group must be \"nipples\" or \"genitals\", not \"{}\"", where, r.group));
					}
					if (c.NeverInBody(a_female, r.morph)) {
						throw Bad(std::format("{}: never part of a body (S-16, S-29), so never rolled", where));
					}
					auto& list = c.variety[a_female ? 1 : 0];
					if (std::ranges::any_of(list, [&](const VarietyRange& a_o) { return IEquals(a_o.morph, r.morph); })) {
						throw Bad(std::format("{}: listed twice for one sex", where));
					}
					list.push_back(std::move(r));
				}
			});

			const auto& rules = At(a_doc, "rules", "catalog");
			c.races = Strings(At(rules, "races", "rules"), "rules.races");
			PerSex(At(rules, "npcFormID", "rules"), "rules.npcFormID", [&](bool a_female, const json& a_v) {
				c.npcFormIDRules[a_female ? 1 : 0] = Refs(a_v, "rules.npcFormID");
			});
			c.blacklistedNpcsFormID = Refs(At(rules, "blacklistedNpcsFormID", "rules"), "rules.blacklistedNpcsFormID");
			PerSex(At(rules, "blacklistedPlugins", "rules"), "rules.blacklistedPlugins", [&](bool a_female, const json& a_v) {
				c.blacklistedPlugins[a_female ? 1 : 0] = Strings(a_v, "rules.blacklistedPlugins");
			});
			PerSex(At(rules, "blacklistedRaces", "rules"), "rules.blacklistedRaces", [&](bool a_female, const json& a_v) {
				c.blacklistedRaces[a_female ? 1 : 0] = Strings(a_v, "rules.blacklistedRaces");
			});
			for (const auto& r : List(At(rules, "npcName", "rules"), "rules.npcName")) {
				NameRule rule;
				rule.name = Str(At(r, "name", "rules.npcName"), "rules.npcName");
				rule.female = Sex(At(r, "sex", "rules.npcName"), "rules.npcName");
				rule.presets = Strings(At(r, "presets", "rules.npcName"), "rules.npcName");
				c.nameRules.push_back(std::move(rule));
			}
			c.blacklistedNpcNames = Strings(At(rules, "blacklistedNpcNames", "rules"), "rules.blacklistedNpcNames");
			for (const auto& r : List(At(rules, "faction", "rules"), "rules.faction")) {
				FactionRule rule;
				rule.faction = Ref(r, "rules.faction");
				rule.editorID = Str(At(r, "editorID", "rules.faction"), "rules.faction");
				rule.female = Sex(At(r, "sex", "rules.faction"), "rules.faction");
				rule.presets = Strings(At(r, "presets", "rules.faction"), "rules.faction");
				// Optional, so a catalog from before S-73 reads as it did: none of its rules is a pool.
				if (const auto p = r.find("pool"); p != r.end() && p->is_boolean()) {
					rule.pool = p->get<bool>();
				}
				c.factionRules.push_back(std::move(rule));
			}
			// S-86. Optional, so a catalog from before it reads as it did.
			if (const auto rp = rules.find("racePool"); rp != rules.end()) {
				for (const auto& r : List(*rp, "rules.racePool")) {
					RacePool pool;
					pool.race = Str(At(r, "race", "rules.racePool"), "rules.racePool");
					pool.female = Sex(At(r, "sex", "rules.racePool"), "rules.racePool");
					pool.presets = Strings(At(r, "presets", "rules.racePool"), "rules.racePool");
					if (const auto w = r.find("without"); w != r.end()) {
						pool.without = Strings(*w, "rules.racePool.without");  // S-87, optional
					}
					if (pool.presets.empty()) {
						throw Bad(std::format("rules.racePool {}: no presets", pool.race));
					}
					c.racePools.push_back(std::move(pool));
				}
			}

			const auto& orefit = At(a_doc, "orefit", "catalog");
			for (const auto& s : List(At(orefit, "slots", "orefit"), "orefit.slots")) {
				const auto slot = Unsigned(s, "orefit.slots");  // whole: 2^32+33 would wrap to 33 through an int
				if (slot < 30 || slot > 61) {
					throw Bad("orefit.slots: biped slots are 30..61");
				}
				c.clothedSlots.push_back(static_cast<int>(slot));
			}
			c.outfitBlacklist = Refs(At(orefit, "blacklist", "orefit"), "orefit.blacklist");
			c.outfitBlacklistNames = Strings(At(orefit, "blacklistNames", "orefit"), "orefit.blacklistNames");
			c.outfitBlacklistPlugins = Strings(At(orefit, "blacklistPlugins", "orefit"), "orefit.blacklistPlugins");
			c.forceRefit = Refs(At(orefit, "force", "orefit"), "orefit.force");
			c.forceRefitNames = Strings(At(orefit, "forceNames", "orefit"), "orefit.forceNames");
			for (const auto& o : List(At(orefit, "outfits", "orefit"), "orefit.outfits")) {
				OutfitRefit refit;
				refit.outfit = Str(At(o, "name", "orefit.outfits"), "orefit.outfits");
				refit.female = Sex(At(o, "sex", "orefit.outfits"), "orefit.outfits");
				refit.refitSet = Str(At(o, "set", "orefit.outfits"), "orefit.outfits");
				c.outfitRefits.push_back(std::move(refit));
			}
			for (const auto& s : List(At(orefit, "sets", "orefit"), "orefit.sets")) {
				RefitSet set;
				set.name = Str(At(s, "name", "orefit.sets"), "orefit.sets");
				const auto where = std::format("refit set \"{}\"", set.name);
				set.female = Sex(At(s, "sex", where), where);
				if (c.FindRefit(set.name, set.female)) {
					throw Bad(std::format("{}: listed twice for one sex", where));
				}
				for (const auto& e : List(At(s, "floors", where), where)) {
					RefitFloor floor;
					floor.morph = Str(At(e, "morph", where), where);
					if (floor.morph.empty() || KindOf(floor.morph) != MarkerKind::kNone) {
						throw Bad(std::format("{}: \"{}\" is not a body slider", where, floor.morph));
					}
					if (c.NeverInBody(set.female, floor.morph)) {
						throw Bad(std::format("{}: \"{}\" is never part of a body (S-16, S-29)", where, floor.morph));
					}
					floor.value = Num(At(e, "value", where), where);
					if (!(floor.value > 0.0F)) {
						throw Bad(std::format("{}: \"{}\" at {}: a floor must be above 0, a refit only raises (S-40)", where, floor.morph, floor.value));
					}
					floor.heavyOnly = Bool(At(e, "heavyOnly", where), where);
					set.floors.push_back(std::move(floor));
				}
				c.refitSets.push_back(std::move(set));
			}
			const auto& heavy = At(orefit, "heavy", "orefit");
			c.heavyWords = Strings(At(heavy, "words", "orefit.heavy"), "orefit.heavy.words");
			for (const auto& w : c.heavyWords) {
				if (Words(w).empty()) {
					throw Bad(std::format("orefit.heavy.words: \"{}\" holds no word", w));
				}
			}
			c.heavyItems = Refs(At(heavy, "items", "orefit.heavy"), "orefit.heavy.items");
			c.heavyNames = Strings(At(heavy, "names", "orefit.heavy"), "orefit.heavy.names");
			const auto& light = At(orefit, "light", "orefit");
			c.lightItems = Refs(At(light, "items", "orefit.light"), "orefit.light.items");
			c.lightNames = Strings(At(light, "names", "orefit.light"), "orefit.light.names");

			// Every preset a rule names must exist for that sex, or the rule is a promise the plugin
			// cannot keep: refuse the file now rather than skipping NPCs later without a word.
			for (const auto& r : c.nameRules) {
				for (const auto& p : r.presets) {
					if (!c.Find(p, r.female)) {
						throw Bad(std::format("rules.npcName \"{}\": preset \"{}\" is not in this catalog", r.name, p));
					}
				}
			}
			for (const auto& r : c.factionRules) {
				for (const auto& p : r.presets) {
					if (!c.Find(p, r.female)) {
						throw Bad(std::format("rules.faction \"{}\": preset \"{}\" is not in this catalog", r.editorID, p));
					}
				}
			}
			for (const bool female : { false, true }) {
				const auto& d = c.playerDefault[female ? 1 : 0];
				if (!d.empty() && !c.Find(d, female)) {
					throw Bad(std::format("player default \"{}\" is not in this catalog", d));
				}
			}
			for (const auto& o : c.outfitRefits) {
				if (!c.FindRefit(o.refitSet, o.female)) {
					throw Bad(std::format("orefit.outfits \"{}\": refit set \"{}\" is not in this catalog", o.outfit, o.refitSet));
				}
			}
			return c;
		} catch (const std::exception& e) {
			a_error = e.what();
			return std::nullopt;
		}
	}

	std::optional<std::pair<std::uint32_t, std::unordered_map<std::string, ManifestEntry>>>
		ParseManifest(const nlohmann::json& a_doc, std::string& a_error)
	{
		try {
			const auto stamp = Unsigned(At(a_doc, "stamp", "manifest"), "manifest.stamp");
			if (stamp == 0 || stamp >= (1ull << 24)) {
				throw Bad("manifest.stamp: must be 1 .. 2^24-1");
			}
			std::unordered_map<std::string, ManifestEntry> markers;
			for (const auto& [marker, entry] : Object(At(a_doc, "templates", "manifest"), "manifest.templates").items()) {
				ManifestEntry m;
				m.preset = Str(At(entry, "preset", marker), marker);
				if (const auto g = entry.find("gender"); g != entry.end()) {
					m.female = Sex(*g, marker);
				}
				if (const auto v = entry.find("values"); v != entry.end() && v->is_object()) {
					for (const auto& item : v->items()) {
						m.morphs.push_back(item.key());
					}
				}
				markers.emplace(marker, std::move(m));
			}
			return std::make_pair(static_cast<std::uint32_t>(stamp), std::move(markers));
		} catch (const std::exception& e) {
			a_error = e.what();
			return std::nullopt;
		}
	}
}
