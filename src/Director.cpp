#include "Director.h"

namespace SH
{
	namespace
	{
		constexpr std::size_t kMaxEvents = 512;
		constexpr std::size_t kMaxLog = 512;
		constexpr float       kRefitPending = 0.25F;  // the refit marker while a refit is being written
		constexpr int         kRefitUnfinished = -2;  // what a probe makes of a pending marker
		constexpr float       kBodyPending = 0.25F;   // a body marker while its body is being written (S-58)

		bool Distributed(const Catalog& a_catalog, const ActorFacts& a_facts)
		{
			return a_catalog.bodySupported[a_facts.female ? 1 : 0] &&
			       std::ranges::any_of(a_catalog.races, [&](const std::string& r) { return IEquals(r, a_facts.race); });
		}

		// Ids start somewhere new each launch: a script stack a save resumed may still hold an id from
		// the last launch, and it must not name one of this launch's orders (S-43).
		std::uint32_t RandomStart()
		{
			std::random_device rd;
			return 0x100000u + (rd() % 0x3F000000u);
		}

		std::uint32_t Next(std::uint32_t& a_counter)
		{
			const auto id = a_counter++;
			if (a_counter == 0 || a_counter >= 0x7FFFFFF0u) {
				a_counter = 0x100000u;  // stays a positive Papyrus Int, never 0
			}
			return id;
		}

		std::uint32_t Stamp(float a_value)
		{
			return a_value > 0.0F && a_value < 16777216.0F ? static_cast<std::uint32_t>(std::lround(a_value)) : 0;
		}

		bool RefitOn(int a_refit)
		{
			return a_refit > 0 || a_refit == kRefitUnfinished;
		}

		bool Chosen(Source a_source)
		{
			return a_source == Source::kPicker || a_source == Source::kAPI;
		}

		// The choice marker's value is the source's byte (S-51).
		Source ChoiceOf(float a_value)
		{
			const auto v = std::lround(a_value);
			if (v == static_cast<long>(Source::kPicker)) {
				return Source::kPicker;
			}
			if (v == static_cast<long>(Source::kAPI)) {
				return Source::kAPI;
			}
			return Source::kNone;
		}

		bool ContainsI(const std::vector<std::string>& a_list, std::string_view a_name)
		{
			return std::ranges::any_of(a_list, [&](const std::string& s) { return IEquals(s, a_name); });
		}

		const char* LaneName(Lane a_lane)
		{
			switch (a_lane) {
			case Lane::kUrgent:
				return "urgent";
			case Lane::kNormal:
				return "normal";
			case Lane::kBackground:
				return "background";
			}
			return "?";
		}
	}

	Director::Director() :
		_nextOrder(RandomStart()), _nextEvent(RandomStart())
	{}

	// ------------------------------------------------------------------ lifecycle

	void Director::SetCatalog(std::shared_ptr<const Catalog> a_catalog)
	{
		std::scoped_lock l{ _lock };
		_catalog = std::move(a_catalog);
		if (_catalog) {
			std::size_t female = 0;
			for (const auto& p : _catalog->presets) {
				female += p.female ? 1 : 0;
			}
			_status = std::format("ready: build {} (stamp {}), {} female and {} male presets, {} rule(s) by name, {} by faction, {} refit set(s)",
				_catalog->build, _catalog->stamp, female, _catalog->presets.size() - female, _catalog->nameRules.size(),
				_catalog->factionRules.size(), _catalog->refitSets.size());
		}
	}

	void Director::Refuse(std::string a_why)
	{
		std::scoped_lock l{ _lock };
		_catalog.reset();
		_status = std::move(a_why);
	}

	bool Director::Ready() const
	{
		std::scoped_lock l{ _lock };
		return _catalog != nullptr;
	}

	std::string Director::Status() const
	{
		std::scoped_lock l{ _lock };
		return _status;
	}

	std::shared_ptr<const Catalog> Director::CatalogPtr() const
	{
		std::scoped_lock l{ _lock };
		return _catalog;
	}

	void Director::Configure(const Settings& a_settings)
	{
		std::scoped_lock l{ _lock };
		const bool refitChanged = a_settings.orefit != _settings.orefit;
		if (a_settings.factionPools != _settings.factionPools) {
			// Who is decided from now on; a body a faction's pool already gave stays until Reset (S-73).
			Log(std::format("faction bodies {} by the settings", a_settings.factionPools ? "on" : "off"));
		}
		_settings = a_settings;
		if (!refitChanged || !_catalog) {
			return;
		}
		// Everyone seen this session follows at once; the rest follow when they are next seen. Actors
		// Silhouette never shapes are left alone (ReconcileRefit only takes a refit it found off them).
		for (const auto& [ref, session] : _sessions) {
			if (session.known) {
				ReconcileRefit(ref);
			}
		}
		Log(std::format("ORefit {} by the settings", _settings.orefit ? "on" : "off"));
	}

	// One switch, read and written under one lock: two callers (the MCM and the API) cannot undo each other.
	void Director::SetSwitch(Switch a_switch, bool a_on)
	{
		std::scoped_lock l{ _lock };
		auto             next = _settings;
		switch (a_switch) {
		case Switch::kORefit:
			next.orefit = a_on;
			break;
		case Switch::kNipples:
			next.variety.nipples = a_on;
			break;
		case Switch::kGenitals:
			next.variety.genitals = a_on;
			break;
		}
		const bool refitChanged = next.orefit != _settings.orefit;
		_settings = next;
		if (refitChanged && _catalog) {
			for (const auto& [ref, session] : _sessions) {
				if (session.known) {
					ReconcileRefit(ref);
				}
			}
			Log(std::format("ORefit {} by the API", _settings.orefit ? "on" : "off"));
		}
	}

	Settings Director::Current() const
	{
		std::scoped_lock l{ _lock };
		return _settings;
	}

	void Director::ForgetWorld()
	{
		std::scoped_lock l{ _lock };
		_sessions.clear();
		_work.clear();
		_queue.clear();
		_inflight.clear();
		_busy.clear();
		_events.clear();
		_notices.clear();
		_taken.clear();
		_picker = {};
	}

	void Director::Notice(std::uint32_t a_ref, const Session& a_session, std::string_view a_what)
	{
		const auto who = a_session.facts.baseName.empty() ? std::format("{:08X}", a_ref) : a_session.facts.baseName;
		_notices.push_back(std::format("{}: {}", who, a_what));
		while (_notices.size() > kMaxNotices) {
			_notices.pop_front();
		}
	}

	std::string Director::NextNotice()
	{
		std::scoped_lock l{ _lock };
		if (_notices.empty()) {
			return {};
		}
		auto line = std::move(_notices.front());
		_notices.pop_front();
		return line;
	}

	void Director::RevertRecords()
	{
		std::scoped_lock l{ _lock };
		_registry.Clear();
	}

	// ------------------------------------------------------------------ what the game saw

	void Director::Admit(Session& a_session, const Sighting& a_sighting)
	{
		const auto ref = a_sighting.ref;
		// A created reference's id, handed to someone new: what we knew was about somebody else. Only a
		// created (0xFF) reference can be. A placed one is its NPC for good, and a leveled one's base is a
		// temporary record the engine replaces when it respawns, while LooksMenu keeps its morphs.
		if ((ref >> 24) == 0xFF && a_sighting.base != 0) {
			bool reused = false;
			if (a_session.known && a_session.base != 0 && a_session.base != a_sighting.base) {
				a_session = {};
				if (const auto w = _work.find(ref); w != _work.end()) {
					w->second = {};
				}
				reused = true;
			}
			if (const auto* rec = _registry.Find(ref); rec && rec->base != 0 && rec->base != a_sighting.base) {
				Log(std::format("{:08X}: the record was for NPC {:08X}, this is {:08X} - forgotten", ref, rec->base, a_sighting.base));
				_registry.Erase(ref);
				reused = true;
			}
			if (const auto p = _registry.pickings.find(ref); p != _registry.pickings.end() && p->second.base != 0 && p->second.base != a_sighting.base) {
				Log(std::format("{:08X}: the unfinished picking was for NPC {:08X}, this is {:08X} - forgotten", ref, p->second.base, a_sighting.base));
				_registry.pickings.erase(p);
				reused = true;
			}
			a_session.stranger = a_session.stranger || reused;
		}
		a_session.known = true;
		a_session.female = a_sighting.facts.female;
		a_session.base = a_sighting.base;
		a_session.clothed = a_sighting.clothed;
		a_session.heavy = a_sighting.heavy;
		a_session.heavyBy = a_sighting.heavyBy;
		a_session.outfitSet = a_sighting.outfitSet;
		a_session.facts = a_sighting.facts;
		if (const auto* rec = _registry.Find(ref)) {
			a_session.facts.salt = rec->salt;  // the rules draw as Back to random last left them (S-60)
		}
		a_session.eligible = a_sighting.eligible && Distributed(*_catalog, a_session.facts);
		a_session.verdict = Decide(*_catalog, a_session.facts, _settings.factionPools);
		a_session.blacklisted = a_session.verdict.blacklisted;
	}

	// The player, the character-creation dummies, creatures and every race Silhouette does not distribute
	// to: never shaped, never refit, so never probed either -- most actors in the world are one of these.
	// Only a refit this session already found (it cannot have been put there by this build) comes off.
	void Director::LeaveAlone(std::uint32_t a_ref, const Session& a_session)
	{
		if (RefitOn(a_session.refit)) {
			ReconcileRefit(a_ref);
		}
	}

	void Director::Unpark(std::uint32_t a_ref)
	{
		const auto w = _work.find(a_ref);
		if (w == _work.end() || !w->second.parked) {
			return;
		}
		w->second.parked = false;
		if (std::ranges::find(_queue, a_ref) == _queue.end()) {
			_queue.push_back(a_ref);
		}
	}

	void Director::Seen(const Sighting& a_sighting)
	{
		std::scoped_lock l{ _lock };
		if (!_catalog) {
			return;
		}
		const auto ref = a_sighting.ref;
		auto&      session = _sessions[ref];
		Admit(session, a_sighting);
		Unpark(ref);
		if (!session.eligible) {
			LeaveAlone(ref, session);
			return;
		}
		PendingRestore(ref, session);
		if (!session.probed) {
			// LooksMenu first (S-43): what the rules decide, they decide knowing the body she has.
			WorkFor(ref, Lane::kBackground).probe = true;
			return;
		}
		if (!session.settled) {
			AfterProbe(ref, session);  // the picker's snapshot read them before they were seen
			return;
		}
		DecideBody(ref, session);
		ReconcileRefit(ref);
	}

	void Director::Dressed(const Sighting& a_sighting, bool a_removedClothing)
	{
		std::scoped_lock l{ _lock };
		if (!_catalog) {
			return;
		}
		const auto ref = a_sighting.ref;
		auto&      session = _sessions[ref];
		const bool knew = session.known;
		const bool was = session.clothed;
		Admit(session, a_sighting);
		Unpark(ref);
		if (!session.eligible) {
			LeaveAlone(ref, session);
			return;
		}
		// Climbing into power armour takes the outfit off and getting out puts the pieces down: neither is
		// undressing, and a mod listening for these would be fooled by both.
		if (a_removedClothing && !a_sighting.powerArmor) {
			Push(EventKind::kRemovingClothes, ref);
		}
		if (knew && was && !session.clothed && !a_sighting.powerArmor) {
			Push(EventKind::kNaked, ref);
		}
		PendingRestore(ref, session);  // the first thing seen of them after a load can be an equip event
		if (!session.probed) {
			WorkFor(ref, Lane::kNormal).probe = true;  // first contact while dressing: the refit follows the probe
			return;
		}
		if (!session.settled) {
			AfterProbe(ref, session);  // the picker's snapshot read them before they were seen
			return;
		}
		ReconcileRefit(ref);
	}

	// ------------------------------------------------------------------ deciding

	bool Director::BodyPending(std::uint32_t a_ref) const
	{
		if (const auto it = _work.find(a_ref); it != _work.end() && it->second.body) {
			return true;
		}
		return std::ranges::any_of(_inflight, [&](const auto& p) { return p.second.ref == a_ref && p.second.kind == OrderKind::kBody; });
	}

	void Director::Intend(std::uint32_t a_ref, const Session& a_session, Source a_source, std::string a_preset)
	{
		auto& rec = _registry.Get(a_ref);
		if (a_session.base != 0) {
			rec.base = a_session.base;
		}
		rec.source = a_source;
		rec.preset = std::move(a_preset);
		rec.stamp = _catalog->stamp;
		_registry.Prune(a_ref);
	}

