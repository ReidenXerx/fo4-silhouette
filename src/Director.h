#pragma once

#include "Catalog.h"
#include "Plan.h"
#include "Registry.h"
#include "Rules.h"

// The plugin's whole decision-making, with no game in it (S-18): what the game side saw goes in, orders
// for the bridge come out, and what the bridge reports comes back. The game side reads actors on the
// main thread and hands over plain facts; the offline tests do the same by hand, so every path below
// runs in the tests exactly as it runs in the game.
//
// Truth first (S-43): the co-save holds INTENT -- who chose which body -- and LooksMenu holds the body.
// Every session begins each actor with a probe of LooksMenu, and orders make the body match the intent.
// Every order is safe to repeat, so a save that lands in the middle of one is repaired by the next probe.
//
// Thread-safe: the natives call in from Papyrus threads and the pump from the main thread, so every
// public member takes the one lock. The co-save callbacks run the registry's bytes under it too.

namespace SH
{
	// Who waits for whom (S-55): the player's own actions first, then decisions, then the rest.
	enum class Lane : int
	{
		kUrgent = 0,      // the picker, the NPC page, a refit coming off
		kNormal = 1,      // other mods' API calls, rules, refits going on, touch-ups, first contact while dressing
		kBackground = 2,  // probes, the bulk buttons, the regeneration window, deferred work
	};

	// One change to an actor's body that the plugin has decided on and the bridge has not made yet.
	struct BodyRequest
	{
		enum class What
		{
			kPreset,      // a preset (and its variety), replacing the unkeyed layer
			kBlacklist,   // bare, with the blacklist marker, no refit (S-23)
			kRestore,     // the picker's Cancel: exactly the unkeyed layer they had
			kRegenerate,  // BodyGen rolls again, keyed layers kept
			kReset,       // the unkeyed layer and the refit removed: a new body at the next load (S-53)
			kMark,        // only the choice marker, beside a body already there (S-51)
		};

		What        what{ What::kPreset };
		std::string preset;
		bool        preview{ false };      // the picker trying a preset on: not intent
		bool        keepVariety{ false };  // Refresh / Reapply: read the layer first, keep the variety in it
		Morphs      keepFrom;              // picker previews: keep the variety of the body they had at Pick
		Morphs      restore;
		Source      choice{ Source::kNone };  // picker or API: the choice marker goes with the body (S-51)
		// The player asked for it: queued in the urgent lane by their own request (the picker, MCM's page for
		// the NPC in your sights). An actor's work takes the most urgent lane of everything it holds, so the
		// lane an order goes out in cannot say whose change it is; this can (S-71).
		bool asked{ false };
	};

	// What the game side read about an actor, on the main thread.
	struct Sighting
	{
		std::uint32_t ref{ 0 };
		std::uint32_t base{ 0 };        // the NPC record's runtime form id
		ActorFacts    facts;
		bool          eligible{ true };  // false: the player, the character-creation dummies
		bool          clothed{ false };
		bool          heavy{ false };      // S-48
		std::string   heavyBy;             // the item that made them heavy, for "Which body"
		bool          powerArmor{ false };  // power armour on the biped: getting in and out is not undressing
		std::string   outfitSet;           // the refit set the worn outfit brings, "" for none
	};

	enum class EventKind : std::int32_t
	{
		kGenerated = 1,        // OnActorGenerated(actor, preset)
		kNaked = 2,            // OnActorNaked(actor)
		kRemovingClothes = 3,  // OnActorRemovingClothes(actor)
		kORefitChanged = 4,    // OnORefitChanged(actor, applied)
	};

	struct Event
	{
		std::uint32_t id{ 0 };
		EventKind     kind{ EventKind::kGenerated };
		std::uint32_t ref{ 0 };
		std::string   preset;
		bool          flag{ false };
		std::uint32_t announce{ 0 };  // kGenerated: the body it announces, recorded when the bridge raised it
		bool          done{ false };  // the bridge raised it (EventDone)
	};

