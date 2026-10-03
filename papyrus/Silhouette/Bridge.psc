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
Int Property Protocol = 8 AutoReadOnly
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
	WindowForget()
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
	; A sex with no body Silhouette supports is left alone, and Invisible Dead Body Fix missing (S-82): said once a
	; launch, in a box.
	String bodies = Silhouette:DLL.BodyWarning()
	If bodies != ""
		Debug.MessageBox("Silhouette: " + bodies)
	EndIf
	PushSettings()
	String menu = Silhouette:Player.Build()
	If menu != Silhouette:DLL.Build()
		Silhouette:DLL.Log("the scripts are build " + menu + " but the catalog is build " + Silhouette:DLL.Build() + ": install one generator run's files together")
	EndIf
	StartTimer(PollSeconds, kPollTimer)
EndFunction

;---------------------------------------------------------------------------
; S-79: the picker window. F4SE opens Interface\SilhouetteMenu.swf as a custom menu --
; F4SE's own menu, none of CommonLibF4's menu code, whose crash on 1.10.163 is why S-22
; had no window. Whose body: the NPC the player aimed at in the last seconds ("them",
; the plugin's picker does the work: a preview, Keep, Cancel), or the player ("me",
; Silhouette:Player's presets, with a snapshot of the body to put back). A click tries a
; preset on live; Apply keeps it; anything else that closes the window puts back what
; they had -- the one place that happens is the menu's close event.
; Neither the menu nor the event registrations survive a load: opening registers again.
; The hotkey opens it at once; MCM's buttons open it when the pause menu closes (MCM
; lives inside it); the console: cgf "Silhouette:API.OpenWindow".
;
; Every call into LooksMenu or the plugin can hand this script to another thread, so the
; window's work runs in SESSIONS (microscope wave 1): opening, closing and the Them / Me
; switch each start a new one, and work of an older session -- a load still reading a
; body, a camera still switching -- stops at its next step and takes back what it did.
; Tries run one at a time, the latest asked for last.
;---------------------------------------------------------------------------
String Property WindowMenu = "SilhouetteMenu" AutoReadOnly
Float Property WindowAimSeconds = 10.0 AutoReadOnly  ; the console takes the crosshair: an NPC aimed at this recently still counts

Int _winThem = 0            ; the NPC aimed at when the window opened, 0 for none
Bool _winMe = False         ; the window is on the player
Bool _winApplied = False    ; Apply was pressed: the close keeps what is on
Bool _winTried = False      ; the player tries a preset on: the close puts the snapshot back
Bool _winMeList             ; the Me tab lists the plugin's presets (yours too), not the player script's (0.3.3)
String[] _winMorphs         ; the player's body before the first try: morph names ...
Float[] _winValues          ; ... and values, the unkeyed layer only
Bool _winAfterMenu = False  ; an MCM button asked for the window: it opens when the pause menu closes
; The NPC the window holds in place (SetRestrained), 0 for none. Lives in the save: a save made while the
; window was open still lets them go at the next load (Connect).
Int _winHeld = 0
Int _winAfterTarget = 0     ; ... on this NPC, 0 for the player
Int _winSession = 0         ; bumped by every open, close and switch: older work stops
Bool _winOpen = False       ; the window is open (between OpenWindowOn and its close event)
Bool _winClosing = False    ; the close is putting things back: no new window until it is done
Bool _winPicking = False    ; a try is being put on
Int _winWantIndex = -1      ; the latest try asked for, -1 none ...
Int _winLoaded = 0          ; the session whose contents were sent: the window's two "ready"s load it once
; The player's controls, off while the window is open: a custom menu does not take them by itself, and the mouse
; and keys went on moving, aiming and looking behind it (the owner's test of 0.3.1, 2026-10-01; ScreenArcherMenu
; does the same with an InputEnableLayer).
InputEnableLayer _winInput
String _winWantName = ""    ; ... and its name

