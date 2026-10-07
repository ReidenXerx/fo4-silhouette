Scriptname Silhouette:API Hidden
{Silhouette for other mods (decision S-24): OBody NG's functions, by OBody's names,
 as global functions. Every one is safe to call whatever is installed: without
 Silhouette.dll they do nothing and say so through their return value.

 Events: register on the bridge quest. Built against the decompiled base sources
 (Papyrus Compiler from F4SE's scripts, most setups), register the mangled name:
     RegisterForCustomEvent(Silhouette:API.Bridge(), "silhouette:bridge_OnActorGenerated")
 Built against the Creation Kit's own sources, the plain name is what compiles:
     RegisterForCustomEvent(Silhouette:API.Bridge(), "OnActorGenerated")
 Either way the handler is
     Event Silhouette:Bridge.OnActorGenerated(Silhouette:Bridge akSender, Var[] akArgs)
         Actor who = akArgs[0] as Actor
         String preset = akArgs[1] as String
     EndEvent
 OnActorGenerated [Actor, String preset], OnActorNaked [Actor],
 OnActorRemovingClothes [Actor], OnORefitChanged [Actor, Bool applied].

 A body change asked for here is carried out by the bridge a moment later, not
 inside the call: LooksMenu is reached through Papyrus, one frame per value.
 GetPresetAssignedToActor already answers with the decision; OnActorGenerated says
 when the body is on them.

 Every argument is passed explicitly: the base sources carry no default values.}

; The order protocol these scripts were built for (Silhouette:Bridge.Protocol, the DLL's
; kProtocol): all three move together -- tools/tests/test_protocol.py holds them to it.
Int Function Protocol() Global
	Return 9
EndFunction

; Other mods' requests wait behind the player's own actions and go before bulk work (S-55).
Int Function LaneOtherMods() Global
	Return 1
EndFunction

; Silhouette.esp's bridge quest, for RegisterForCustomEvent. None without the plugin.
Silhouette:Bridge Function Bridge() Global
	If !Game.IsPluginInstalled("Silhouette.esp")
		Return None
	EndIf
	Return Game.GetFormFromFile(0x802, "Silhouette.esp") as Silhouette:Bridge
EndFunction

; Silhouette.dll is loaded and from the same release as these scripts. Nothing
; below calls it otherwise: a missing or mismatched native only fills the log.
Bool Function Loaded() Global
	If F4SE.GetPluginVersion("Silhouette") <= 0
		Return False
	EndIf
	Return Silhouette:DLL.ProtocolVersion() == Protocol()
EndFunction

; LooksMenu: the only door to the morph store. Its F4SE plugin registers as "F4EE" on old-gen (LooksMenu
; 1.6) and as "Fallout 4 Engine Extender" on the Anniversary build's (1.7, f4se.log 2026-09-26): either.
; Every script of Silhouette asks here, so the names live in one place (S-75).
Bool Function LooksMenuLoaded() Global
	Return F4SE.GetPluginVersion("F4EE") > 0 || F4SE.GetPluginVersion("Fallout 4 Engine Extender") > 0
EndFunction

; MCM, by the name it registers with F4SE ("F4MCM", as f4se.log shows it) -- not its
; file name. "MCM" is asked too, for a build that ever registers under it.
Bool Function McmInstalled() Global
	Return F4SE.GetPluginVersion("F4MCM") > 0 || F4SE.GetPluginVersion("MCM") > 0
EndFunction

; The plugin is loaded, its catalog matches the BodyGen files, LooksMenu is there and
; the bridge exists: a change asked for now is carried out.
Bool Function IsReady() Global
	Return Loaded() && LooksMenuLoaded() && Silhouette:DLL.IsReady() && Bridge() != None
EndFunction

; Why the last refusal here said no -- the last of ANY caller's: read it right after
; the call that returned False.
String Function LastError() Global
	If F4SE.GetPluginVersion("Silhouette") <= 0
		Return "Silhouette.dll is not loaded"
	EndIf
	If !Loaded()
		Return "Silhouette.dll is from another release than its scripts"
	EndIf
	If !LooksMenuLoaded()
		Return "LooksMenu is not loaded: no body can be shaped"
	EndIf
	If Bridge() == None
		Return "Silhouette.esp is not enabled: nothing would carry a change out"
	EndIf
	If !Silhouette:DLL.IsReady()
		Return Silhouette:DLL.Status()
	EndIf
	Return Silhouette:DLL.LastError()
EndFunction

; One message: what is loaded, what it listens to, how much work waits. For the MCM,
; and for anyone asking why nothing happens -- it works without Silhouette.esp.
Function ShowStatus() Global
	String esp = " Silhouette.esp is enabled."
	If !Game.IsPluginInstalled("Silhouette.esp")
		esp = " Silhouette.esp is NOT enabled: without it nothing carries the plugin's decisions out."
	EndIf
	If !LooksMenuLoaded()
		Debug.MessageBox("Silhouette: LooksMenu is not loaded, so no body can be shaped: its BodyGen gives every body, and it is the only way to change one." + esp)
		Return
	EndIf
	If F4SE.GetPluginVersion("Silhouette") <= 0
		Debug.MessageBox("Silhouette: Silhouette.dll is not loaded. BodyGen still gives everyone a body; the rules by name and faction, ORefit, the NPC picker and the API are off." + esp)
		Return
	EndIf
	If !Loaded()
		Debug.MessageBox("Silhouette: Silhouette.dll and its scripts are from different releases. Install one release's files together." + esp)
		Return
	EndIf
	Debug.MessageBox("Silhouette " + Silhouette:DLL.Version() + ": " + Silhouette:DLL.Status() + "." + esp)
EndFunction

Bool Function IsFemale(Actor akActor) Global
	If !akActor
		Return False
	EndIf
	Return akActor.GetLeveledActorBase().GetSex() == 1
EndFunction

; The preset the actor has: the one Silhouette decided (even if the bridge has not
; made it yet), else the one their body's marker names. "" for none, or for a body
; Silhouette did not give.
String Function GetPresetAssignedToActor(Actor akActor) Global
	If !akActor || !Loaded()
		Return ""
	EndIf
	String decided = Silhouette:DLL.AssignedPreset(akActor.GetFormID())
	If decided != ""
		Return decided
	EndIf
	Return MarkerPreset(akActor)
EndFunction

; What their marker names, read from LooksMenu now.
String Function MarkerPreset(Actor akActor) Global
	If !akActor || !Loaded() || !LooksMenuLoaded()
		Return ""
	EndIf
	Bool female = IsFemale(akActor)
	String[] morphs = BodyGen.GetMorphs(akActor, female)
	Int count = 0
	If morphs
		count = morphs.Length
	EndIf
	Int i = 0
	While i < count
		If Silhouette:DLL.MarkerKind(morphs[i]) == 1
			Float v = BodyGen.GetMorph(akActor, female, morphs[i], None)
			If v > 0.0
				Return Silhouette:DLL.PresetForMarker(morphs[i], v)
			EndIf
		EndIf
		i += 1
	EndWhile
	Return ""
EndFunction

; Every preset that fits this actor's body, as the pickers offer them.
String[] Function GetAllPossiblePresets(Actor akActor) Global
	String[] out = new String[0]
	If !akActor || !Loaded()
		Return out
	EndIf
	Bool female = IsFemale(akActor)
	Int n = Silhouette:DLL.PresetCount(female)
	Int i = 0
	While i < n && i < 128
		out.Add(Silhouette:DLL.PresetName(female, i), 1)
		i += 1
	EndWhile
	Return out
EndFunction

; Gives the actor this preset, kept like a choice made in the picker: rules do not
; override it, and it is marked in LooksMenu beside the body (S-51). False (and
; LastError says why) for a preset that does not fit them.
Bool Function AssignPresetToActor(Actor akActor, String asPreset) Global
	If !akActor || !IsReady()
		Return False
	EndIf
	Return Silhouette:DLL.RequestPreset(akActor.GetFormID(), asPreset, 4, LaneOtherMods()) == ""
EndFunction

Bool Function ApplyPresetByName(Actor akActor, String asPreset) Global
	Return AssignPresetToActor(akActor, asPreset)
EndFunction

; A new body, as if met for the first time: BodyGen rolls, the rules get their say --
; a rule with several presets draws again, and lands on another of them -- and other
; mods' keyed morphs stay. Waits while AAF has them in a scene; a save before it is
; carried out does not lose it.
Bool Function GenActor(Actor akActor) Global
	If !akActor || !IsReady()
		Return False
	EndIf
	Return Silhouette:DLL.RequestRegenerate(akActor.GetFormID(), LaneOtherMods()) == ""
EndFunction

; Takes Silhouette's body off: they are bare now, and get a new body when a save is
; next loaded (S-53) -- from LooksMenu's BodyGen when nothing else is stored on
; them, from Silhouette when another mod's morphs are.
Bool Function ResetActorMorphs(Actor akActor) Global
	If !akActor || !IsReady()
		Return False
	EndIf
	Return Silhouette:DLL.RequestReset(akActor.GetFormID(), LaneOtherMods()) == ""
EndFunction

Bool Function ResetActorOBodyMorphs(Actor akActor) Global
	Return ResetActorMorphs(akActor)
EndFunction

; The body they have, again, with this build's values and their own variety.
Bool Function ReapplyActorMorphs(Actor akActor) Global
	If !akActor || !IsReady()
		Return False
	EndIf
	Return Silhouette:DLL.RequestReapply(akActor.GetFormID(), MarkerPreset(akActor), LaneOtherMods()) == ""
EndFunction

Bool Function ReapplyActorOBodyMorphs(Actor akActor) Global
	Return ReapplyActorMorphs(akActor)
EndFunction

; ORefit on or off: the same switch as MCM > Silhouette. With MCM installed it is
; kept in MCM's settings; without MCM it lasts until the game is closed.
Function SetORefit(Bool abEnabled) Global
	If !Loaded()
		Return
	EndIf
	If McmInstalled()
		MCM.SetModSettingBool("Silhouette", "bORefit:General", abEnabled)
	EndIf
	Silhouette:DLL.SetORefit(abEnabled)
EndFunction

Bool Function IsORefitEnabled() Global
	Return Loaded() && Silhouette:DLL.IsORefitEnabled()
EndFunction

; The refit marker under Silhouette.esp's refit keyword (0x803), read from LooksMenu
; now: 0 when no refit is on them, 0.25 while one is being written, else a whole
; number -- EVEN under heavy clothes (armour, jackets: nipples flattened), ODD under
; light ones (S-40, S-50).
Float Function RefitMarkerValue(Actor akActor) Global
	If !akActor || !LooksMenuLoaded() || !Game.IsPluginInstalled("Silhouette.esp")
		Return 0.0
	EndIf
	Keyword refit = Game.GetFormFromFile(0x803, "Silhouette.esp") as Keyword
	If !refit
		Return 0.0
	EndIf
	Return BodyGen.GetMorph(akActor, IsFemale(akActor), "Silhouette_Refit", refit)
EndFunction

; ORefit's floors are on them now (clothes on, ORefit on).
Bool Function IsORefitApplied(Actor akActor) Global
	Return RefitMarkerValue(akActor) >= 1.0
EndFunction

; They wear something heavy -- armour, a jacket, a coat -- and ORefit has flattened
; their nipples under it. A mod raising nipples (arousal) holds them flat while this
; is true (S-49): the refit's floor loses to a higher value anywhere else.
Bool Function IsHeavilyDressed(Actor akActor) Global
	Float v = RefitMarkerValue(akActor)
	Return v >= 1.0 && (v as Int) % 2 == 0
EndFunction

; Nipple variety in the bodies Silhouette gives from now on. BodyGen's own rolls
; come from the generated files and always carry it. Kept like SetORefit.
Function SetNippleRand(Bool abEnabled) Global
	If !Loaded()
		Return
	EndIf
	If McmInstalled()
		MCM.SetModSettingBool("Silhouette", "bNippleRand:General", abEnabled)
	EndIf
	Silhouette:DLL.SetNippleRand(abEnabled)
EndFunction

; Genital shape variety (women) and ball size (men), as SetNippleRand. Never the shaft.
Function SetGenitalRand(Bool abEnabled) Global
	If !Loaded()
		Return
	EndIf
	If McmInstalled()
		MCM.SetModSettingBool("Silhouette", "bGenitalRand:General", abEnabled)
	EndIf
	Silhouette:DLL.SetGenitalRand(abEnabled)
EndFunction

; S-79: opens the picker window on the NPC aimed at, or on the player. From the console:
; cgf "Silhouette:API.OpenWindow"
Function OpenWindow() Global
	Silhouette:Bridge b = Bridge()
	If b
		b.OpenWindow()
	EndIf
EndFunction

; S-79: closes the picker window as Cancel does. From the console, if the window ever takes no input:
; cgf "Silhouette:API.CloseWindow"
Function CloseWindow() Global
	Silhouette:Bridge b = Bridge()
	If b
		b.CloseWindow()
	EndIf
EndFunction
