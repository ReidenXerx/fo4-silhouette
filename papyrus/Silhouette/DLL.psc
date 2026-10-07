Scriptname Silhouette:DLL Native Hidden
{What Papyrus can ask of Silhouette.dll (decisions S-18, S-46). Native functions have
 to live in a script flagged Native, which a Quest script cannot be, so they sit here
 and Silhouette:Bridge and Silhouette:API call them by name. ("Native" itself is a
 reserved word, hence the name.)

 The direction is one way: Papyrus calls in, the plugin never calls out (dispatching
 into the VM from a plugin crashed Rapport twice in DispatchMethodCallImpl). The
 plugin decides every body; the bridge does what it decides through LooksMenu's
 BodyGen, which has no other door.

 Actors travel as form ids (Int). Another mod should call Silhouette:API, not these:
 this surface follows the bridge and changes with it -- ProtocolVersion says how.}

; ---- the plugin ------------------------------------------------------------
Bool Function IsReady() Global Native         ; a catalog that matches the BodyGen files is loaded
String Function Status() Global Native        ; one line: catalog, events, queue, records
String Function Version() Global Native
Int Function ProtocolVersion() Global Native  ; what RunOrder must do; the bridge checks it
Int Function Stamp() Global Native            ; the build's marker stamp, 0 without a catalog
String Function Build() Global Native
Function Configure(Bool abORefit, Bool abNipples, Bool abGenitals, Bool abFactionPools) Global Native
Int Function Pending() Global Native          ; orders that can go out now, or in flight
Function Log(String asLine) Global Native     ; into Silhouette.log

; Main thread: turns what the event sinks saw (actors loading, dressing) into
; decisions. The bridge calls it once per poll.
Function Pump() Global Native