; The hotkey: the NPC in the player's sights (or aimed at in the last seconds), else the player. Pressed
; while the window is open, it closes it (as Cancel): a way out whatever the window's own input does.
Function OpenWindow()
	If UI.IsMenuOpen(WindowMenu)
		CloseWindow()
		Return
	EndIf
	Int target = 0
	If _plugin
		target = Silhouette:DLL.CrosshairActor(WindowAimSeconds)
	EndIf
	OpenWindowOn(target)
EndFunction

; MCM's button on the NPC page: whoever was aimed at in the half minute before the menu opened.
Function MenuOpenWindow()
	Int target = 0
	If _plugin
		target = Silhouette:DLL.CrosshairActor(30.0)
	EndIf
	If target == 0
		Debug.Notification("Silhouette: nobody was in your sights before the menu opened - the window opens on you.")
	EndIf
	OpenWindowAfterMenu(target)
EndFunction

; MCM's button on the Bodies page: the player.
Function MenuOpenWindowMe()
	OpenWindowAfterMenu(0)
EndFunction

Function OpenWindowAfterMenu(Int aiTarget)
	_winAfterMenu = True
	_winAfterTarget = aiTarget
	RegisterForMenuOpenCloseEvent("PauseMenu")
	Debug.Notification("Silhouette: the picker window opens when you close the menu.")
EndFunction

Function OpenWindowOn(Int aiTarget)
	If UI.IsMenuOpen(WindowMenu) || _winOpen
		Return
	EndIf
	If _winClosing
		Debug.Notification("Silhouette: the window is still putting things back - a moment.")
		Return
	EndIf
	If !_looksMenu
		Debug.MessageBox("Silhouette: LooksMenu is not loaded, so no body can be shaped: its BodyGen is what gives every body.")
		Return
	EndIf
	If Game.GetPlayer().IsInCombat()
		Debug.Notification("Silhouette: not in combat.")
		Return
	EndIf
	If !UI.IsMenuRegistered(WindowMenu)
		UI:MenuData data = new UI:MenuData
		; ScreenArcherMenu's flags (cursor, modal, the game running behind it) -- the ones the owner's test in
		; game (2026-09-30) clicked through. 0.3.0 added the menu input context (0x8) for the gamepad, and a
		; player could click nothing, not even Esc, and had to quit the game (0.3.1).
		data.menuFlags = 0x8018496
		; Inherit the HUD's colours, and keep the cursor even with a gamepad plugged in: F4SE's "check for
		; gamepad" (2) takes the cursor away, and the window is then out of reach of the mouse.
		data.extendedFlags = 1
		If !UI.RegisterCustomMenu(WindowMenu, "SilhouetteMenu", "root1.Menu_mc", data)
			Debug.MessageBox("Silhouette: the picker window could not be registered. Is Interface/SilhouetteMenu.swf installed?")
			Return
		EndIf
	EndIf
	RegisterForExternalEvent("Silhouette_WindowReady", "OnWindowReady")
	RegisterForExternalEvent("Silhouette_WindowPick", "OnWindowPick")
	RegisterForExternalEvent("Silhouette_WindowApply", "OnWindowApply")
	RegisterForExternalEvent("Silhouette_WindowCancel", "OnWindowCancel")
	RegisterForExternalEvent("Silhouette_WindowTarget", "OnWindowTarget")
	RegisterForExternalEvent("Silhouette_WindowNote", "OnWindowNote")
	RegisterForMenuOpenCloseEvent(WindowMenu)
	_winSession += 1
	_winOpen = True
	_winThem = aiTarget
	_winMe = _winThem == 0
	_winApplied = False
	_winTried = False
	_winWantIndex = -1
	WindowLockControls()
	UI.OpenMenu(WindowMenu)
	; F4SE keeps the window's movie between openings, with all it held: once it is open, it is told to start
	; again (Panel.Begin), and it asks for its contents. A first opening also says so by itself; that one is
	; loaded once (_winLoaded).
	Int session = _winSession
	Int i = 0
	While !UI.IsMenuOpen(WindowMenu) && i < 40
		Utility.WaitMenuMode(0.05)
		i += 1
	EndWhile
	If WindowLive(session)
		UI.Invoke(WindowMenu, "root1.Menu_mc.Begin")
	EndIf
