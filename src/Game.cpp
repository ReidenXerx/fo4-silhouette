#include "Game.h"

#include "Compat.h"
#include "Crosshair.h"
#include "EventSources.h"

namespace SH::Game
{
	namespace
	{
		constexpr auto kFolder = "Data/F4SE/Plugins/Silhouette"sv;
		constexpr auto kTemplates = "Data/F4SE/Plugins/F4EE/BodyGen/Loose/Silhouette_templates.ini"sv;
		constexpr auto kMorphs = "Data/F4SE/Plugins/F4EE/BodyGen/Loose/Silhouette_morphs.ini"sv;

		// The two character-creation dummies (Fallout4.esm). LooksMenu CLONES the chosen one's body
		// onto the player when character creation ends, so nothing of ours may ever be on them.
		constexpr std::uint32_t kSpouseMale = 0x0A7D34;
		constexpr std::uint32_t kSpouseFemale = 0x0A7D35;

		// Power armour (Fallout4.esm): a frame is a machine an NPC climbs into, not clothes (L5 #8).
		constexpr std::uint32_t kPowerArmorFrameKeyword = 0x15503F;  // isPowerArmorFrame
		constexpr std::uint32_t kPowerArmorPieceKeyword = 0x04D8A1;  // ArmorTypePower

		constexpr std::size_t  kInboxLimit = 4096;
		constexpr std::int64_t kSilentBridgeMs = 60'000;
		constexpr std::int64_t kSummaryMs = 30'000;

		struct Resolved
		{
			std::vector<std::pair<RE::TESFaction*, FormRef>> factions;  // the faction rules' factions
			std::unordered_set<std::uint32_t>                 blacklist;  // ORefit, runtime form ids
			std::unordered_set<std::uint32_t>                 force;
			std::unordered_set<std::uint32_t>                 heavy;  // S-48: the explicit lists
			std::unordered_set<std::uint32_t>                 light;
			std::uint32_t                                     clothedMask{ 0 };
			std::array<const RE::BGSKeyword*, 2>              powerArmor{};
			std::unordered_map<std::uint32_t, bool>           heavyOf;  // each item decided once (main thread only)
		};

		struct Inbox
		{
			struct Equip
			{
				std::uint32_t ref;
				std::uint32_t item;
				bool          equipped;
			};

			std::mutex                lock;
			std::deque<std::uint32_t> loaded;
			std::deque<Equip>         equips;
			std::size_t               dropped{ 0 };  // since the last load
			bool                      warned{ false };
		};

		Director                   g_director;
		Resolved                   g_resolved;
		Inbox                      g_inbox;
		CrosshairTrail             g_trail;              // the view caster's activate picks, as handles
		std::atomic<std::uint32_t> g_dialoguePick{ 0 };  // its dialogue pick, a handle: for the log only
		std::atomic<std::int64_t>  g_loadedMs{ 0 };      // when the last load finished, 0 before any
		std::atomic<std::int64_t>  g_pumpedMs{ 0 };  // the bridge's last poll
		std::atomic<std::int64_t>  g_askedMs{ 0 };   // the bridge's last protocol check (Connect)
		std::atomic<bool>          g_watching{ false };
		std::int64_t               g_summaryMs{ 0 };  // main thread: the last summary line

		// Main thread: the load sweep. After a load in a running game the game does not report the people
		// already around the player as loaded, so nobody was read and a picking saved mid-preview was never
		// put back (S-47). Measured 2026-09-24: Silhouette read 19 people after a load from the main menu and
		// none after two loads in the running game; F4MCP's own sink on the same source got 85 events after
		// a main-menu load and 2 (both created references) after an in-session one, the sink still attached.
		// For a while after each load, the bridge's polls read every actor the game is simulating, once each.
		// The while starts at the first poll, not at the load: the bridge polls only while the game runs, and
		// a player who alt-tabs out at once (bAlwaysActive=0), or a menu, can hold that off past it (wave 5).
		constexpr std::int64_t            kSweepMs = 30'000;
		bool                              g_sweepArmed{ false };  // a load happened; the first poll starts the sweep
		std::int64_t                      g_sweepArmedMs{ 0 };
		std::int64_t                      g_sweepFirstMs{ 0 };    // that first poll
		std::int64_t                      g_sweepUntilMs{ 0 };
		bool                              g_sweepSaid{ true };
		std::unordered_set<std::uint32_t> g_swept;     // read by the sweep since the load
		std::unordered_set<std::uint32_t> g_reported;  // reported loaded by the game since the load

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// Main thread: the reference a handle names now, or null -- a handle outlives its reference, and
		// then names nobody. A handle IS its 32-bit value, and that is all the sink copied.
		RE::NiPointer<RE::TESObjectREFR> RefFor(std::uint32_t a_handle)
		{
			static_assert(sizeof(RE::ObjectRefHandle) == sizeof(std::uint32_t));
			if (a_handle == 0) {
				return {};
			}
			RE::ObjectRefHandle handle;
			std::memcpy(static_cast<void*>(&handle), &a_handle, sizeof(a_handle));
			return handle.get();
		}