	enum class OrderKind : std::int32_t
	{
		kProbe = 1,     // what LooksMenu holds for them
		kBody = 2,      // change the body
		kRefit = 3,     // the refit layer on or off (S-40)
		kSnapshot = 4,  // the picker's copy of the whole unkeyed layer
		kTouch = 5,     // heal and top-up (S-29, S-44)
	};

	enum class Layer : std::int32_t
	{
		kUnkeyed = 0,  // the body's own layer (keyword None)
		kRefit = 1,    // Silhouette's refit keyword (Silhouette.esp 0x803)
	};

	struct Write
	{
		std::string morph;
		float       value{ 0.0F };
		Layer       layer{ Layer::kUnkeyed };
	};

	// One job for the bridge. It does, in this order: regenerate, probe (names, markers), readAll, the
	// reads (stopping early when told), Prepare, the clears, the writes, update; then reports Done -- or
	// Gone when the actor is not in memory, or Defer when another mod has them busy.
	struct Order
	{
		std::uint32_t id{ 0 };
		std::uint32_t ref{ 0 };
		bool          female{ false };
		OrderKind     kind{ OrderKind::kProbe };
		Lane          lane{ Lane::kBackground };

		bool                     regenerate{ false };
		bool                     probe{ false };
		bool                     readAll{ false };
		std::vector<std::string> reads;
		bool                     readsUntilBody{ false };  // stop at the first non-zero own value (S-41)

		std::string              marker;  // reported by the probe: the body marker
		float                    markerValue{ 0.0F };
		float                    refitValue{ 0.0F };   // the refit marker's value, 0 for none
		float                    choiceValue{ 0.0F };  // the choice marker's value, 0 for none
		std::vector<std::string> names;
		std::vector<float>       readValues;  // parallel to reads; NaN until reported
		Morphs                   layer;       // reported by readAll

		bool               readsDecided{ false };
		bool               prepared{ false };
		bool               clearUnkeyed{ false };
		bool               clearRefit{ false };
		std::vector<Write> writes;
		bool               update{ false };

		BodyRequest   body;
		bool          refitOn{ false };
		bool          heavy{ false };
		std::string   refitSet;
		std::uint32_t touchKey{ 0 };
	};

	struct Settings
	{
		bool            orefit{ true };  // MCM, and SetORefit (S-24)
		VarietySwitches variety;
		bool            factionPools{ true };  // MCM: Silhouette's own faction pools (S-72, S-73)
	};

	enum class Switch
	{
		kORefit,
		kNipples,
		kGenitals,
	};

	class Director
	{
	public:
		Director();

		// --- lifecycle ---
		void SetCatalog(std::shared_ptr<const Catalog> a_catalog);
		void Refuse(std::string a_why);  // no catalog, or one that cannot be trusted: act on nothing
		[[nodiscard]] bool                           Ready() const;
		[[nodiscard]] std::string                    Status() const;
		[[nodiscard]] std::shared_ptr<const Catalog> CatalogPtr() const;

		void                   Configure(const Settings& a_settings);
		void                   SetSwitch(Switch a_switch, bool a_on);  // one switch, under one lock
		[[nodiscard]] Settings Current() const;

		// A save is being left: everything about the world goes, the records stay (the co-save's).
		void ForgetWorld();
		// The co-save is reverting (new game, or before a load): the records go too.
		void RevertRecords();

		// --- what the game saw ---
		void Seen(const Sighting& a_sighting);
		// An equip event was handled: a_sighting is the actor after it. a_removedClothing: the event took
		// off an item from a body, chest or pelvis slot (OnActorRemovingClothes).
		void Dressed(const Sighting& a_sighting, bool a_removedClothing);

