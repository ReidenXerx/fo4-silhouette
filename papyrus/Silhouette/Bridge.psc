Scriptname Silhouette:Bridge extends Quest
{Silhouette's hands (decisions S-18, S-22, S-24, S-40, S-43, S-54, S-55). Silhouette.dll
 decides every body; this script carries each decision out through LooksMenu's BodyGen
 -- the only door to the morph store -- and raises the events other mods listen for.
 It polls; the plugin never calls in.

 On Silhouette.esp's second quest (0x802). The regeneration window
 (Silhouette:Adopter, 0x800) needs none of this, and nothing here needs it.

 What an order does is decided in the plugin and tested there, against a fake
 bridge that does exactly what RunOrder below does. Change one, change the other,
 and ProtocolVersion with them.

 Every argument is passed explicitly: the base sources are decompiled and carry no
 default values.}

; For other mods (S-24). Register on this quest; against the decompiled base sources
; the name is the mangled one:
;   RegisterForCustomEvent(Silhouette:API.Bridge(), "silhouette:bridge_OnActorGenerated")
;   Event Silhouette:Bridge.OnActorGenerated(Silhouette:Bridge akSender, Var[] akArgs)
; (Built against the CK's own sources the plain "OnActorGenerated" is what compiles.)
CustomEvent OnActorGenerated        ; akArgs: [0] Actor, [1] String preset
CustomEvent OnActorNaked            ; akArgs: [0] Actor
CustomEvent OnActorRemovingClothes  ; akArgs: [0] Actor
CustomEvent OnORefitChanged         ; akArgs: [0] Actor, [1] Bool applied

; One timer id is all this needs. Papyrus never runs two OnTimer handlers of one
; script at once, so a handler that stalls holds up every timer of the script:
; nothing that waits on the game (a drain, one frame per BodyGen call) runs on the
; timer's stack -- it runs on its own, through CallFunctionNoWait.
Int Property kPollTimer = 1 AutoReadOnly
Float Property PollSeconds = 1.0 AutoReadOnly
; While work waits the poll comes round faster: the picker's orders are the player
; watching.
Float Property BusyPollSeconds = 0.25 AutoReadOnly
; Without a usable Silhouette.dll: how often the people around the player are
; looked at for a refit left on them (S-54).
Float Property SweepSeconds = 30.0 AutoReadOnly
; An order is up to ~120 BodyGen calls, each waiting for a frame on the main thread.
Int Property OrdersPerPoll = 6 AutoReadOnly
; MCM settings are read again every this many polls (no F4SE external events needed).
Int Property SettingsEvery = 10 AutoReadOnly
; The menu acts on the last NPC aimed at within this long of the crosshair leaving
; them: opening it takes the crosshair off them.
Float Property RecentAimSeconds = 30.0 AutoReadOnly
String Property ModName = "Silhouette" AutoReadOnly
Int Property SourcePicker = 3 AutoReadOnly
; The player's own actions (the picker, the NPC page) go first (S-55).
Int Property LaneUrgent = 0 AutoReadOnly
; What RunOrder below does, and the natives the menu calls. Silhouette.dll says what it
; expects; they must agree. 4: ResetEveryone (S-68).
Int Property Protocol = 5 AutoReadOnly
; "Reset everyone" forgets every body, picks included: a second press within this long
; confirms the first. A minute, not ten seconds: the clock runs while the player reads the
; first press's message box, and the owner's first try ran out reading it.
Float Property ResetConfirmSeconds = 60.0 AutoReadOnly
; Silhouette.esp's refit keyword (S-40): ORefit's floors live under it, apart from the body.
Int Property RefitKeywordID = 0x803 AutoReadOnly
String Property RefitMarker = "Silhouette_Refit" AutoReadOnly
; AAF.esm: AAF_ActorBusy (for the length of a scene) and AAF_ActorLocked (the flag AAF
; asks other mods to respect).
Int Property AAFActorBusy = 0x00915A AutoReadOnly
Int Property AAFActorLocked = 0x017CEA AutoReadOnly

; Real time the drain began, -1 when none runs. Not a "busy" flag that could outlive
; a crash: Connect() clears it on every load, and after two minutes it is treated as
; a drain that is not coming back.
Float _drainStarted = -1.0
Float _resetAsked = -1.0  ; real time of the first press of "Reset everyone", -1 when none waits
Int _polls = 0
Bool _plugin = false      ; Silhouette.dll is loaded and speaks this protocol
Bool _looksMenu = false
Bool _mcm = false
Bool _sweeping = false    ; no usable plugin: refits left behind are taken off (S-54)
Keyword _refitKeyword
Keyword _aafBusy
Keyword _aafLocked
; Form ids, not actors: an Actor held in a script variable is kept in memory with it.
Int[] _swept
; The switches last pushed to the plugin (ORefit 1, nipples 2, genitals 4, faction bodies 8), -1 for none this
; load: MCM's values are pushed only when they change.
Int _pushed = -1
; S-73: MCM's "Tell me when a change I asked for waits" off. False (the default a variable added to a script
; has in a save made before it) is on: the notices show unless the player switched them off.
Bool _hideNotices = False

