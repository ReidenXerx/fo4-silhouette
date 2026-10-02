#include "Papyrus.h"

#include "Camera.h"
#include "Game.h"
#include "Sinks.h"

namespace SH::Papyrus
{
	namespace
	{
		// "Native" is a reserved word in Papyrus, so the script cannot be called that (S-46).
		constexpr auto kScript = "Silhouette:DLL"sv;

		// What an order asks of the bridge, and in which order. The bridge checks this at every load and
		// refuses to run orders it would carry out differently: a DLL and scripts of different builds.
		// 3: OrderReadsDone, OrderGone, OrderDefer, EventDone, RequestAdopt; Request* take a lane and
		// return why they refused ("" = accepted); marker kind 3, the choice marker.
		// 4: ResetEveryone (S-68), which the bridge's MCM button calls.
		// 5: NextNotice (S-71), which the bridge shows on the player's screen; FreshStart (S-70), which the
		// regeneration window presses once for a save new to Silhouette; Configure's faction pools (S-73).
		// 6: BodyWarning, which the bridge shows once a launch when a sex has no body Silhouette supports.
		// 7: PickerShow, PickerPresets, PickerIndex, PickerCurrent, PickerFemale, BodySupported, CameraFrame,
		// CameraRestore, CameraStep -- the picker
		// window (S-79).
		// 8: PlayerPresets, PlayerPresetIndex, PlayerBodyCount, PlayerBodyMorph, PlayerBodyValue -- the window's Me
		// tab lists the catalog's presets, the player's own BodySlide presets among them (0.3.3).
		constexpr std::int32_t kProtocol = 8;

		using Str = RE::BSFixedString;

		std::mutex  g_errorLock;
		std::string g_lastError;

		void SetError(std::string a_why)
		{
			std::scoped_lock l{ g_errorLock };
			g_lastError = std::move(a_why);
		}

		Director& D() { return Game::TheDirector(); }

		std::uint32_t Ref(std::int32_t a_id) { return static_cast<std::uint32_t>(a_id); }
		std::uint32_t Id(std::int32_t a_order) { return static_cast<std::uint32_t>(a_order); }

		// 0 urgent (the player's own actions), 1 normal (other mods, rules), 2 background (bulk work) (S-55).
		Lane LaneOf(std::int32_t a_lane) { return static_cast<Lane>(std::clamp(a_lane, 0, 2)); }

		// ---- lifecycle (data only) ----

		bool IsReady(std::monostate) { return D().Ready(); }

		Str Status(std::monostate)
		{
			return Str{ std::format("{} | events: {} | {} order(s) waiting, {} record(s)", D().Status(), Sinks::Status(), D().Pending(), D().RecordCount()) };
		}

		Str Version(std::monostate) { return Str{ SH_VERSION_STRING }; }

		// The bridge asks at every load: the watchdog then knows it is there, even if it never polls.
		std::int32_t ProtocolVersion(std::monostate)
		{
			Game::NoteAsked();
			return kProtocol;
		}

		std::int32_t Stamp(std::monostate)
		{
			const auto c = D().CatalogPtr();
			return c ? static_cast<std::int32_t>(c->stamp) : 0;
		}

		Str Build(std::monostate)
		{
			const auto c = D().CatalogPtr();
			return Str{ c ? c->build : std::string{} };
		}

		void Configure(std::monostate, bool a_orefit, bool a_nipples, bool a_genitals, bool a_factionPools)
		{
			D().Configure(Settings{ .orefit = a_orefit, .variety = { .nipples = a_nipples, .genitals = a_genitals },
				.factionPools = a_factionPools });
		}

		std::int32_t Pending(std::monostate) { return static_cast<std::int32_t>(D().Pending()); }

		void Log(std::monostate, Str a_line) { logger::info("bridge: {}", a_line.c_str()); }

		// ---- orders (data only) ----

		std::int32_t NextOrder(std::monostate) { return static_cast<std::int32_t>(D().NextOrder()); }

		std::int32_t OrderActor(std::monostate, std::int32_t a_order) { return static_cast<std::int32_t>(D().OrderActor(Id(a_order))); }

		std::int32_t OrderKind(std::monostate, std::int32_t a_order)
		{
			const auto o = D().Peek(Id(a_order));
			return o ? static_cast<std::int32_t>(o->kind) : 0;
		}