	BodyRequest Director::RestoreOf(const Morphs& a_snapshot, bool a_female) const
	{
		BodyRequest b{ .what = BodyRequest::What::kRestore };
		for (const auto& [m, v] : a_snapshot) {
			// What they had -- but never what no body may hold (S-16, S-29): a restore does not undo a heal.
			if (!_catalog->NeverInBody(a_female, m)) {
				b.restore.emplace_back(m, v);
			}
		}
		return b;
	}

	// A picking ends as a Cancel: the choice behind the body goes back as it was when the picking began. What
	// was done to bodies meanwhile stays done -- an announcement raised while they were picked is not made
	// again, nor a touch-up (fifth wave: one body, announced twice).
	void Director::PutBackChoice(std::uint32_t a_ref, const std::optional<Record>& a_before, std::uint32_t a_base)
	{
		const auto* now = _registry.Find(a_ref);
		const auto  announced = now ? now->announced : 0;
		const auto  touched = now ? now->touched : 0;
		if (a_before) {
			_registry.Get(a_ref) = *a_before;
		} else {
			_registry.Erase(a_ref);
		}
		if (announced != 0 || touched != 0) {
			auto& rec = _registry.Get(a_ref);
			rec.announced = announced;
			rec.touched = touched;
			if (rec.base == 0) {
				rec.base = a_base;
			}
		}
	}

	void Director::PendingRestore(std::uint32_t a_ref, Session& a_session)
	{
		const auto it = _registry.pickings.find(a_ref);
		if (it == _registry.pickings.end() || _picker.ref == a_ref || a_session.restoring) {
			return;
		}
		// A picking a save or an unreachable actor left unfinished (S-47): the choice behind the body goes
		// back now, as a Cancel's does, and the body follows.
		PutBackChoice(a_ref, it->second.before, a_session.base);
		a_session.restoring = true;
		Log(std::format("{:08X}: a picking left unfinished - the body they had goes back", a_ref));
		QueueBody(a_ref, RestoreOf(it->second.snapshot, it->second.female), Lane::kUrgent);
	}

	// A decision made elsewhere ends a picking of this actor and anything a picking left behind.
	void Director::Retire(std::uint32_t a_ref)
	{
		if (_picker.ref == a_ref) {
			ClosePicker();
		}
		_registry.pickings.erase(a_ref);
		if (const auto s = _sessions.find(a_ref); s != _sessions.end()) {
			s->second.restoring = false;
		}
	}

	void Director::AfterProbe(std::uint32_t a_ref, Session& a_session)
	{
		if (!a_session.known || !a_session.eligible) {
			ReconcileRefit(a_ref);
			return;
		}
		// While the player is picking them, or a picking is being put back, most of what follows waits: it
		// runs again when that ends (Resettle), not at the next load.
		a_session.settled = !(_picker.ref == a_ref || _registry.pickings.contains(a_ref) || a_session.restoring);
		RebuildChoice(a_ref, a_session);
		FollowReset(a_ref, a_session);
		FollowRoll(a_ref, a_session);
		FollowResetEveryone(a_ref, a_session);
		DecideBody(a_ref, a_session);
		Reconcile(a_ref, a_session);
		FinishPendingBody(a_ref, a_session);
		AnnounceBody(a_ref, a_session);
		CheckTouch(a_ref, a_session);
		ReconcileRefit(a_ref);
	}

	// The picker let go of them: what AfterProbe held back while they were picked is done now. A Seen
	// would do it too, but one comes only when their 3D loads again.
	void Director::Resettle(std::uint32_t a_ref)
	{
		if (const auto it = _sessions.find(a_ref); it != _sessions.end() && it->second.probed && !it->second.settled) {
			AfterProbe(a_ref, it->second);
		}
	}

	// A change to their body is on its way, or the player is choosing it: nothing else starts one.
	bool Director::Claimed(std::uint32_t a_ref, const Session& a_session) const
	{
		return BodyPending(a_ref) || _picker.ref == a_ref || _registry.pickings.contains(a_ref) || a_session.restoring;
	}

	// S-51: a picked or API-given body carries a marker saying so, beside its own. A save made while the
	// plugin was not loaded dropped the record; LooksMenu kept the marker, and the record comes back from it.
	void Director::RebuildChoice(std::uint32_t a_ref, Session& a_session)
	{
		if (!Chosen(a_session.choice) || a_session.stranger) {
			return;  // no choice -- or one that came with a reused id, from whoever had it before (Reconcile takes it off)
		}
		if (const auto* rec = _registry.Find(a_ref); rec && (rec->source != Source::kNone || !rec->preset.empty())) {
			return;  // the co-save still knows
		}
		const auto preset = PresetNamedBy(a_session.marker, a_session.stamp);
		if (preset.empty()) {
			return;
		}
		Intend(a_ref, a_session, a_session.choice, preset);
		_registry.Get(a_ref).stamp = a_session.stamp;  // the build the body has: a newer one re-gives it
		if (const auto p = _registry.pickings.find(a_ref); p != _registry.pickings.end()) {
			p->second.before = *_registry.Find(a_ref);  // picked meanwhile: a Cancel puts the choice back, not the lost record
		}
		Log(std::format("{:08X}: {} ({}) rebuilt from the choice LooksMenu keeps beside the body", a_ref, preset, SourceName(a_session.choice)));
	}