;---------------------------------------------------------------------------
; Startup: on quest start and on every load. This script's variables live in the
; save; the plugin starts from nothing on every launch.
;---------------------------------------------------------------------------

Event OnQuestInit()
	RegisterForRemoteEvent(Game.GetPlayer(), "OnPlayerLoadGame")
	Connect()
EndEvent

Event Actor.OnPlayerLoadGame(Actor akSender)
	Connect()
EndEvent

Function Connect()
	_drainStarted = -1.0
	_polls = 0
	_plugin = False
	_sweeping = False
	_swept = new Int[0]
	_pushed = -1    ; the plugin starts from its defaults every launch
	; Cancel first: a timer started before the save may still be counting down.
	CancelTimer(kPollTimer)
	_refitKeyword = Game.GetFormFromFile(RefitKeywordID, "Silhouette.esp") as Keyword
	_aafBusy = None
	_aafLocked = None
	If Game.IsPluginInstalled("AAF.esm")
		_aafBusy = Game.GetFormFromFile(AAFActorBusy, "AAF.esm") as Keyword
		_aafLocked = Game.GetFormFromFile(AAFActorLocked, "AAF.esm") as Keyword
	EndIf
	; Checked once per load: calling into a missing plugin's script fills the log on
	; every call. By the names the plugins register with F4SE (f4se.log), not their
	; file names: MCM is "F4MCM"; LooksMenu "F4EE", or "Fallout 4 Engine Extender" on AE (API.LooksMenuLoaded).
	_mcm = F4SE.GetPluginVersion("F4MCM") > 0 || F4SE.GetPluginVersion("MCM") > 0
	_looksMenu = Silhouette:API.LooksMenuLoaded()
	If !_looksMenu
		Debug.Trace("Silhouette bridge: LooksMenu is not loaded - no body can be shaped", 0)
		Debug.Notification("Silhouette: LooksMenu is not loaded, so no body can be shaped.")
		Return
	EndIf
	If F4SE.GetPluginVersion("Silhouette") <= 0
		; No DLL: nothing below may call a native, or every poll logs an error.
		; BodyGen still gives every NPC a body (Phase 1 needs none of this).
		Debug.Trace("Silhouette bridge: Silhouette.dll is not loaded - rules by name and faction, ORefit, the NPC picker and the API are off", 0)
		StartSweeping()
		Return
	EndIf
	Int theirs = Silhouette:DLL.ProtocolVersion()
	If theirs != Protocol
		String why = "the scripts speak protocol " + Protocol + " but Silhouette.dll speaks " + theirs + ": install one release's files together. The bridge stays off."
		Debug.Trace("Silhouette bridge: " + why, 0)
		Silhouette:DLL.Log(why)
		Debug.Notification("Silhouette: Silhouette.dll and its scripts are from different releases - install one release's files together.")
		StartSweeping()
		Return
	EndIf
	_plugin = True
	Silhouette:DLL.Log("bridge connected - " + Silhouette:DLL.Status())
	If !_refitKeyword
		Silhouette:DLL.Log("Silhouette.esp holds no refit keyword (an older Silhouette.esp?): ORefit stays off")
	EndIf
	If !Silhouette:DLL.IsReady()
		Debug.Notification("Silhouette: " + Silhouette:DLL.Status())
		StartSweeping()
		Return
	EndIf
	PushSettings()
	String menu = Silhouette:Player.Build()
	If menu != Silhouette:DLL.Build()
		Silhouette:DLL.Log("the scripts are build " + menu + " but the catalog is build " + Silhouette:DLL.Build() + ": install one generator run's files together")
	EndIf
	StartTimer(PollSeconds, kPollTimer)