EndFunction

; Movement, fighting, looking, the camera switch, sneaking, activating, the journal, VATS, favourites, running and
; jumping: off. The pause menu is left as it is -- the window's own flags keep it shut while it is open.
Function WindowLockControls()
	If _winInput
		Return
	EndIf
	_winInput = InputEnableLayer.Create()
	_winInput.DisablePlayerControls(True, True, True, True, True, False, True, True, True, True, True)
	_winInput.EnableJumping(False)
EndFunction

Function WindowUnlockControls()
	If _winInput
		InputEnableLayer layer = _winInput
		_winInput = None
		layer.Delete()
	EndIf
EndFunction

; Work of session aiSession may go on: the window is open and nothing has moved on since.
Bool Function WindowLive(Int aiSession)
	Return _winOpen && aiSession == _winSession
EndFunction

; What the window received (a control, a mouse press, a click it ignored and why), into Silhouette.log.
Function OnWindowNote(String asNote)
	If _plugin
		Silhouette:DLL.Log("window: " + asNote)
	EndIf
EndFunction

Function OnWindowReady()
	If !_winOpen || _winLoaded == _winSession
		Return
	EndIf
	_winLoaded = _winSession
	WindowLoad(_winSession)
EndFunction

Function WindowLoad(Int aiSession)
	If _winMe
		WindowLoadMe(aiSession)
	Else
		WindowLoadThem(aiSession)
	EndIf
EndFunction

Function WindowLoadThem(Int aiSession)
	Actor dead = Game.GetForm(_winThem) as Actor
	If dead && dead.IsDead()
		If !WindowLive(aiSession)
			Return
		EndIf
		WindowTarget(Silhouette:DLL.NameOf(_winThem), "them", Silhouette:Player.IsFemale(dead), Silhouette:DLL.Build())
		WindowItems("", "Silhouette leaves the dead alone: they keep the body they have.", -1)
		Return
	EndIf
	String said = Silhouette:DLL.PickerStart(_winThem)
	String name = Silhouette:DLL.NameOf(_winThem)
	If !WindowLive(aiSession)
		WindowDropPicking()
		Return
	EndIf
	WindowTarget(name, "them", Silhouette:DLL.PickerFemale(), Silhouette:DLL.Build())
	If Silhouette:DLL.PickerTarget() != _winThem
		WindowItems("", said, -1)
		Return
	EndIf
	WindowItems(Silhouette:DLL.PickerPresets(), "Reading " + name + "'s body...", Silhouette:DLL.PickerIndex())
	Actor them = Game.GetForm(_winThem) as Actor
	WindowHold(them, aiSession)
	WindowFrame(them, aiSession)
	Int i = 0
	While i < 40 && !Silhouette:DLL.PickerReady() && WindowLive(aiSession)
		Utility.WaitMenuMode(0.1)
		i += 1
	EndWhile
	If !WindowLive(aiSession)
		Return
	EndIf
	If !Silhouette:DLL.PickerReady()
		WindowStatus("Still reading " + name + "'s body: presets try on once it is in.")
		Return
	EndIf
	String had = Silhouette:DLL.PickerCurrent()
	If had == ""
		had = "a body Silhouette did not give"
	EndIf
	WindowStatus("Wears " + had + ". Click a preset to try it on.")
	WindowSelected(Silhouette:DLL.PickerIndex())
EndFunction