	// S-53: Reset means a new body at the next load. Without other mods' morphs LooksMenu's BodyGen gives
	// it; with them it never runs, and the plugin rolls one.
	void Director::FollowReset(std::uint32_t a_ref, Session& a_session)
	{
		const auto* rec = _registry.Find(a_ref);
		if (!rec || rec->source != Source::kReset || a_session.reset) {
			return;
		}
		if (_picker.ref == a_ref || _registry.pickings.contains(a_ref) || a_session.restoring) {
			// The body on them may be a preview: it is no body BodyGen gave them, and no new body starts under
			// the picker. This runs again when the picking ends (Resettle). Taking a preview for her body left a
			// reset owner bare for good (fifth wave).
			return;
		}
		if (rec->stamp == 0) {
			// S-59: asked for, and a save came before the bridge carried it out -- it is carried out now.
			if (!Claimed(a_ref, a_session)) {
				Log(std::format("{:08X}: a reset asked for before the save - carried out now", a_ref));
				QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kReset }, Lane::kNormal);
			}
			return;
		}
		if (a_session.hasBody) {
			Log(std::format("{:08X}: reset earlier; BodyGen has given them a body", a_ref));
			Intend(a_ref, a_session, Source::kNone, {});
			return;
		}
		if (!BodyPending(a_ref)) {
			Log(std::format("{:08X}: reset earlier and still bare - a new body", a_ref));
			QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kRegenerate }, Lane::kNormal);
		}
	}

	// S-59: a roll asked for -- Back to random, or the regeneration window's hand-off -- that a save came
	// before. The work was the session's and is gone; the record says it is owed.
	void Director::FollowRoll(std::uint32_t a_ref, Session& a_session)
	{
		const auto* rec = _registry.Find(a_ref);
		if (!rec || rec->source != Source::kRoll || Claimed(a_ref, a_session)) {
			return;
		}
		Log(std::format("{:08X}: a new body asked for before the save - rolled now", a_ref));
		QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kRegenerate }, Lane::kNormal);
	}

	// S-68: after "Reset everyone", someone met whose body a build older than the press made -- a roll, a
	// pick, a rule's body of then -- is decided again, once: a roll, and the rules have their say when it
	// lands. The records the press could reach it changed itself (ForgetChoices); this is everyone else.
	void Director::FollowResetEveryone(std::uint32_t a_ref, Session& a_session)
	{
		auto& stamps = _registry.resetStamps;
		if (stamps.empty()) {
			return;
		}
		if (std::ranges::find(stamps, _catalog->stamp) == stamps.end()) {
			stamps.push_back(_catalog->stamp);  // a build newer than the press: whatever it made came after it
		}
		if (Claimed(a_ref, a_session)) {
			return;
		}
		// S-70: the first sighting since the press. What they wear is looked at now and not again -- unless a
		// created reference's id has since been handed to somebody else (S-57): that is a first sighting too.
		auto [met, first] = _registry.resetMet.try_emplace(a_ref, a_session.base);
		if (!first && a_session.base != 0 && met->second != a_session.base) {
			first = met->second != 0 && (a_ref >> 24) == 0xFF;
			met->second = a_session.base;
		}
		if (const auto* rec = _registry.Find(a_ref);
			rec && (rec->source == Source::kRoll || rec->source == Source::kReset || rec->source == Source::kNameBlacklist)) {
			return;  // a new body is owed already, or they are kept bare by name
		}
		if (a_session.verdict.tier != Tier::kNone) {
			return;  // a rule by name or faction decides them: DecideBody gives the rule's body, this build's values
		}
		if (KindOf(a_session.marker) == MarkerKind::kBody && !IEquals(a_session.marker, kBlacklistMarker)) {
			if (a_session.pendingBody || a_session.stamp == 0 || std::ranges::find(stamps, a_session.stamp) != stamps.end()) {
				return;  // being written, or made since the press
			}
			Log(std::format("{:08X} \"{}\": {} is from before Reset everyone - a new body", a_ref, a_session.facts.baseName,
				PresetNamedBy(a_session.marker, a_session.stamp)));
		} else {
			// A body Silhouette did not make -- from before it was installed, or another mod's -- cannot be
			// dated -- nor can only other mods' keyed morphs, with which BodyGen never rolls them. It is decided
			// again at the first sighting since the press, where BodyGen gives them a body; after that, what is
			// put on them stays theirs.
			if (!first || !a_session.verdict.bodyGen) {
				return;
			}
			Log(std::format("{:08X} \"{}\": a body Silhouette did not make, met since Reset everyone - a new body", a_ref,
				a_session.facts.baseName));
		}
		Intend(a_ref, a_session, Source::kRoll, {});  // owed until it lands (S-59)
		QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kRegenerate }, Lane::kBackground);
	}

	// S-58: a body a save cut short while it was being written. Its marker went first, as "pending", so
	// the preset is known: it is given again, whole, keeping whatever variety made it in. A body with
	// intent behind it has had that done already (Reconcile); this is the one BodyGen gave, which a
	// Refresh or Reapply was writing again.
	void Director::FinishPendingBody(std::uint32_t a_ref, Session& a_session)
	{
		if (!a_session.pendingBody || !a_session.eligible || BodyReplacing(a_ref) || _picker.ref == a_ref || _registry.pickings.contains(a_ref) ||
			a_session.restoring) {
			return;
		}
		const auto  preset = _catalog->PresetForMarker(a_session.marker, _catalog->stamp).value_or(std::string{});
		const auto* p = preset.empty() ? nullptr : _catalog->Find(preset, a_session.female);
		if (!p) {
			Log(std::format("{:08X}: half a body of {} (a save cut it short) that this build cannot name - left as it is", a_ref, a_session.marker));
			return;
		}
		Log(std::format("{:08X}: half a body of {} (a save cut it short) - given again, whole", a_ref, p->name));
		QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kPreset, .preset = p->name, .keepVariety = true }, Lane::kNormal);
	}

	void Director::DecideBody(std::uint32_t a_ref, Session& a_session)
	{
		if (!a_session.eligible || !a_session.probed || _picker.ref == a_ref || _registry.pickings.contains(a_ref) || BodyPending(a_ref) ||
			a_session.restoring) {
			return;  // not ours, not read yet, the player is choosing or a picking waits to be put back, or a change is on its way
		}
		const auto& c = *_catalog;
		auto*       rec = _registry.Find(a_ref);

		// A choice somebody made stays made; a new build gives it this build's values, and the variety
		// the body already has (a picked body carries the variety it was picked with).
		if (rec && Chosen(rec->source)) {
			if (rec->stamp == c.stamp) {
				return;
			}
			if (const auto* p = c.Find(rec->preset, a_session.female)) {
				Log(std::format("{:08X}: {} ({}) again, with build {}'s values", a_ref, p->name, SourceName(rec->source), c.build));
				rec->stamp = c.stamp;
				rec->preset = p->name;
				QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kPreset, .preset = p->name, .keepVariety = true, .choice = rec->source },
					Lane::kNormal);
				return;
			}
			// The preset is gone from this build. The body stays, and so would the choice marker beside
			// it -- rebuilding the record from it every session only to drop it again: it comes off, and
			// the rules decide as for anyone else.
			Log(std::format("{:08X}: {} ({}) is not in build {}; their body stays, the rules decide from now on", a_ref, rec->preset,
				SourceName(rec->source), c.build));
			rec->source = Source::kNone;
			rec->preset.clear();
			_registry.Prune(a_ref);
			rec = _registry.Find(a_ref);
			if (Chosen(a_session.choice)) {
				a_session.marked = true;
				QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kMark, .choice = Source::kNone }, Lane::kNormal);
			}
		}
		if (rec && (rec->source == Source::kReset || rec->source == Source::kRoll)) {
			return;  // a new body is owed: it decides, and the rules have their say when it lands
		}

		const auto& v = a_session.verdict;
		switch (v.tier) {
		case Tier::kName:
		case Tier::kFaction:
			{
				const auto  source = v.tier == Tier::kName ? Source::kNameRule : Source::kFactionRule;
				std::string preset = v.preset;
				// A met NPC keeps what the rule drew for them while the rule still lists it (S-52).
				if (rec && rec->source == source && ContainsI(v.options, rec->preset)) {
					if (const auto* kept = c.Find(rec->preset, a_session.female)) {
						preset = kept->name;
					}
				}
				const auto* p = c.Find(preset, a_session.female);
				if (!p || (rec && rec->source == source && IEquals(rec->preset, preset) && rec->stamp == c.stamp)) {
					break;
				}
				Intend(a_ref, a_session, source, p->name);
				if (IEquals(a_session.marker, p->marker) && a_session.stamp == c.stamp) {
					break;  // that body is on them already: only the record was missing
				}
				Log(std::format("{:08X} \"{}\": {}", a_ref, a_session.facts.baseName, v.why));
				// The same preset again (a new build's values): the variety she has stays, as for a choice.
				QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kPreset, .preset = p->name, .keepVariety = IEquals(a_session.marker, p->marker) },
					Lane::kNormal);
				break;
			}
		case Tier::kNameBlacklist:
			if (!(rec && rec->source == Source::kNameBlacklist)) {
				Log(std::format("{:08X}: {}", a_ref, v.why));
				Intend(a_ref, a_session, Source::kNameBlacklist, {});
				if (!IEquals(a_session.marker, kBlacklistMarker)) {
					QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kBlacklist }, Lane::kNormal);
				}
			}
			break;
		case Tier::kNone:
			if (rec && (rec->source == Source::kNameRule || rec->source == Source::kFactionRule)) {
				// The rule is gone. Their body is a real one, so it stays; it is just no longer ours.
				Log(std::format("{:08X} \"{}\": no rule gives them {} any more; it stays, as BodyGen's", a_ref, a_session.facts.baseName, rec->preset));
				Intend(a_ref, a_session, Source::kNone, {});
			} else if (rec && rec->source == Source::kNameBlacklist) {
				Log(std::format("{:08X} \"{}\": no longer blacklisted - BodyGen rolls them", a_ref, a_session.facts.baseName));
				Intend(a_ref, a_session, Source::kRoll, {});  // owed until it lands (S-59)
				QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kRegenerate }, Lane::kNormal);
			}
			break;
		}
	}

	std::string Director::PresetNamedBy(std::string_view a_marker, std::uint32_t a_stamp) const
	{
		if (a_marker.empty() || a_stamp == 0 || IEquals(a_marker, kBlacklistMarker)) {
			return {};
		}
		return _catalog->PresetForMarker(a_marker, a_stamp).value_or(std::string{});
	}

	// For the log: the preset a marker names, with the build that made it -- the raw marker when no manifest
	// names it, "no Silhouette body" without one.
	std::string Director::BodyNamed(std::string_view a_marker, std::uint32_t a_stamp) const
	{
		if (a_marker.empty()) {
			return "no Silhouette body";
		}
		if (!_catalog) {
			return std::format("{} (build stamp {})", a_marker, a_stamp);
		}
		const auto preset = PresetNamedBy(a_marker, a_stamp);
		const auto build = a_stamp == _catalog->stamp ? "this build" : std::format("build stamp {}", a_stamp);
		return std::format("{} ({})", preset.empty() ? std::string{ a_marker } : preset, build);
	}

	void Director::OnProbed(std::uint32_t a_ref, const Order& a_order)
	{
		auto& s = _sessions[a_ref];
		s.probed = true;
		s.marker = a_order.marker;
		s.stamp = Stamp(a_order.markerValue);
		s.refit = a_order.refitValue >= 0.9F ? static_cast<int>(std::lround(a_order.refitValue))
		        : a_order.refitValue > 0.0F  ? kRefitUnfinished
		                                     : 0;
		s.choice = ChoiceOf(a_order.choiceValue);
		s.names = a_order.names;
		s.own.clear();
		if (a_order.readAll) {
			for (const auto& [m, v] : a_order.layer) {
				if (v != 0.0F && KindOf(m) == MarkerKind::kNone) {
					s.own.push_back(m);
				}
			}
		} else {
			for (std::size_t i = 0; i < a_order.reads.size() && i < a_order.readValues.size(); ++i) {
				const auto v = a_order.readValues[i];
				if (!std::isnan(v) && v != 0.0F) {
					s.own.push_back(a_order.reads[i]);
				}
			}
		}
		const bool bodyMarker = !s.marker.empty() && !IEquals(s.marker, kBlacklistMarker) && s.stamp != 0;
		s.pendingBody = !s.marker.empty() && !IEquals(s.marker, kBlacklistMarker) && a_order.markerValue > 0.0F && a_order.markerValue < 0.9F;
		s.hasBody = bodyMarker || !s.own.empty();
	}

	void Director::Reconcile(std::uint32_t a_ref, Session& a_session)
	{
		if (!a_session.eligible || _picker.ref == a_ref || _registry.pickings.contains(a_ref) || BodyPending(a_ref) || a_session.regiven ||
			a_session.restoring) {
			return;  // a picking waiting to be put back holds a preview: given again, it would spend the one re-give
		}
		const auto* rec = _registry.Find(a_ref);
		if (a_session.stranger && Chosen(a_session.choice) && !(rec && Chosen(rec->source)) && !a_session.marked) {
			// The body stays; the choice beside it was the previous owner's of this id, and would pin it against the rules.
			a_session.marked = true;
			Log(std::format("{:08X}: the choice LooksMenu holds came with a reused id - taken off", a_ref));
			QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kMark, .choice = Source::kNone }, Lane::kNormal);
			return;
		}
		if (!rec) {
			return;
		}
		// What the intent says the layer holds (S-43), against what the probe found.
		if (rec->source == Source::kNameBlacklist) {
			if (!IEquals(a_session.marker, kBlacklistMarker)) {
				a_session.regiven = true;
				Log(std::format("{:08X}: blacklisted by name but LooksMenu holds a body - bare again", a_ref));
				QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kBlacklist }, Lane::kNormal);
			}
			return;
		}
		if (rec->preset.empty()) {
			return;
		}
		const auto* p = _catalog->Find(rec->preset, a_session.female);
		if (!p) {
			return;
		}
		const auto choice = Chosen(rec->source) ? rec->source : Source::kNone;
		if (!IEquals(a_session.marker, p->marker) || a_session.stamp != rec->stamp) {
			a_session.regiven = true;
			Log(std::format("{:08X}: should have {} ({}), LooksMenu holds {} - given again", a_ref, rec->preset, SourceName(rec->source),
				a_session.marker.empty() ? std::string{ "no body marker" } : a_session.marker));
			if (rec->stamp != _catalog->stamp) {
				_registry.Get(a_ref).stamp = _catalog->stamp;
			}
			// Whatever variety is still on them stays: a body half written, or picked with the variety
			// it had, is not rolled anew for having been given again.
			QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kPreset, .preset = p->name, .keepVariety = true, .choice = choice }, Lane::kNormal);
			return;
		}
		// The body is right. A choice with no marker beside it (given before S-51, or the marker lost) gets one.
		if (choice != Source::kNone && a_session.choice != choice && !a_session.marked) {
			a_session.marked = true;
			QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kMark, .choice = choice }, Lane::kNormal);
		}
	}

	// A body that replaces the one on them is queued or being written. A choice marker written beside the body
	// leaves it as it is: what waits for a new body does not wait for a marker (nothing runs it again when a
	// marker lands -- the fifth wave found a top-up and a half body's repair held back for the session).
	bool Director::BodyReplacing(std::uint32_t a_ref) const
	{
		const auto replaces = [](const BodyRequest& a_body) { return a_body.what != BodyRequest::What::kMark; };
		if (const auto it = _work.find(a_ref); it != _work.end() && it->second.body && replaces(*it->second.body)) {
			return true;
		}
		return std::ranges::any_of(_inflight, [&](const auto& p) { return p.second.ref == a_ref && p.second.kind == OrderKind::kBody && replaces(p.second.body); });
	}

	void Director::AnnounceBody(std::uint32_t a_ref, const Session& a_session)
	{
		if (!a_session.eligible || _picker.ref == a_ref || _registry.pickings.contains(a_ref)) {
			return;  // while picked, the body on them may be a preview: Keep announces the one they keep
		}
		if (BodyReplacing(a_ref)) {
			return;  // a body about to be replaced is not announced: its replacement is, when it lands
		}
		const auto preset = PresetNamedBy(a_session.marker, a_session.stamp);
		if (preset.empty()) {
			return;
		}
		const auto hash = BodyHash(a_session.marker, a_session.stamp);
		if (const auto* rec = _registry.Find(a_ref); rec && rec->announced == hash) {
			return;
		}
		Push(EventKind::kGenerated, a_ref, preset, false, hash);
	}

	void Director::CheckTouch(std::uint32_t a_ref, const Session& a_session)
	{
		if (!a_session.eligible || _picker.ref == a_ref || BodyReplacing(a_ref) || _registry.pickings.contains(a_ref)) {
			return;
		}
		if (PresetNamedBy(a_session.marker, a_session.stamp).empty()) {
			return;  // only a body Silhouette can name is healed or topped up
		}
		const auto* without = Without(a_ref);
		const auto  key = TouchKey(*_catalog, a_session.marker, a_session.stamp, a_session.female, _settings.variety, without);
		if (const auto* rec = _registry.Find(a_ref); rec && rec->touched == key) {
			return;
		}
		const auto heal = _catalog->HealFor(a_session.marker, a_session.stamp);
		const bool healing = std::ranges::any_of(heal, [&](const std::string& m) { return ContainsI(a_session.own, m); }) ||
		                     (without && std::ranges::any_of(*without, [&](const std::string& m) { return ContainsI(a_session.own, m); }));
		if (healing || !TopUp(*_catalog, a_session.female, a_ref, _settings.variety, a_session.own).empty()) {
			WorkFor(a_ref, Lane::kNormal).touch = true;
			return;
		}
		// Nothing wanted: remembered, so a value the player takes off later is not put back (S-44).
		auto& rec = _registry.Get(a_ref);
		if (rec.base == 0) {
			rec.base = a_session.base;
		}
		rec.touched = key;
	}

	Director::Want Director::WantRefit(const Session& a_session) const
	{
		if (!_settings.orefit || !a_session.known || !a_session.eligible || !a_session.clothed || a_session.blacklisted ||
			a_session.reset || !a_session.probed || !a_session.hasBody) {
			return {};
		}
		const auto  preset = PresetNamedBy(a_session.marker, a_session.stamp);
		const auto* set = _catalog->RefitFor(preset, a_session.female, a_session.outfitSet);
		return set ? Want{ set, a_session.heavy } : Want{};
	}

	void Director::ReconcileRefit(std::uint32_t a_ref)
	{
		const auto it = _sessions.find(a_ref);
		if (it == _sessions.end() || !it->second.known) {
			return;
		}
		const auto& s = it->second;
		if (!s.eligible) {
			// Never refit (S-41): only a refit already found on them comes off; nothing is probed.
			if (RefitOn(s.refit)) {
				WorkFor(a_ref, Lane::kUrgent).refit = true;
			}
			return;
		}
		if (s.refit == -1) {
			if (!s.probed) {
				WorkFor(a_ref, Lane::kBackground).probe = true;  // unknown until the probe says
			}
			return;
		}
		const auto want = WantRefit(s);
		if (!want.set) {
			if (RefitOn(s.refit)) {
				WorkFor(a_ref, Lane::kUrgent).refit = true;  // coming off: at once
			}
			return;
		}
		if (s.refit != static_cast<int>(RefitMarker(*want.set, want.heavy))) {
			WorkFor(a_ref, Lane::kNormal).refit = true;
		}
	}

	Director::Work& Director::WorkFor(std::uint32_t a_ref, Lane a_lane)
	{
		auto [it, inserted] = _work.try_emplace(a_ref);
		auto& w = it->second;
		if (inserted) {
			w.lane = a_lane;
			_queue.push_back(a_ref);
		} else {
			w.lane = static_cast<Lane>(std::min(static_cast<int>(w.lane), static_cast<int>(a_lane)));
			if (!w.parked && std::ranges::find(_queue, a_ref) == _queue.end()) {
				_queue.push_back(a_ref);
			}
		}
		return w;
	}

	void Director::QueueBody(std::uint32_t a_ref, BodyRequest a_body, Lane a_lane)
	{
		a_body.asked = a_body.asked || a_lane == Lane::kUrgent;
		auto& w = WorkFor(a_ref, a_lane);
		w.body = std::move(a_body);  // the latest decision wins
		w.bodyNotBefore = {};        // and is tried at once, not after the one it replaced was deferred
	}

	void Director::Requeue(Order& a_order, bool a_park)
	{
		auto& w = WorkFor(a_order.ref, a_order.lane);
		switch (a_order.kind) {
		case OrderKind::kBody:
			if (!w.body) {
				w.body = std::move(a_order.body);  // a newer decision already waiting wins
			} else if (w.body->what == BodyRequest::What::kMark && a_order.body.what == BodyRequest::What::kPreset) {
				// ...but a marker is no body. Keep pressed while the preview was written queued the choice's
				// marker; the preview coming back is the body kept, and goes out as that choice. (The marker
				// winning left her old body with the new choice's marker beside it.)
				const auto choice = w.body->choice;
				w.body = std::move(a_order.body);
				w.body->choice = choice;
				if (Chosen(choice)) {
					w.body->preview = false;
				}
			}
			break;
		case OrderKind::kTouch:
			w.touch = true;
			break;
		case OrderKind::kRefit:
			w.refit = true;
			break;
		case OrderKind::kProbe:
			w.probe = true;
			break;
		case OrderKind::kSnapshot:
			break;
		}
		if (a_park) {
			w.parked = true;
			std::erase(_queue, a_order.ref);
		}
	}

	// ------------------------------------------------------------------ requests

	namespace
	{
		template <class F>
		bool WithCatalog(const std::shared_ptr<const Catalog>& a_catalog, const std::string& a_status, std::string& a_why, F&& a_fn)
		{
			if (!a_catalog) {
				a_why = a_status;
				return false;
			}
			return a_fn();
		}
	}

	bool Director::RequestPreset(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, std::string_view a_preset, Source a_source, Lane a_lane,
		std::string& a_why)
	{
		std::scoped_lock l{ _lock };
		return WithCatalog(_catalog, _status, a_why, [&] {
			const auto* p = _catalog->Find(a_preset, a_female);
			if (!p) {
				a_why = std::format("there is no preset \"{}\" for a {} body", a_preset, a_female ? "female" : "male");
				return false;
			}
			auto& session = _sessions[a_ref];
			if (!session.known) {
				session.female = a_female;
				session.base = a_base;
			}
			Retire(a_ref);
			session.reset = false;
			Intend(a_ref, session, a_source, p->name);
			QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kPreset, .preset = p->name, .choice = Chosen(a_source) ? a_source : Source::kNone },
				a_lane);
			Unpark(a_ref);
			return true;
		});
	}

	bool Director::RequestRegenerate(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, Lane a_lane, std::string& a_why)
	{
		std::scoped_lock l{ _lock };
		return WithCatalog(_catalog, _status, a_why, [&] {
			auto& session = _sessions[a_ref];
			if (!session.known) {
				session.female = a_female;
				session.base = a_base;
			}
			Retire(a_ref);
			session.reset = false;
			Redraw(a_ref, session);
			// Owed until it lands (S-59): a save before the bridge gets to it does not lose it.
			Intend(a_ref, session, Source::kRoll, {});
			QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kRegenerate }, a_lane);
			Unpark(a_ref);
			return true;
		});
	}

	// S-60: under a rule with several presets, Back to random draws from the rule again, and lands
	// somewhere new -- a draw that came back to the preset they have would look like nothing happened.
	// The count of presses is the salt the draw is mixed with, kept in the record.
	void Director::Redraw(std::uint32_t a_ref, Session& a_session)
	{
		const auto& v = a_session.verdict;
		if (!a_session.known || (v.tier != Tier::kName && v.tier != Tier::kFaction) || v.options.size() < 2) {
			return;
		}
		const auto source = v.tier == Tier::kName ? Source::kNameRule : Source::kFactionRule;
		auto&      rec = _registry.Get(a_ref);
		// "Somewhere new" is measured against the body on them: the one the player is looking at.
		auto now = PresetNamedBy(a_session.marker, a_session.stamp);
		if (now.empty()) {
			now = rec.source == source && !rec.preset.empty() ? rec.preset : v.preset;
		}
		auto        facts = a_session.facts;
		std::string drawn;
		for (int i = 0; i < 64; ++i) {
			facts.salt = facts.salt + 1 == 0 ? 1 : facts.salt + 1;
			drawn = Decide(*_catalog, facts, _settings.factionPools).preset;
			if (!IEquals(drawn, now)) {
				break;
			}
		}
		if (rec.base == 0) {
			rec.base = a_session.base;
		}
		rec.salt = facts.salt;
		a_session.facts.salt = facts.salt;
		a_session.verdict = Decide(*_catalog, a_session.facts, _settings.factionPools);
		Log(std::format("{:08X} \"{}\": the rule draws again - {} instead of {}", a_ref, a_session.facts.baseName, drawn, now));
	}

	bool Director::RequestReset(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, Lane a_lane, std::string& a_why)
	{
		std::scoped_lock l{ _lock };
		return WithCatalog(_catalog, _status, a_why, [&] {
			auto& session = _sessions[a_ref];
			if (!session.known) {
				session.female = a_female;
				session.base = a_base;
			}
			Retire(a_ref);
			Intend(a_ref, session, Source::kReset, {});
			// Owed until it lands (S-59): stamp 0 says so. And the next body is new, whatever preset it is:
			// announced, and touched up, as any other.
			auto& rec = _registry.Get(a_ref);
			rec.stamp = 0;
			rec.announced = 0;
			rec.touched = 0;
			QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kReset }, a_lane);
			Unpark(a_ref);
			return true;
		});
	}

	bool Director::RequestReapply(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, std::string_view a_markerPreset, Lane a_lane,
		std::string& a_why)
	{
		std::scoped_lock l{ _lock };
		return WithCatalog(_catalog, _status, a_why, [&] {
			if (_picker.ref == a_ref || _registry.pickings.contains(a_ref)) {
				// The body on them is a preview: given again it would become theirs.
				a_why = "they are being picked: Keep or Cancel first";
				return false;
			}
			const auto* rec = _registry.Find(a_ref);
			if (rec && (rec->source == Source::kRoll || rec->source == Source::kReset || rec->source == Source::kNameBlacklist)) {
				a_why = rec->source == Source::kRoll    ? "a new body is already on its way"
				      : rec->source == Source::kReset   ? "they were reset: a new body comes at the next load"
				                                        : "they are blacklisted by name: kept bare";
				return false;
			}
			const bool ours = rec && !rec->preset.empty();
			const auto  source = ours ? rec->source : Source::kNone;
			const auto* p = _catalog->Find(ours ? std::string_view{ rec->preset } : a_markerPreset, a_female);
			if (!ours && a_markerPreset.empty()) {
				a_why = "they have no Silhouette body to give again";
				return false;
			}
			if (!p) {
				a_why = std::format("\"{}\" is not in this build", ours ? rec->preset : std::string{ a_markerPreset });
				return false;
			}
			auto& session = _sessions[a_ref];
			if (!session.known) {
				session.female = a_female;
				session.base = a_base;
			}
			Retire(a_ref);
			// A body BodyGen gave stays BodyGen's: giving it again pins nobody's choice on it.
			if (ours) {
				Intend(a_ref, session, source, p->name);
			}
			QueueBody(a_ref,
				BodyRequest{ .what = BodyRequest::What::kPreset, .preset = p->name, .keepVariety = true, .choice = Chosen(source) ? source : Source::kNone },
				a_lane);
			Unpark(a_ref);
			return true;
		});
	}

	bool Director::RequestAdopt(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, std::string& a_why)
	{
		std::scoped_lock l{ _lock };
		return WithCatalog(_catalog, _status, a_why, [&] {
			if (const auto* rec = _registry.Find(a_ref); rec && (rec->source != Source::kNone || !rec->preset.empty())) {
				switch (rec->source) {
				case Source::kRoll:
					a_why = "a new body is already on its way";
					break;
				case Source::kReset:
					a_why = "they were reset: a new body comes at the next load";
					break;
				case Source::kNameBlacklist:
					a_why = "they are blacklisted by name: kept bare";
					break;
				default:
					a_why = std::format("their body is decided ({}: {})", SourceName(rec->source), rec->preset);
					break;
				}
				return false;
			}
			const auto s = _sessions.find(a_ref);
			if (_picker.ref == a_ref || _registry.pickings.contains(a_ref) || BodyPending(a_ref) || (s != _sessions.end() && s->second.reset)) {
				a_why = "a change to their body is already on its way";
				return false;
			}
			auto& session = _sessions[a_ref];
			if (!session.known) {
				session.female = a_female;
				session.base = a_base;
			}
			// Owed until it lands (S-59): the Adopter does not ask about them again, so a save before the
			// bridge gets to it must not lose it.
			Intend(a_ref, session, Source::kRoll, {});
			QueueBody(a_ref, BodyRequest{ .what = BodyRequest::What::kRegenerate }, Lane::kBackground);
			Unpark(a_ref);
			return true;
		});
	}

	bool Director::RequestResetEveryone(std::string& a_said)
	{
		std::scoped_lock l{ _lock };
		return WithCatalog(_catalog, _status, a_said, [&] {
			if (_picker.ref != 0) {
				a_said = "someone is being picked: Keep or Cancel first";
				return false;
			}
			if (std::ranges::any_of(_sessions, [](const auto& a_s) { return a_s.second.restoring; })) {
				a_said = "a picking is being put back: try again in a moment";
				return false;
			}
			_registry.resetStamps = { _catalog->stamp };
			_registry.resetMet.clear();
			const auto forgotten = _registry.ForgetChoices();
			std::size_t now = 0;
			for (auto& [ref, s] : _sessions) {
				if (!s.known || !s.eligible) {
					continue;
				}
				_registry.resetMet[ref] = s.base;  // decided here, whatever they wear (S-70)
				const bool ours = KindOf(s.marker) == MarkerKind::kBody && !IEquals(s.marker, kBlacklistMarker);
				s.facts.salt = 0;  // a rule draws by id alone, as for someone met the first time
				s.verdict = Decide(*_catalog, s.facts, _settings.factionPools);
				const auto& v = s.verdict;
				// Eligible: a race Silhouette distributes to (S-11), neither the player nor a dummy. A Silhouette
				// body is decided again whatever the rules now say (a blacklist's: bare); anyone else only where
				// a rule or BodyGen gives them a body now. A choice just forgotten is always carried out here:
				// someone blacklisted by name who wore another mod's choice goes bare once the roll lands.
				const auto* rec = _registry.Find(ref);
				const bool  owed = rec && rec->source == Source::kRoll;
				if (!owed &&
					(v.tier == Tier::kNameBlacklist || (!ours && !v.bodyGen && v.tier != Tier::kName && v.tier != Tier::kFaction))) {
					Log(std::format("{:08X} \"{}\": Reset everyone leaves them - {}", ref, s.facts.baseName, v.why));
					continue;  // kept bare by name (DecideBody sees to it), or not Silhouette's and nothing to give
				}
				// One line a person: who wore what at the press, and what decides them now -- the roll's own
				// line comes when it lands (FinishBody), a hold when another mod has them busy (Defer).
				Log(std::format("{:08X} \"{}\": Reset everyone - wore {}; {}", ref, s.facts.baseName, BodyNamed(s.marker, s.stamp), v.why));
				Retire(ref);
				s.reset = false;
				Intend(ref, s, Source::kRoll, {});  // owed until it lands (S-59)
				QueueBody(ref, BodyRequest{ .what = BodyRequest::What::kRegenerate }, Lane::kBackground);
				Unpark(ref);
				++now;
			}
			// A choice asked for before the press and not written yet -- still queued, or with the bridge now --
			// would land after it. Whoever is owed a roll and has such work gets the roll in its place (it waits
			// for an order in flight), seen this session or not; anyone else owed one gets it when met (S-59).
			for (const auto& [ref, rec] : _registry.All()) {
				if (rec.source != Source::kRoll) {
					continue;
				}
				const auto w = _work.find(ref);
				const bool queued = w != _work.end() && w->second.body && w->second.body->what != BodyRequest::What::kRegenerate;
				const bool flying = _busy.contains(ref) && !(w != _work.end() && w->second.body);
				if (queued || flying) {
					QueueBody(ref, BodyRequest{ .what = BodyRequest::What::kRegenerate }, Lane::kBackground);
				}
			}
			Log(std::format("Reset everyone (build {}): {} seen this session get a new body now, {} choice(s) or rule draw(s) on record forgotten; "
							"anyone met later with a body from an older build is decided again then",
				_catalog->build, now, forgotten));
			a_said = std::format("{} around you get a new body now; {} pick(s) and rule draw(s) forgotten. Everyone else is decided again when you meet them.",
				now, forgotten);
			return true;
		});
	}

	// ------------------------------------------------------------------ the bridge

	std::uint32_t Director::NextOrder()
	{
		std::scoped_lock l{ _lock };
		if (!_catalog) {
			return 0;
		}
		for (;;) {
			// The most urgent actor with work and nothing in flight; within a lane, the first to ask.
			std::ptrdiff_t best = -1;
			int            bestLane = static_cast<int>(Lane::kBackground) + 1;
			const auto     now = _clock();
			for (std::size_t i = 0; i < _queue.size();) {
				const auto ref = _queue[i];
				const auto wit = _work.find(ref);
				if (wit == _work.end() || wit->second.Empty()) {
					_work.erase(ref);
					_queue.erase(_queue.begin() + static_cast<std::ptrdiff_t>(i));
					continue;
				}
				const auto lane = static_cast<int>(wit->second.lane);
				if (!_busy.contains(ref) && lane < bestLane && wit->second.Due(now)) {
					best = static_cast<std::ptrdiff_t>(i);
					bestLane = lane;
					if (bestLane == static_cast<int>(Lane::kUrgent)) {
						break;
					}
				}
				++i;
			}
			if (best < 0) {
				return 0;
			}
			const auto ref = _queue[static_cast<std::size_t>(best)];
			auto&      w = _work[ref];
			auto&      session = _sessions[ref];

			Order o;
			o.ref = ref;
			o.female = session.female;
			o.lane = w.lane;
			// The work was chosen for having something due; that is what goes out, in this order. A body
			// or touch-up waiting out a deferral holds nothing else back (a refit coming off, a probe).
			bool made = true;
			if (w.snapshot) {
				w.snapshot = false;
				o.kind = OrderKind::kSnapshot;
				o.probe = true;
				o.readAll = true;
			} else if (w.body && w.bodyNotBefore <= now) {
				o.kind = OrderKind::kBody;
				o.body = std::move(*w.body);
				w.body.reset();
				if (o.body.what == BodyRequest::What::kRegenerate) {
					o.regenerate = true;
					o.probe = true;
				} else if (o.body.keepVariety) {
					o.readAll = true;
				}
			} else if (w.touch && !w.body && w.touchNotBefore <= now) {
				w.touch = false;
				o.kind = OrderKind::kTouch;
			} else if (w.refit) {
				w.refit = false;
				const auto want = WantRefit(session);
				if (want.set) {
					if (session.refit == static_cast<int>(RefitMarker(*want.set, want.heavy))) {
						// Already right: they changed and changed back before the bridge came. Written
						// again it would jolt the body and tell other mods of a change that did not happen.
						made = false;
					} else {
						o.kind = OrderKind::kRefit;
						o.refitOn = true;
						o.heavy = want.heavy;
						o.refitSet = want.set->name;
					}
				} else if (session.refit != 0) {  // on, unfinished, or unknown: coming off is safe to repeat
					o.kind = OrderKind::kRefit;
					o.refitOn = false;
				} else {
					made = false;
				}
			} else {
				w.probe = false;
				o.kind = OrderKind::kProbe;
				o.probe = true;
			}
			if (w.Empty()) {
				_work.erase(ref);
				_queue.erase(_queue.begin() + best);
			}
			if (!made) {
				continue;
			}
			o.id = Next(_nextOrder);
			const auto id = o.id;
			_busy.insert(ref);
			_inflight.emplace(id, std::move(o));
			return id;
		}
	}

	std::optional<Order> Director::Peek(std::uint32_t a_order) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		if (it == _inflight.end()) {
			return std::nullopt;
		}
		return it->second;
	}

	std::uint32_t Director::OrderActor(std::uint32_t a_order) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		return it == _inflight.end() ? 0 : it->second.ref;
	}

	Order* Director::Find(std::uint32_t a_order)
	{
		const auto it = _inflight.find(a_order);
		return it == _inflight.end() ? nullptr : &it->second;
	}

	void Director::NoteName(std::uint32_t a_order, std::string_view a_morph)
	{
		std::scoped_lock l{ _lock };
		if (auto* o = Find(a_order); o && o->probe && !a_morph.empty() && o->names.size() < 1024) {
			o->names.emplace_back(a_morph);
		}
	}

	void Director::NoteMarker(std::uint32_t a_order, std::string_view a_marker, float a_value)
	{
		std::scoped_lock l{ _lock };
		auto*            o = Find(a_order);
		if (!o || !(a_value > 0.0F)) {
			return;  // 0 is what "removed" looks like: the name stays listed until a load
		}
		switch (KindOf(a_marker)) {
		case MarkerKind::kRefit:
			o->refitValue = a_value;
			return;
		case MarkerKind::kChoice:
			o->choiceValue = a_value;
			return;
		case MarkerKind::kNone:
			return;
		case MarkerKind::kBody:
			break;
		}
		// A body carries one marker. Should a layer hold two, a Silhouette preset's wins over the
		// blacklist marker, and the first stays.
		if (o->marker.empty() || (IEquals(o->marker, kBlacklistMarker) && !IEquals(a_marker, kBlacklistMarker))) {
			o->marker = std::string{ a_marker };
			o->markerValue = a_value;
		}
	}

	std::int32_t Director::ReadCount(std::uint32_t a_order)
	{
		std::scoped_lock l{ _lock };
		auto*            o = Find(a_order);
		if (!o || !_catalog) {
			return 0;
		}
		if (!o->readsDecided) {
			o->readsDecided = true;
			if (o->probe && !o->readAll) {
				const auto& c = *_catalog;
				const auto  stamp = Stamp(o->markerValue);
				if (!o->marker.empty() && !IEquals(o->marker, kBlacklistMarker) && stamp != 0) {
					// A Silhouette body: her own values of what the touch-up weighs -- another mod's keyed value
					// of the same name is not hers (S-44).
					auto weighed = c.HealFor(o->marker, stamp);
					for (const auto& r : c.variety[o->female ? 1 : 0]) {
						weighed.push_back(r.morph);
					}
					if (const auto* without = Without(o->ref)) {
						weighed.insert(weighed.end(), without->begin(), without->end());  // S-87: what the touch-up takes off
					}
					for (const auto& n : o->names) {
						if (ContainsI(weighed, n) && !ContainsI(o->reads, n)) {
							o->reads.push_back(n);
						}
					}
				} else {
					// No Silhouette marker: is there a body at all? One value of her own says so (S-41).
					for (const auto& n : o->names) {
						if (KindOf(n) == MarkerKind::kNone && !c.NeverInBody(o->female, n) && !ContainsI(o->reads, n)) {
							o->reads.push_back(n);
						}
					}
					o->readsUntilBody = true;
				}
				o->readValues.assign(o->reads.size(), std::numeric_limits<float>::quiet_NaN());
			}
		}
		return static_cast<std::int32_t>(o->reads.size());
	}

	std::string Director::ReadMorph(std::uint32_t a_order, std::int32_t a_index) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		if (it == _inflight.end() || a_index < 0 || static_cast<std::size_t>(a_index) >= it->second.reads.size()) {
			return {};
		}
		return it->second.reads[static_cast<std::size_t>(a_index)];
	}

	void Director::NoteRead(std::uint32_t a_order, std::int32_t a_index, float a_value)
	{
		std::scoped_lock l{ _lock };
		if (auto* o = Find(a_order); o && a_index >= 0 && static_cast<std::size_t>(a_index) < o->readValues.size()) {
			o->readValues[static_cast<std::size_t>(a_index)] = a_value;
		}
	}

	bool Director::ReadsDone(std::uint32_t a_order) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		if (it == _inflight.end()) {
			return true;
		}
		const auto& o = it->second;
		return o.readsUntilBody && std::ranges::any_of(o.readValues, [](float v) { return !std::isnan(v) && v != 0.0F; });
	}

	void Director::NoteLayer(std::uint32_t a_order, std::string_view a_morph, float a_value)
	{
		std::scoped_lock l{ _lock };
		if (auto* o = Find(a_order); o && o->readAll && !a_morph.empty() && a_value != 0.0F) {
			const auto it = std::ranges::find_if(o->layer, [&](const auto& p) { return IEquals(p.first, a_morph); });
			if (it == o->layer.end()) {
				o->layer.emplace_back(std::string{ a_morph }, a_value);
			} else {
				it->second = a_value;
			}
		}
	}

	const std::vector<std::string>* Director::Without(std::uint32_t a_ref) const
	{
		const auto s = _sessions.find(a_ref);
		if (s == _sessions.end() || !_catalog) {
			return nullptr;
		}
		const auto* pool = _catalog->RacePoolOf(s->second.facts.race);
		return pool && !pool->without.empty() ? &pool->without : nullptr;
	}

	bool Director::Prepare(std::uint32_t a_order)
	{
		std::scoped_lock l{ _lock };
		auto*            o = Find(a_order);
		if (!o || !_catalog) {
			return false;
		}
		const bool ok = PrepareWrites(o);
		if (const auto* without = ok ? Without(o->ref) : nullptr) {
			// S-87 (owner, 2026-10-08): a race's list is never written -- a body, a preview, a refit's floors, the
			// variety. Writing 0 (taking a value off) stays.
			std::erase_if(o->writes, [&](const Write& w) { return w.value != 0.0F && ContainsI(*without, w.morph); });
		}
		return ok;
	}

	// The writes of one order. Called under the lock.
	bool Director::PrepareWrites(Order* o)
	{
		const auto& c = *_catalog;
		o->prepared = true;
		o->clearUnkeyed = false;
		o->clearRefit = false;
		o->writes.clear();
		o->update = false;
		const auto unkeyed = [&](const Morphs& a_morphs) {
			for (const auto& [m, v] : a_morphs) {
				o->writes.push_back(Write{ m, v, Layer::kUnkeyed });
			}
		};

		switch (o->kind) {
		case OrderKind::kProbe:
		case OrderKind::kSnapshot:
			return true;
		case OrderKind::kBody:
			switch (o->body.what) {
			case BodyRequest::What::kPreset:
				{
					const auto* p = c.Find(o->body.preset, o->female);
					if (!p) {
						Log(std::format("{:08X}: no preset \"{}\" for a {} body in this build", o->ref, o->body.preset, o->female ? "female" : "male"));
						return false;
					}
					std::unordered_map<std::string, float> keep;
					for (const auto& [m, v] : o->layer) {
						keep.emplace(m, v);
					}
					for (const auto& [m, v] : o->body.keepFrom) {
						keep.emplace(m, v);
					}
					const bool keeping = o->body.keepVariety || !o->body.keepFrom.empty();
					auto       body = BodyFor(c, *p, o->ref, _settings.variety, keeping ? &keep : nullptr);
					if (const auto* rec = _registry.Find(o->ref);
						!o->body.preview && !Chosen(o->body.choice) && rec && Chosen(rec->source) && IEquals(rec->preset, p->name)) {
						// A choice recorded since the request was made (a probe rebuilt it meanwhile, S-51): the
						// body of that choice goes out with its marker.
						o->body.choice = rec->source;
					}
					if (o->body.keepVariety && !Chosen(o->body.choice)) {
						// A choice marker on a body nothing is recorded for: the co-save lost the choice (a
						// save made without the plugin), LooksMenu kept it (S-51). Given again, it stays --
						// unless the id came to someone new with it (S-57).
						const auto* rec = _registry.Find(o->ref);
						const auto  s = _sessions.find(o->ref);
						if (!(rec && rec->source != Source::kNone) && !(s != _sessions.end() && s->second.stranger)) {
							for (const auto& [m, v] : o->layer) {
								if (KindOf(m) == MarkerKind::kChoice && Chosen(ChoiceOf(v))) {
									o->body.choice = ChoiceOf(v);
								}
							}
						}
					}
					if (Chosen(o->body.choice)) {
						// Beside the body, before its marker: the marker last is what tells a whole body.
						body.insert(body.end() - 1, { std::string{ kChoiceMarker }, static_cast<float>(o->body.choice) });
					}
					// The marker also goes FIRST, as "pending" (S-58): a save that cuts the body short leaves
					// it saying which preset was being written, and the next probe gives that preset again.
					body.insert(body.begin(), { p->marker, kBodyPending });
					o->clearUnkeyed = true;
					unkeyed(body);
					o->update = true;
					return true;
				}
			case BodyRequest::What::kBlacklist:
				o->clearUnkeyed = true;
				o->clearRefit = true;
				o->writes.push_back(Write{ std::string{ kBlacklistMarker }, static_cast<float>(c.stamp), Layer::kUnkeyed });
				o->update = true;
				return true;
			case BodyRequest::What::kRestore:
				o->clearUnkeyed = true;
				for (const auto& [m, v] : o->body.restore) {
					if (std::abs(v) >= 1e-6F) {
						o->writes.push_back(Write{ m, v, Layer::kUnkeyed });
					}
				}
				o->update = true;
				return true;
			case BodyRequest::What::kRegenerate:
				o->update = true;  // after the bridge put the keyed morphs back
				return true;
			case BodyRequest::What::kReset:
				o->clearUnkeyed = true;
				o->clearRefit = true;
				o->update = true;
				return true;
			case BodyRequest::What::kMark:
				// The choice's marker written beside the body -- or, for nobody's choice, erased (SetMorph 0).
				o->writes.push_back(Write{ std::string{ kChoiceMarker }, Chosen(o->body.choice) ? static_cast<float>(o->body.choice) : 0.0F, Layer::kUnkeyed });
				return true;  // a marker moves nothing: no update
			}
			return false;
		case OrderKind::kTouch:
			{
				const auto& s = _sessions[o->ref];
				o->touchKey = TouchKey(c, s.marker, s.stamp, s.female, _settings.variety, Without(o->ref));
				for (const auto& m : c.HealFor(s.marker, s.stamp)) {
					if (ContainsI(s.own, m)) {
						o->writes.push_back(Write{ m, 0.0F, Layer::kUnkeyed });  // SetMorph(0) erases her own value only
					}
				}
				if (const auto* without = Without(o->ref)) {
					for (const auto& m : s.own) {
						if (ContainsI(*without, m)) {
							o->writes.push_back(Write{ m, 0.0F, Layer::kUnkeyed });  // S-87: what 0.3.5 wrote there
						}
					}
				}
				unkeyed(TopUp(c, s.female, o->ref, _settings.variety, s.own));
				o->update = !o->writes.empty();
				return true;
			}
		case OrderKind::kRefit:
			o->clearRefit = true;
			o->update = true;
			if (o->refitOn) {
				const auto* set = c.FindRefit(o->refitSet, o->female);
				if (!set) {
					return false;
				}
				// The marker goes first as "pending" and last with its value: a refit a save cut short
				// reads back as unfinished, and the next probe finishes it or takes it off (S-43).
				o->writes.push_back(Write{ std::string{ kRefitMarker }, kRefitPending, Layer::kRefit });
				for (auto& [m, v] : RefitFloors(*set, o->heavy)) {
					o->writes.push_back(Write{ std::move(m), v, Layer::kRefit });
				}
				o->writes.push_back(Write{ std::string{ kRefitMarker }, RefitMarker(*set, o->heavy), Layer::kRefit });
			}
			return true;
		}
		return false;
	}

	bool Director::ClearsUnkeyed(std::uint32_t a_order) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		return it != _inflight.end() && it->second.prepared && it->second.clearUnkeyed;
	}

	bool Director::ClearsRefit(std::uint32_t a_order) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		return it != _inflight.end() && it->second.prepared && it->second.clearRefit;
	}

	bool Director::Updates(std::uint32_t a_order) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		return it != _inflight.end() && it->second.prepared && it->second.update;
	}

	std::int32_t Director::WriteCount(std::uint32_t a_order) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		return it == _inflight.end() || !it->second.prepared ? 0 : static_cast<std::int32_t>(it->second.writes.size());
	}

	std::string Director::WriteMorph(std::uint32_t a_order, std::int32_t a_index) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		if (it == _inflight.end() || a_index < 0 || static_cast<std::size_t>(a_index) >= it->second.writes.size()) {
			return {};
		}
		return it->second.writes[static_cast<std::size_t>(a_index)].morph;
	}

	float Director::WriteValue(std::uint32_t a_order, std::int32_t a_index) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		if (it == _inflight.end() || a_index < 0 || static_cast<std::size_t>(a_index) >= it->second.writes.size()) {
			return 0.0F;
		}
		return it->second.writes[static_cast<std::size_t>(a_index)].value;
	}

	Layer Director::WriteLayer(std::uint32_t a_order, std::int32_t a_index) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		if (it == _inflight.end() || a_index < 0 || static_cast<std::size_t>(a_index) >= it->second.writes.size()) {
			return Layer::kUnkeyed;
		}
		return it->second.writes[static_cast<std::size_t>(a_index)].layer;
	}

	void Director::Done(std::uint32_t a_order, bool a_ok)
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		if (it == _inflight.end()) {
			return;
		}
		Order o = std::move(it->second);
		_inflight.erase(it);
		_busy.erase(o.ref);
		if (!_catalog) {
			return;
		}
		if (!a_ok) {
			++_counts.failed;
			Log(std::format("{:08X}: order {} (kind {}) not completed by the bridge", o.ref, o.id, static_cast<int>(o.kind)));
			if (o.kind == OrderKind::kSnapshot && _picker.ref == o.ref) {
				ClosePicker();  // without the snapshot a Cancel could not put them back
				Resettle(o.ref);
			}
			if (o.kind == OrderKind::kBody && o.body.what == BodyRequest::What::kRestore) {
				if (auto s = _sessions.find(o.ref); s != _sessions.end()) {
					s->second.restoring = false;  // the picking stays: the next sighting tries again
				}
			}
			return;
		}
		auto& session = _sessions[o.ref];
		switch (o.kind) {
		case OrderKind::kProbe:
			++_counts.probes;
			OnProbed(o.ref, o);
			AfterProbe(o.ref, session);
			break;
		case OrderKind::kSnapshot:
			{
				++_counts.snapshots;
				const bool first = !session.probed;
				OnProbed(o.ref, o);
				if (first) {
					// Picked before anything probed them: this is the session's probe, and what a probe
					// settles is settled now -- or at their first sighting, if they were not seen yet.
					AfterProbe(o.ref, session);
				}
				if (_picker.ref == o.ref && !_picker.snapped) {
					_picker.snapped = true;
					_picker.current = PresetNamedBy(session.marker, session.stamp);
					_picker.index = -1;
					for (std::size_t i = 0; i < _picker.presets.size(); ++i) {
						if (IEquals(_picker.presets[i], _picker.current)) {
							_picker.index = static_cast<std::int32_t>(i);
						}
					}
					PickerSave save;
					save.ref = o.ref;
					save.base = _picker.base;
					save.female = _picker.female;
					save.snapshot = o.layer;
					if (const auto* rec = _registry.Find(o.ref)) {
						save.before = *rec;
					}
					_registry.Keep(std::move(save));
				}
				ReconcileRefit(o.ref);
				break;
			}
		case OrderKind::kBody:
			++_counts.bodies;
			FinishBody(o);
			break;
		case OrderKind::kTouch:
			{
				++_counts.touches;
				auto& rec = _registry.Get(o.ref);
				if (rec.base == 0) {
					rec.base = session.base;
				}
				rec.touched = o.touchKey;
				if (!o.writes.empty()) {
					Log(std::format("{:08X}: {} slider(s) healed or topped up on {}", o.ref, o.writes.size(), PresetNamedBy(session.marker, session.stamp)));
				}
				if (session.deferNoted) {
					session.deferNoted = false;
					Log(std::format("{:08X}: the change that waited for another mod is done", o.ref));
				}
				break;
			}
		case OrderKind::kRefit:
			++_counts.refits;
			FinishRefit(o);
			break;
		}
	}

	void Director::Gone(std::uint32_t a_order)
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		if (it == _inflight.end()) {
			return;
		}
		Order o = std::move(it->second);
		_inflight.erase(it);
		_busy.erase(o.ref);
		if (!_catalog) {
			return;
		}
		++_counts.gone;
		if (o.kind == OrderKind::kSnapshot) {
			if (_picker.ref == o.ref) {
				ClosePicker();
				Resettle(o.ref);  // like every other end of a picking (the bridge's failed snapshot comes as this)
			}
			return;
		}
		if (o.kind == OrderKind::kBody && o.body.what == BodyRequest::What::kRestore) {
			if (auto s = _sessions.find(o.ref); s != _sessions.end()) {
				s->second.restoring = false;  // the picking stays: the next sighting puts them back
			}
			return;
		}
		Requeue(o, true);  // waits for them to be seen again
	}

	void Director::Defer(std::uint32_t a_order)
	{
		std::scoped_lock l{ _lock };
		const auto       it = _inflight.find(a_order);
		if (it == _inflight.end()) {
			return;
		}
		Order o = std::move(it->second);
		_inflight.erase(it);
		_busy.erase(o.ref);
		if (!_catalog) {
			return;
		}
		++_counts.deferred;
		auto& s = _sessions[o.ref];
		if (!s.deferNoted) {
			s.deferNoted = true;
			Log(std::format("{:08X} \"{}\": another mod has them busy (AAF's busy or locked keyword) - the change waits", o.ref, s.facts.baseName));
		}
		// S-71: a change the player asked for, held back where they cannot see why. Bulk work (a Reset
		// everyone's rolls, the regeneration window), a touch-up and another mod's request are not the player's
		// to wait for, and are not said -- whatever lane the actor's work drifted to beside them.
		if (o.kind == OrderKind::kBody && o.body.asked && !s.deferTold) {
			s.deferTold = true;
			Notice(o.ref, s, "busy in another mod's scene - the change you asked for waits until it ends");
		}
		// Only this kind of work waits: a refit coming off while she undresses in the scene must not.
		const auto until = _clock() + kDeferWait;
		auto&      w = WorkFor(o.ref, Lane::kBackground);
		switch (o.kind) {
		case OrderKind::kBody:
			if (!w.body) {  // a newer decision already waiting is tried at once, not after this one's wait
				w.body = std::move(o.body);
				w.bodyNotBefore = until;
			}
			break;
		case OrderKind::kTouch:
			w.touch = true;
			w.touchNotBefore = until;
			break;
		default:
			o.lane = Lane::kBackground;
			Requeue(o, false);
			break;
		}
	}

	std::size_t Director::Pending() const
	{
		std::scoped_lock l{ _lock };
		const auto       now = _clock();
		std::size_t      due = 0;
		for (const auto ref : _queue) {
			if (const auto w = _work.find(ref); w != _work.end() && w->second.Due(now)) {
				++due;
			}
		}
		return due + _inflight.size();
	}

	void Director::SetClock(Clock a_clock)
	{
		std::scoped_lock l{ _lock };
		_clock = std::move(a_clock);
	}

	void Director::FinishBody(Order& a_order)
	{
		const auto& c = *_catalog;
		const auto  ref = a_order.ref;
		auto&       s = _sessions[ref];
		s.probed = s.probed || a_order.probe;
		s.pendingBody = false;
		if (s.deferTold && a_order.body.what != BodyRequest::What::kMark) {
			s.deferTold = false;
			if (a_order.body.asked) {
				Notice(ref, s, "the change you asked for is done");
			}  // else another mod's change took its place before it landed: that one is not the player's
		}
		if (s.deferNoted && a_order.body.what != BodyRequest::What::kMark) {
			s.deferNoted = false;
			Log(std::format("{:08X}: the change that waited for another mod is done", ref));
		}
		switch (a_order.body.what) {
		case BodyRequest::What::kPreset:
			{
				const auto* p = c.Find(a_order.body.preset, a_order.female);
				s.marker = p ? p->marker : std::string{};
				s.stamp = c.stamp;
				s.hasBody = true;
				s.reset = false;
				s.names.clear();
				if (p) {
					for (const auto& [m, v] : p->values) {
						s.names.push_back(m);
					}
					for (const auto& r : c.variety[a_order.female ? 1 : 0]) {
						s.names.push_back(r.morph);
					}
				}
				s.own = s.names;  // all of it written into her own layer
				s.choice = a_order.body.choice;
				if (Chosen(s.choice)) {
					s.names.emplace_back(kChoiceMarker);
				}
				if (p && (!a_order.body.preview || s.announceOnDone)) {
					// Every body given on request is announced, the same preset again included (S-46).
					Push(EventKind::kGenerated, ref, p->name, false, BodyHash(p->marker, c.stamp));
					s.announceOnDone = false;
				}
				if (p && !a_order.body.preview) {
					auto& rec = _registry.Get(ref);
					if (rec.base == 0) {
						rec.base = s.base;
					}
					rec.touched = TouchKey(c, p->marker, c.stamp, a_order.female, _settings.variety, Without(ref));
					if (Chosen(s.choice) && rec.source == Source::kNone && rec.preset.empty()) {
						// A choice the co-save had lost came back with the body (Prepare): recorded again.
						Intend(ref, s, s.choice, p->name);
						Log(std::format("{:08X}: {} ({}) rebuilt from the choice LooksMenu keeps beside the body", ref, p->name, SourceName(s.choice)));
					}
				}
				break;
			}
		case BodyRequest::What::kBlacklist:
			s.marker = std::string{ kBlacklistMarker };
			s.stamp = c.stamp;
			s.hasBody = false;
			s.refit = 0;
			s.choice = Source::kNone;
			s.own.clear();
			break;
		case BodyRequest::What::kRestore:
			{
				s.marker.clear();
				s.stamp = 0;
				s.hasBody = false;
				s.choice = Source::kNone;
				s.names.clear();
				s.own.clear();
				for (const auto& [m, v] : a_order.body.restore) {
					s.names.push_back(m);
					switch (KindOf(m)) {
					case MarkerKind::kBody:
						if (v > 0.0F) {
							s.marker = m;
							s.stamp = Stamp(v);
							// The body put back was itself half written (S-58): it still says so.
							s.pendingBody = v < 0.9F && !IEquals(m, kBlacklistMarker);
						}
						break;
					case MarkerKind::kChoice:
						s.choice = ChoiceOf(v);
						break;
					case MarkerKind::kNone:
						if (v != 0.0F) {
							s.own.push_back(m);
						}
						break;
					case MarkerKind::kRefit:
						break;
					}
				}
				s.hasBody = (!s.marker.empty() && !IEquals(s.marker, kBlacklistMarker) && s.stamp != 0) || !s.own.empty();
				if (const auto w = _work.find(ref); w != _work.end() && w->second.body && w->second.body->what == BodyRequest::What::kRestore) {
					break;  // picked again and ended again meanwhile: the restore still queued ends the picking, not this one
				}
				s.restoring = false;
				if (_picker.ref != ref) {
					_registry.pickings.erase(ref);  // the picking is over
					Resettle(ref);                  // and what it held back is done now
				}
				break;
			}
		case BodyRequest::What::kRegenerate:
			{
				OnProbed(ref, a_order);
				s.reset = false;
				// The roll owed has landed (S-59) -- unless another is still queued behind it. A reset asked
				// while this roll was in flight has not landed (its stamp is 0) and stays owed: cleared here, it
				// would leave her bare with nothing remembered.
				if (const auto* rec = _registry.Find(ref);
					rec && ((rec->source == Source::kRoll && !BodyPending(ref)) || (rec->source == Source::kReset && rec->stamp != 0))) {
					Intend(ref, s, Source::kNone, {});
				}
				// Generated as if new: the rules get their say first -- a body they replace at once is not
				// the one to announce; its replacement is, when it lands.
				DecideBody(ref, s);
				Log(std::format("{:08X} \"{}\": rolled again - BodyGen gave {}{}", ref, s.facts.baseName, BodyNamed(s.marker, s.stamp),
					BodyPending(ref) ? "; a rule's body follows" : ""));
				if (const auto preset = PresetNamedBy(s.marker, s.stamp); !preset.empty() && !BodyPending(ref)) {
					// Announced like every body given on request (S-46), whether or not this session has seen them yet.
					Push(EventKind::kGenerated, ref, preset, false, BodyHash(s.marker, s.stamp));
					auto& rec = _registry.Get(ref);
					if (rec.base == 0) {
						rec.base = s.base;
					}
					rec.touched = TouchKey(c, s.marker, s.stamp, a_order.female, _settings.variety, Without(ref));
				}
				break;
			}
		case BodyRequest::What::kReset:
			s.marker.clear();
			s.stamp = 0;
			s.hasBody = false;
			s.refit = 0;
			s.reset = true;
			s.choice = Source::kNone;
			s.names.clear();
			s.own.clear();
			if (auto* rec = _registry.Find(ref); rec && rec->source == Source::kReset) {
				rec->stamp = c.stamp;  // landed: what the next load does with them is S-53's (S-59)
			}
			break;
		case BodyRequest::What::kMark:
			s.choice = a_order.body.choice;
			break;
		}
		ReconcileRefit(ref);
	}

	void Director::FinishRefit(Order& a_order)
	{
		auto& s = _sessions[a_order.ref];
		if (a_order.refitOn) {
			const auto* set = _catalog->FindRefit(a_order.refitSet, a_order.female);
			s.refit = set ? static_cast<int>(RefitMarker(*set, a_order.heavy)) : 1;
			Log(std::format("{:08X} \"{}\": refit on ({}{})", a_order.ref, s.facts.baseName, a_order.refitSet, a_order.heavy ? ", heavy" : ""));
		} else {
			s.refit = 0;
			Log(std::format("{:08X} \"{}\": refit off", a_order.ref, s.facts.baseName));
		}
		Push(EventKind::kORefitChanged, a_order.ref, {}, a_order.refitOn);
		ReconcileRefit(a_order.ref);  // they may have dressed or undressed while it ran
	}

	// ------------------------------------------------------------------ events

	void Director::Push(EventKind a_kind, std::uint32_t a_ref, std::string a_preset, bool a_flag, std::uint32_t a_announce)
	{
		const auto same = [&](const Event& e) { return e.kind == a_kind && e.ref == a_ref && e.announce == a_announce; };
		if (a_kind == EventKind::kGenerated && a_announce != 0 &&
			(std::ranges::any_of(_events, same) || std::ranges::any_of(_taken, [&](const Event& e) { return !e.done && same(e); }))) {
			return;  // already on its way: waiting, or handed to the bridge and not raised yet
		}
		if (_events.size() >= kMaxEvents) {
			_events.pop_front();
			if (!_eventsDropped) {
				Log("events: the bridge is not taking them; the oldest are dropped");
				_eventsDropped = true;
			}
		}
		_events.push_back(Event{ .id = Next(_nextEvent), .kind = a_kind, .ref = a_ref, .preset = std::move(a_preset), .flag = a_flag, .announce = a_announce });
	}

	std::uint32_t Director::NextEvent()
	{
		std::scoped_lock l{ _lock };
		// The bridge raises strictly in order and says EventDone before it asks for the next: one handed out
		// and not done by now was skipped (its actor was not in memory). It is settled -- not remembered as
		// announced, and no longer standing in the way of the same body announced again. This holds because ONE
		// loop raises (Bridge.RaiseEvents, on the bridge's one timer): a second raising at the same time would
		// have its event settled here while still being raised, and the same body could be announced twice.
		for (auto& e : _taken) {
			e.done = true;
		}
		if (_events.empty()) {
			return 0;
		}
		_eventsDropped = false;
		_taken.push_back(std::move(_events.front()));
		_events.pop_front();
		if (_taken.size() > 64) {
			_taken.pop_front();
		}
		return _taken.back().id;
	}

	std::optional<Event> Director::EventAt(std::uint32_t a_event) const
	{
		std::scoped_lock l{ _lock };
		for (const auto& e : _taken) {
			if (e.id == a_event) {
				return e;
			}
		}
		return std::nullopt;
	}

	// Announced once the bridge has raised it: a save between hand-out and raise announces it again.
	void Director::EventDone(std::uint32_t a_event)
	{
		std::scoped_lock l{ _lock };
		for (auto& e : _taken) {
			if (e.id != a_event) {
				continue;
			}
			e.done = true;
			if (e.kind != EventKind::kGenerated || e.announce == 0) {
				return;
			}
			auto& rec = _registry.Get(e.ref);
			if (rec.base == 0) {
				if (const auto s = _sessions.find(e.ref); s != _sessions.end()) {
					rec.base = s->second.base;
				}
			}
			rec.announced = e.announce;
			return;
		}
	}

	// ------------------------------------------------------------------ the picker

	void Director::ClosePicker()
	{
		_picker = {};
	}

	std::string Director::CancelPicking(std::string_view a_message)
	{
		const auto name = _picker.name;
		const auto ref = _picker.ref;
		const auto entry = _registry.pickings.find(ref);
		if (!_picker.snapped || !_picker.tried || entry == _registry.pickings.end()) {
			// Nothing was tried on: nothing to undo.
			if (const auto w = _work.find(ref); w != _work.end()) {
				w->second.snapshot = false;
			}
			ClosePicker();
			if (entry != _registry.pickings.end()) {
				_registry.pickings.erase(entry);
			}
			Resettle(ref);  // what the picking held back is done now
			return a_message.empty() ? std::format("{} keeps the body they had.", name) : std::string{ a_message };
		}
		// The choice behind the body goes back at once; the body follows with the restore, and the saved
		// picking stays until that restore is done (S-47).
		PutBackChoice(ref, entry->second.before, _sessions[ref].base);
		_sessions[ref].restoring = true;
		QueueBody(ref, RestoreOf(entry->second.snapshot, entry->second.female), Lane::kUrgent);
		const auto back = _picker.current.empty() ? std::string{ "the body they had" } : _picker.current;
		ClosePicker();
		return a_message.empty() ? std::format("{} is back to {}.", name, back) : std::string{ a_message };
	}

	std::string Director::PickerStart(std::uint32_t a_ref, bool a_female, std::uint32_t a_base, std::string_view a_name)
	{
		std::scoped_lock l{ _lock };
		if (!_catalog) {
			return std::format("Silhouette is not ready: {}", _status);
		}
		const auto name = a_name.empty() ? std::format("{:08X}", a_ref) : std::string{ a_name };
		if (a_base == 0) {
			// A picking keeps the NPC record it was made for: a created id given to somebody else is told by it
			// (S-57), and one kept with none would put the previous owner's choice and body on the newcomer.
			return std::format("{} has no NPC record: there is nobody to pick.", name);
		}
		if (_picker.ref == a_ref) {
			return std::format("{} is already picked: Next / Previous try presets, Keep or Cancel ends it.", _picker.name);
		}
		const auto pending = _registry.pickings.find(a_ref);
		if (pending == _registry.pickings.end() && BodyPending(a_ref)) {
			return std::format("{}'s body is still changing - pick them again in a moment.", name);
		}
		std::string before;
		if (_picker.ref != 0) {
			before = CancelPicking() + " ";
		}
		std::vector<std::string> presets;
		for (const auto* p : _catalog->MenuPresets(a_female)) {
			presets.push_back(p->name);
		}
		if (presets.empty()) {
			return before + std::format("There are no presets for a {} body in this build.", a_female ? "female" : "male");
		}
		auto& session = _sessions[a_ref];
		if (!session.known) {
			session.female = a_female;
			session.base = a_base;
		}
		_picker = {};
		_picker.ref = a_ref;
		_picker.female = a_female;
		_picker.base = a_base;
		_picker.name = name;
		_picker.presets = std::move(presets);
		const auto again = _registry.pickings.find(a_ref);
		if (again != _registry.pickings.end()) {
			// A picking a save or a Cancel left unfinished: it carries on from the body they had then, whatever
			// is on them now (S-47). A queued restore of it is not needed any more.
			if (const auto w = _work.find(a_ref); w != _work.end() && w->second.body && w->second.body->what == BodyRequest::What::kRestore) {
				w->second.body.reset();
			}
			session.restoring = false;
			_picker.snapped = true;
			_picker.tried = true;
			for (const auto& [m, v] : again->second.snapshot) {
				if (KindOf(m) == MarkerKind::kBody && v > 0.0F) {
					_picker.current = PresetNamedBy(m, Stamp(v));
				}
			}
			for (std::size_t i = 0; i < _picker.presets.size(); ++i) {
				if (IEquals(_picker.presets[i], _picker.current)) {
					_picker.index = static_cast<std::int32_t>(i);
				}
			}
			return before + std::format("{} picked again: Next / Previous try their {} presets; Keep or Cancel ends it.", _picker.name, _picker.presets.size());
		}
		WorkFor(a_ref, Lane::kUrgent).snapshot = true;
		return before + std::format("{} picked. Next / Previous try their {} presets; Keep or Cancel ends it.", _picker.name, _picker.presets.size());
	}

	std::string Director::PickerStep(std::int32_t a_step)
	{
		std::scoped_lock l{ _lock };
		if (_picker.ref == 0) {
			return "Pick an NPC first: aim at them and press Pick.";
		}
		const auto entry = _registry.pickings.find(_picker.ref);
		if (!_picker.snapped || entry == _registry.pickings.end()) {
			return std::format("Still reading {}'s body - a moment.", _picker.name);
		}
		const auto n = static_cast<std::int32_t>(_picker.presets.size());
		if (a_step == 0) {
			a_step = 1;
		}
		if (_picker.index < 0) {
			// Nothing tried on and their body is none of these: Next is the first preset, Previous the last.
			_picker.index = a_step > 0 ? (a_step - 1) % n : ((n + a_step % n) % n);
		} else {
			_picker.index = ((_picker.index + a_step) % n + n) % n;
		}
		return TryOn(entry->second.snapshot);
	}

	// S-79: the picker window names the preset instead of stepping to it.
	std::string Director::PickerShow(std::string_view a_preset)
	{
		std::scoped_lock l{ _lock };
		if (_picker.ref == 0) {
			return "Pick an NPC first: aim at them and press Pick.";
		}
		const auto entry = _registry.pickings.find(_picker.ref);
		if (!_picker.snapped || entry == _registry.pickings.end()) {
			return std::format("Still reading {}'s body - a moment.", _picker.name);
		}
		const auto it = std::ranges::find_if(_picker.presets, [&](const std::string& a_p) { return IEquals(a_p, a_preset); });
		if (it == _picker.presets.end()) {
			return std::format("{} is not a preset for {}.", a_preset, _picker.name);
		}
		_picker.index = static_cast<std::int32_t>(it - _picker.presets.begin());
		return TryOn(entry->second.snapshot);
	}

	// The picker's preset at its index goes on them as a preview. Called under the lock.
	std::string Director::TryOn(const Morphs& a_snapshot)
	{
		const auto& preset = _picker.presets[static_cast<std::size_t>(_picker.index)];
		_picker.tried = true;
		// Their own variety comes along: previews differ only in the preset (S-21).
		QueueBody(_picker.ref, BodyRequest{ .what = BodyRequest::What::kPreset, .preset = preset, .preview = true, .keepFrom = a_snapshot },
			Lane::kUrgent);
		return std::format("{}: {} ({}/{})", _picker.name, preset, _picker.index + 1, _picker.presets.size());
	}

	// S-79: the picker's presets for the window, "name<TAB>kind" joined by "|" -- kind y for the player's own
	// (S-76), p for the random pool, o for the rest (named people, factions, CBBE's and BodyTalk's stock).
	std::string Director::PickerPresets() const
	{
		std::scoped_lock l{ _lock };
		std::string out;
		for (const auto& name : _picker.presets) {
			const auto* p = _catalog ? _catalog->Find(name, _picker.female) : nullptr;
			const char  kind = p && p->installed ? 'y' : p && p->random ? 'p' : 'o';
			if (!out.empty()) {
				out += '|';
			}
			out += name;
			out += '	';
			out += kind;
		}
		return out;
	}

	std::int32_t Director::PickerIndex() const
	{
		std::scoped_lock l{ _lock };
		return _picker.ref != 0 ? _picker.index : -1;
	}

	bool Director::PickerFemale() const
	{
		std::scoped_lock l{ _lock };
		return _picker.female;
	}

	std::string Director::PickerCurrent() const
	{
		std::scoped_lock l{ _lock };
		return _picker.ref != 0 ? _picker.current : std::string{};
	}

	std::string Director::PickerKeep()
	{
		std::scoped_lock l{ _lock };
		if (_picker.ref == 0) {
			return "Nobody is picked.";
		}
		const auto name = _picker.name;
		const auto ref = _picker.ref;
		if (!_picker.snapped) {
			return CancelPicking();
		}
		const bool same = _picker.index < 0 || IEquals(_picker.presets[static_cast<std::size_t>(_picker.index)], _picker.current);
		if (same) {
			// Back on their own preset: they keep the body they had, exactly -- not a preview of it.
			return CancelPicking(std::format("{} keeps the body they had.", name));
		}
		const auto preset = _picker.presets[static_cast<std::size_t>(_picker.index)];
		auto&      s = _sessions[ref];
		Intend(ref, s, Source::kPicker, preset);
		const auto w = _work.find(ref);
		if (w != _work.end() && w->second.body && w->second.body->what == BodyRequest::What::kPreset && w->second.body->preview) {
			// The preview is not written yet: it is written as the choice, its marker beside it, and announced.
			w->second.body->preview = false;
			w->second.body->choice = Source::kPicker;
		} else {
			const bool writing = std::ranges::any_of(_inflight, [&](const auto& p) { return p.second.ref == ref && p.second.kind == OrderKind::kBody; });
			if (writing) {
				s.announceOnDone = true;  // announced when the preview lands
			} else if (const auto* p = _catalog->Find(preset, _picker.female)) {
				Push(EventKind::kGenerated, ref, p->name, false, BodyHash(p->marker, _catalog->stamp));
			}
			s.marked = true;
			QueueBody(ref, BodyRequest{ .what = BodyRequest::What::kMark, .choice = Source::kPicker }, Lane::kNormal);
		}
		_registry.pickings.erase(ref);
		ClosePicker();
		Resettle(ref);  // what the picking held back (a heal, a first announcement) is done now
		return std::format("{} keeps {}.", name, preset);
	}

	std::string Director::PickerCancel()
	{
		std::scoped_lock l{ _lock };
		if (_picker.ref == 0) {
			return "Nobody is picked.";
		}
		return CancelPicking();
	}

	std::uint32_t Director::PickerTarget() const
	{
		std::scoped_lock l{ _lock };
		return _picker.ref;
	}

	bool Director::PickerReady() const
	{
		std::scoped_lock l{ _lock };
		return _picker.ref != 0 && _picker.snapped;
	}

	// ------------------------------------------------------------------ queries

	std::string Director::AssignedPreset(std::uint32_t a_ref) const
	{
		std::scoped_lock l{ _lock };
		const auto*      rec = _registry.Find(a_ref);
		return rec && rec->source != Source::kNameBlacklist && rec->source != Source::kReset ? rec->preset : std::string{};
	}

	bool Director::RefitApplied(std::uint32_t a_ref) const
	{
		std::scoped_lock l{ _lock };
		const auto       it = _sessions.find(a_ref);
		return it != _sessions.end() && it->second.refit > 0;
	}

	std::string Director::Describe(std::uint32_t a_ref) const
	{
		std::scoped_lock l{ _lock };
		const auto*      rec = _registry.Find(a_ref);
		std::string      out;
		// What was asked for stays owed while Silhouette does not shape them -- their race left the build, say
		// (S-59) -- and is carried out once it shapes them again: not "on its way" until then.
		const auto session = _sessions.find(a_ref);
		const bool shaped = session == _sessions.end() || !session->second.known || session->second.eligible;
		if (rec && rec->source == Source::kNameBlacklist) {
			out = "blacklisted by name: kept bare";
		} else if (rec && rec->source == Source::kReset) {
			out = !shaped             ? "reset, owed: carried out once Silhouette shapes them again"
			      : rec->stamp == 0 ? "reset: bare in a moment, a new body at the next load"
			                        : "reset: a new body at the next load";
		} else if (rec && rec->source == Source::kRoll) {
			out = shaped ? "a new body is on its way" : "a new body, owed: rolled once Silhouette shapes them again";
		} else if (rec && !rec->preset.empty()) {
			out = std::format("chosen: {} ({})", rec->preset, SourceName(rec->source));
		} else {
			out = "nobody chose their body: it is BodyGen's";
		}
		if (const auto it = _sessions.find(a_ref); it != _sessions.end()) {
			const auto& s = it->second;
			if (s.refit > 0 && s.refit % 2 == 0) {
				out += s.heavyBy.empty() ? "; dressed heavily, refit on" : std::format("; dressed heavily ({}), refit on", s.heavyBy);
			} else if (s.refit > 0) {
				out += "; dressed, refit on";
			} else if (s.refit == kRefitUnfinished) {
				out += "; a refit half written";
			} else if (s.refit == 0 && s.clothed) {
				out += "; dressed, not refit";
			}
		}
		return out;
	}

	// ------------------------------------------------------------------ the co-save

	std::vector<std::byte> Director::SaveRecords(const Registry::KeepFn& a_keep) const
	{
		std::scoped_lock l{ _lock };
		return _registry.Serialize(a_keep);
	}

	Registry::Loaded Director::LoadRecords(std::span<const std::byte> a_bytes, std::uint32_t a_version,
		const std::function<std::uint32_t(std::uint32_t)>& a_resolve, std::string& a_error)
	{
		std::scoped_lock l{ _lock };
		return _registry.Deserialize(a_bytes, a_version, a_resolve, a_error);
	}

	std::size_t Director::RecordCount() const
	{
		std::scoped_lock l{ _lock };
		return _registry.Size();
	}

	std::vector<std::byte> Director::SaveReset() const
	{
		std::scoped_lock l{ _lock };
		return _registry.SerializeReset();
	}

	Registry::Loaded Director::LoadReset(std::span<const std::byte> a_bytes, std::uint32_t a_version, std::string& a_error,
		const std::function<std::uint32_t(std::uint32_t)>& a_resolve)
	{
		std::scoped_lock l{ _lock };
		return _registry.DeserializeReset(a_bytes, a_version, a_error, a_resolve);
	}

	std::optional<Record> Director::RecordOf(std::uint32_t a_ref) const
	{
		std::scoped_lock l{ _lock };
		const auto*      rec = _registry.Find(a_ref);
		return rec ? std::optional<Record>{ *rec } : std::nullopt;
	}

	bool Director::HasPicking(std::uint32_t a_ref) const
	{
		std::scoped_lock l{ _lock };
		return _registry.pickings.contains(a_ref);
	}

	void Director::Log(std::string a_line)
	{
		if (_log.size() >= kMaxLog) {
			_log.erase(_log.begin());
		}
		_log.push_back(std::move(a_line));
	}

	std::vector<std::string> Director::TakeLog()
	{
		std::scoped_lock l{ _lock };
		return std::exchange(_log, {});
	}

	std::string Director::TakeSummary()
	{
		std::scoped_lock l{ _lock };
		const auto       now = _clock();
		std::size_t      lanes[3]{};
		std::size_t      held = 0;
		for (const auto ref : _queue) {
			if (const auto w = _work.find(ref); w != _work.end() && !w->second.Empty()) {
				if (w->second.Due(now)) {
					++lanes[static_cast<int>(w->second.lane)];
				} else {
					++held;
				}
			}
		}
		const auto parked = static_cast<std::size_t>(std::ranges::count_if(_work, [](const auto& p) { return p.second.parked && !p.second.Empty(); }));
		const auto waiting = lanes[0] + lanes[1] + lanes[2] + _inflight.size();
		if (!_counts.Any() && waiting == 0) {
			return {};  // nothing done, nothing to do now: work held back or out of reach is said when something happens
		}
		const auto& n = _counts;
		auto line = std::format("bridge: {} probe(s), {} body order(s), {} refit(s), {} touch-up(s), {} snapshot(s); {} failed, {} out of reach, {} deferred; {} waiting",
			n.probes, n.bodies, n.refits, n.touches, n.snapshots, n.failed, n.gone, n.deferred, waiting);
		if (waiting != 0) {
			line += std::format(" ({} {}, {} {}, {} {})", lanes[0], LaneName(Lane::kUrgent), lanes[1], LaneName(Lane::kNormal), lanes[2], LaneName(Lane::kBackground));
		}
		if (held != 0) {
			line += std::format("; {} held while another mod has them busy", held);
		}
		if (parked != 0) {
			line += std::format("; {} for people out of memory, done when they are seen again", parked);
		}
		_counts = {};
		return line;
	}
}