		// Main thread: "Harold Roach (00115EA1)", for the log.
		std::string Described(RE::TESObjectREFR* a_ref)
		{
			const auto name = Compat::DisplayName(a_ref);
			return std::format("{} ({:08X})", !name.empty() ? name : "unnamed", a_ref ? a_ref->GetFormID() : 0u);
		}

		bool AnyIEquals(const std::vector<std::string>& a_list, std::string_view a_name)
		{
			return std::ranges::any_of(a_list, [&](const std::string& n) { return IEquals(n, a_name); });
		}

		// A path for the log: path::string() throws for a name the ANSI code page cannot hold, and a throw at
		// data load takes the game down. (An unpaired surrogate throws even here: every caller is inside the
		// manifest loop's try.)
		std::string Utf8(const std::filesystem::path& a_path)
		{
			const auto u = a_path.generic_u8string();
			return { reinterpret_cast<const char*>(u.data()), u.size() };
		}

		std::optional<nlohmann::json> ReadJson(const std::filesystem::path& a_path, std::string& a_error)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				a_error = std::format("{} is missing", Utf8(a_path));
				return std::nullopt;
			}
			try {
				return nlohmann::json::parse(in);
			} catch (const std::exception& e) {
				a_error = std::format("{} is not valid JSON: {}", Utf8(a_path), e.what());
				return std::nullopt;
			}
		}

		std::optional<FilesHeader> ReadHeader(std::string_view a_file, std::string& a_error)
		{
			std::ifstream in{ std::filesystem::path{ a_file } };
			if (!in) {
				a_error = std::format("{} is missing: Silhouette's BodyGen files are not installed", a_file);
				return std::nullopt;
			}
			auto header = ParseFilesHeader(in);
			if (!header) {
				a_error = std::format("{} names no build in its header", a_file);
			}
			return header;
		}

		std::uint32_t Resolve(const FormRef& a_ref)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* form = dh ? dh->LookupForm(a_ref.id, a_ref.plugin) : nullptr;
			return form ? form->GetFormID() : 0;
		}

		std::unordered_set<std::uint32_t> ResolveAll(const std::vector<FormRef>& a_refs)
		{
			std::unordered_set<std::uint32_t> out;
			for (const auto& f : a_refs) {
				if (const auto id = Resolve(f)) {
					out.insert(id);
				}
			}
			return out;
		}

		std::string PluginOf(const RE::TESForm* a_form)
		{
			const auto* file = a_form ? a_form->GetFile(0) : nullptr;
			return file ? std::string{ file->filename } : std::string{};
		}

		std::string NameOfForm(const RE::TESForm* a_form)
		{
			return a_form ? std::string{ RE::TESFullName::GetFullName(*a_form) } : std::string{};
		}

		// Loaded as far as a body is concerned: the biped is built with the 3D and let go with it. A
		// member read, where Get3D() would be a virtual call on a table this library maps by hand.
		bool Has3D(RE::Actor* a_actor)
		{
			const auto& biped = a_actor->biped;
			return biped && biped->root;
		}

		// Everyone the game is simulating around the player (the two lists Rapport's ActorScan reads on this
		// runtime) whose body is built and whom the sweep has not read since the load.
		void Sweep(std::deque<std::uint32_t>& a_loaded)
		{
			const auto lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return;
			}
			for (const auto* handles : { &lists->highActorHandles, &lists->middleHighActorHandles }) {
				for (const auto& handle : *handles) {
					const auto ptr = handle.get();
					auto*      actor = ptr.get();
					if (actor && Has3D(actor) && g_swept.insert(actor->GetFormID()).second) {
						a_loaded.push_back(actor->GetFormID());
					}
				}
			}
		}

		// TESObjectARMO has GetFilledSlots twice over (two of its bases); the biped object form's is
		// the one that says which slots it takes.
		std::uint32_t SlotsOf(const RE::TESObjectARMO* a_item)
		{
			return Compat::FilledSlots(static_cast<const RE::BGSBipedObjectForm*>(a_item));
		}

		// The item's own keywords, read from the members: no virtual call on a table mapped by hand.
		bool HasKeyword(const RE::TESObjectARMO* a_item, const RE::BGSKeyword* a_keyword)
		{
			const auto* form = static_cast<const RE::BGSKeywordForm*>(a_item);
			if (!a_keyword || !form->keywords) {
				return false;
			}
			for (std::uint32_t i = 0; i < form->numKeywords; ++i) {
				if (form->keywords[i] == a_keyword) {
					return true;
				}
			}
			return false;
		}

		bool PowerArmor(const RE::TESObjectARMO* a_item)
		{
			return std::ranges::any_of(g_resolved.powerArmor, [&](const RE::BGSKeyword* k) { return HasKeyword(a_item, k); });
		}

		// A race's editor id, from the member (as LooksMenu reads it): the virtual getter returns "" for
		// most forms on this runtime.
		std::string RaceName(const RE::TESRace* a_race)
		{
			const char* edid = a_race ? a_race->formEditorID.c_str() : nullptr;
			return edid ? std::string{ edid } : std::string{};
		}

		bool IsDummy(RE::TESNPC* a_npc)
		{
			int depth = 0;
			for (auto* n = a_npc; n && depth < 16; n = n->faceNPC, ++depth) {
				const auto* file = n->GetFile(0);
				if (file && IEquals(file->filename, "Fallout4.esm")) {
					const auto local = n->GetLocalFormID();
					if (local == kSpouseMale || local == kSpouseFemale) {
						return true;
					}
				}
			}
			return false;
		}

		// Every skin the actor's body could be: the record's, each template's up the chain, the race's.
		// The one on the biped is not clothing, whichever of them it is -- counting a template's skin as
		// clothes would refit a naked woman.
		std::vector<RE::TESObjectARMO*> SkinsOf(RE::Actor* a_actor)
		{
			std::vector<RE::TESObjectARMO*> out;
			int                             depth = 0;
			for (auto* n = a_actor->GetNPC(); n && depth < 16; n = n->faceNPC, ++depth) {
				if (n->formSkin) {
					out.push_back(n->formSkin);
				}
			}
			if (a_actor->race && a_actor->race->formSkin) {
				out.push_back(a_actor->race->formSkin);
			}
			return out;
		}

		// S-75: this plugin reads a few members of the game's classes directly (the biped, the race, the NPC
		// record's race, template and skin, an item's keywords and slots, the process lists). Runtime Database
		// finds the game's functions on OG, NG and AE; it does not make a class's layout the same. So before
		// any of it is trusted, each member is read once -- on the player and on the Vault 111 jumpsuit -- and
		// checked against what it must be. A runtime where one differs turns the plugin off, never half on.
		enum class Layout
		{
			kUnchecked,
			kGood,
			kBad
		};
		Layout g_layout = Layout::kUnchecked;

		struct LayoutRun
		{
			std::vector<std::string> problems;
			bool                     complete{ false };  // the player's 3D was there to read the biped
		};

		constexpr std::uint32_t kJumpsuit = 0x0001EED7;  // Fallout4.esm "Vault 111 Jumpsuit": body slot 33

		void CheckLayout(void* a_run)
		{
			auto& run = *static_cast<LayoutRun*>(a_run);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			const auto is = [&](const void* a_form, RE::ENUM_FORM_ID a_type, std::string_view a_what, bool a_emptyOk) {
				if (!a_form) {
					if (!a_emptyOk) {
						run.problems.push_back(std::format("{} is empty", a_what));
					}
					return false;
				}
				const auto type = Events::SafeFormType(a_form);
				if (type != std::to_underlying(a_type)) {
					run.problems.push_back(std::format("{} is not the form it should be (type {})", a_what, type));
					return false;
				}
				return true;
			};
			auto* npc = player->GetNPC();
			const bool npcOk = is(npc, RE::ENUM_FORM_ID::kNPC_, "the player's base record", false);
			const bool raceOk = is(player->race, RE::ENUM_FORM_ID::kRACE, "Actor::race", false);
			if (npcOk) {
				is(npc->formRace, RE::ENUM_FORM_ID::kRACE, "TESNPC::formRace", false);
				is(npc->faceNPC, RE::ENUM_FORM_ID::kNPC_, "TESNPC::faceNPC", true);
				is(npc->formSkin, RE::ENUM_FORM_ID::kARMO, "TESNPC::formSkin", true);
			}
			if (raceOk) {
				const auto edid = RaceName(player->race);
				if (edid.empty() || edid.size() > 128 || !std::ranges::all_of(edid, [](char c) { return c > 32 && c < 127; })) {
					run.problems.push_back(std::format("TESForm::formEditorID reads \"{}\" for the player's race", edid.substr(0, 40)));
				}
				is(player->race->formSkin, RE::ENUM_FORM_ID::kARMO, "TESRace::formSkin", true);
			}
			if (auto* form = RE::TESForm::GetFormByID(kJumpsuit); form && form->Is(RE::ENUM_FORM_ID::kARMO)) {
				auto* item = static_cast<RE::TESObjectARMO*>(form);
				if ((SlotsOf(item) & (1u << (33 - 30))) == 0) {
					run.problems.push_back(std::format("BGSBipedObjectForm slots read {:08X} for the Vault 111 jumpsuit", SlotsOf(item)));
				}
				const auto* keywords = static_cast<const RE::BGSKeywordForm*>(item);
				if (keywords->numKeywords > 256) {
					run.problems.push_back(std::format("BGSKeywordForm::numKeywords reads {}", keywords->numKeywords));
				} else {
					for (std::uint32_t i = 0; i < keywords->numKeywords; ++i) {
						if (!is(keywords->keywords[i], RE::ENUM_FORM_ID::kKYWD, "BGSKeywordForm::keywords", false)) {
							break;
						}
					}
				}
			}
			if (const auto lists = RE::ProcessLists::GetSingleton()) {
				std::size_t checked = 0;
				for (const auto& handle : lists->highActorHandles) {
					if (checked++ == 8) {
						break;
					}
					if (auto ptr = handle.get(); ptr && !is(ptr.get(), RE::ENUM_FORM_ID::kACHR, "ProcessLists::highActorHandles", false)) {
						break;
					}
				}
			}
			if (!Has3D(player)) {
				return;  // the biped is read once the player's body is built
			}
			const auto& biped = player->biped;
			for (std::size_t i = 0; i < 32; ++i) {
				if (auto* form = biped->object[i].parent.object; form && Events::SafeFormType(form) == 0) {
					run.problems.push_back(std::format("BipedAnim::object[{}] holds no form", i));
					break;
				}
			}
			run.complete = true;
		}

		// Main thread, from the pump: once, when the player's body is built.
		void GuardLayout()
		{
			if (g_layout != Layout::kUnchecked) {
				return;
			}
			LayoutRun run;
			if (!Events::Guarded(&CheckLayout, &run)) {
				run.problems.push_back("a read faulted");
				run.complete = true;
			}
			if (!run.complete && run.problems.empty()) {
				return;  // no body yet: next pump
			}
			if (run.problems.empty()) {
				g_layout = Layout::kGood;
				logger::info("layout: every member Silhouette reads checks out on this runtime (S-75)");
				return;
			}
			g_layout = Layout::kBad;
			std::string all;
			for (const auto& p : run.problems) {
				all += (all.empty() ? "" : "; ") + p;
			}
			const auto why = std::format("this game's classes are laid out differently from what Silhouette.dll reads ({}) - "
										 "the plugin is off, BodyGen still gives bodies. Please report it with Silhouette.log (S-75)",
				all);
			logger::error("layout: {}", why);
			g_director.Refuse(why);
		}

		struct Worn
		{
			bool        clothed{ false };
			bool        heavy{ false };
			std::string heavyBy;  // the first heavy item's name
			bool        powerArmor{ false };
			std::string outfitSet;
			bool        removing{ false };  // the event's item comes off a body, chest or pelvis slot
		};

		bool Dresses(const Catalog& a_catalog, RE::TESObjectARMO* a_item)
		{
			const auto id = a_item->GetFormID();
			const auto name = NameOfForm(a_item);
			if (g_resolved.force.contains(id) || AnyIEquals(a_catalog.forceRefitNames, name)) {
				return true;
			}
			if ((SlotsOf(a_item) & g_resolved.clothedMask) == 0 || PowerArmor(a_item)) {
				return false;
			}
			const bool blacklisted = g_resolved.blacklist.contains(id) || AnyIEquals(a_catalog.outfitBlacklistNames, name) ||
			                         AnyIEquals(a_catalog.outfitBlacklistPlugins, PluginOf(a_item));
			return !blacklisted;
		}

		// S-48: heavy only where it is plain. The catalog's lists decide first; then a whole word or
		// phrase of the item's name from orefit.heavy.words ("armor", "jacket", ...). Whatever the name
		// does not say is light: a chest flattened under a shirt is worse than a nipple showing through a
		// coat. Each item is decided once, and a heavy or listed one says why in the log.
		bool Heavy(const Catalog& a_catalog, RE::TESObjectARMO* a_item)
		{
			const auto id = a_item->GetFormID();
			if (const auto it = g_resolved.heavyOf.find(id); it != g_resolved.heavyOf.end()) {
				return it->second;
			}
			const auto  name = NameOfForm(a_item);
			bool        heavy = false;
			std::string why;
			if (g_resolved.heavy.contains(id) || AnyIEquals(a_catalog.heavyNames, name)) {
				heavy = true;
				why = "listed as heavy";
			} else if (g_resolved.light.contains(id) || AnyIEquals(a_catalog.lightNames, name)) {
				why = "listed as light";
			} else if (auto word = a_catalog.HeavyWord(name); !word.empty()) {
				heavy = true;
				why = std::format("the name says \"{}\"", word);
			}
			g_resolved.heavyOf.emplace(id, heavy);
			if (!why.empty()) {
				logger::info("clothing {:08X} \"{}\" ({}): {} - {}", id, name, PluginOf(a_item), heavy ? "heavy" : "light", why);
			}
			return heavy;
		}

		// What they wear, from the biped, as OBody decides it (S-20): the item of the equip event
		// being handled counts as already off or already on, since the biped may not show it yet.
		Worn ReadWorn(RE::Actor* a_actor, const Catalog& a_catalog, bool a_female, RE::TESForm* a_changing, bool a_equipping)
		{
			std::array<RE::TESObjectARMO*, 32> bySlot{};
			if (const auto& biped = a_actor->biped; biped) {
				for (std::size_t i = 0; i < bySlot.size(); ++i) {
					auto* form = biped->object[i].parent.object;
					bySlot[i] = form && form->Is(RE::ENUM_FORM_ID::kARMO) ? static_cast<RE::TESObjectARMO*>(form) : nullptr;
				}
			}
			auto* changing = a_changing && a_changing->Is(RE::ENUM_FORM_ID::kARMO) ? static_cast<RE::TESObjectARMO*>(a_changing) : nullptr;
			if (changing) {
				if (a_equipping) {
					const auto slots = SlotsOf(changing);
					for (std::size_t i = 0; i < bySlot.size(); ++i) {
						if (slots & (1u << i)) {
							bySlot[i] = changing;
						}
					}
				} else {
					for (auto& item : bySlot) {
						if (item == changing) {
							item = nullptr;
						}
					}
				}
			}

			Worn       worn;
			const auto skins = SkinsOf(a_actor);
			const auto skin = [&](RE::TESObjectARMO* a_item) { return std::ranges::find(skins, a_item) != skins.end(); };
			std::unordered_set<RE::TESObjectARMO*> checked;
			for (auto* item : bySlot) {
				if (!item || skin(item) || !checked.insert(item).second) {
					continue;
				}
				worn.powerArmor = worn.powerArmor || PowerArmor(item);
				if (!Dresses(a_catalog, item)) {
					continue;
				}
				worn.clothed = true;
				if (!worn.heavy && Heavy(a_catalog, item)) {
					worn.heavy = true;
					worn.heavyBy = NameOfForm(item);
				}
			}
			if (worn.clothed) {
				for (const int slot : a_catalog.clothedSlots) {
					auto* item = bySlot[static_cast<std::size_t>(slot - 30)];
					if (!item || skin(item)) {
						continue;
					}
					if (auto set = a_catalog.OutfitRefitSet(NameOfForm(item), a_female); !set.empty()) {
						worn.outfitSet = std::move(set);
						break;
					}
				}
			}
			// OBody raises OnActorRemovingClothes for whatever leaves the body, chest or pelvis slots,
			// whatever ORefit's own lists say about it -- but a power armour piece put down is not clothing.
			worn.removing = changing && !a_equipping && !skin(changing) && (SlotsOf(changing) & g_resolved.clothedMask) != 0 && !PowerArmor(changing);
			return worn;
		}

		std::optional<Sighting> Read(RE::Actor* a_actor, const Catalog& a_catalog, RE::TESForm* a_changing, bool a_equipping, bool* a_removing)
		{
			auto* npc = a_actor ? a_actor->GetNPC() : nullptr;
			if (!npc) {
				return std::nullopt;
			}
			Sighting s;
			s.ref = a_actor->GetFormID();
			s.base = npc->GetFormID();
			s.facts.female = Compat::Female(npc);
			s.facts.seed = s.ref;
			// The NPC record's name, as OBody reads it: a reference renamed at runtime (Rapport names the
			// settlers it befriends) keeps the rule its record matched.
			s.facts.baseName = NameOfForm(npc);
			s.eligible = !NeverShaped(a_actor);

			// The record and every template up its chain, as BodyGen matches a form-id line; the plugin
			// of the chain's root, as BodyGen applies a plugin line (only to records with no template).
			int depth = 0;
			for (auto* n = npc; n && depth < 16; n = n->faceNPC, ++depth) {
				if (const auto* file = n->GetFile(0)) {
					s.facts.bases.push_back(FormRef{ file->filename, n->GetLocalFormID() });
					s.facts.originPlugin = file->filename;
				}
			}
			// The record's own factions, as OBody reads them. A leveled record that takes its factions
			// from a template already carries them; walking the chain could only add false matches.
			for (const auto& [faction, ref] : g_resolved.factions) {
				if (faction && npc->IsInFaction(faction) &&
					std::ranges::none_of(s.facts.factions, [&](const FormRef& f) { return f.Is(ref.plugin, ref.id); })) {
					s.facts.factions.push_back(ref);
				}
			}
			s.facts.race = RaceOf(a_actor);
			const auto worn = ReadWorn(a_actor, a_catalog, s.facts.female, a_changing, a_equipping);
			s.clothed = worn.clothed;
			s.heavy = worn.heavy;
			s.heavyBy = worn.heavyBy;
			s.powerArmor = worn.powerArmor;
			s.outfitSet = worn.outfitSet;
			if (a_removing) {
				*a_removing = worn.removing;
			}
			return s;
		}

		template <class T>
		void PushCapped(std::deque<T>& a_queue, T a_item)
		{
			if (a_queue.size() >= kInboxLimit) {
				a_queue.pop_front();  // the oldest: an actor seen again later is read again then
				++g_inbox.dropped;
			}
			a_queue.push_back(a_item);
		}

		void Watch()
		{
			for (;;) {
				std::this_thread::sleep_for(std::chrono::seconds{ 20 });
				auto loaded = g_loadedMs.load();
				if (loaded != 0 && g_pumpedMs.load() < loaded && NowMs() - loaded > kSilentBridgeMs) {
					// Once per load -- and not over a newer load's stamp, set while this one was being checked.
					// The bridge only sweeps when this plugin cannot be used (S-54): say why, not "check the esp".
					if (g_loadedMs.compare_exchange_strong(loaded, 0)) {
						if (!g_director.Ready()) {
							logger::warn("nobody is shaped one by one this session - {}. The bridge, where Silhouette.esp and its scripts are "
										 "there, only takes refits off (BodyGen still gives bodies)",
								g_director.Status());
						} else if (g_askedMs.load() >= loaded) {
							// Asked by Silhouette's scripts -- the bridge's, or the API's (the regeneration window,
							// the MCM page, which shows without the esp, another mod) -- so they are there, but the
							// bridge does not poll.
							logger::warn("Silhouette's scripts answered but the bridge does not poll: Silhouette.esp is not enabled (its "
										 "quest runs the bridge), the bridge's script is missing or from another release than Silhouette.dll, "
										 "or LooksMenu is not loaded. Install one release's files together and enable the esp; until the "
										 "bridge polls, nobody is shaped one by one (BodyGen still gives bodies)");
						} else {
							logger::warn("the bridge has not polled in the minute since the save loaded. If that goes on, check that Silhouette.esp "
										 "is enabled, its scripts are installed and LooksMenu is loaded: until it polls, nobody is shaped one by one "
										 "(BodyGen still gives bodies)");
						}
					}
				}
			}
		}
	}

	Director& TheDirector()
	{
		return g_director;
	}

	void Load()
	{
		std::string error;
		const auto  doc = ReadJson(std::filesystem::path{ kFolder } / "catalog.json", error);
		if (!doc) {
			logger::error("catalog: {} - rules, ORefit and the picker are off (BodyGen still gives bodies)", error);
			g_director.Refuse(std::format("no catalog: {}", error));
			return;
		}
		auto catalog = ParseCatalog(*doc, error);
		if (!catalog) {
			logger::error("catalog refused: {}", error);
			g_director.Refuse(std::format("catalog refused: {}", error));
			return;
		}

		// The catalog and the BodyGen files come from one generator run, or neither can be trusted: a
		// marker would name a preset of another build (S-19), or the runtime would apply rules the
		// BodyGen lines do not agree with.
		for (const auto file : { kTemplates, kMorphs }) {
			const auto header = ReadHeader(file, error);
			if (!header) {
				logger::error("catalog: {}", error);
				g_director.Refuse(error);
				return;
			}
			if (header->build != catalog->build || header->stamp != catalog->stamp || (!header->rules.empty() && header->rules != catalog->rulesHash)) {
				const auto why = std::format("the catalog is build {} (stamp {}, rules {}) but {} is build {} (stamp {}, rules {}): install one generator run's files together",
					catalog->build, catalog->stamp, catalog->rulesHash, file, header->build, header->stamp, header->rules.empty() ? "?" : header->rules);
				logger::error("catalog refused: {}", why);
				g_director.Refuse(why);
				return;
			}
		}

		// Walked with increment(ec): the range-for's ++ throws on an error, and a throw here would take
		// the game down at data load.
		std::size_t     manifests = 0;
		std::error_code ec;
		for (std::filesystem::directory_iterator it{ std::filesystem::path{ kFolder } / "manifests", ec }, end; !ec && it != end; it.increment(ec)) {
			// One file at a time, and nothing it does may throw out of here: a name the ANSI code page
			// cannot hold makes path::string() throw, and so would anything that formats it.
			try {
				const auto& path = it->path();
				if (path.extension() != ".json") {
					continue;
				}
				const auto  name = Utf8(path.filename());
				std::string merror;
				const auto  m = ReadJson(path, merror);
				auto        parsed = m ? ParseManifest(*m, merror) : std::nullopt;
				if (!parsed) {
					logger::warn("manifest {}: {}", name, merror);
					continue;
				}
				// A build's manifest is <stamp>.json: one under another name (copied, renamed by hand) would
				// replace the real one's meaning for every body of that build.
				if (path.stem().wstring() != std::to_wstring(parsed->first)) {
					logger::warn("manifest {} says it is build stamp {}: not read (a manifest is named for its stamp)", name, parsed->first);
					continue;
				}
				catalog->AddManifest(parsed->first, std::move(parsed->second));
				++manifests;
			} catch (const std::exception& e) {
				logger::warn("manifests: a file could not be read ({}) - skipped", e.what());
			}
		}
		if (ec) {
			logger::warn("manifests: {} - bodies of older builds may not be named or healed", ec.message());
		}

		g_resolved = {};
		for (const auto& rule : catalog->factionRules) {
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* faction = dh ? dh->LookupForm<RE::TESFaction>(rule.faction.id, rule.faction.plugin) : nullptr;
			if (!faction) {
				logger::warn("faction rule {} ({}|{:X}): not in this load order - the rule never matches", rule.editorID, rule.faction.plugin, rule.faction.id);
			}
			g_resolved.factions.emplace_back(faction, rule.faction);
		}
		g_resolved.blacklist = ResolveAll(catalog->outfitBlacklist);
		g_resolved.force = ResolveAll(catalog->forceRefit);
		g_resolved.heavy = ResolveAll(catalog->heavyItems);
		g_resolved.light = ResolveAll(catalog->lightItems);
		for (const int slot : catalog->clothedSlots) {
			g_resolved.clothedMask |= 1u << (slot - 30);
		}
		if (auto* dh = RE::TESDataHandler::GetSingleton()) {
			g_resolved.powerArmor = { dh->LookupForm<RE::BGSKeyword>(kPowerArmorFrameKeyword, "Fallout4.esm"sv),
				dh->LookupForm<RE::BGSKeyword>(kPowerArmorPieceKeyword, "Fallout4.esm"sv) };
		}
		if (!g_resolved.powerArmor[0] || !g_resolved.powerArmor[1]) {
			logger::warn("power armour keywords not found in Fallout4.esm: NPCs in power armour count as dressed");
		}

		logger::info("catalog: build {}, stamp {}, rules {}, {} presets, {} manifest(s), {} faction rule(s), {} refit set(s)",
			catalog->build, catalog->stamp, catalog->rulesHash, catalog->presets.size(), manifests, catalog->factionRules.size(), catalog->refitSets.size());
		g_director.SetCatalog(std::make_shared<const Catalog>(std::move(*catalog)));
		logger::info("{}", g_director.Status());
	}

	void NoteLoaded(std::uint32_t a_ref)
	{
		std::scoped_lock l{ g_inbox.lock };
		PushCapped(g_inbox.loaded, a_ref);
	}

	void NoteEquip(std::uint32_t a_ref, std::uint32_t a_item, bool a_equipped)
	{
		std::scoped_lock l{ g_inbox.lock };
		PushCapped(g_inbox.equips, Inbox::Equip{ a_ref, a_item, a_equipped });
	}

	void NoteCrosshair(std::uint32_t a_activate, std::uint32_t a_dialogue)
	{
		// "Aimed at within the last N seconds" counts from when the crosshair LEFT them: a long look
		// followed by opening a menu is the case the window exists for.
		g_trail.Note(a_activate, NowMs());
		g_dialoguePick.store(a_dialogue);
	}

	void ForgetInbox()
	{
		{
			std::scoped_lock l{ g_inbox.lock };
			g_inbox.loaded.clear();
			g_inbox.equips.clear();
			g_inbox.dropped = 0;
			g_inbox.warned = false;
		}
		g_trail.Forget();
		g_dialoguePick.store(0);
		// Main thread (a load or a new game starting): an item created in the save being left (0xFF)
		// has an id the next save gives to something else. Every other id keeps its answer.
		std::erase_if(g_resolved.heavyOf, [](const auto& a_item) { return (a_item.first >> 24) == 0xFF; });
		g_sweepArmed = false;
		g_sweepUntilMs = 0;
		g_sweepSaid = true;
		g_swept.clear();
		g_reported.clear();
	}

	void NoteAsked()
	{
		g_askedMs.store(NowMs());
	}

	void See(RE::Actor* a_actor)
	{
		const auto catalog = g_director.CatalogPtr();
		if (!catalog || !a_actor || !Has3D(a_actor)) {
			return;
		}
		if (const auto s = Read(a_actor, *catalog, nullptr, false, nullptr)) {
			g_director.Seen(*s);
		}
	}

	void NoteGameLoaded()
	{
		g_loadedMs.store(NowMs());
		if (!g_watching.exchange(true)) {
			std::thread{ Watch }.detach();
		}
	}

	void ArmSweep()
	{
		// Main thread, like the pump that reads it.
		g_sweepArmed = true;
		g_sweepArmedMs = NowMs();
		g_sweepFirstMs = 0;
		g_sweepUntilMs = 0;
		g_sweepSaid = false;
		g_swept.clear();
		g_reported.clear();
	}

	RE::Actor* ActorFor(std::uint32_t a_ref)
	{
		if (a_ref == 0) {
			return nullptr;
		}
		auto* form = RE::TESForm::GetFormByID(a_ref);
		if (!form || !form->Is(RE::ENUM_FORM_ID::kACHR)) {
			return nullptr;
		}
		return static_cast<RE::Actor*>(form);
	}

	bool IsFemale(RE::Actor* a_actor)
	{
		auto* npc = a_actor ? a_actor->GetNPC() : nullptr;
		return Compat::Female(npc);
	}

	std::uint32_t BaseOf(RE::Actor* a_actor)
	{
		auto* npc = a_actor ? a_actor->GetNPC() : nullptr;
		return npc ? npc->GetFormID() : 0;
	}

	std::string NameOf(RE::Actor* a_actor)
	{
		return Compat::DisplayName(a_actor);
	}

	bool NeverShaped(RE::Actor* a_actor)
	{
		return !a_actor || a_actor == RE::PlayerCharacter::GetSingleton() || IsDummy(a_actor->GetNPC());
	}

	std::string RaceOf(RE::Actor* a_actor)
	{
		auto* npc = a_actor ? a_actor->GetNPC() : nullptr;
		return RaceName(npc && npc->formRace ? npc->formRace : (a_actor ? a_actor->race : nullptr));
	}

	void Pump()
	{
		g_pumpedMs.store(NowMs());
		GuardLayout();
		if (g_layout == Layout::kBad) {
			return;  // refused: nothing is read from members this runtime lays out otherwise
		}
		std::deque<std::uint32_t> loaded;
		std::deque<Inbox::Equip>  equips;
		std::size_t               dropped = 0;
		{
			std::scoped_lock l{ g_inbox.lock };
			loaded.swap(g_inbox.loaded);
			equips.swap(g_inbox.equips);
			if (g_inbox.dropped != 0 && !g_inbox.warned) {
				g_inbox.warned = true;
				dropped = g_inbox.dropped;
			}
		}
		if (dropped != 0) {
			logger::warn("the bridge fell behind: {} actor event(s) dropped, the oldest first; those actors are read again when they next load", dropped);
		}
		if (g_sweepArmed) {
			const auto now = NowMs();
			if (g_sweepUntilMs == 0) {
				g_sweepFirstMs = now;
				g_sweepUntilMs = now + kSweepMs;  // from the first poll after the load, however late it came
			}
			if (now < g_sweepUntilMs) {
				g_reported.insert(loaded.begin(), loaded.end());
				if (g_director.CatalogPtr()) {
					Sweep(loaded);  // without a catalog nobody is read, so nobody is counted as read
				}
			} else if (!g_sweepSaid) {
				g_sweepSaid = true;
				g_sweepArmed = false;
				const auto told = static_cast<std::size_t>(std::ranges::count_if(g_swept, [](std::uint32_t a_ref) { return g_reported.contains(a_ref); }));
				logger::info("after loading: {} actor(s) around the player read; the game reported {} of them as loaded, and {} it reported were "
							 "not among them; the first poll came {:.1f} s after the load",
					g_swept.size(), told, g_reported.size() - told, static_cast<double>(g_sweepFirstMs - g_sweepArmedMs) / 1000.0);
			}
		}
		const auto catalog = g_director.CatalogPtr();
		if (catalog) {
			std::unordered_set<std::uint32_t> done;
			for (const auto ref : loaded) {
				if (!done.insert(ref).second) {
					continue;
				}
				auto* actor = ActorFor(ref);
				if (!actor || !Has3D(actor)) {
					continue;
				}
				if (const auto s = Read(actor, *catalog, nullptr, false, nullptr)) {
					g_director.Seen(*s);
				}
			}
			for (const auto& e : equips) {
				auto* item = RE::TESForm::GetFormByID(e.item);
				if (!item || !item->Is(RE::ENUM_FORM_ID::kARMO)) {
					continue;  // a weapon, ammunition, aid: nothing anyone wears
				}
				auto* actor = ActorFor(e.ref);
				if (!actor || !Has3D(actor)) {
					continue;  // no biped to read: they are read again when they load
				}
				bool removing = false;
				if (const auto s = Read(actor, *catalog, item, e.equipped, &removing)) {
					g_director.Dressed(*s, removing);
				}
			}
		}
		FlushLog();
		// What the bridge did, every half minute while there is anything to say (L5 #2).
		if (const auto now = NowMs(); now - g_summaryMs >= kSummaryMs) {
			g_summaryMs = now;
			if (const auto line = g_director.TakeSummary(); !line.empty()) {
				logger::info("{}", line);
			}
		}
	}

	std::uint32_t CrosshairActor(float a_recentSeconds)
	{
		const auto recentMs = a_recentSeconds > 0.0F ? static_cast<std::int64_t>(a_recentSeconds * 1000.0F) : std::int64_t{ 0 };
		RE::NiPointer<RE::TESObjectREFR> chosen;
		const auto handle = g_trail.Choose(recentMs, NowMs(), [&](std::uint32_t a_handle) {
			auto  ref = RefFor(a_handle);
			auto* actor = ref ? ActorFor(ref->GetFormID()) : nullptr;
			if (!actor || NeverShaped(actor) || !actor->GetNPC()) {
				return false;
			}
			chosen = std::move(ref);
			return true;
		});
		// Pick and the menu ask only when the player acts, so every answer is written: the log says what the
		// game reported there either way.
		if (chosen) {
			logger::info("pick: {}, {}", Described(chosen.get()),
				handle == g_trail.Current() ? std::string{ "under the crosshair" } : std::format("aimed at within the last {:.0f} s", a_recentSeconds));
			return chosen->GetFormID();
		}
		{
			// The one question a player cannot answer from the screen: what did the game report there.
			const auto current = g_trail.Current();
			const auto ref = RefFor(current);
			auto*      actor = ref ? ActorFor(ref->GetFormID()) : nullptr;
			std::string what;
			if (current == 0) {
				what = "nothing is under the crosshair within reach -- aim at someone close enough to talk to";
			} else if (!ref) {
				what = "the reference under the crosshair is gone";
			} else if (!actor || !actor->GetNPC()) {
				what = std::format("the crosshair is on {}, not an NPC", Described(ref.get()));
			} else {
				what = std::format("the crosshair is on {}, whom Silhouette never shapes (the player or a character-creation dummy)", Described(ref.get()));
			}
			if (const auto talk = g_dialoguePick.load(); talk != 0 && talk != current) {
				const auto other = RefFor(talk);
				what += std::format("; the dialogue pick is {}", other ? Described(other.get()) : "gone");
			}
			if (recentMs > 0) {
				what += std::format("; nobody Silhouette shapes was aimed at in the last {:.0f} s", a_recentSeconds);
			}
			logger::info("pick: nobody to pick - {}", what);
		}
		return 0;
	}

	void FlushLog()
	{
		for (const auto& line : g_director.TakeLog()) {
			logger::info("{}", line);
		}
	}
}