Function WindowLoadMe(Int aiSession)
	Actor player = Game.GetPlayer()
	Bool female = Silhouette:Player.IsFemale(player)
	WindowTarget("You", "me", female, Silhouette:Player.Build())
	If _plugin && !Silhouette:DLL.BodySupported(female)
		; S-78: no body Silhouette supports for this sex -- its presets would change nothing.
		WindowItems("", "No " + WindowSex(female) + " body Silhouette supports is installed, so it leaves yours alone (Silhouette.log says why).", -1)
		Return
	EndIf
	_winMeList = _plugin
	If _winMeList
		; The plugin's list, as the NPC tab shows it: your own BodySlide presets (S-76) are read in game, and the
		; player script only knows the presets it was built with (a tester, 2026-10-02: none of theirs on "Me").
		String list = Silhouette:DLL.PlayerPresets(female)
		String mine = WindowMyPreset(player, female)
		If !WindowLive(aiSession)
			Return
		EndIf
		WindowItems(list, "You have " + WindowBodyName(mine) + ". Click a preset to try it on.", Silhouette:DLL.PlayerPresetIndex(mine, female))
		Game.ForceThirdPerson()
		WindowFrame(player, aiSession)
		Return
	EndIf
	String[] n0
	String[] n1
	String[] m0
	String[] m1
	If female
		n0 = Silhouette:Player.FemaleNames0()
		n1 = Silhouette:Player.FemaleNames1()
		m0 = Silhouette:Player.FemaleMarkers0()
		m1 = Silhouette:Player.FemaleMarkers1()
	Else
		n0 = Silhouette:Player.MaleNames0()
		n1 = Silhouette:Player.MaleNames1()
		m0 = Silhouette:Player.MaleMarkers0()
		m1 = Silhouette:Player.MaleMarkers1()
	EndIf
	Int n = Silhouette:Player.Count(female)
	String joined = ""
	Int i = 0
	While i < n
		If i > 0
			joined += "|"
		EndIf
		joined += Silhouette:Player.At(i, n0, n1)
		i += 1
	EndWhile
	String had = Silhouette:Player.PresetOf(player, female, m0, n0, m1, n1)
	If !WindowLive(aiSession)
		Return
	EndIf
	Int at = -1
	If had != "" && had != "*"
		at = Silhouette:Player.Locate(had, n0, n1)
	EndIf
	WindowItems(joined, "You have " + WindowBodyName(had) + ". Click a preset to try it on.", at)
	Game.ForceThirdPerson()  ; the free camera shows the body the third-person view has
	WindowFrame(player, aiSession)
EndFunction

String Function WindowSex(Bool abFemale)
	If abFemale
		Return "female"
	EndIf
	Return "male"
EndFunction

; What Silhouette:Player.PresetOf says, as words.
; The preset the player's body is, by its marker: the plugin's catalog names your own presets too.
String Function WindowMyPreset(Actor akPlayer, Bool abFemale)
	If _winMeList
		String named = Silhouette:API.MarkerPreset(akPlayer)
		If named != ""
			Return named
		EndIf
	EndIf
	If abFemale
		Return Silhouette:Player.PresetOf(akPlayer, abFemale, Silhouette:Player.FemaleMarkers0(), Silhouette:Player.FemaleNames0(), Silhouette:Player.FemaleMarkers1(), Silhouette:Player.FemaleNames1())
	EndIf
	Return Silhouette:Player.PresetOf(akPlayer, abFemale, Silhouette:Player.MaleMarkers0(), Silhouette:Player.MaleNames0(), Silhouette:Player.MaleMarkers1(), Silhouette:Player.MaleNames1())
EndFunction

; A preset on the player from the plugin's list, as the player script gives one: the unkeyed layer cleared,
; the preset's values and its marker, the 3D reshaped. "" for a preset this build does not have.
String Function WindowGiveMe(Actor akPlayer, Bool abFemale, String asPreset)
	Int n = Silhouette:DLL.PlayerBodyCount(asPreset, abFemale)
	If n <= 0
		Return ""
	EndIf
	BodyGen.RemoveMorphsByKeyword(akPlayer, abFemale, None)
	Int i = 0
	While i < n
		BodyGen.SetMorph(akPlayer, abFemale, Silhouette:DLL.PlayerBodyMorph(asPreset, abFemale, i), None, Silhouette:DLL.PlayerBodyValue(asPreset, abFemale, i))
		i += 1
	EndWhile
	BodyGen.UpdateMorphs(akPlayer)
	Return asPreset