		bool OrderFemale(std::monostate, std::int32_t a_order)
		{
			const auto o = D().Peek(Id(a_order));
			return o && o->female;
		}

		bool OrderRegenerates(std::monostate, std::int32_t a_order)
		{
			const auto o = D().Peek(Id(a_order));
			return o && o->regenerate;
		}

		bool OrderProbes(std::monostate, std::int32_t a_order)
		{
			const auto o = D().Peek(Id(a_order));
			return o && o->probe;
		}

		bool OrderReadsAll(std::monostate, std::int32_t a_order)
		{
			const auto o = D().Peek(Id(a_order));
			return o && o->readAll;
		}

		void NoteName(std::monostate, std::int32_t a_order, Str a_morph) { D().NoteName(Id(a_order), a_morph.c_str()); }

		// 0 an ordinary morph, 1 a body marker (read unkeyed), 2 the refit marker (read under the keyword),
		// 3 the choice marker (read unkeyed, S-51).
		std::int32_t MarkerKind(std::monostate, Str a_morph) { return static_cast<std::int32_t>(KindOf(a_morph.c_str())); }

		void NoteMarker(std::monostate, std::int32_t a_order, Str a_marker, float a_value)
		{
			D().NoteMarker(Id(a_order), a_marker.c_str(), a_value);
		}

		std::int32_t OrderReadCount(std::monostate, std::int32_t a_order) { return D().ReadCount(Id(a_order)); }

		Str OrderReadMorph(std::monostate, std::int32_t a_order, std::int32_t a_index) { return Str{ D().ReadMorph(Id(a_order), a_index) }; }

		void NoteRead(std::monostate, std::int32_t a_order, std::int32_t a_index, float a_value) { D().NoteRead(Id(a_order), a_index, a_value); }

		// The reads so far already answer the order: the bridge stops reading.
		bool OrderReadsDone(std::monostate, std::int32_t a_order) { return D().ReadsDone(Id(a_order)); }

		void NoteLayer(std::monostate, std::int32_t a_order, Str a_morph, float a_value) { D().NoteLayer(Id(a_order), a_morph.c_str(), a_value); }

		bool Prepare(std::monostate, std::int32_t a_order) { return D().Prepare(Id(a_order)); }

		bool OrderClearsUnkeyed(std::monostate, std::int32_t a_order) { return D().ClearsUnkeyed(Id(a_order)); }

		bool OrderClearsRefit(std::monostate, std::int32_t a_order) { return D().ClearsRefit(Id(a_order)); }

		std::int32_t OrderWriteCount(std::monostate, std::int32_t a_order) { return D().WriteCount(Id(a_order)); }

		Str OrderWriteMorph(std::monostate, std::int32_t a_order, std::int32_t a_index) { return Str{ D().WriteMorph(Id(a_order), a_index) }; }

		float OrderWriteValue(std::monostate, std::int32_t a_order, std::int32_t a_index) { return D().WriteValue(Id(a_order), a_index); }

		// 0 the unkeyed layer (keyword None), 1 Silhouette's refit keyword.
		std::int32_t OrderWriteLayer(std::monostate, std::int32_t a_order, std::int32_t a_index)
		{
			return static_cast<std::int32_t>(D().WriteLayer(Id(a_order), a_index));
		}

		bool OrderUpdates(std::monostate, std::int32_t a_order) { return D().Updates(Id(a_order)); }

		void OrderDone(std::monostate, std::int32_t a_order, bool a_ok) { D().Done(Id(a_order), a_ok); }

		// The actor is not in memory (Game.GetForm gave None): the work waits for their next sighting.
		void OrderGone(std::monostate, std::int32_t a_order) { D().Gone(Id(a_order)); }

		// Another mod has them busy (AAF): the work is tried again later, not now.
		void OrderDefer(std::monostate, std::int32_t a_order) { D().Defer(Id(a_order)); }

		// ---- events (data only) ----

		std::int32_t NextEvent(std::monostate) { return static_cast<std::int32_t>(D().NextEvent()); }

		// S-71: a line for the player's screen, "" when none. The director's state only.
		Str NextNotice(std::monostate) { return Str{ D().NextNotice() }; }
		// Which sex has no body Silhouette supports (Game::CheckBodies), once a launch: the bridge shows it in a box.
		Str BodyWarning(std::monostate) { return Str{ Game::TakeBodyWarning() }; }