; ---- orders (protocol 3): exactly this, in this order ----------------------
; NextOrder -> OrderActor: no actor in memory -> OrderGone, stop.
; -> (OrderKind 5, a touch-up, while busy in another mod's scene -> OrderDefer, stop)
; -> (Regenerates: busy -> OrderDefer, stop; else read the keyed values, then OrderActor
;    and busy again -> gone: stop / busy: OrderDefer, stop; else regenerate)
; -> (Probes: NoteName each morph; MarkerKind 1 or 3 -> NoteMarker of the unkeyed
;    value, 2 -> NoteMarker of the refit keyword's value) -> (ReadsAll: NoteLayer)
; -> OrderReadCount / OrderReadMorph / NoteRead, stopping once OrderReadsDone
; -> Prepare -> OrderActor again -> (OrderClearsUnkeyed) (OrderClearsRefit)
; -> OrderWriteCount / OrderWriteMorph / OrderWriteValue / OrderWriteLayer
; -> (OrderUpdates) -> OrderDone.
Int Function NextOrder() Global Native        ; 0: nothing to do now
Int Function OrderActor(Int aiOrder) Global Native   ; 0: the order is gone (a load forgot it)
Int Function OrderKind(Int aiOrder) Global Native    ; 1 probe, 2 body, 3 refit, 4 snapshot, 5 touch-up
Bool Function OrderFemale(Int aiOrder) Global Native
Bool Function OrderRegenerates(Int aiOrder) Global Native
Bool Function OrderProbes(Int aiOrder) Global Native
Bool Function OrderReadsAll(Int aiOrder) Global Native
Function NoteName(Int aiOrder, String asMorph) Global Native
; 0 none, 1 body marker (unkeyed), 2 refit marker (refit keyword), 3 choice marker (unkeyed)
Int Function MarkerKind(String asMorph) Global Native
Function NoteMarker(Int aiOrder, String asMarker, Float afValue) Global Native
Int Function OrderReadCount(Int aiOrder) Global Native
String Function OrderReadMorph(Int aiOrder, Int aiIndex) Global Native
Function NoteRead(Int aiOrder, Int aiIndex, Float afValue) Global Native
Bool Function OrderReadsDone(Int aiOrder) Global Native  ; the reads so far answer it: stop reading
Function NoteLayer(Int aiOrder, String asMorph, Float afValue) Global Native
Bool Function Prepare(Int aiOrder) Global Native
Bool Function OrderClearsUnkeyed(Int aiOrder) Global Native
Bool Function OrderClearsRefit(Int aiOrder) Global Native
Int Function OrderWriteCount(Int aiOrder) Global Native
String Function OrderWriteMorph(Int aiOrder, Int aiIndex) Global Native
Float Function OrderWriteValue(Int aiOrder, Int aiIndex) Global Native
Int Function OrderWriteLayer(Int aiOrder, Int aiIndex) Global Native  ; 0 unkeyed, 1 the refit keyword
Bool Function OrderUpdates(Int aiOrder) Global Native
Function OrderDone(Int aiOrder, Bool abOk) Global Native
Function OrderGone(Int aiOrder) Global Native   ; not in memory: the work waits for their next sighting
Function OrderDefer(Int aiOrder) Global Native  ; busy in another mod's scene: tried again later

; ---- events for the bridge to raise (S-24) ---------------------------------
Int Function NextEvent() Global Native        ; 0: none
Int Function EventKind(Int aiEvent) Global Native   ; 1 generated, 2 naked, 3 removing clothes, 4 ORefit changed
Int Function EventActor(Int aiEvent) Global Native
String Function EventPreset(Int aiEvent) Global Native
Bool Function EventFlag(Int aiEvent) Global Native
Function EventDone(Int aiEvent) Global Native  ; raised: only now is an announcement remembered as made
String Function NextNotice() Global Native     ; a line for the player's screen (S-71), "" when none
String Function BodyWarning() Global Native    ; which sex has no supported body, once a launch; "" when none

; ---- the NPC picker (S-22, S-47) ---------------------------------------------
Int Function CrosshairActor(Float afRecentSeconds) Global Native  ; main thread
; S-79: the free camera in front of someone (their feet, heading in degrees, height), and back to the
; player's camera. "" when it is there, "wait" while the game carries out "tfc" (ask CameraStep again in a
; moment), else why not. Main thread.
String Function CameraFrame(Float afX, Float afY, Float afZ, Float afAngle, Float afHeight) Global Native
String Function CameraRestore() Global Native
String Function CameraStep() Global Native
String Function PickerStart(Int aiActor) Global Native            ; main thread
String Function PickerStep(Int aiStep) Global Native
String Function PickerShow(String asPreset) Global Native   ; S-79: try this preset on, by name
String Function PickerPresets() Global Native               ; "name<TAB>kind|..." (y yours, p pool, o other)
Int Function PickerIndex() Global Native                    ; the preset tried on, -1 none
String Function PickerCurrent() Global Native               ; what they had at Pick, "" not read yet
Bool Function PickerFemale() Global Native                  ; the sex the picking lists presets for
Bool Function BodyFemale(Int aiActor) Global Native  ; S-86: the sex of the body they wear (Servitrons: female, flagged male)
Bool Function BodySupported(Bool abFemale) Global Native    ; S-78: a body Silhouette supports is installed for the sex
; S-79 (0.3.3), the window's Me tab: the catalog's presets for the player, your own among them, and each one's body.
String Function PlayerPresets(Bool abFemale) Global Native                  ; "name<TAB>kind|..." as PickerPresets
Int Function PlayerPresetIndex(String asPreset, Bool abFemale) Global Native ; its place in that list, -1 none
Int Function PlayerBodyCount(String asPreset, Bool abFemale) Global Native   ; morphs to write, marker last; 0 = no such preset
String Function PlayerBodyMorph(String asPreset, Bool abFemale, Int aiIndex) Global Native
Float Function PlayerBodyValue(String asPreset, Bool abFemale, Int aiIndex) Global Native
String Function PickerKeep() Global Native
String Function PickerCancel() Global Native
Int Function PickerTarget() Global Native
Bool Function PickerReady() Global Native

; ---- what Silhouette:API offers ---------------------------------------------
Bool Function CanShape(Int aiActor) Global Native                 ; main thread
String Function NameOf(Int aiActor) Global Native                 ; main thread
String Function AssignedPreset(Int aiActor) Global Native
String Function PresetForMarker(String asMarker, Float afStamp) Global Native  ; the marker's value; below 1 = being written (S-58)
Int Function PresetCount(Bool abFemale) Global Native
String Function PresetName(Bool abFemale, Int aiIndex) Global Native
; Main thread. Each answers "" when it accepted the request, or why it did not.
; aiLane (S-55): 0 the player's own actions, 1 other mods and rules, 2 bulk work.
String Function RequestPreset(Int aiActor, String asPreset, Int aiSource, Int aiLane) Global Native  ; source 3 picker, 4 another mod
String Function RequestRegenerate(Int aiActor, Int aiLane) Global Native
String Function RequestReset(Int aiActor, Int aiLane) Global Native
String Function RequestReapply(Int aiActor, String asMarkerPreset, Int aiLane) Global Native
String Function RequestAdopt(Int aiActor) Global Native  ; the regeneration window (S-15), bulk lane
; MCM's "Reset everyone" (S-68): a fresh start, picks included. Answers what happened, or "not done: <why>".
String Function ResetEveryone() Global Native
String Function FreshStart() Global Native  ; S-70: Reset everyone for a save new to Silhouette; "" = done, else why not
Bool Function IsORefitEnabled() Global Native
Bool Function IsORefitApplied(Int aiActor) Global Native  ; what this session has seen; the API reads LooksMenu
Function SetORefit(Bool abOn) Global Native
Function SetNippleRand(Bool abOn) Global Native
Function SetGenitalRand(Bool abOn) Global Native
String Function Describe(Int aiActor) Global Native
String Function LastError() Global Native  ; the last refusal of any caller: prefer the Request* answer