EndFunction

String Function WindowBodyName(String asPresetOf)
	If asPresetOf == "*"
		Return "sliders Silhouette did not set"
	ElseIf asPresetOf == ""
		Return "the bare body built in BodySlide"
	EndIf
	Return asPresetOf
EndFunction

; One try at a time, the latest asked for last: a key held down or fast clicks ask for many, and only the
; one the player stops on matters.
Function OnWindowPick(String asPreset, Int aiIndex)
	_winWantName = asPreset
	_winWantIndex = aiIndex
	If _winPicking
		Return
	EndIf
	_winPicking = True
	Int session = _winSession
	While _winWantIndex >= 0 && WindowLive(session)
		Int index = _winWantIndex
		String preset = _winWantName
		_winWantIndex = -1
		WindowTry(preset, index, session)
	EndWhile
	_winWantIndex = -1
	_winPicking = False
EndFunction

Function WindowTry(String asPreset, Int aiIndex, Int aiSession)
	If _winMe
		Actor player = Game.GetPlayer()
		Bool female = Silhouette:Player.IsFemale(player)
		If !_winTried
			_winTried = True  ; before the snapshot: the close waits for this try, then puts it back
			WindowSnapshotMe(player, female)
		EndIf
		If !WindowLive(aiSession)
			Return
		EndIf
		String name
		If _winMeList
			name = WindowGiveMe(player, female, asPreset)
		Else
			name = Silhouette:Player.Give(player, female, aiIndex)
		EndIf
		WindowStatus("Trying " + name + ". Apply keeps it; Cancel puts yours back.")
	ElseIf _winThem != 0 && Silhouette:DLL.PickerTarget() == _winThem
		WindowStatus(Silhouette:DLL.PickerShow(asPreset))
		WindowSelected(Silhouette:DLL.PickerIndex())  ; the card follows what is really on them
	EndIf
EndFunction

Function OnWindowApply()
	If !_winOpen || _winApplied
		Return
	EndIf
	_winApplied = True
	WindowSettle()
	If _winMe
		_winTried = False
		_winMorphs = None
		_winValues = None
		Actor player = Game.GetPlayer()
		Bool female = Silhouette:Player.IsFemale(player)
		Debug.Notification("Silhouette: your body is now " + WindowBodyName(WindowMyPreset(player, female)) + ".")
	ElseIf _winThem != 0 && Silhouette:DLL.PickerTarget() == _winThem
		Debug.Notification("Silhouette: " + Silhouette:DLL.PickerKeep())
	EndIf
	UI.CloseMenu(WindowMenu)
EndFunction

Function OnWindowCancel()
	UI.CloseMenu(WindowMenu)
EndFunction

; Closes the window as Cancel does (what was tried is put back): the hotkey again, or the console --
; cgf "Silhouette:API.CloseWindow".
Function CloseWindow()
	If UI.IsMenuOpen(WindowMenu)
		UI.CloseMenu(WindowMenu)
	EndIf
EndFunction

; The Them / Me switch: a new session; what was tried on the one being left is put back first.
Function OnWindowTarget(String asMode)
	If !_winOpen
		Return
	EndIf
	_winSession += 1
	Int session = _winSession
	WindowSettle()
	WindowRelease()
	WindowUnframe()  ; framed again for the other one
	WindowUndo()
	If !WindowLive(session)
		Return
	EndIf
	_winMe = asMode == "me" || _winThem == 0
	_winLoaded = session
	WindowLoad(session)
EndFunction