		// --- requests (API, MCM, picker); each writes the intent at once (S-43) ---
		bool RequestPreset(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, std::string_view a_preset, Source a_source, Lane a_lane,
			std::string& a_why);
		// Back to random: BodyGen rolls again and the rules get their say -- a rule with several presets
		// draws again, somewhere new (S-60). Owed until it lands, across a save too (S-59).
		bool RequestRegenerate(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, Lane a_lane, std::string& a_why);
		// Bare now, a new body at the next load (S-53); owed until it lands, across a save too (S-59).
		bool RequestReset(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, Lane a_lane, std::string& a_why);
		// The body they have, again, with this build's values and their own variety: their record's
		// preset, or the one their marker names (a_markerPreset, from a probe) for a body BodyGen gave.
		// Refused while they are picked (the preview is not their body) or a roll or reset is owed.
		bool RequestReapply(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, std::string_view a_markerPreset, Lane a_lane,
			std::string& a_why);
		// The regeneration window (S-15): a roll for someone other mods marked first -- refused for anyone
		// with a choice behind their body, a reset waiting, a picking, or work on the way.
		bool RequestAdopt(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, std::string& a_why);
		// S-68, MCM's "Reset everyone": a fresh start. Every body Silhouette gave, picks included, is decided
		// again as if they were met for the first time -- a roll, then the rules have their say. Everyone seen
		// this session now; every choice and rule's draw on record, wherever they are; and anyone met later
		// whose body a build older than the press made, when met. Refused while the picker is open. a_said:
		// what happened, or why not.
		bool RequestResetEveryone(std::string& a_said);

		// --- the bridge ---
		[[nodiscard]] std::uint32_t        NextOrder();
		[[nodiscard]] std::optional<Order> Peek(std::uint32_t a_order) const;
		[[nodiscard]] std::uint32_t        OrderActor(std::uint32_t a_order) const;
		void                               NoteName(std::uint32_t a_order, std::string_view a_morph);
		void                               NoteMarker(std::uint32_t a_order, std::string_view a_marker, float a_value);
		// How many morphs to read before Prepare -- decided on this first call, after the probe.
		[[nodiscard]] std::int32_t ReadCount(std::uint32_t a_order);
		[[nodiscard]] std::string  ReadMorph(std::uint32_t a_order, std::int32_t a_index) const;
		void                       NoteRead(std::uint32_t a_order, std::int32_t a_index, float a_value);
		[[nodiscard]] bool         ReadsDone(std::uint32_t a_order) const;  // the reads can stop here
		void                       NoteLayer(std::uint32_t a_order, std::string_view a_morph, float a_value);
		bool                       Prepare(std::uint32_t a_order);
		// S-87: the morphs never written on this actor's race (Servitron: no breast sliders), nullptr for none.
		[[nodiscard]] const std::vector<std::string>* Without(std::uint32_t a_ref) const;
		[[nodiscard]] bool         ClearsUnkeyed(std::uint32_t a_order) const;
		[[nodiscard]] bool         ClearsRefit(std::uint32_t a_order) const;
		[[nodiscard]] bool         Updates(std::uint32_t a_order) const;
		[[nodiscard]] std::int32_t WriteCount(std::uint32_t a_order) const;
		[[nodiscard]] std::string  WriteMorph(std::uint32_t a_order, std::int32_t a_index) const;
		[[nodiscard]] float        WriteValue(std::uint32_t a_order, std::int32_t a_index) const;
		[[nodiscard]] Layer        WriteLayer(std::uint32_t a_order, std::int32_t a_index) const;
		void                       Done(std::uint32_t a_order, bool a_ok);
		// The actor is not in memory: the work waits for the next sighting.
		void Gone(std::uint32_t a_order);
		// Another mod has them busy (an AAF scene): that order goes to the back and is not handed out again
		// for kDeferWait -- the bridge's drain ends instead of spinning on it until the scene is over. Only
		// that kind of work waits: a refit coming off, a probe, the picker still go out.
		void Defer(std::uint32_t a_order);
		// Orders the bridge can be handed now, and those in flight: work waiting out a deferral, or for an
		// actor out of memory, does not make the bridge poll faster.
		[[nodiscard]] std::size_t Pending() const;