		std::int32_t EventKind(std::monostate, std::int32_t a_event)
		{
			const auto e = D().EventAt(Id(a_event));
			return e ? static_cast<std::int32_t>(e->kind) : 0;
		}

		std::int32_t EventActor(std::monostate, std::int32_t a_event)
		{
			const auto e = D().EventAt(Id(a_event));
			return e ? static_cast<std::int32_t>(e->ref) : 0;
		}

		Str EventPreset(std::monostate, std::int32_t a_event)
		{
			const auto e = D().EventAt(Id(a_event));
			return Str{ e ? e->preset : std::string{} };
		}

		bool EventFlag(std::monostate, std::int32_t a_event)
		{
			const auto e = D().EventAt(Id(a_event));
			return e && e->flag;
		}

		// The bridge raised it: only now is an OnActorGenerated remembered as said (a save between the
		// hand-out and the raise says it again after the load).
		void EventDone(std::monostate, std::int32_t a_event) { D().EventDone(Id(a_event)); }

		// ---- the picker ----

		// Each answer is logged as well as returned: a tester driving the picker from the console (cgf) cannot
		// read what a native returns, and a refusal must not look like a press that did nothing.
		std::string Said(std::string_view a_what, std::string a_answer)
		{
			logger::info("picker: {} -> {}", a_what, a_answer.empty() ? "done"s : std::format("\"{}\"", a_answer));
			return a_answer;
		}

		Str          PickerStep(std::monostate, std::int32_t a_step) { return Str{ Said(std::format("step {}", a_step), D().PickerStep(a_step)) }; }
		// S-79, the picker window: a preset by name, the list with each one's kind, and where the picking is.
		Str          PickerShow(std::monostate, Str a_preset) { return Str{ Said(std::format("show {}", a_preset.c_str()), D().PickerShow(a_preset.c_str())) }; }
		Str          PickerPresets(std::monostate) { return Str{ D().PickerPresets() }; }
		std::int32_t PickerIndex(std::monostate) { return D().PickerIndex(); }
		Str          PickerCurrent(std::monostate) { return Str{ D().PickerCurrent() }; }
		bool         PickerFemale(std::monostate) { return D().PickerFemale(); }
		// S-78, for the window's own body: whether a sex has a body Silhouette supports.
		bool BodySupported(std::monostate, bool a_female)
		{
			const auto c = D().CatalogPtr();
			return c && c->bodySupported[a_female ? 1 : 0];
		}
		// S-79 (0.3.3), the window's Me tab: the catalog's presets for the player and each one's body, data only --
		// the bridge writes it. Asked by name every time, so nothing is held between calls.
		Str PlayerPresets(std::monostate, bool a_female)
		{
			const auto c = D().CatalogPtr();
			return c ? Str{ MenuList(*c, a_female) } : Str{};
		}
		std::int32_t PlayerPresetIndex(std::monostate, Str a_preset, bool a_female)
		{
			const auto c = D().CatalogPtr();
			if (!c) {
				return -1;
			}
			const auto list = c->MenuPresets(a_female);
			const auto it = std::ranges::find_if(list, [&](const Preset* a_p) { return IEquals(a_p->name, a_preset.c_str()); });
			return it == list.end() ? -1 : static_cast<std::int32_t>(it - list.begin());
		}
		Morphs PlayerMorphs(Str a_preset, bool a_female)
		{
			const auto  c = D().CatalogPtr();
			const auto* p = c ? c->Find(a_preset.c_str(), a_female) : nullptr;
			return p ? PlayerBody(*c, *p) : Morphs{};
		}
		std::int32_t PlayerBodyCount(std::monostate, Str a_preset, bool a_female)
		{
			return static_cast<std::int32_t>(PlayerMorphs(a_preset, a_female).size());
		}
		Str PlayerBodyMorph(std::monostate, Str a_preset, bool a_female, std::int32_t a_index)
		{
			const auto m = PlayerMorphs(a_preset, a_female);
			return a_index >= 0 && a_index < static_cast<std::int32_t>(m.size()) ? Str{ m[static_cast<std::size_t>(a_index)].first } : Str{};
		}
		float PlayerBodyValue(std::monostate, Str a_preset, bool a_female, std::int32_t a_index)
		{
			const auto m = PlayerMorphs(a_preset, a_female);
			return a_index >= 0 && a_index < static_cast<std::int32_t>(m.size()) ? m[static_cast<std::size_t>(a_index)].second : 0.0F;
		}
		Str          PickerKeep(std::monostate) { return Str{ Said("keep", D().PickerKeep()) }; }
		Str          PickerCancel(std::monostate) { return Str{ Said("cancel", D().PickerCancel()) }; }
		std::int32_t PickerTarget(std::monostate) { return static_cast<std::int32_t>(D().PickerTarget()); }
		bool         PickerReady(std::monostate) { return D().PickerReady(); }