EndFunction

; The MCM's switches, when MCM is there. Without it the plugin keeps what it has --
; its defaults, or what another mod set through Silhouette:API.
; MCM answers False for a key it never read (settings.ini missing or unread), which
; would switch ORefit off for everyone: its values count only while the sentinel key
; it reads with them, on no control, says they were read. And a value unchanged since
; the last push is not pushed again: a read made just before another mod's switch
; (Silhouette:API, MCM first, then the plugin) must not undo it.
Function PushSettings()
	If !_mcm && _refitKeyword
		Return
	EndIf
	Bool orefit = True
	Bool nipples = True
	Bool genitals = True
	Bool factionPools = True
	If _mcm && MCM.GetModSettingInt(ModName, "iDefaults:Meta") == 1
		orefit = MCM.GetModSettingBool(ModName, "bORefit:General")
		nipples = MCM.GetModSettingBool(ModName, "bNippleRand:General")
		genitals = MCM.GetModSettingBool(ModName, "bGenitalRand:General")
		factionPools = MCM.GetModSettingBool(ModName, "bFactionPools:General")
		_hideNotices = !MCM.GetModSettingBool(ModName, "bNotices:General")
	ElseIf _refitKeyword
		Return
	EndIf
	If !_refitKeyword
		; Nowhere to put a refit but the body's own layer: none at all instead.
		orefit = False
	EndIf
	Int now = 0
	If orefit
		now += 1
	EndIf
	If nipples
		now += 2
	EndIf
	If genitals
		now += 4
	EndIf
	If factionPools
		now += 8
	EndIf
	If now == _pushed
		Return
	EndIf
	_pushed = now
	Silhouette:DLL.Configure(orefit, nipples, genitals, factionPools)
EndFunction

;---------------------------------------------------------------------------
; The poll
;---------------------------------------------------------------------------

Event OnTimer(Int aiTimerID)
	If aiTimerID != kPollTimer
		Return
	EndIf
	If _sweeping
		StartTimer(SweepSeconds, kPollTimer)
		Sweep()
		Return
	EndIf
	If !_plugin
		Return
	EndIf
	; The next poll is scheduled BEFORE this one does anything, so nothing below
	; can stop the clock.
	If Silhouette:DLL.Pending() > 0
		StartTimer(BusyPollSeconds, kPollTimer)
	Else
		StartTimer(PollSeconds, kPollTimer)
	EndIf
	_polls += 1
	If _polls % SettingsEvery == 0
		PushSettings()
	EndIf
	Silhouette:DLL.Pump()
	; Events are raised here only, on this one stack: in the order they happened.
	RaiseEvents()
	ShowNotices()
	; A drain waits on the main thread once per BodyGen call, so it runs on a stack of
	; its own and this poll returns at once. One drain at a time keeps them few; the
	; plugin hands an actor to one order at a time either way, so two could not collide.
	Float now = Utility.GetCurrentRealTime()
	If _drainStarted >= 0.0 && now >= _drainStarted && now - _drainStarted < 120.0
		Return
	EndIf
	_drainStarted = now
	CallFunctionNoWait("PollDrain", new Var[0])
EndEvent

Function PollDrain()
	Drain(OrdersPerPoll)
	_drainStarted = -1.0
EndFunction

Function Drain(Int aiBudget)
	Int done = 0
	While done < aiBudget
		Int id = Silhouette:DLL.NextOrder()
		If id == 0
			Return
		EndIf
		RunOrder(id)
		done += 1
	EndWhile
EndFunction

; In an AAF scene, or flagged for other mods to leave alone.
Bool Function Busy(Actor a)
	If _aafBusy && a.HasKeyword(_aafBusy)
		Return True
	EndIf
	If _aafLocked && a.HasKeyword(_aafLocked)
		Return True
	EndIf
	Return False
EndFunction