		using Clock = std::function<std::chrono::steady_clock::time_point()>;
		static constexpr std::chrono::seconds kDeferWait{ 10 };
		void SetClock(Clock a_clock);  // tests move time by hand

		// Events for the bridge to raise, oldest first. 0: none. EventDone once it has been raised.
		[[nodiscard]] std::uint32_t        NextEvent();
		// S-71: the next line for the player's screen, "" when none. Said when a change the player asked for
		// (the urgent lane: the picker, MCM's page for the NPC in your sights) waits for another mod, and when
		// it lands after that.
		[[nodiscard]] std::string NextNotice();
		[[nodiscard]] std::optional<Event> EventAt(std::uint32_t a_event) const;
		void                               EventDone(std::uint32_t a_event);

		// --- the NPC picker (S-22, S-47) ---
		std::string                 PickerStart(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, std::string_view a_name);
		std::string                 PickerStep(std::int32_t a_step);
		std::string                 PickerShow(std::string_view a_preset);  // S-79: the window names the preset
		[[nodiscard]] std::string   PickerPresets() const;                 // "name<TAB>kind|...", kind y / p / o
		[[nodiscard]] std::int32_t  PickerIndex() const;                   // the preset tried on, -1 none
		[[nodiscard]] std::string   PickerCurrent() const;                 // what they had at Pick, "" not read yet
		[[nodiscard]] bool          PickerFemale() const;                  // the sex the picking lists presets for
		std::string                 PickerKeep();
		std::string                 PickerCancel();
		[[nodiscard]] std::uint32_t PickerTarget() const;
		[[nodiscard]] bool          PickerReady() const;  // the snapshot is in: Next can start

		// --- queries ---
		[[nodiscard]] std::string AssignedPreset(std::uint32_t a_ref) const;  // the intent
		[[nodiscard]] bool        RefitApplied(std::uint32_t a_ref) const;
		[[nodiscard]] std::string Describe(std::uint32_t a_ref) const;

		// --- the co-save (S-25, S-47) ---
		[[nodiscard]] std::vector<std::byte> SaveRecords(const Registry::KeepFn& a_keep) const;  // a_keep runs under the lock
		Registry::Loaded                     LoadRecords(std::span<const std::byte> a_bytes, std::uint32_t a_version,
								const std::function<std::uint32_t(std::uint32_t)>& a_resolve, std::string& a_error);
		[[nodiscard]] std::size_t            RecordCount() const;
		// S-68: the reset's own record; empty bytes when it was never pressed (nothing is written then).
		[[nodiscard]] std::vector<std::byte> SaveReset() const;
		Registry::Loaded                     LoadReset(std::span<const std::byte> a_bytes, std::uint32_t a_version, std::string& a_error,
								const std::function<std::uint32_t(std::uint32_t)>& a_resolve = {});
		[[nodiscard]] std::optional<Record>  RecordOf(std::uint32_t a_ref) const;
		[[nodiscard]] bool                   HasPicking(std::uint32_t a_ref) const;

		// Lines for the log, taken by the game side (the director never logs itself: no game here).
		[[nodiscard]] std::vector<std::string> TakeLog();
		// One line of what the bridge did since the last call, "" when nothing happened and nothing waits.
		[[nodiscard]] std::string TakeSummary();

	private:
		struct Work
		{
			using Time = std::chrono::steady_clock::time_point;

			Lane                       lane{ Lane::kBackground };
			bool                       snapshot{ false };
			std::optional<BodyRequest> body;
			bool                       touch{ false };
			bool                       refit{ false };
			bool                       probe{ false };
			bool                       parked{ false };  // the actor was not in memory: waits for a sighting
			Time                       bodyNotBefore{};   // a deferred body change: not handed out before this
			Time                       touchNotBefore{};  // a deferred touch-up, the same