		// ---- queries (data only) ----

		Str AssignedPreset(std::monostate, std::int32_t a_actor) { return Str{ D().AssignedPreset(Ref(a_actor)) }; }

		// a_value: the marker's value, the build stamp -- or below 1 while the body is being written (S-58),
		// and then read against this build.
		Str PresetForMarker(std::monostate, Str a_marker, float a_value)
		{
			const auto c = D().CatalogPtr();
			if (!c || !(a_value > 0.0F) || a_value >= 16777216.0F) {
				return Str{};
			}
			const auto stamp = a_value < 1.0F ? c->stamp : static_cast<std::uint32_t>(std::lround(a_value));
			return Str{ c->PresetForMarker(a_marker.c_str(), stamp).value_or(std::string{}) };
		}

		std::int32_t PresetCount(std::monostate, bool a_female)
		{
			const auto c = D().CatalogPtr();
			return c ? static_cast<std::int32_t>(c->MenuPresets(a_female).size()) : 0;
		}

		Str PresetName(std::monostate, bool a_female, std::int32_t a_index)
		{
			const auto c = D().CatalogPtr();
			if (!c) {
				return Str{};
			}
			const auto list = c->MenuPresets(a_female);
			return a_index >= 0 && static_cast<std::size_t>(a_index) < list.size() ? Str{ list[static_cast<std::size_t>(a_index)]->name } : Str{};
		}

		bool IsORefitEnabled(std::monostate)
		{
			const auto c = D().CatalogPtr();
			return c && !c->refitSets.empty() && D().Current().orefit;
		}

		bool IsORefitApplied(std::monostate, std::int32_t a_actor) { return D().RefitApplied(Ref(a_actor)); }

		// One switch each, read and written under the director's one lock: the MCM and another mod's API
		// call cannot undo each other.
		void SetORefit(std::monostate, bool a_on) { D().SetSwitch(Switch::kORefit, a_on); }
		void SetNippleRand(std::monostate, bool a_on) { D().SetSwitch(Switch::kNipples, a_on); }
		void SetGenitalRand(std::monostate, bool a_on) { D().SetSwitch(Switch::kGenitals, a_on); }

		Str Describe(std::monostate, std::int32_t a_actor) { return Str{ D().Describe(Ref(a_actor)) }; }

		Str LastError(std::monostate)
		{
			std::scoped_lock l{ g_errorLock };
			return Str{ g_lastError };
		}

		// ---- main thread: these read the game ----

		void Pump(std::monostate)
		{
			Sinks::Attach();  // the crosshair's source appears with the HUD; cheap once attached
			Game::Pump();
		}

		std::int32_t CrosshairActor(std::monostate, float a_recentSeconds) { return static_cast<std::int32_t>(Game::CrosshairActor(a_recentSeconds)); }

		// S-79, the picker window's camera: the free camera in front of them, and back. "" when it is there,
		// "wait" while the game carries out the toggle (the bridge asks CameraStep again), else why not.
		Str CameraSaid(std::string a_said)
		{
			if (!a_said.empty() && a_said != Camera::kWait) {
				logger::info("window: camera - {}", a_said);
			}
			return Str{ a_said };
		}
		Str CameraFrame(std::monostate, float a_x, float a_y, float a_z, float a_angle, float a_height)
		{
			return CameraSaid(Camera::Frame(a_x, a_y, a_z, a_angle, a_height));
		}
		Str CameraRestore(std::monostate) { return CameraSaid(Camera::Restore()); }
		Str CameraStep(std::monostate) { return CameraSaid(Camera::Step()); }