Event OnMenuOpenCloseEvent(string asMenuName, bool abOpening)
	If asMenuName == "PauseMenu"
		If !abOpening && _winAfterMenu
			_winAfterMenu = False
			UnregisterForMenuOpenCloseEvent("PauseMenu")
			OpenWindowOn(_winAfterTarget)
		EndIf
		Return
	EndIf
	If asMenuName != WindowMenu || abOpening || !_winOpen
		Return
	EndIf
	; Everything the window started is put back, the camera and the hold first (the player sees them at
	; once), then the body -- once any try still being put on has landed.
	_winOpen = False
	_winSession += 1
	_winClosing = True
	WindowUnlockControls()
	WindowRelease()
	WindowUnframe()
	WindowSettle()
	If !_winApplied
		WindowUndo()
	EndIf
	_winApplied = False
	_winClosing = False
EndEvent

; Waits for a try still being put on (at most 5 seconds): what is put back must come after it.
Function WindowSettle()
	Int i = 0
	While _winPicking && i < 100
		Utility.WaitMenuMode(0.05)
		i += 1
	EndWhile
EndFunction

; A picking the window started for a session that is already over (the window closed while it began).
Function WindowDropPicking()
	If _winThem != 0 && _plugin && Silhouette:DLL.PickerTarget() == _winThem
		Silhouette:DLL.PickerCancel()
	EndIf
EndFunction

; Held in place while they are picked: SetRestrained, the game's own "cannot move" -- their AI keeps running,
; so quests, companion routines and dialogue go on, and they carry on walking once let go. Nobody is held
; whose movement another mod or the game may be holding already (the game cannot say who is restrained):
; not in a scene, in combat, dead, or busy in AAF.
Function WindowHold(Actor akActor, Int aiSession)
	WindowRelease()
	If !WindowLive(aiSession) || !akActor || akActor.IsDead() || akActor.IsInCombat() || akActor.IsInScene() || Busy(akActor)
		Return
	EndIf
	akActor.SetRestrained(True)
	_winHeld = akActor.GetFormID()
	If !WindowLive(aiSession)
		WindowRelease()  ; the window closed while they were being held
	EndIf
EndFunction

Function WindowRelease()
	If _winHeld == 0
		Return
	EndIf
	Actor held = Game.GetForm(_winHeld) as Actor
	_winHeld = 0
	If held
		held.SetRestrained(False)
	EndIf
EndFunction

; The camera in front of them, the window beside them (Silhouette:DLL.CameraFrame: the game's free camera,
; switched off again when the window closes). Without the plugin, or where the camera cannot be moved, the
; window works as it is and the log says why.
Function WindowFrame(Actor akActor, Int aiSession)
	If !_plugin || !akActor || !WindowLive(aiSession)
		Return
	EndIf
	WindowCameraWait(Silhouette:DLL.CameraFrame(akActor.GetPositionX(), akActor.GetPositionY(), akActor.GetPositionZ(), akActor.GetAngleZ(), akActor.GetHeight()))
	If !WindowLive(aiSession)
		WindowUnframe()  ; the window closed or switched while the camera came on
	EndIf
EndFunction

Function WindowUnframe()
	If _plugin
		WindowCameraWait(Silhouette:DLL.CameraRestore())
	EndIf
EndFunction

; The game carries out "tfc" a frame or more after it is typed (measured: a camera checked at once had not
; changed, came on anyway and outlived the window). The plugin answers "wait" until it has, and gives up by
; itself after 3 seconds.
Function WindowCameraWait(String asSaid)
	Int i = 0
	While asSaid == "wait" && i < 80
		Utility.WaitMenuMode(0.05)
		asSaid = Silhouette:DLL.CameraStep()
		i += 1
	EndWhile
EndFunction

Function WindowUndo()
	If _winTried
		WindowRestoreMe()
		_winTried = False
	EndIf
	WindowDropPicking()
EndFunction