			[[nodiscard]] bool Empty() const { return !snapshot && !body && !touch && !refit && !probe; }

			// Something here can be handed out now. A touch-up waits for a body on its way: the body it
			// would touch is about to be replaced.
			[[nodiscard]] bool Due(Time a_now) const
			{
				return snapshot || (body && bodyNotBefore <= a_now) || (touch && !body && touchNotBefore <= a_now) || refit || probe;
			}
		};

		struct Session
		{
			bool          known{ false };  // seen this session, with facts
			bool          eligible{ false };
			bool          blacklisted{ false };
			bool          female{ false };
			std::uint32_t base{ 0 };
			bool          clothed{ false };
			bool          heavy{ false };
			std::string   heavyBy;
			std::string   outfitSet;
			ActorFacts    facts;
			Verdict       verdict;

			// What this session knows of LooksMenu's layers (S-43).
			bool                     probed{ false };
			bool                     settled{ false };  // AfterProbe has acted on it: a snapshot can probe someone not seen yet
			std::string              marker;  // the body marker, "" for none
			std::uint32_t            stamp{ 0 };
			bool                     pendingBody{ false };  // the marker still says "pending": a save cut the body short (S-58)
			bool                     hasBody{ false };
			int                      refit{ -1 };  // -1 unknown, 0 none, -2 unfinished, else the refit marker's value
			Source                   choice{ Source::kNone };  // what the choice marker says (S-51)
			std::vector<std::string> names;                    // every layer's names
			std::vector<std::string> own;                      // of those read, the ones her OWN layer holds

			bool stranger{ false };        // a created reference's id reused: a choice LooksMenu holds was somebody else's
			bool reset{ false };           // reset this session: bare, never refit (S-41, S-53)
			bool regiven{ false };         // intent was restored once this session
			bool restoring{ false };       // a picker restore is queued
			bool marked{ false };          // the choice marker was asked for this session
			bool announceOnDone{ false };  // Keep on a preview still on its way: announce when it lands
			bool deferNoted{ false };
			bool deferTold{ false };  // a change the player asked for waits for another mod: they were told (S-71)
		};

		struct Picker
		{
			std::uint32_t            ref{ 0 };
			bool                     female{ false };
			std::uint32_t            base{ 0 };
			std::string              name;
			std::vector<std::string> presets;
			std::int32_t             index{ -1 };  // -1: nothing tried on yet
			bool                     snapped{ false };
			bool                     tried{ false };  // a preview was asked for: Cancel has something to undo
			std::string              current;         // the preset they had at Pick, "" unknown
		};

		struct Want
		{
			const RefitSet* set{ nullptr };
			bool            heavy{ false };
		};

		struct Counts
		{
			std::size_t probes{ 0 }, bodies{ 0 }, refits{ 0 }, touches{ 0 }, snapshots{ 0 }, failed{ 0 }, gone{ 0 }, deferred{ 0 };

			// Deferring again, and nothing else, is not news: a scene that never ends would print a line every
			// half minute. The count still goes out with the next line that has something to say.
			[[nodiscard]] bool Any() const { return probes || bodies || refits || touches || snapshots || failed || gone; }
		};