		// An NPC Silhouette shapes: not the player, not a character-creation dummy (S-13), a distributed race.
		RE::Actor* Shapeable(std::int32_t a_actor, std::string& a_why)
		{
			auto* actor = Game::ActorFor(Ref(a_actor));
			if (!actor || Game::NeverShaped(actor) || !actor->GetNPC()) {
				a_why = "that is not an NPC Silhouette can shape";
				return nullptr;
			}
			const auto c = D().CatalogPtr();
			if (!c) {
				a_why = D().Status();
				return nullptr;
			}
			const auto race = Game::RaceOf(actor);
			if (race.empty() || std::ranges::none_of(c->races, [&](const std::string& r) { return IEquals(r, race); })) {
				a_why = std::format("{} is of a race Silhouette does not shape ({})", Game::NameOf(actor), race.empty() ? "?" : race);
				return nullptr;
			}
			if (const bool female = Game::IsFemale(actor); !c->bodySupported[female ? 1 : 0]) {
				a_why = std::format("no {} body Silhouette supports is installed, so it leaves {} alone (Silhouette.log says why)",
					female ? "female" : "male", female ? "women" : "men");
				return nullptr;
			}
			return actor;
		}

		bool CanShape(std::monostate, std::int32_t a_actor)
		{
			std::string why;
			return Shapeable(a_actor, why) != nullptr;
		}

		Str PickerStart(std::monostate, std::int32_t a_actor)
		{
			std::string why;
			auto*       actor = Shapeable(a_actor, why);
			if (!actor) {
				return Str{ Said(std::format("start {:08X}", static_cast<std::uint32_t>(a_actor)), why) };
			}
			return Str{ Said(std::format("start {:08X}", static_cast<std::uint32_t>(a_actor)),
				D().PickerStart(Ref(a_actor), Game::IsFemale(actor), Game::BaseOf(actor), Game::NameOf(actor))) };
		}

		// Each request answers with why it said no, "" when it was accepted: the answer belongs to the call
		// that asked, where a shared LastError could be another script's by the time it is read. LastError
		// is still set, for scripts built before the answer was returned.
		template <class F>
		Str Request(std::int32_t a_actor, F&& a_do)
		{
			std::string why;
			auto*       actor = Shapeable(a_actor, why);
			if (!actor || !a_do(actor, why)) {
				if (why.empty()) {
					why = "refused";
				}
				SetError(why);
				return Str{ why };
			}
			SetError({});
			return Str{};
		}

		Str RequestPreset(std::monostate, std::int32_t a_actor, Str a_preset, std::int32_t a_source, std::int32_t a_lane)
		{
			const auto source = a_source == static_cast<std::int32_t>(Source::kPicker) ? Source::kPicker : Source::kAPI;
			return Request(a_actor, [&](RE::Actor* a, std::string& why) {
				return D().RequestPreset(Ref(a_actor), Game::IsFemale(a), Game::BaseOf(a), a_preset.c_str(), source, LaneOf(a_lane), why);
			});
		}

		Str RequestRegenerate(std::monostate, std::int32_t a_actor, std::int32_t a_lane)
		{
			return Request(a_actor, [&](RE::Actor* a, std::string& why) {
				// A rule draws again only for someone the director knows (S-60): one asked about before the
				// pump saw them load would get the rule's same preset back.
				Game::See(a);
				return D().RequestRegenerate(Ref(a_actor), Game::IsFemale(a), Game::BaseOf(a), LaneOf(a_lane), why);
			});
		}

		Str RequestReset(std::monostate, std::int32_t a_actor, std::int32_t a_lane)
		{
			return Request(a_actor, [&](RE::Actor* a, std::string& why) {
				return D().RequestReset(Ref(a_actor), Game::IsFemale(a), Game::BaseOf(a), LaneOf(a_lane), why);
			});
		}

		Str RequestReapply(std::monostate, std::int32_t a_actor, Str a_markerPreset, std::int32_t a_lane)
		{
			return Request(a_actor, [&](RE::Actor* a, std::string& why) {
				return D().RequestReapply(Ref(a_actor), Game::IsFemale(a), Game::BaseOf(a), a_markerPreset.c_str(), LaneOf(a_lane), why);
			});
		}

		// The regeneration window's roll (S-15): refused for anyone with a choice, a reset, a picking or
		// work already on the way. Always the background lane.
		Str RequestAdopt(std::monostate, std::int32_t a_actor)
		{
			return Request(a_actor, [&](RE::Actor* a, std::string& why) {
				return D().RequestAdopt(Ref(a_actor), Game::IsFemale(a), Game::BaseOf(a), why);
			});
		}