; The player's unkeyed body as it is, the first 128 values: a Papyrus array holds no more.
Function WindowSnapshotMe(Actor akPlayer, Bool abFemale)
	String[] names = new String[0]
	Float[] values = new Float[0]
	String[] morphs = BodyGen.GetMorphs(akPlayer, abFemale)
	Int i = 0
	While morphs && i < morphs.Length && names.Length < 128
		Float v = BodyGen.GetMorph(akPlayer, abFemale, morphs[i], None)
		If v != 0.0
			names.Add(morphs[i], 1)
			values.Add(v, 1)
		EndIf
		i += 1
	EndWhile
	_winMorphs = names
	_winValues = values
EndFunction

Function WindowRestoreMe()
	Actor player = Game.GetPlayer()
	Bool female = Silhouette:Player.IsFemale(player)
	String[] names = _winMorphs
	Float[] values = _winValues
	_winMorphs = None
	_winValues = None
	BodyGen.RemoveMorphsByKeyword(player, female, None)
	Int i = 0
	While names && i < names.Length
		BodyGen.SetMorph(player, female, names[i], None, values[i])
		i += 1
	EndWhile
	BodyGen.UpdateMorphs(player)
EndFunction

; A load forgets the window: the menu is gone, and nothing of it may carry into the save just loaded.
Function WindowForget()
	WindowUnlockControls()
	WindowRelease()
	_winSession += 1
	_winOpen = False
	_winClosing = False
	_winPicking = False
	_winWantIndex = -1
	_winApplied = False
	_winTried = False
	_winMorphs = None
	_winValues = None
	If _winAfterMenu
		_winAfterMenu = False
		UnregisterForMenuOpenCloseEvent("PauseMenu")
	EndIf
EndFunction

; The sex picks the atlas of pictures the window shows (tools/thumbnails.py), and the build names it: an
; atlas of another build is not found, and the cards show no picture rather than the wrong one.
Function WindowTarget(String asName, String asMode, Bool abFemale, String asBuild)
	Var[] args = new Var[5]
	args[0] = asName
	args[1] = _winThem != 0
	args[2] = asMode
	args[3] = abFemale
	args[4] = asBuild
	UI.Invoke(WindowMenu, "root1.Menu_mc.SetTarget", args)
EndFunction

Function WindowItems(String asJoined, String asStatus, Int aiSelected)
	Var[] args = new Var[3]
	args[0] = asJoined
	args[1] = asStatus
	args[2] = aiSelected
	UI.Invoke(WindowMenu, "root1.Menu_mc.SetItems", args)
EndFunction

Function WindowStatus(String asStatus)
	Var[] args = new Var[1]
	args[0] = asStatus
	UI.Invoke(WindowMenu, "root1.Menu_mc.SetStatus", args)
EndFunction

Function WindowSelected(Int aiSelected)
	Var[] args = new Var[1]
	args[0] = aiSelected
	UI.Invoke(WindowMenu, "root1.Menu_mc.SetSelected", args)
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

	; The dead are left as BodyGen gave them when they loaded (2026-10-02: reports of invisible bodies on pre-placed
	; corpses, only head and hands showing). A body, a refit or a touch-up would re-apply a ragdolled corpse's
	; geometry, a known way for Fallout 4 corpses to lose parts; reading them (a probe, a snapshot) changes
	; nothing. The work waits as for someone out of reach.
	Int orderKind = Silhouette:DLL.OrderKind(aiOrder)
	If orderKind != 1 && orderKind != 4 && a.IsDead()
		Silhouette:DLL.OrderGone(aiOrder)
		Return
	EndIf

	If orderKind == 5 && Busy(a)
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
	If LeftDead(target)
		Return
	EndIf
	Debug.Notification(Silhouette:DLL.PickerStart(target))
	Act()
EndFunction

; The dead are not shaped (RunOrder): a picker or a menu button on one says so instead of promising a change.
Bool Function LeftDead(Int aiTarget)
	Actor a = Game.GetForm(aiTarget) as Actor
	If a && a.IsDead()
		Debug.Notification("Silhouette leaves the dead alone: " + Silhouette:DLL.NameOf(aiTarget) + " keeps their body.")
		Return True
	EndIf
	Return False
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
	If LeftDead(target)
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
	If LeftDead(target)
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