		// all of these expect the lock held
		void                      Admit(Session& a_session, const Sighting& a_sighting);
		void                      LeaveAlone(std::uint32_t a_ref, const Session& a_session);
		void                      Unpark(std::uint32_t a_ref);
		void                      PendingRestore(std::uint32_t a_ref, Session& a_session);
		void                      Retire(std::uint32_t a_ref);
		[[nodiscard]] Order*      Find(std::uint32_t a_order);
		bool                      PrepareWrites(Order* a_order);
		void                      Log(std::string a_line);
		void                      Push(EventKind a_kind, std::uint32_t a_ref, std::string a_preset = {}, bool a_flag = false, std::uint32_t a_announce = 0);
		Work&                     WorkFor(std::uint32_t a_ref, Lane a_lane);
		void                      QueueBody(std::uint32_t a_ref, BodyRequest a_body, Lane a_lane);
		void                      Requeue(Order& a_order, bool a_park);
		[[nodiscard]] bool        BodyPending(std::uint32_t a_ref) const;
		[[nodiscard]] bool        BodyReplacing(std::uint32_t a_ref) const;
		void                      PutBackChoice(std::uint32_t a_ref, const std::optional<Record>& a_before, std::uint32_t a_base);
		void                      Intend(std::uint32_t a_ref, const Session& a_session, Source a_source, std::string a_preset);
		[[nodiscard]] BodyRequest RestoreOf(const Morphs& a_snapshot, bool a_female) const;
		void                      AfterProbe(std::uint32_t a_ref, Session& a_session);
		void                      RebuildChoice(std::uint32_t a_ref, Session& a_session);
		[[nodiscard]] bool        Claimed(std::uint32_t a_ref, const Session& a_session) const;
		void                      Resettle(std::uint32_t a_ref);
		void                      FollowReset(std::uint32_t a_ref, Session& a_session);
		void                      FollowRoll(std::uint32_t a_ref, Session& a_session);
		void                      FollowResetEveryone(std::uint32_t a_ref, Session& a_session);
		void                      FinishPendingBody(std::uint32_t a_ref, Session& a_session);
		void                      DecideBody(std::uint32_t a_ref, Session& a_session);
		void                      Redraw(std::uint32_t a_ref, Session& a_session);
		void                      OnProbed(std::uint32_t a_ref, const Order& a_order);
		void                      Reconcile(std::uint32_t a_ref, Session& a_session);
		void                      AnnounceBody(std::uint32_t a_ref, const Session& a_session);
		void                      CheckTouch(std::uint32_t a_ref, const Session& a_session);
		[[nodiscard]] Want        WantRefit(const Session& a_session) const;
		void                      ReconcileRefit(std::uint32_t a_ref);
		[[nodiscard]] std::string PresetNamedBy(std::string_view a_marker, std::uint32_t a_stamp) const;
		[[nodiscard]] std::string BodyNamed(std::string_view a_marker, std::uint32_t a_stamp) const;
		void                      FinishBody(Order& a_order);
		void                      FinishRefit(Order& a_order);
		void                      ClosePicker();
		std::string               CancelPicking(std::string_view a_message = {});
		std::string               TryOn(const Morphs& a_snapshot);  // under the lock

		mutable std::mutex _lock;

		std::shared_ptr<const Catalog> _catalog;
		std::string                    _status{ "starting" };
		Settings                       _settings;

		Registry                                   _registry;
		std::unordered_map<std::uint32_t, Session> _sessions;
		std::unordered_map<std::uint32_t, Work>    _work;
		std::deque<std::uint32_t>                  _queue;  // references with work, in turn within a lane
		std::unordered_map<std::uint32_t, Order>   _inflight;
		std::unordered_set<std::uint32_t>          _busy;  // references with an order in flight
		std::deque<Event>                          _events;
		std::deque<std::string>                    _notices;  // S-71; the oldest go past kMaxNotices
		static constexpr std::size_t               kMaxNotices = 16;
		void                                       Notice(std::uint32_t a_ref, const Session& a_session, std::string_view a_what);
		std::deque<Event>                          _taken;  // the last few handed out, for their details
		bool                                       _eventsDropped{ false };
		std::uint32_t                              _nextOrder{ 1 };
		std::uint32_t                              _nextEvent{ 1 };
		Picker                                     _picker;
		std::vector<std::string>                   _log;
		Counts                                     _counts;
		Clock                                      _clock{ [] { return std::chrono::steady_clock::now(); } };
	};
}