; One order, exactly as the plugin's tests run it (protocol 3).
Function RunOrder(Int aiOrder)
	Int who = Silhouette:DLL.OrderActor(aiOrder)
	Actor a = Game.GetForm(who) as Actor
	If !a
		; Not in memory: the plugin keeps the work until they are seen again.
		Silhouette:DLL.OrderGone(aiOrder)
		Return
	EndIf
	Bool female = Silhouette:DLL.OrderFemale(aiOrder)

	If Silhouette:DLL.OrderKind(aiOrder) == 5 && Busy(a)
		; A touch-up can wait: it would change her shapes in the middle of the scene.
		Silhouette:DLL.OrderDefer(aiOrder)
		Return
	EndIf
	If Silhouette:DLL.OrderRegenerates(aiOrder)
		If Busy(a)
			; A roll keeps every keyed value it finds, and in a scene those are the
			; scene's (AAF's erection under its own keyword): they would stay for good.
			Silhouette:DLL.OrderDefer(aiOrder)
			Return
		EndIf
		If !Regenerate(aiOrder, who, a, female)
			Return
		EndIf
	EndIf

	Bool probe = Silhouette:DLL.OrderProbes(aiOrder)
	Bool all = Silhouette:DLL.OrderReadsAll(aiOrder)
	If probe || all
		String[] morphs = BodyGen.GetMorphs(a, female)
		Int count = 0
		If morphs
			count = morphs.Length
		EndIf
		Int i = 0
		While i < count
			Int kind = 0
			If probe
				Silhouette:DLL.NoteName(aiOrder, morphs[i])
				kind = Silhouette:DLL.MarkerKind(morphs[i])
			EndIf
			If kind == 2
				; The refit marker lives under the refit keyword, not in the body.
				If _refitKeyword
					Silhouette:DLL.NoteMarker(aiOrder, morphs[i], BodyGen.GetMorph(a, female, morphs[i], _refitKeyword))
				EndIf
			ElseIf kind == 1 || kind == 3 || all
				; A body marker and the choice beside it (S-51) are in the body's own layer.
				Float v = BodyGen.GetMorph(a, female, morphs[i], None)
				If kind != 0
					Silhouette:DLL.NoteMarker(aiOrder, morphs[i], v)
				EndIf
				If all && v != 0.0
					Silhouette:DLL.NoteLayer(aiOrder, morphs[i], v)
				EndIf
			EndIf
			i += 1
		EndWhile
	EndIf

	; After the probe: what to read, and which refit applies, depend on what it found.
	; The plugin may know enough before the last read (one value of her own is a body).
	Int reads = Silhouette:DLL.OrderReadCount(aiOrder)
	Int r = 0
	While r < reads && !Silhouette:DLL.OrderReadsDone(aiOrder)
		Silhouette:DLL.NoteRead(aiOrder, r, BodyGen.GetMorph(a, female, Silhouette:DLL.OrderReadMorph(aiOrder, r), None))
		r += 1
	EndWhile

	If !Silhouette:DLL.Prepare(aiOrder)
		Silhouette:DLL.OrderDone(aiOrder, False)
		Return
	EndIf
	; A load since the probe forgot the order, and an id of the save left behind can
	; name somebody else in this one: nothing is written for an order that is gone.
	If Silhouette:DLL.OrderActor(aiOrder) != who
		Return
	EndIf

	Int writes = Silhouette:DLL.OrderWriteCount(aiOrder)
	If !_refitKeyword
		; Without the keyword a refit would land in the body's own layer.
		Int k = 0
		While k < writes
			If Silhouette:DLL.OrderWriteLayer(aiOrder, k) == 1
				Silhouette:DLL.Log("a refit without Silhouette.esp's refit keyword was not carried out")
				Silhouette:DLL.OrderDone(aiOrder, False)
				Return
			EndIf
			k += 1
		EndWhile
	EndIf
	; The unkeyed layer is the body. Other mods' keyed morphs (AAF's, a pregnancy
	; belly, the anatomy arousal layer) are never touched.
	If Silhouette:DLL.OrderClearsUnkeyed(aiOrder)
		BodyGen.RemoveMorphsByKeyword(a, female, None)
	EndIf
	If _refitKeyword && Silhouette:DLL.OrderClearsRefit(aiOrder)
		BodyGen.RemoveMorphsByKeyword(a, female, _refitKeyword)
	EndIf
	Int w = 0
	While w < writes
		Keyword layer = None
		If Silhouette:DLL.OrderWriteLayer(aiOrder, w) == 1
			layer = _refitKeyword
		EndIf
		BodyGen.SetMorph(a, female, Silhouette:DLL.OrderWriteMorph(aiOrder, w), layer, Silhouette:DLL.OrderWriteValue(aiOrder, w))
		w += 1
	EndWhile
	If Silhouette:DLL.OrderUpdates(aiOrder)
		BodyGen.UpdateMorphs(a)
	EndIf
	Silhouette:DLL.OrderDone(aiOrder, True)