		// S-68, MCM's "Reset everyone". What happened, or why not -- either way the line for the player. The
		// director's state only: the people it resets are the ones it has already read.
		Str ResetEveryone(std::monostate)
		{
			std::string said;
			const bool  done = D().RequestResetEveryone(said);
			SetError(done ? std::string{} : said);
			return Str{ done ? said : "not done: " + said };
		}

		// S-70: Reset everyone for a save new to Silhouette, pressed by the regeneration window's first scans.
		// "" when it is done, else why not -- this call's own answer, never a LastError another script may
		// have set between two calls (seventh wave): a wrong "done" would never ask again.
		Str FreshStart(std::monostate)
		{
			std::string said;
			if (!D().RequestResetEveryone(said)) {
				return Str{ said.empty() ? std::string{ "not done" } : said };
			}
			logger::info("a save new to Silhouette: Reset everyone pressed for it (S-70) - {}", said);
			return Str{};
		}

		Str NameOf(std::monostate, std::int32_t a_actor) { return Str{ Game::NameOf(Game::ActorFor(Ref(a_actor))) }; }

		// Binds a_fn. a_fast: callable from tasklets, so a call costs no frame -- set on our own
		// function object before binding rather than through the VM's SetCallableFromTasklets, a
		// virtual this plugin has never been seen to call on this runtime. Only functions that touch
		// nothing but the director's state (its own lock) may be fast; anything that reads the game
		// stays on the main thread.
		template <class F>
		void Bind(RE::BSScript::IVirtualMachine* a_vm, std::string_view a_name, F a_fn, bool a_fast)
		{
			auto* fn = new RE::BSScript::NativeFunction(kScript, a_name, a_fn, false);
			fn->isCallableFromTasklet = a_fast;
			if (!a_vm->BindNativeMethod(fn)) {
				logger::error("papyrus: could not bind {}.{}", kScript, a_name);
			}
		}
	}