EndFunction

; BodyGen rolls them again. RegenerateMorphs clears EVERY key, so the keyed values
; (other mods', and the refit) are remembered first and put back after -- the first
; 128 of them: a Papyrus array holds no more. Reading them takes a frame a value, so
; a scene can start meanwhile, or a load forget the order: both are asked again right
; before the roll. False: nothing was rolled, and the order is deferred or gone.
Bool Function Regenerate(Int aiOrder, Int aiWho, Actor a, Bool female)
	String[] morphs = BodyGen.GetMorphs(a, female)
	String[] names = new String[0]
	Keyword[] keys = new Keyword[0]
	Float[] values = new Float[0]
	Int count = 0
	If morphs
		count = morphs.Length
	EndIf
	Int i = 0
	While i < count
		Keyword[] kws = BodyGen.GetKeywords(a, female, morphs[i])
		If kws
			Int k = 0
			While k < kws.Length
				If kws[k] && names.Length < 128
					Float v = BodyGen.GetMorph(a, female, morphs[i], kws[k])
					If v != 0.0
						names.Add(morphs[i], 1)
						keys.Add(kws[k], 1)
						values.Add(v, 1)
					EndIf
				EndIf
				k += 1
			EndWhile
		EndIf
		i += 1
	EndWhile
	If Silhouette:DLL.OrderActor(aiOrder) != aiWho
		Return False
	EndIf
	If Busy(a)
		Silhouette:DLL.OrderDefer(aiOrder)
		Return False
	EndIf
	BodyGen.RegenerateMorphs(a, False)
	Int j = 0
	While j < names.Length
		BodyGen.SetMorph(a, female, names[j], keys[j], values[j])
		j += 1
	EndWhile
	Return True
EndFunction

; The names are sent as the compiler would have mangled them: against the decompiled
; base sources SendCustomEvent takes a plain string, and a listener's registration
; asks for "<script>_<event>" (S-46). Each raised event is told back to the plugin:
; one handed out but not raised before a save is made again after the load. At most
; 64 a poll, and the next one is only taken while there is room to raise it: one
; taken and not raised would be lost until the actor is next probed. ONE loop raises,
; on the bridge's one timer: NextEvent settles every event handed out before, so a
; second loop raising at the same time would get the same body announced twice.
; S-71: what the player should know and cannot see -- a change they asked for waiting for
; another mod's scene, and landing after it. A few a poll: the screen shows them one by one.
Function ShowNotices()
	Int shown = 0
	String line = Silhouette:DLL.NextNotice()
	While line != "" && shown < 4
		If !_hideNotices    ; switched off (S-73): taken all the same, so none waits to show later
			Debug.Notification("Silhouette: " + line)
		EndIf
		shown += 1
		If shown < 4
			line = Silhouette:DLL.NextNotice()
		EndIf
	EndWhile
EndFunction

Function RaiseEvents()
	Int e = Silhouette:DLL.NextEvent()
	Int raised = 0
	While e != 0
		Int kind = Silhouette:DLL.EventKind(e)
		Actor a = Game.GetForm(Silhouette:DLL.EventActor(e)) as Actor
		If a
			Var[] args
			If kind == 1
				args = new Var[2]
				args[0] = a
				args[1] = Silhouette:DLL.EventPreset(e)
				SendCustomEvent("silhouette:bridge_OnActorGenerated", args)
			ElseIf kind == 2
				args = new Var[1]
				args[0] = a
				SendCustomEvent("silhouette:bridge_OnActorNaked", args)
			ElseIf kind == 3
				args = new Var[1]
				args[0] = a
				SendCustomEvent("silhouette:bridge_OnActorRemovingClothes", args)
			ElseIf kind == 4
				args = new Var[2]
				args[0] = a
				args[1] = Silhouette:DLL.EventFlag(e)
				SendCustomEvent("silhouette:bridge_OnORefitChanged", args)
			EndIf
			Silhouette:DLL.EventDone(e)
		EndIf
		raised += 1
		If raised < 64
			e = Silhouette:DLL.NextEvent()
		Else
			e = 0
		EndIf
	EndWhile
EndFunction

;---------------------------------------------------------------------------
; Without a usable Silhouette.dll (S-54): missing, from another release, or its
; catalog refused. A refit layer left on someone would stay on for good, dressed or
; not, so everyone around the player is looked at once per load and a refit found
; is taken off. Their own body stays: it is BodyGen's, in the unkeyed layer.
;---------------------------------------------------------------------------

Function StartSweeping()
	If !_refitKeyword || !_looksMenu
		Return
	EndIf
	_sweeping = True
	StartTimer(1.0, kPollTimer)
EndFunction

Function Sweep()
	Actor[] people = Silhouette:Player.Nearby()
	Int taken = 0
	Int i = 0
	While i < people.Length
		Actor a = people[i]
		If a && _swept.Find(a.GetFormID(), 0) < 0
			If _swept.Length >= 128
				_swept = new Int[0]
			EndIf
			_swept.Add(a.GetFormID(), 1)
			Bool female = a.GetLeveledActorBase().GetSex() == 1
			If BodyGen.GetMorph(a, female, RefitMarker, _refitKeyword) > 0.0
				BodyGen.RemoveMorphsByKeyword(a, female, _refitKeyword)
				BodyGen.UpdateMorphs(a)
				taken += 1
			EndIf
		EndIf
		i += 1
	EndWhile
	If taken > 0
		Debug.Trace("Silhouette bridge: " + taken + " refit(s) left on people without a working Silhouette.dll taken off", 0)
	EndIf
EndFunction

;---------------------------------------------------------------------------
; The NPC picker (S-22): MCM hotkeys call these on this quest.
;---------------------------------------------------------------------------

Bool Function Ready()
	If !_looksMenu
		Debug.Notification("Silhouette: LooksMenu is not loaded, so no body can be shaped.")
		Return False
	EndIf
	If !_plugin
		Debug.Notification("Silhouette: Silhouette.dll is not loaded, or is from another release.")
		Return False
	EndIf
	If !Silhouette:DLL.IsReady()
		Debug.Notification("Silhouette: " + Silhouette:DLL.Status())
		Return False
	EndIf
	Return True
EndFunction

; The picker's own orders are at the front of the queue (S-55), so the work below
; starts with them; its events are raised by the next poll.
Function Act()
	Drain(2)
EndFunction

Function PickerPick()
	If !Ready()
		Return
	EndIf
	Int target = Silhouette:DLL.CrosshairActor(0.0)
	If target == 0
		Debug.Notification("Silhouette: aim at an NPC close enough to talk to, then Pick.")
		Return
	EndIf
	Debug.Notification(Silhouette:DLL.PickerStart(target))
	Act()
EndFunction

Function PickerNext()
	If Ready()
		Debug.Notification(Silhouette:DLL.PickerStep(1))
		Act()
	EndIf
EndFunction

Function PickerPrevious()
	If Ready()
		Debug.Notification(Silhouette:DLL.PickerStep(-1))
		Act()
	EndIf
EndFunction

Function PickerKeep()
	If Ready()
		Debug.Notification(Silhouette:DLL.PickerKeep())
		Act()
	EndIf
EndFunction

Function PickerCancel()
	If Ready()
		Debug.Notification(Silhouette:DLL.PickerCancel())
		Act()
	EndIf
EndFunction

;---------------------------------------------------------------------------
; The MCM page "The NPC in your sights" calls these on this quest.
;---------------------------------------------------------------------------

; The NPC the menu acts on: the one picked with the hotkey, or the last one aimed at.
Int Function MenuTarget()
	Int target = Silhouette:DLL.PickerTarget()
	If target == 0
		target = Silhouette:DLL.CrosshairActor(RecentAimSeconds)
	EndIf
	Return target
EndFunction

Bool Function MenuReady()
	If !_looksMenu
		Debug.MessageBox("Silhouette: LooksMenu is not loaded, so no body can be shaped: its BodyGen is what gives every body.")
		Return False
	EndIf
	If !_plugin
		Debug.MessageBox("Silhouette: Silhouette.dll is not loaded (or is from another release), so NPCs cannot be shaped one by one. BodyGen still gives everyone a body.")
		Return False
	EndIf
	If !Silhouette:DLL.IsReady()
		Debug.MessageBox("Silhouette: " + Silhouette:DLL.Status())
		Return False
	EndIf
	Return True
EndFunction

Function MenuApply()
	If !MenuReady()
		Return
	EndIf
	Int target = MenuTarget()
	Actor a = Game.GetForm(target) as Actor
	If !a
		Debug.MessageBox("Silhouette: aim at an NPC before opening the menu, or Pick one with the hotkey.")
		Return
	EndIf
	Bool female = a.GetLeveledActorBase().GetSex() == 1
	String preset = Silhouette:Player.NpcChoice(female)
	If preset == ""
		Debug.MessageBox("Silhouette: that choice is not in this build of the menu. Nothing was changed.")
		Return
	EndIf
	String why = Silhouette:DLL.RequestPreset(target, preset, SourcePicker, LaneUrgent)
	If why != ""
		Debug.MessageBox("Silhouette: " + why)
		Return
	EndIf
	Act()
	Debug.MessageBox(Silhouette:DLL.NameOf(target) + " gets " + preset + ". Close the menu to see it.")
EndFunction

Function MenuRandom()
	If !MenuReady()
		Return
	EndIf
	Int target = MenuTarget()
	If target == 0
		Debug.MessageBox("Silhouette: aim at an NPC before opening the menu, or Pick one with the hotkey.")
		Return
	EndIf
	String why = Silhouette:DLL.RequestRegenerate(target, LaneUrgent)
	If why != ""
		Debug.MessageBox("Silhouette: " + why)
		Return
	EndIf
	Act()
	String see = " Close the menu to see it."
	Actor a = Game.GetForm(target) as Actor
	If a && Busy(a)
		see = " Another mod has them in a scene: the new body comes when it ends."
	EndIf
	Debug.MessageBox(Silhouette:DLL.NameOf(target) + " is reset: Silhouette decides their body again, as if met for the first time (their own body if they have one; a rule with several presets draws again). Other mods' body morphs are kept." + see)
EndFunction

; S-68: every body Silhouette gave, picks included, decided again. The first press only
; asks; a second within ResetConfirmSeconds does it.
Function MenuResetEveryone()
	If !MenuReady()
		Return
	EndIf
	Float now = Utility.GetCurrentRealTime()
	If _resetAsked < 0.0 || now - _resetAsked > ResetConfirmSeconds || now < _resetAsked
		_resetAsked = now
		Debug.MessageBox("Silhouette: NOTHING HAS CHANGED YET. Press Reset everyone AGAIN (within a minute) to forget every body Silhouette gave, your picks too, and decide them all again: named characters get their own body, everyone else a new roll from the pool. People around you change at once, everyone else when you next meet them.")
		Return
	EndIf
	_resetAsked = -1.0
	String said = Silhouette:DLL.ResetEveryone()
	Act()
	Debug.MessageBox("Silhouette: " + said)
EndFunction

Function MenuWhich()
	If !MenuReady()
		Return
	EndIf
	Int target = MenuTarget()
	Actor a = Game.GetForm(target) as Actor
	If !a
		Debug.MessageBox("Silhouette: aim at an NPC before opening the menu, or Pick one with the hotkey.")
		Return
	EndIf
	Debug.MessageBox(Silhouette:DLL.NameOf(target) + ": " + BodyOf(a) + ". " + Silhouette:DLL.Describe(target) + ".")
EndFunction

; The preset their marker names, read from LooksMenu now.
String Function BodyOf(Actor a)
	Bool female = a.GetLeveledActorBase().GetSex() == 1
	String[] morphs = BodyGen.GetMorphs(a, female)
	Int count = 0
	If morphs
		count = morphs.Length
	EndIf
	Int i = 0
	While i < count
		If Silhouette:DLL.MarkerKind(morphs[i]) == 1
			Float v = BodyGen.GetMorph(a, female, morphs[i], None)
			If v > 0.0
				If morphs[i] == "Silhouette_Blacklisted"
					Return "kept bare by the blacklist"
				EndIf
				String preset = Silhouette:DLL.PresetForMarker(morphs[i], v)
				If preset != "" && v < 1.0
					; The marker says "pending" (S-58): being written now, or cut short by a
					; save and given again at the next probe.
					Return preset + " (being written)"
				ElseIf preset != ""
					Return preset
				EndIf
				Return "a marker this install cannot name (" + morphs[i] + ")"
			EndIf
		EndIf
		i += 1
	EndWhile
	If count == 0
		Return "no body sliders at all"
	EndIf
	Return "body sliders Silhouette did not set"
EndFunction