	bool Register(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		constexpr bool fast = true;
		constexpr bool main = false;

		Bind(a_vm, "IsReady"sv, IsReady, fast);
		Bind(a_vm, "Status"sv, Status, fast);
		Bind(a_vm, "Version"sv, Version, fast);
		Bind(a_vm, "ProtocolVersion"sv, ProtocolVersion, fast);
		Bind(a_vm, "Stamp"sv, Stamp, fast);
		Bind(a_vm, "Build"sv, Build, fast);
		Bind(a_vm, "Configure"sv, Configure, fast);
		Bind(a_vm, "Pending"sv, Pending, fast);
		Bind(a_vm, "Log"sv, Log, fast);

		Bind(a_vm, "NextOrder"sv, NextOrder, fast);
		Bind(a_vm, "OrderActor"sv, OrderActor, fast);
		Bind(a_vm, "OrderKind"sv, OrderKind, fast);
		Bind(a_vm, "OrderFemale"sv, OrderFemale, fast);
		Bind(a_vm, "OrderRegenerates"sv, OrderRegenerates, fast);
		Bind(a_vm, "OrderProbes"sv, OrderProbes, fast);
		Bind(a_vm, "OrderReadsAll"sv, OrderReadsAll, fast);
		Bind(a_vm, "NoteName"sv, NoteName, fast);
		Bind(a_vm, "MarkerKind"sv, MarkerKind, fast);
		Bind(a_vm, "NoteMarker"sv, NoteMarker, fast);
		Bind(a_vm, "OrderReadCount"sv, OrderReadCount, fast);
		Bind(a_vm, "OrderReadMorph"sv, OrderReadMorph, fast);
		Bind(a_vm, "NoteRead"sv, NoteRead, fast);
		Bind(a_vm, "OrderReadsDone"sv, OrderReadsDone, fast);
		Bind(a_vm, "NoteLayer"sv, NoteLayer, fast);
		Bind(a_vm, "Prepare"sv, Prepare, fast);
		Bind(a_vm, "OrderClearsUnkeyed"sv, OrderClearsUnkeyed, fast);
		Bind(a_vm, "OrderClearsRefit"sv, OrderClearsRefit, fast);
		Bind(a_vm, "OrderWriteCount"sv, OrderWriteCount, fast);
		Bind(a_vm, "OrderWriteMorph"sv, OrderWriteMorph, fast);
		Bind(a_vm, "OrderWriteValue"sv, OrderWriteValue, fast);
		Bind(a_vm, "OrderWriteLayer"sv, OrderWriteLayer, fast);
		Bind(a_vm, "OrderUpdates"sv, OrderUpdates, fast);
		Bind(a_vm, "OrderDone"sv, OrderDone, fast);
		Bind(a_vm, "OrderGone"sv, OrderGone, fast);
		Bind(a_vm, "OrderDefer"sv, OrderDefer, fast);

		Bind(a_vm, "NextEvent"sv, NextEvent, fast);
		Bind(a_vm, "NextNotice"sv, NextNotice, fast);
		Bind(a_vm, "BodyWarning"sv, BodyWarning, fast);
		Bind(a_vm, "EventKind"sv, EventKind, fast);
		Bind(a_vm, "EventActor"sv, EventActor, fast);
		Bind(a_vm, "EventPreset"sv, EventPreset, fast);
		Bind(a_vm, "EventFlag"sv, EventFlag, fast);
		Bind(a_vm, "EventDone"sv, EventDone, fast);

		Bind(a_vm, "PickerStep"sv, PickerStep, fast);
		Bind(a_vm, "PickerShow"sv, PickerShow, fast);
		Bind(a_vm, "PickerPresets"sv, PickerPresets, fast);
		Bind(a_vm, "PickerIndex"sv, PickerIndex, fast);
		Bind(a_vm, "PickerCurrent"sv, PickerCurrent, fast);
		Bind(a_vm, "PickerFemale"sv, PickerFemale, fast);
		Bind(a_vm, "BodySupported"sv, BodySupported, fast);
		Bind(a_vm, "PlayerPresets"sv, PlayerPresets, fast);
		Bind(a_vm, "PlayerPresetIndex"sv, PlayerPresetIndex, fast);
		Bind(a_vm, "PlayerBodyCount"sv, PlayerBodyCount, fast);
		Bind(a_vm, "PlayerBodyMorph"sv, PlayerBodyMorph, fast);
		Bind(a_vm, "PlayerBodyValue"sv, PlayerBodyValue, fast);
		Bind(a_vm, "PickerKeep"sv, PickerKeep, fast);
		Bind(a_vm, "PickerCancel"sv, PickerCancel, fast);
		Bind(a_vm, "PickerTarget"sv, PickerTarget, fast);
		Bind(a_vm, "PickerReady"sv, PickerReady, fast);

		Bind(a_vm, "AssignedPreset"sv, AssignedPreset, fast);
		Bind(a_vm, "PresetForMarker"sv, PresetForMarker, fast);
		Bind(a_vm, "PresetCount"sv, PresetCount, fast);
		Bind(a_vm, "PresetName"sv, PresetName, fast);
		Bind(a_vm, "IsORefitEnabled"sv, IsORefitEnabled, fast);
		Bind(a_vm, "IsORefitApplied"sv, IsORefitApplied, fast);
		Bind(a_vm, "SetORefit"sv, SetORefit, fast);
		Bind(a_vm, "SetNippleRand"sv, SetNippleRand, fast);
		Bind(a_vm, "SetGenitalRand"sv, SetGenitalRand, fast);
		Bind(a_vm, "Describe"sv, Describe, fast);
		Bind(a_vm, "LastError"sv, LastError, fast);

		Bind(a_vm, "Pump"sv, Pump, main);
		Bind(a_vm, "CrosshairActor"sv, CrosshairActor, main);
		Bind(a_vm, "CameraFrame"sv, CameraFrame, main);
		Bind(a_vm, "CameraRestore"sv, CameraRestore, main);
		Bind(a_vm, "CameraStep"sv, CameraStep, main);
		Bind(a_vm, "CanShape"sv, CanShape, main);
		Bind(a_vm, "PickerStart"sv, PickerStart, main);
		Bind(a_vm, "RequestPreset"sv, RequestPreset, main);
		Bind(a_vm, "RequestRegenerate"sv, RequestRegenerate, main);
		Bind(a_vm, "RequestReset"sv, RequestReset, main);
		Bind(a_vm, "RequestReapply"sv, RequestReapply, main);
		Bind(a_vm, "RequestAdopt"sv, RequestAdopt, main);
		Bind(a_vm, "ResetEveryone"sv, ResetEveryone, fast);
		Bind(a_vm, "FreshStart"sv, FreshStart, fast);
		Bind(a_vm, "NameOf"sv, NameOf, main);

		logger::info("papyrus: {} bound (protocol {})", kScript, kProtocol);
		return true;
	}
}
