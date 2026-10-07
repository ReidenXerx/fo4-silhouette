# Decisions

Settled by the owner unless marked otherwise. Each carries the evidence it rests on, so it can be
re-opened only by evidence that addresses that.

## S-1 — The name is Silhouette

Owner poll, 2026-09-23. It replicates OBody NG's feature set in Fallout 4 under our own naming.

## S-2 — Generator first, then plugin

Owner poll, 2026-09-23. **Phase 1** writes LooksMenu BodyGen files and has no runtime code at all;
**Phase 2** is an F4SE plugin (the Rapport pattern) for what needs run time — the picker, ORefit,
run-time faction rules, events. Phase 1's measuring code is the specification Phase 2 reuses.

Why it is safe to start without a plugin: LooksMenu's BodyGen is already installed and enabled
(`f4ee.ini`: `bEnable=1`, `bEnableBodyGen=1`) and was never fed. Its `Loose` folder is read whatever
plugins are loaded, so Phase 1 ships no `.esp`.

## S-3 — v1 is OBody parity, all four areas

Owner poll, 2026-09-23: random + persistent distribution; rules config + blacklists; in-game picker;
ORefit + nipple/genital variety. Mapping and phase of each piece: [obody-feature-map.md](obody-feature-map.md).

## S-4 — Partial-fit presets are included, if they work without issues

Owner poll, 2026-09-23: *"I would pick 3 if it's still work with no issues."*

Partial fit = 50–95% of a preset's sliders exist on the installed body, **and** the preset declares
this body's family (or none). Measured here: 14 presets, e.g. TWB variants at 77–83% (TWB is built on
CBBE, so the shared names are the same shapes). A Fusion Girl preset overlapping CBBE by 53% through
coincident names is excluded by the family rule.

- **Technically verified**: LooksMenu stores unknown morph names and never moves anything for them;
  the generator does not even write them. The shared sliders land exactly (verify_bodygen.py).
- **Still owed**: an in-game look at partial-fit NPCs, with screenshots to the owner, before this is
  called settled. Switch: `--no-partial`.

## S-5 — Base bodies are built zeroed; the tool measures and says so

**Owner decision, 2026-09-23 (poll): "I'll rebuild zeroed."** The standard BodyGen setup: FemaleBody
and MaleBody (and the outfits) are built with zeroed sliders, and templates hold **absolute** preset
values.

**Why it had to be asked.** BodyGen adds morphs on top of the mesh on disk. `tools/base_body.py` fits
the built `.nif` against BodySlide's reference mesh using the `.tri` diffs, and on this machine found
the bases were NOT zeroed: FemaleBody = "CBBE Chubby" on CBBE Body Physics (0.005% of the
displacement unexplained), MaleBody = "BT - Average" on BodyTalk4 (0.015%). Every NPC would have been
off its preset by RMS 0.97 units (female, up to 3.5) and 0.69 (male). Nothing on disk said so.

**What the tool does about it.** It measures the base on every run. A base that is not zeroed gets a
loud warning naming the exact BodySlide body and zeroed preset to rebuild with, and
`verify_bodygen.py` fails in one line per body until the rebuild is done.

**The alternative, kept as an option.** `--compensate` writes every template as `target − baked`.
LooksMenu applies `vertex += diff × value` with no clamp and parses values with `atof`, so this is
exact: all 65 bodies verified within 0.031 units (half a half-float step). It was offered and not
chosen because it ties the files to the current build — rebuild with another preset and NPCs already
met in a save stay off until they are re-derived — and a zeroed base is what every other BodyGen tool
expects. It stays for installs that cannot be rebuilt.

## S-6 — Every template carries a marker morph named after itself

Agent decision, 2026-09-23, from LooksMenu's source.

LooksMenu runs BodyGen for an actor only while it has **no stored morphs**, and skips zero values
before storing. So a roll that sets nothing is re-rolled on every load — with S-5 that is exactly the
roll of the baked preset itself. The marker (`Silhouette_<Preset>@1`) is a morph no body has: stored,
never moves a vertex, makes every roll permanent, and records which preset the NPC got. Phase 2's
`GetPresetAssignedToActor` reads it back, including for NPCs rolled in Phase 1.

## S-7 — The player is never randomised: an average body by default, a picker in MCM

**Owner request, 2026-09-23**, replacing an earlier agent decision that left the player untouched:
*"make for player picker from installed presets applicable to him via MCM menu and by default use
average body for both sex of player."*

`All|...|HumanRace` includes the Player record (`Fallout4.esm` `0x7`), so without a rule of their own
a player with no LooksMenu body sliders would be randomised on the next load. `Fallout4.esm|7|Female`
and `|Male` each name ONE template: the **most average** preset (S-10). LooksMenu itself skips a
character that already has body sliders, so an existing LooksMenu look is never overwritten by this.

MCM > Silhouette > Your character: a dropdown per sex of every preset that fits (zeroed ones
included, as OBody's menu shows presets blacklisted from random distribution), and buttons *Apply to
my character*, *Back to the default* (`BodyGen.RegenerateMorphs` — BodyGen's own answer, so there is
one source of truth) and *Which body do I have?* (reads the marker back with `BodyGen.GetMorphs`).

## S-8 — Zeroed presets stay out of random distribution

Parity with OBody NG's shipped config (`blacklistedPresetsFromRandomDistribution`: "Zeroed Sliders",
"HIMBO Zero for OBody", …). Decided by **values** rather than names, so FO4's "CBBE Zeroed Sliders"
and "BT - Zero" are caught without a list.

## S-9 — The player picker needs no plugin

Agent decision, 2026-09-23. MCM buttons can call a **global** Papyrus function
(`"type": "CallGlobalFunction"`) — the shape Rapport's "Show recent narration" already uses in this
install — and dropdowns store an index in a `ModSettingInt`. So `Silhouette:Player` is a script with
global functions and nothing else; there is no quest, no alias and no `.esp`. The preset values are
generated into it, by the same function that writes the BodyGen templates, and `verify_bodygen.py`
checks the two agree preset by preset. The cost: the menu is as fresh as the last generator run, like
the templates. Phase 2 can make both live.

## S-10 — "Average" is measured, in vertex space

Agent decision, 2026-09-23. The average body is the full-fit preset whose built body lies closest,
RMS over the vertices, to the **mean body** of the random pool — the pool's medoid. For men that is
"BT - Average", the preset its author named average: the check that the definition means what the
word means. For women, whose collection has no preset called average, it is "xy - Type 3DCG
(Blessed)(2)(a)" (0.467 units from the mean body). "CBBE Vanilla" was 48th of 58, so "vanilla" and
"average" are not the same thing here. Partial fits are not candidates until S-4 is settled in game.

## S-11 — Rules are OBody's config, compiled in OBody's order

Agent decision, 2026-09-23, from OBody's source. The config file and includes use OBody's keys and
shapes unchanged, so an OBody user needs no new vocabulary. Priority is the one
`OBody::GenerateActorBody` actually implements — per-NPC blacklist, per-NPC preset, plugin/race
blacklist, faction, plugin, race, random — not the one commonly summarised; BodyGen expresses
priority only as line order, so the lines are written in reverse (`tools/rules.py`).

What a morphs.ini line cannot name — an NPC's display name (`npc`, `blacklistedNpcs`) or a faction
(`faction*`) — is read, validated and reported as pending, not guessed: base-game names are localised
into string tables inside `Fallout4 - Interface.ba2`, and the Phase 2 plugin can ask the game
directly. ORefit keys are reported the same way.

One addition, `distributeRaces` (default `["HumanRace"]`): OBody gives a body to every NPC race; in
Fallout 4 only races that wear the human body should, and naming them is the only way a BodyGen line
reaches them.

## S-12 — Every marker carries the build's stamp; a manifest says what it means

Agent decision, 2026-09-23, from the microscope review (judgment lens). A marker's NAME is a
sanitised template name, which loses the exact preset name, and its VALUE was a constant 1.0 —
yet LooksMenu stores any non-zero value and moves no vertex for it. The value is now the build's
stamp: the first 24 bits of a hash of everything the files say (exact in float32), and
`F4SE/Plugins/Silhouette/manifests/<stamp>.json` records, for every marker of that build, the exact
preset, its file and the values written. Manifests are never deleted: an NPC rolled by a build
carries its stamp for the rest of that save, and Phase 2 can interpret it. This had to happen
before the first real save, because a roll cannot be re-stamped afterwards.

## S-13 — The character-creation dummies get the player's default

Agent decision, 2026-09-23, found by two review lenses independently. See bodygen-format.md "A new
game clones a dummy onto the player". Without their lines, S-7 fails on every new game. The spouse
in the intro therefore wears the player's default body too.

## S-14 — "Zeroed" is about BODY sliders, and some builds cannot follow a body at all

Agent decision, 2026-09-23. A zeroed build puts every body slider (a morph the body's `.tri` has)
at 0 and leaves an outfit's own sliders (FootShape, OFFSET, ...) at their authored defaults. A
preset's name proves nothing: "CBBE Zeroed Sliders" names one slider, so on a BodyTalk set it
leaves 26 body sliders at 100. `audit_builds.py` also reports STATIC builds — zeroed but unable to
follow any body: a set made for another body family, or an empty `.tri` because the mod ships the
wrong slider data. On the owner's install four were fixed by building a CBBE variant instead of a
Fusion Girl one or a broken one; two have no alternative (FurbyKnight 1st-person sleeves, BodyTalk4
Suit Clean).

## S-15 — A 24-in-game-hour regeneration window for people other mods marked first

**Owner, 2026-09-23:** *"Maybe we need kinda overwrite mod? That temporary will regenerate them and
that aaf set morphs?"* ... *"this regeneration feature will be kinda enable on 24 in game hour and
after that it will automatically disabled."*

LooksMenu runs BodyGen only for an actor with no stored morphs at all, so anyone another mod marked
before Silhouette arrived never gets a body (measured: a Diamond City guard, `000F61B6`, holding
AAF's `Erection` under `AAF_MorphKeyword`). AAF and other mods write under their OWN keywords, so
the fix can keep their morphs:

- `Silhouette.esp` (flagged light, no load-order slot): one self-starting quest running
  `Silhouette:Adopter`, and a form list of people already handled.
- The window opens on the first load with Silhouette and closes itself after 24 in-game hours;
  MCM opens a new one and reports its status.
- Every 10 seconds while it is open, people around the player who hold ONLY keyed morphs are
  rolled by BodyGen (`RegenerateMorphs` -- the same rules, blacklists and player lines as everyone
  else) and their keyed morphs are put back. Left alone: anyone with an unkeyed value (a
  Silhouette body, or sliders set by hand), anyone AAF has busy (`AAF_ActorBusy`) or locked
  (`AAF_ActorLocked`), anyone already handled.
- This is Silhouette's only plugin; everything else still works without it (S-9).

## S-16 — A runtime STATE is never part of a body

Agent decision, 2026-09-23, from the rapport session's co-save reading: two men carried
`Erection=1` in the UNKEYED layer, rolled from `Sirius_Male_preset`, which sets Erection at 100%.
AAF raises Erection under its own keyword for a scene and takes it away after. A preset that sets
it writes it where nothing ever takes it away, so those two had it for good.

- `STATE_MORPHS` in `silhouette_gen.py` lists the morphs something drives at runtime: Erection,
  Erection Up/Down and CErection (AAF), plus VaginaPenetrate and AnusPenetrate (the opening
  sliders of the genital body in `fo4-anatomy`). They are never written into a template or the
  picker, and never compensated either; the base's own state is not ours to change. The
  generator says which presets lost one.
- `verify_bodygen.py` fails any template that sets one. It was proven on the unfixed files first
  (1 failure: Sirius). It also compares each body with its preset MINUS the states.
- Bodies an older build already gave are healed by the regeneration window (S-15). Only the
  unkeyed value of a state morph goes: `SetMorph(..., None, 0.0)` erases exactly that key, per
  LooksMenu's `UserValues::SetValue`. Only bodies carrying a Silhouette marker are touched, so a
  state another mod keeps under its keyword, or one set by hand on a body that is not ours, stays.
  "Refresh the people around me" fixes them as well, because Give re-applies the preset.
- Build dffb550bfd66, stamp 14678869. Its manifest joins the old one; manifests are never deleted
  (S-12).

A related question from the same report: does an actor whose last keyed morph was removed keep
BodyGen away for good? No. The emptied entry lasts until the next save and load. LooksMenu does
not load an empty morph map (`BodyMorphInterface::Load`: `if(morphValueMap->empty()) return
true;`, and `MorphValueMap::Load` skips a morph with no values). BodyGen then rolls the actor as
new, because `ActorUpdateManager` evaluates only when `GetMorphMap` returns null.

## S-17 — Genital shape variety per woman (owner poll, 2026-09-23)

The owner chose, for the labia: jiggle and react (fo4-anatomy A-13), change their look, and vary
per woman. The anatomy body carries Nahka's genital SHAPE sliders (VaginaLabiaSize, VaginaInnie,
VaginaInnie2, VaginaSize, VaginaNarrower, VaginaClitSize, AnusDonut, AnusBack) in its .tri, so
BodyGen can roll them.
- `tools/genital_shapes.json` holds `{morph: [low, high]}`. Every female template gets
  `Morph@low:high` for the morphs the female body has, so LooksMenu rolls each woman her own shape.
  The centre is the owner's chosen look, picked in BodySlide on "Anatomy Body ZeX". It is empty
  until then, and with it empty the generator's output is unchanged.
- Runtime states are refused by name (S-16). VaginaSpread and ButtcheeksSpread join STATE_MORPHS:
  like VaginaPenetrate and AnusPenetrate, they are what scenes animate, never a body.
- `verify_bodygen.fixed_values` sets these ranges aside and compares the rest of each template with
  its preset, so the body check still covers every template.
- AnusBack stays out of the random variety for now. It moves the anal ring ~0.85 back toward ZeX's
  anus bones, so it is an alignment control for fo4-anatomy's anus decision, not a taste.

**Kept by the owner, 2026-09-24, after fo4-anatomy measured it** ("i think lets keep it as it is and i
will test in game and maybe in future we return to it"). fo4-anatomy built each range's edge the way
LooksMenu applies it and ran its fit check (the game's colliders and weights, six paths each): nothing
breaks -- clipping depth stays ~1.5 as on the all-zero body, stretch within the approved maximum. What
grows is how much of a bigger vulva a shaft passes through in contact, against the 31% the owner approved
before: VaginaSize +0.35 39%, VaginaInnie +0.40 33%, every edge at once 44% (the rest at or below 28%;
VaginaSize -0.35 16%). The measured alternative, if the in-game test calls for it: VaginaSize
-0.35..+0.15 and VaginaInnie 0..0.30, which keeps every woman at or under 31% (35% at every edge).

## S-18 — Phase 2: the plugin decides, a Papyrus bridge applies, BodyGen keeps distributing

Owner poll, 2026-09-23: production-ready means **full v1 with the F4SE plugin**. Agent decisions on
its shape, from the sources (the map: [phase2.md](phase2.md)):
- LooksMenu offers other plugins no C++ interface: `f4ee/exports.def` exports only F4SE's entry
  points, and its message handler answers only F4SE's own messages. Its Papyrus `BodyGen` is the
  only door to the morph store.
- A plugin must not dispatch into the Papyrus VM. Rapport crashed twice in
  `DispatchMethodCallImpl`, because F4SE tasks run on a BSJobs thread and the VM packs arguments
  through the per-thread scrap heap. So `Silhouette.dll` decides and queues, and
  `Silhouette:Bridge` (one quest script, ONE timer, per Rapport's scar) asks and does every
  LooksMenu call. MCM hotkeys call the bridge directly, so the picker previews instantly.
- BodyGen keeps doing random distribution. It applies a body at load, before anyone looks, and a
  plugin reaching LooksMenu through Papyrus arrives a poll later. The plugin adds only what a file
  cannot express.
- Built against alandtse/CommonLibF4 (the commit Rapport runs, `ba22620e`), OG 1.10.163 only,
  refusing any other runtime.

## S-19 — The generator stays the compiler; the plugin reads a catalog

**Amended by the first microscope wave: the build names the bodies, a rules hash names the rules.** Both
BodyGen files state "Build <hex>, marker stamp <n> (<mode>), rules <hex>", the catalog carries the same
rules hash, and the plugin refuses the set when any of the three differs: two runs with different rules
had produced the same build. The build scripts run the plugin's own parser on the generated files
(`SilhouetteTests.exe --check`), and the generator refuses a config value of the wrong shape, so what
the game would refuse at load is refused before anything is deployed.

Agent decision, 2026-09-23. `silhouette_gen.py --write` also writes
`F4SE/Plugins/Silhouette/catalog.json`: every preset that fits, per sex, with its final values and
its classification; the player defaults; the compiled rules, including the tiers BodyGen already
carries, so the plugin knows when NOT to act; and the ORefit sets. The plugin re-derives nothing
the generator measures. The catalog carries the build and stamp, and the plugin refuses to act on
rules or ORefit when they disagree with the BodyGen files, and says so.

## S-20 — ORefit writes final values and restores a snapshot

**Superseded in its mechanism by S-40 (owner, 2026-09-23): the refit is a keyword layer of floors, not final
values with a snapshot. The clothed slots and the order of refit sets below still stand.**

Agent decision, 2026-09-23, from both sources. OBody puts the clothed set under a second key
("OClothe"), and RaceMenu adds keys, so a negative delta works. LooksMenu takes the MAXIMUM over
keys (`UserValues::GetEffectiveValue` is `std::max_element`), so a refit that lowers a value cannot
live under a key of its own. While an NPC is clothed, Silhouette's own (unkeyed) layer holds the
refit's final values. Before writing them, the bridge snapshots exactly those morphs into the
plugin's co-save, and puts the snapshot back on undressing. An NPC's values are their preset plus
their own rolled variety, or a picker choice, or a hand edit, and only a snapshot knows which.
Clothed follows OBody in Fallout 4's slots: BODY (33), [U] Torso (36) or [A] Torso (41), not
blacklisted, or any force-refit item. A refit comes, in order, from the outfit's own refit preset,
`<Preset>-Refit`, `Female-Refit`/`Male-Refit`, and finally the built-in set for this body.

## S-21 — Variety rides on BodyGen ranges; men get nipples and balls only

Owner polls, 2026-09-23. Nipple variety for both sexes, plus ball size for men. Shaft length and
width are NEVER varied. The owner asked whether it would harm "alignment of penis-hole-mouth", and it
would: animations aim the penis bones, and fo4-anatomy's collision spheres are sized to BodyTalk's
current shaft (r 2.0 against a visible 1.55). A wider shaft clips through the lips; a thinner one
opens them with nothing inside. Same mechanism as S-17: `Morph@low:high` entries in the templates,
from `tools/variety.json`. One loader reads both range files, refuses runtime states (S-16), and
FAILS if both files name the same morph. AnusBack is never randomised (S-17).

Addendum, 2026-09-23 (the ranges, agent's choice within the poll): `tools/variety.json` rolls, for
women, NippleSize -0.2..0.5, NippleAreola -0.2..0.5, NippleTip 0..0.35, NippleLength 0..0.25; for men,
BTNippleSize 0..0.5, BTNippleWidth -0.2..0.3, BTNippleTipSize 0..0.35, BTBallSize 0.1..0.7,
BTBallAsymmetry 0..0.4. Measured per unit of slider on the deployed bodies: the worst local stretch is
1.1x to 1.9x for each of them except NippleLength (3.6x, hence the narrowest range); each range spans
what the installed presets themselves use, without their extremes. Left to the presets: nipple
placement and breast-point shape, which are a preset's design; `Balls`, the big size control; NipBGone,
which is ORefit's. Ranges stay ABSOLUTE per sex, as S-17's are, so a range replaces the preset's own
value for that morph (OBody's nipple randomisation does the same under its key) and the verifier can
still check every range exactly. The build hash now covers the ranges: files that roll differently are
a different build. The loader refuses the shaft by name (`Penis Length`, `Penis Width`, `TipShape` and
the BT penis sliders) and AnusBack.

## S-22 — The NPC picker is hotkeys and an MCM page

**Superseded in part by S-79 (2026-09-30): a Scaleform window, built without CommonLibF4's menu code.**

Owner poll, 2026-09-23. Aim at an NPC and press Pick: they become the target. Next and Previous
cycle every preset that fits their body, live, with the name shown. Keep records the choice; Cancel
restores exactly what they had. The MCM page shows the target with a dropdown of every preset,
plus Apply, Back to random and Which body. There is no custom Scaleform menu: it would be several
times the work, and menu code is where FO4 plugins crash (CommonLibF4's `VisitMembers` on 1.10.163).

## S-23 — Rules only the runtime can see: name, faction, name blacklist

**Amended by the first microscope wave, from OBody NG's source (Body.cpp): a name rule matches the NPC
RECORD's name (`actorBase->GetName()`), not the display name.** A reference renamed at runtime (Rapport names
the settlers it befriends) keeps the rule its record matched; by display name, a blacklisted "Settler"
renamed by Rapport would have been rolled by BodyGen on the next sighting. Faction rules read the record's
own factions, as OBody does, not every template's up the chain. The plugin tier (BodyGen's) reads the
plugin of the chain's root, which is where LooksMenu applies a plugin line.

Agent decision, 2026-09-23. OBody's priority stands (S-11). The plugin acts only when a NAME or
FACTION tier wins for an NPC: it assigns that preset, and the bridge replaces BodyGen's roll. A name
blacklist leaves the NPC bare, with a stored blacklist marker (a morph no body has), so BodyGen
never rolls them again. OBody's `obody_blacklisted` morph does the same job. Names are the NPC
record's name, as the amendment above says -- the name OBody's users write, since the record's name is
the one the game shows until a script renames the reference.

## S-24 — The API and events are OBody's, in Papyrus

**Amended by the third microscope wave: MCM is found by the name it registers with F4SE, "F4MCM" (f4se.log
shows `mcm.dll (00000001 F4MCM 01020000)`), not by its file name. Wave 2 asked for "MCM", which never
answers, so no MCM switch reached the plugin and the API's setters never wrote MCM's settings. The bridge
and `Silhouette:API.McmInstalled` ask for both names.** **Amended by the fourth microscope wave: MCM answers
False for a key it never read, which would switch ORefit off for everyone, so the bridge trusts MCM's values
only while a sentinel key in settings.ini (`[Meta] iDefaults=1`, on no control) says they were read; and it
pushes them only when they change, so a read made just before another mod's switch cannot undo it.**

Agent decision, 2026-09-23. `Silhouette:API` offers global functions named as in OBodyNative. The
bridge raises the events as custom events that any script can register for: OnActorGenerated,
OnActorNaked, OnActorRemovingClothes and OnORefitChanged. The plugin never raises an event itself
(S-18).

## S-25 — The plugin's co-save: who it assigned, why, and what ORefit took

**Superseded by S-43 and S-47: the co-save keeps intent and a picking in progress, nothing of ORefit.** A
placed reference's record is always written, even when its cell is out of memory at save time (the form
map does not hold it then, and dropping it lost picked presets); a created (0xFF) reference's record goes
with the reference.

Agent decision, 2026-09-23. It keeps a record per reference: the preset it assigned and the tier
that chose it (name rule, faction rule, picker, API), the stamp, whether ORefit is on, and ORefit's
snapshot. Form ids are resolved through F4SE, so load-order changes are followed. A new game
starts empty.

## S-26 — ORefit's sets: a built-in one, and OBody's refit presets

**Amended by S-40 and S-42: every entry is a floor now, and the built-in set is BreastsTogether ≥ 0.3, PushUp ≥
0.2, and NipBGone 1 under heavy clothes only.**

Agent decision, 2026-09-23. `builtin:female`, for CBBE: BreastsTogether at least 0.3, BreastGravity2 at
most 0.2, PushUp at least 0.2, NipBGone 1 with NippleLength, NipplePerkiness, NipplePerk2 and NippleTip
at 0, Butt and AppleCheeks 0.05 less. Floors, ceilings and small steps, so a modest body changes little.
There is no male set. OBody's refit presets -- a BodySlide preset named "<Preset>-Refit",
"Female-Refit" or "Male-Refit" -- are compiled into sets (every slider they carry, as `set`), are never
handed out as bodies, and outfits name theirs by the outfit's in-game name
(`refitOutfitPresetsFemale/Male`, OBody's keys). Outfits that never refit and outfits that always do
are OBody's keys too, by form id, name or plugin. The player is never refit.

## S-27 — Reset, Back to random, and leaving

**Amended by S-40/S-41: Reset also removes the refit layer and nothing is refit until she has a body again;
leaving needs no step at all -- removing Silhouette.esp removes every refit.** **Amended by S-53: with
another mod's keyed morph on them LooksMenu keeps their map and never runs BodyGen, so the plugin remembers
a reset and rolls them a body itself at the next load. And by S-54: without a working Silhouette.dll the
bridge takes refits off the people around the player. And by S-59 and S-60: a Reset or Back to random asked
for and saved before it ran is carried out after the load; Back to random under a rule draws from the rule
again.**

**Amended by the third microscope wave (rolling back).** Removing Silhouette.esp outright is safe: LooksMenu
drops keyed values whose plugin is not loaded. Loading a Silhouette.esp WITHOUT the refit keyword over one
that had it is not: LooksMenu's MorphValueMap::Load checks only that the plugin NAME is loaded, then files a
value whose keyword FORM is missing under key 0 -- her own body. Every refit on anyone (NipBGone under heavy
clothes, PushUp, the refit marker) becomes her body for good. So a rollback never restages an older build's
esp (Phase 1's has no 0x803): it removes Silhouette entirely, or keeps the new Silhouette.esp and its scripts
and removes only Silhouette.dll (S-54 then sweeps the refits off). `scripts/deploy-dev.ps1` and
`scripts/make-release.ps1` refuse a Silhouette.esp that lacks KYWD 0x803. The same holds for any mod that
keeps a keyed layer (fo4-anatomy's 0x801 was told).

**Amended by the fourth microscope wave (restaging).** Vortex deploys by hardlink: a staged file written in
place is live at once, a new one waits for the Deploy. `scripts/deploy-dev.ps1` removed and re-copied its
folders, so every restage left Data half-deployed until the Deploy -- the new esp and DLL beside the old
scripts. It now stages file by file in place and names the new files that wait for the Deploy. A peer's rule
worth keeping: a presence check tests the half that cannot go live early (the DLL), never the esp.

**Amended by the fifth microscope wave.** Since every write is live, the copy is the deploy: it refuses before
the first write unless every file can be written (nothing holds one open, no folder stands where a file goes),
and a write that still fails names what was written and says Data now mixes two builds. Whether the Deploy
is needed is read from Data itself, file by file (the same file id as the staged one, or not): a run that
listed new files and was not followed by a Deploy told the next run that nothing waited. (Sixth wave, from the
first real run: Vortex's own `__folder_managed_by_vortex` markers are not files an older build left, and do
not call for a Deploy. A folder that refuses a NEW file is found by the copy, not before it; the copy then
says what was written.)

Agent decision, 2026-09-23, from LooksMenu's source. Reset removes Silhouette's (unkeyed) layer: the NPC
is bare now, and LooksMenu drops emptied entries when it loads a save (MorphValueMap::Load skips them),
so BodyGen gives them a body again after the next load. Back to random (GenActor) runs BodyGen at once
through RegenerateMorphs, which clears every key, so the bridge keeps other mods' keyed values and puts
them back -- the regeneration window's way. For uninstalling, MCM > Silhouette > Settings takes ORefit
off everyone the save remembers with a refit on: the people in memory at once, the rest when next seen,
as long as ORefit stays off. BodyGen bodies need nothing: they are LooksMenu's data.

## S-28 — The bridge's protocol is fixed, and tested where it is decided

Agent decision, 2026-09-23. The plugin builds each order when the bridge asks for it, from the state at
that moment, one order per actor at a time, the latest decision winning while it waits. The bridge does
exactly: regenerate, probe, read, Prepare, clear, write, update, Done -- nothing else -- and the offline
tests run the same steps against a fake LooksMenu layer, so what a body ends up holding is tested, not
only the orders. The native script is `Silhouette:Plugin` (Papyrus reserves `Native`) -- renamed `Silhouette:DLL` by S-46. Functions that
touch only the plugin's own state are callable from tasklets (no frame each); anything that reads an
actor runs on the main thread, where the game changes it. Actors travel as form ids.

## S-29 — The shaft is never part of a body

Owner poll, 2026-09-23: "Should Silhouette strip shaft sliders from every body, as it already does
for erection states?" -- "yes". Two of the eight installed male presets set `Penis Width` (0.2 and
1.0), the width the owner had already ruled out for variety (S-21): animations aim the penis bones
and fo4-anatomy's collision is sized to BodyTalk's current shaft, so a wider one clips through the
lips. Like the runtime states (S-16), the shaft's sliders -- `Penis Length`, `Penis Width`,
`TipShape` and BodyTalk's `BTPenis*`, `BTShaftRootSize`, `BTUrethraCurve`, `BTSmoothPenis*` -- are
never written into a template, the player's picker, the catalog or a refit set, and the verifier fails
a template that has one. Bodies an older build already gave are healed the way S-16 heals states: the
plugin reads, from the manifest of the stamp an NPC's marker carries, whether that template held any
morph that is now never part of a body, and zeroes exactly those in the unkeyed layer once; the
regeneration window does the same for Silhouette-marked NPCs without the plugin. The player keeps
their own body until they apply a preset again in MCM.

## S-40 — ORefit is a keyword layer that only raises (supersedes S-20's mechanism)

**Amended by S-50: the refit marker's value names the set, its weight and the floors applied, so a build
with new floors reaches a woman who never undresses; odd is light, even is heavy. A floor must be above
0: the parser refuses anything else.**

Owner poll, 2026-09-23, after the first microscope wave showed that S-20's design keeps a clothed
woman's naked values only in Silhouette's co-save, so one save without the DLL, a save that lands in the
middle of a refit, or an NPC whose cell unloads makes the clothed shape her naked body for good. The owner
chose the recommended option and added: "dressed and undressed bodyshape should be in sync (besides that
intentional difference u named)".
- The refit lives under Silhouette's own keyword (Silhouette.esp, KYWD 0x803). LooksMenu shows the
  MAXIMUM over keyword layers, so each entry is a FLOOR: while dressed she has at least that value, and
  everything else is her own body. The clothed shape therefore follows every change to her body -- a new
  preset, the picker, variety -- with nothing to keep in step.
- Refit on replaces Silhouette's keyword layer with the set's floors and a refit marker; refit off removes
  the layer. Both are safe to repeat and to interrupt. Nothing about a refit is kept in the co-save: the
  marker in LooksMenu's own data says whether one is on.
- Removing Silhouette.esp removes every refit: LooksMenu drops keyed values whose plugin is not loaded
  when it loads a save. (Third microscope wave: only removing it. A Silhouette.esp that is loaded but lacks
  the keyword turns every refit into her own body -- see S-27's amendment.)
- The cost: a refit cannot lower anything. No cap on breast sag, no easing of the seat, and a
  "<Preset>-Refit" preset raises its sliders only. OBody's other refit rules (S-20's slots and order of
  sets, S-26's refit presets and outfits by name) stand.

## S-41 — Who is refit

Owner poll, 2026-09-23: only bodies that should be, "but we need to be sure we didnt miss something we
should cover and also i think custom followers should be included by default if user didnt blacklist them".
Every clothed woman of a distributed race who HAS a body is refit: a Silhouette body, a body another
mod's BodyGen files gave, a custom follower's, one edited by hand in LooksMenu. Never refit:
- anyone blacklisted -- by name, by form id, by plugin or by race;
- anyone reset this session (S-27): she is bare until BodyGen gives her a body after a load;
- anyone with no body at all: the refit would be her first stored morph, and LooksMenu never runs BodyGen
  for an actor that holds one;
- the player and the character-creation dummies.
Having a body is read from LooksMenu: a Silhouette marker, or a non-zero value of her own.
Nobody outside this -- the player, the dummies, creatures, races Silhouette does not distribute to -- is
ever probed: that is most actors in the world. A race taken out of `distributeRaces` keeps the refits its
women already have until Silhouette.esp is removed: they are never probed, and a Reset is refused for a
race Silhouette does not shape. Nor are anyone in power armour refit (S-48).

## S-42 — Nipples are flattened under heavy clothes only

**Superseded by S-48 as to what is heavy -- the rating and chest-piece rules below are gone -- and by S-49
as to arousal. The built-in set stands.**

Owner poll, 2026-09-23 ("would be cool to flatten nipples only on heavy clothes like in reality with rough
tissue"; then "Armour pieces + armoured outfits"). Measured in the base game and DLCs: most clothes take
BODY (33) AND [A] Torso (41) -- dresses, suits, lab coats, even the bathrobe -- so the slot says nothing;
366 of 492 BODY items are rated 0. Heavy is: a separate chest armour piece ([A] Torso without BODY), or an
outfit whose armour rating is 10 or more (the Brotherhood uniform 10, the Courser jacket 30, Maxson's coat
50). Config lists name single items heavy or light by form id or name (`heavyOutfitsFormID`,
`heavyOutfits`, `lightOutfitsFormID`, `lightOutfits`; the rating is `heavyArmorRating`). The built-in CBBE set is
BreastsTogether at least 0.3 and PushUp at least 0.2 whenever she is dressed, and NipBGone 1 under heavy
clothes. An arousal bump for nipples is the anatomy project's (owner); under a keyword of its own it
combines with this one by the same maximum.

## S-43 — Truth first: the co-save keeps intent, LooksMenu keeps the body

Agent decision, 2026-09-23, from the first microscope wave. The co-save and LooksMenu's own data are two
files that can disagree -- a save that lands between two bridge calls, a save made without the DLL, a
non-persistent NPC whose morphs LooksMenu drops at load, a reroll that goes around the plugin. So:
- The first order for an actor in a session is a probe of LooksMenu: her markers (body and refit) and the
  names she holds.
- The co-save keeps only INTENT -- who chose which body (a rule, the picker, the API, the name
  blacklist) -- and it is written when the choice is made, not when the bridge finishes.
- After each probe the plugin makes reality match intent: a body that is not the chosen one is given
  again (once a session), a refit that should not be there comes off, one that should is put on.
- Every order is safe to repeat. Order ids start at a random number each launch, and the bridge checks
  that an order still names its actor before it writes: a script stack a save resumed cannot act on
  someone else's order.
- A record belongs to its reference. Only a created (0xFF) reference's id can be handed to somebody new,
  so only there does a different NPC record mean somebody else; a placed leveled NPC is given a new
  temporary record when it respawns and stays the same person to LooksMenu, and keeps its record.
- Work goes in three lanes (S-55 moved other mods' calls out of the first): the player's own actions,
  then decisions and other mods, then probes and bulk work.
- A rule decides after the first probe, knowing the body she has (wave 2): a choice LooksMenu mirrors
  (S-51) is rebuilt first, so a rule never replaces a body somebody chose.

**Amended in game, 2026-09-24 (fo4-mcp ran the test plan on wave 3).** "The first order in a session" waited
for the game to say who was loaded, and after a load in a running game nobody was read. Across three loads in
one session, 19 people were probed after the first and none after the other two. So a picking saved mid-preview
was never put back (S-47), and everything else owed to the first sighting after a load waited for the next
cell change. The game does not report people already around the player after such a load. fo4-mcp measured it
with a sink of its own on the same event source: 85 loaded events after a load from the main menu, and 2 (both
created references) after an in-session load of the same crowd, with the sink still attached. For 30 seconds
after each load the plugin now reads every actor the game is simulating (its high and middle-high process
lists), once each, and then logs how many of them the game itself reported. In game with wave 4 that line
read 27 of 27 after a load from the main menu and 0 of 27 after an in-session one, and step 13 passed.
(Fifth wave: the 30 seconds start at the bridge's first poll after the load, not at the load, because the
bridge polls only while the game runs; a failed load, which has already forgotten everyone, starts the sweep
too; and the line also counts actors the game reported that the sweep did not find, and says how long the
first poll took. In game with wave 5: 26 of 26 reported after a main-menu load, one more reported that the
sweep did not find -- the player, whom the process lists do not hold -- first poll after 6.3 s; 26 of 0 after
an in-session one, first poll after 3.2 s.)

## S-44 — Top-up: existing bodies get the variety they lack

**Amended by the second microscope wave: a body is touched once per build of what is wanted of it -- its
marker, the heals its build needs and the ranges switched on -- so a later build's heal or new range still
reaches a body touched before, and a body that needed nothing is remembered too (a value the player takes
off afterwards is not put back). Presence is read from her own layer: another mod's keyed value of a variety
slider is not hers. A body Silhouette gives or rolls counts as touched when it lands.** **Amended by the
third microscope wave: a touch-up waits while AAF has her in a scene (S-56), and a body given again -- a new
build's values for a choice, a rule's body put back -- keeps the variety she has, the variety a picked body
was picked with included.** **Fourth wave: so does a rule's body a new build gives again, when the rule's
preset is the one already on her (her variety was BodyGen's roll, and was being drawn anew).**

Owner poll, 2026-09-23 ("i aggree with recommended, but force regen on 24h function ofc will override
it"). The first time the plugin sees a Silhouette body that lacks a variety slider the current build rolls
(S-17, S-21), it writes a drawn value for just those sliders; everything else about her body stays. The
same order carries the S-29 heal. A regeneration (the window, Back to random, GenActor) replaces the body
and its variety with a new roll. The variety switches (S-24) apply.

## S-45 — The player is never randomised (S-7, restored)

Agent decision, 2026-09-23, from the first microscope wave: S-21 had put ranges on the player's template,
because the player's line named the same template as the random pool. The player and the two
character-creation dummies now name range-free templates with the same values and marker, so a new
character is the most average preset exactly, and "Back to the default" gives the same body.

## S-46 — The API's events and names

**Amended by the second microscope wave: every body given on request is announced, the same preset again
included (Back to random rolling the same preset, a Reapply); a body about to be replaced is not; and an
announcement counts as made only once the bridge has raised it (EventDone), so one a save cut off is made
again after the load. A listener compiled against the decompiled base sources registers the mangled name,
"silhouette:bridge_OnActorGenerated"; one compiled against the Creation Kit's own sources, the plain one.**
**Amended by the third microscope wave: a roll the rules replace at once (Back to random under a rule, a
blacklisted NPC rolled) announces only the body she ends with; the body after a Reset is announced even when
it is the same preset as before; an announcement handed to the bridge and not raised yet is not made twice;
and at most 64 are raised a poll without losing the 65th. While an NPC is being picked nothing about her body
is announced -- the body on her may be a preview; Keep announces the one she keeps.** **Fourth wave: an
announcement the bridge skipped (its actor was not in memory) is not remembered as made, and no longer stands
in the way of the same body announced again later in the session.** **Fifth wave: that rests on ONE loop
raising the events (the bridge's, on its one timer) -- written down on both sides, since a second raiser would
announce a body twice; a Cancel no longer forgets an announcement raised while the NPC was picked.**

Agent decision, 2026-09-23. The bridge sends its custom events under the names the compiler gives them,
"silhouette:bridge_<Event>": sent under the bare name, no listener ever receives them (the vanilla scripts
and Rapport do the same). The native script is `Silhouette:DLL` -- "Plugin" also means an .esp in this
project's own config. OBody's exact names (ResetActorOBodyMorphs, ReapplyActorOBodyMorphs) are aliases.
Calls that change a body return before it changes; OnActorGenerated says it happened.

## S-47 — The NPC picker survives a save

Agent decision, 2026-09-23. The picker's copy of the body and the choice the NPC had before are kept in
the co-save while picking. A save made mid-preview loads as a Cancel: the preview never becomes a body
nobody kept.

**Amended by the second microscope wave.** One saved picking per NPC (up to 64): picking somebody else no
longer loses the first one's way back. Picking an NPC again carries on from the body they had then. A
restore puts back the choice behind the body with it, and never a value no body may hold (S-16, S-29).
Keep on the preset they already had is a Cancel -- exactly the body they had, their own edits included --
and previews keep their own variety. Any decision made elsewhere (a rule, another mod, Back to random)
ends a picking and anything it left to restore. A created reference's id handed to someone new drops the
picking (S-57).

**Amended by the third microscope wave.** A Refresh or a Reapply refuses an NPC being picked, or one whose
picking a save left unfinished: the body on them is a preview, and given again it would become theirs. The
order pickings arrived in is saved, so the cap drops the oldest after a load too. Picking someone nothing
had probed yet is the session's probe: what a probe settles (a choice rebuilt, a body announced) is settled
at their first sighting.

**Amended by the fourth microscope wave.** What a probe settles waits while they are picked -- a roll or
reset owed, a half-written body, a heal, a first announcement -- and is done when the picking ends (Keep,
Cancel, the restore landing, or a snapshot the bridge could not take), not at the next load. A choice rebuilt
from LooksMenu during a picking is what a Cancel puts back. A half-written body a Cancel puts back still says
so (S-58). An unfinished picking is put back from whichever is seen of them first after a load, an equip event
included. In game, a save made mid-preview and loaded without quitting stayed on the preview. The co-save
had kept the picking, but nobody was read after that load. The load sweep (S-43's amendment) is the fix.

**Amended by the fifth microscope wave.** A preview is no body of theirs, and nothing reads it as one: a landed
Reset (S-53) is not dropped because a preview was on them when a probe came -- with another mod's keyed morph
on her, that left her bare for good -- and its new body does not start, nor is announced, while she is picked.
A rule's re-give is not spent on a preview left to restore. Picked again and ended again while the first
restore was being written, the first to land does not end the picking the second belongs to. Keep pressed
while the preview was being written, then the NPC out of memory before it landed: the preview comes back as the
body kept, with its choice (the choice's marker had won, leaving her old body with the new choice beside it).
A Cancel puts back the choice as it was, and keeps what was announced meanwhile -- one body, one
OnActorGenerated. A snapshot that comes back gone ends the picking like the rest.

## S-48 — Heavy is told by the item's name (supersedes S-42's mechanism)

Owner poll, 2026-09-23, after the second microscope wave measured S-42 misfiring: NipBGone shows only where
the garment's own .tri carries it (2 of 64 vanilla clothes, none of the DLC04 armours), while bras, tops and
shirts in slot 41 were classed heavy -- 635 items heavy, 137 of them visibly. Nothing about slots or ratings
separated them. The owner: "i think we need here kinda regexp by name maybe? flatten only obvious armors ...
armor-like words in name, etc or top type of cloth such as jacket, so flattened nipples where it shouldnt be
worse than visible nipples where they shouldnt".
- The config's lists decide first: items named heavy or light by form id (`heavyOutfitsFormID`,
  `lightOutfitsFormID`) or by exact name (`heavyOutfits`, `lightOutfits`).
- Otherwise an item is heavy when its NAME holds one of `heavyWords` as a whole word or phrase, in any case
  and with any separator: "Combat Armor Chest Piece", "Leather chest-piece", "Minuteman Coat". A word inside
  another word is not the word: "Armorsmith's Apron" and "Coated Dress" are light. The default words are
  armor, armour, armored, armoured, chest piece, chestpiece, breastplate, cuirass, jacket, coat, trenchcoat,
  parka.
- Everything the name does not say is light.
- Power armour -- frames and pieces, by their Fallout4.esm keywords -- is not clothes at all: an NPC in a
  frame is not refit, and climbing in and out is not dressing.
- Each item is decided once; a heavy or listed one says why in Silhouette.log.

**Amended by the owner, 2026-09-24 ("Add words + fix lists", third microscope wave).** Measured on the
owner's load order (1,426 dressing items, 809 plugins): 290 heavy by the words above, 56 misses. The
defaults gain overcoat, greatcoat, longcoat, raincoat, dreadcoat, battlecoat, duster, chestplate, plate,
carapace, kevlar and torso -- 52 more items heavy, none wrongly by its name; nothing is dropped. The shipped
config names three Eli_Armour_Compendium.esp armours built as shirts light (10028B, 10483F, 100288) and its
Institute Courser Uniform heavy (100014). Two limits stand: names are read as the game shows them, so on a
localized Fallout4.esm (German, French, Russian ...) no vanilla item's name holds an English word -- add your
language's words to `heavyWords`; and a refit flattens nipples only on a garment whose mesh carries the refit
sliders -- no vanilla or DLC HEAVY garment does (the two vanilla clothes that carry NipBGone are light, and
light clothes are never flattened), mod outfits built with them do (Mercenary, Clothing Of The
Commonwealth). The power armour rule now also covers the events: climbing into a frame raises no
OnActorNaked or OnActorRemovingClothes, and a piece put down is not clothing coming off.

## S-49 — Arousal is held flat under heavy clothes

Owner poll, 2026-09-23: "Hold it flat (Recommended)". NipBGone is a floor, and LooksMenu shows the MAXIMUM
per morph over keyword layers: the anatomy mod's arousal layer, on its own keyword, raises other nipple
morphs that add to the shape, so a heavily dressed, aroused woman showed 0.90 against 0.13 calm. The anatomy
mod holds its nipple rise at 0 while Silhouette's heavy refit is on her, which it reads from S-50's marker.

## S-50 — The refit marker says which refit is on, and how heavy

Agent decision, 2026-09-23, from the second microscope wave, shaped by S-49's contract. The marker
`Silhouette_Refit`, under Silhouette.esp's keyword 0x803, holds 1 + heavy + 2 x (a hash of the set's name,
its weight and the floors applied, modulo 8388606): a whole number below 2^24, exact in the float LooksMenu
keeps, ODD under light clothes and EVEN under heavy ones; 0.25 while a refit is being written; nothing when
no refit is on. A build whose floors changed gives another value, so a woman who never undresses is refit
again the next time she is seen. Other mods read it from LooksMenu, or through `Silhouette:API.IsHeavilyDressed`
and `Silhouette:API.IsORefitApplied`, which read it there.

**Amended by the third microscope wave: a refit that is already right is not written again.** Dressed heavy
and back before the bridge came, she was refit anyway: the layer cleared, the marker pending, the floors
written, the body updated (a visible jolt) and OnORefitChanged raised for nothing -- and a reader like the
anatomy mod, checking every 3 seconds, could land on the pending 0.25 and let nipples through the armour.

## S-51 — A choice is mirrored into LooksMenu

Owner poll, 2026-09-23: "Mirror into LooksMenu (Recommended)". F4SE keeps no co-save chunk of a plugin that
is not loaded, so one save made without Silhouette.dll lost every choice, and the rules then took picked
bodies back. A body the picker or another mod chose carries `Silhouette_Chosen` beside it in its own layer
(3: picked, 4: another mod) -- a morph no body has. When the co-save has no record, the probe rebuilds the
choice from it; a choice recorded without the marker (given before this) gets it.

**Amended by the third microscope wave.** A Refresh or Reapply that finds the marker on a body nothing is
recorded for keeps it beside the body it gives again, and records the choice again -- before, a Reapply ahead
of the first probe erased the only trace of it. A choice whose preset is gone from the build loses its marker
and the rules decide: it was rebuilt from the marker and dropped again every session.

## S-52 — A rule's draw is kept while the rule lists it

Owner poll, 2026-09-23: "Keep it (Recommended)". A name or faction rule with several presets draws one per
person. A met NPC keeps the preset recorded for them while the rule still lists it: adding a preset to a
rule re-bodies nobody (before, about two thirds of the people it covered), and taking theirs out draws again
from what is left.

## S-53 — Reset means a new body at the next load

Owner poll, 2026-09-23: "New body next load (Recommended)". Reset leaves them bare now, with no refit, and
the plugin remembers it. At the next load: when LooksMenu dropped their emptied map, BodyGen gives them a
body and the reset is over; when another mod's keyed morph kept the map (BodyGen never runs for a stored
map), the plugin rolls them a body itself, the other mod's morphs kept. The regeneration window leaves a
reset NPC alone.

**Amended by the third microscope wave.** A reset that has not landed yet is owed across a save (S-59). One
case stays as it is, by choice: a reset saved and then loaded WITHOUT Silhouette.dll, on someone another
mod's keyed morph keeps in LooksMenu's map, stays bare -- BodyGen never runs for a stored map, and without
the plugin only the regeneration window could roll them, inside its 24 hours. Loading with the plugin again
gives them their body at the next load. Mirroring the reset into LooksMenu itself would have cost more than
it saves: a marker in the map means BodyGen never gives that person a body again.

## S-54 — Without a working Silhouette.dll, refits are swept off

Owner poll, 2026-09-23: "Sweep them off (Recommended)". A missing Silhouette.dll, one of another release, or
a catalog it refused left every refit on for good, dressed or not. The bridge then looks at the people
around the player -- once each per load, every 30 seconds -- and takes off a refit layer it finds; their own
body, BodyGen's, stays.

## S-55 — Lanes: the player's own actions first

Owner poll, 2026-09-23: "Player actions first (Recommended)". Three lanes, in order: urgent -- the picker,
the NPC page, a refit coming off; normal -- other mods' API calls, the rules, a refit going on, touch-ups,
first contact with someone who is dressing; background -- probes, the bulk buttons (Refresh, Give the
people around me new bodies) and the regeneration window. Within a lane, first asked, first done. Before,
a bulk button's hundred orders in the urgent lane made the picker wait behind them.

## S-56 — Work waits instead of being lost

Agent decision, 2026-09-23, from the second microscope wave. An order whose actor is not in memory waits,
off the queue, until they are seen again -- a roll has no recorded intent to fall back on (the regeneration
window's hand-offs were lost). A roll for someone AAF has busy or locked waits too, tried again no sooner
than 10 seconds later: a roll keeps every keyed value it finds, and in a scene those are the scene's -- AAF's
erection would have stayed for good. Found while fixing it: an order handed straight back would have kept
the bridge's drain spinning for the whole scene. A new decision replaces the one waiting and is tried at
once.

**Amended by the third microscope wave.** Only the kind of work that was deferred waits: a roll put off by a
scene held back everything else for that actor, so a woman undressed in the scene kept her refit (and the
anatomy mod held her arousal flat) until it ended. A refit coming off, a probe and the picker now go out
meanwhile. Touch-ups wait out a scene too, and the roll is asked again right before it is made (a scene can
start while its keyed values are read). The bridge polls faster only for work it can be handed now; the
half-minute summary says what is held back by a scene and what waits for people out of memory. (Fourth
wave: deferring again, and nothing else, prints no summary: a scene that never ends filled the log.)

## S-57 — A created reference's id handed to someone new

Agent decision, 2026-09-23. The game gives a deleted created (0xFF) reference's id to whoever is created
next, and LooksMenu can keep the old morphs on it. Everything that came with the id is the previous NPC's:
the record and a saved picking are dropped, and a choice marker is taken off -- never rebuilt into a choice
that would pin the body against the rules. The body itself stays. The co-save writes a created reference's
record only while the id is still an actor of the same NPC.

**Amended by the third microscope wave.** Only its bookkeeping (what was announced or touched, a rule's
plain draw): a record that holds intent -- a choice, a roll or reset owed (S-59), a rule drawn again (S-60)
-- and every saved picking are written whatever the game says of the reference at the save. A created NPC
whose cell is unloaded is not in memory, and is not gone: dropping them lost a picked settler's choice, and
then nothing flagged the stranger who got the id later, whose body the old marker pinned. The next sighting
sorts it out, as above. Intent grows only with what the player and other mods do. (Fourth wave, the bound:
a thousand created NPCs pressed once and then deleted leave a thousand records, about 33 KB, that are never
pruned. At a save, a deleted created NPC and one whose cell is unloaded look the same. Accepted.) (Sixth wave:
nobody is picked without an NPC record. A picking keeps the record it was made for, and that is how an id given
to somebody else is told; one kept with none would have put the previous owner's choice and body on the
newcomer at a Cancel. No caller passes none today; the director refuses it anyway.)

## S-58 — A body being written says so

Agent decision, 2026-09-24, from the third microscope wave. A body order clears her own layer and writes
20 to 55 values, a frame each; a save in that second kept half a body with no marker. A Refresh or Reapply
of a body BodyGen gave had nothing recorded to put it back, so she kept the half body for good -- and with
another mod's keyed morph on her, LooksMenu never ran BodyGen for her again. Now the body's marker is written
first, with 0.25 ("pending"), and last with the build stamp, as S-50 does for refits. A probe that finds a
pending marker gives that preset again, whole, keeping the variety that made it in; a body with intent behind
it is put back by S-43 as before. What is left is the one call between the clear and the first write. The
marker reads as the preset in "Which body" ("being written"), and the no-plugin paths, which only ask whether
a marker holds a value, read it as the preset too.

**Amended by the fifth microscope wave.** A choice marker on its way is not a body on its way: what waits for
a new body -- this repair, the top-up (S-44), the first announcement -- does not wait for a marker, since
nothing ran it again when the marker landed. A reused created id kept a half body for the session while the
previous owner's choice marker was being taken off.

## S-59 — What was asked for is owed until it lands

Agent decision, 2026-09-24, from the third microscope wave. Back to random and the regeneration window's
hand-off lived only in the session's queue, and a Reset counted as done as soon as it was asked: a save before
the bridge got to them lost the roll -- the marker then rebuilt the old choice -- and a reset left the old body
on them as if BodyGen had given it. The co-save now records a roll owed (a new source) and a reset not landed
yet (its stamp 0 until it lands); the next probe carries either out, and a landed one is owed no more. An
older plugin reads the new source as nobody's choice. The regeneration window hands a person over once, only
when the plugin accepts; a refusal is asked again at its next scan, and anyone with a body of their own is not
read again.

**Amended by the fourth microscope wave.** A roll that lands clears only what it was: a second Back to random
still queued behind it stays owed, and a Reset asked while the roll was in flight stays owed until it lands
(it was being erased, and with another mod's keyed morph on her she stayed bare for good). The roll after a
name leaves the name blacklist is owed too. The window remembers a refusal that cannot change (a race
Silhouette does not shape) and asks no more; a new window looks at everyone again.

**Amended by the fifth microscope wave.** Owed means owed while Silhouette does not shape them, too -- their race
left the build, say: the roll or reset is carried out once it shapes them again (it is never rolled at every
load), and "Which body" says it waits for that instead of "on its way".

## S-60 — Back to random draws the rule again

Owner poll, 2026-09-24: "Re-draw the rule (Recommended)". Under a name or faction rule, S-52 kept the
preset a person drew for good, so Back to random rolled BodyGen and the rule put the same preset straight back
-- 16 raiders pressed 6 times each came back to their preset every time. Now each press mixes a salt, kept in
her record, into the rule's draw, and lands on another of the rule's presets than the body she has (a rule with
one preset has nothing to draw). S-52 keeps the new draw from then on, across saves. Salt 0 is exactly the draw
by id, so nobody's body changes until they are pressed. OBody's GenActor draws again the same way. (Fourth
wave: someone asked about before the plugin had seen them this session is read first; without it the rule
gave back the same preset, and two bodies were announced. Fifth wave: that reading needs their body built --
another mod's GenActor on someone in memory without 3D still reaches the plugin unread, and a rule may then
give back the same preset once. Accepted: the picker and the MCM page only reach people in sight.)

## S-61 — The rules file ships with every key, and a race list cannot be empty

Owner poll, 2026-09-24. On where the rules live: "lets ship this config with default values in mod package".
`Silhouette_presetDistributionConfig.json` stays in the package and stays the file the generator reads: it
lists every key with its value written out -- `distributeRaces` ["HumanRace"], the heavy words, empty lists
and maps -- with the owner's tuning in it (S-48's amendment). The plugin never reads it: it reads what the
generator compiled from it (catalog.json, S-19), so an edit takes effect only after running the generator
and installing its output. On an empty race list: "yes i agree recommended but default value should be human
race so user should deliberately break feature by deleting it from there". An empty `distributeRaces` meant
["HumanRace"] without a word; now the generator refuses it and says what the list is for. A missing key is
still the default.

**Amended by the fourth and fifth microscope waves.** The package ships the config it was compiled from, and
that is the package's own: a run writing a package elsewhere (`--out`) compiles that folder's config when it has
one. A config there that the run does not compile is refused, never replaced -- somebody may have edited it by
hand believing it is read. The same settings in another layout (line endings, key order) count as the same
config. A test holds the shipped file to exactly the default keys.

## S-62 — fo4-anatomy's build slider is never part of a body

Agent decision, 2026-09-24, at fo4-anatomy's request. fo4-anatomy's body added `AnatomyOpening`, a slider it
sets in its own build: the opening's shape is baked into the built base, and any value LooksMenu applies at
run time ADDS to it. The generator reads a preset's values as BodySlide applies them -- a preset that does not
name a slider gets the set's default -- so with a default of 50% every template would have written 0.5 on top
of the baked 0.5 (every female body up to 0.22 units off: the verifier refused it before anything shipped),
and "Anatomy Zero" stopped reading as zeroed. It joins the states (S-16) and the shaft (S-29) as never part of
a body: never written into a template, the picker, the catalog or a refit set, never counted in "zeroed" or
"average", and a base whose only baked values are such sliders counts as zeroed. Its runtime value stays 0:
the body as built. fo4-anatomy also moved the 50% into its base mesh and made the slider an extra with
default 0, so nothing that honours defaults can double it; this entry is the second guard.

The contract this makes, told to fo4-anatomy: it never writes such a slider into the UNKEYED layer. Like the
states, an unkeyed value of it on a body Silhouette gave is healed away by the regeneration window when the
plugin is not there (the plugin's own touch-up heals only what a body's template wrote, which it never was).
fo4-anatomy's answer, 2026-09-24: it never sets `AnatomyOpening` at run time at all, keyed or unkeyed -- its
right runtime value is 0, "as built", so that heal enforces the same contract. Its only runtime layer is
keyword Anatomy.esp 0x801 ("AnatomyArousalLayer"), four morphs: NippleLength, NipplePerk2, NippleTip,
NippleSize; it says so before that changes. (Wave 4 made the "average" measurement ignore these sliders too,
as this entry had promised; on the owner's install the winners did not change.)

## S-63 — The release archive keeps its docs in the plugin's folder

Owner poll, 2026-09-24: "Plugin folder (Recommended)". The archive held README.md and LICENSE at its root, which
a mod manager installs into Data's root, where every other mod that does the same collides with them. They go
to `F4SE/Plugins/Silhouette/`, beside the catalog and the config the plugin already owns. The licence stays in
the archive: GPL-3.0 wants its text shipped with the DLL.

## S-64 — The crosshair is the view caster's activate pick

Agent decision, 2026-09-24, after the owner's report: the Pick hotkey answered "aim at an NPC" with the
crosshair on one, even up close. Silhouette read the crosshair from `PickRefStateChangedEvent`, as F4MCP's
`aim` does, taking a reference at +08. That event is a `BSTValueEvent<bool>`, two bytes: the HUD's "update
the activate prompt" flag (`HUDRolloverModel::activatePromptUpdateQueued` in the CommonLibF4 that Papyrus
Common Library builds on, LucaDotGit/CommonLibF4, whose layouts carry static asserts). Whatever sat at +08
was the sender's stack. It held what F4MCP's checks looked at (a door, and once Richard), and in the owner's
game never the NPC they aimed at. A read-only look at the running game's memory confirmed the sink was
attached and receiving: it held a trapdoor, and it recorded no actor at all between the last load and the
quit, about three minutes.

The crosshair is now `ViewCasterUpdateEvent`'s `activatePickRef`: the reference the player would activate,
which for an NPC is the one they could talk to. Papyrus Common Library's `GetCurrentCrosshairRef` reads the
same field. Its reach is the game's activation reach, so Pick takes an NPC close enough to talk to, and the
notification says so. The sink copies two handles and looks nothing up; the main thread resolves them when
Pick or the menu asks. The menu's "last NPC aimed at within 30 seconds" (S-22) comes from a trail of the
last 64 picks, each with the time the crosshair left it, so the sink's thread never has to know which picks
were actors. When a Pick finds nobody, the log says what the crosshair was on: nothing within reach, a named
object, or someone Silhouette never shapes.

The dialogue pick (`dialoguePickRef`) is logged, not used. It reaches no further than the activate pick:
at 300 units it was empty too.

Verified in game by fo4-mcp the same day, with each stance proved from the actors themselves (F4MCP's
crosshair string being the broken one above). The source attached after a main-menu load and stayed
attached across an in-session one. At 74 units, squared up: `pick: Randall Chase (001D1F49), under the
crosshair`. At 300 units: `nothing is under the crosshair within reach`, with no dialogue-pick tail. With
another NPC behind the player and a Drifter in front, the pick was the Drifter: it follows where the player
faces, not the nearest or the last actor. Not produced there: the menu's 30-second window (the player could
not be turned away) and a door. The window's logic is the tested trail, and it resolves handles the way the
crosshair case just proved. The owner then pressed the real hotkey in his own game: "yep it works".

## S-65 — NPCs are drawn from Silhouette's own body pool, ordinary bodies most often

Owner decision, 2026-09-24, by poll. The random pool was every installed preset that fits, which on the
owner's install meant pin-ups: Rocket Bomb, Blessed and the like, on every woman in the Commonwealth. The
owner asked for real people instead: an ordinary body most often, an unflattering one ("very flat or very fat
or disproportional") less often, a conventionally beautiful one rarely, and real variety inside each. Poll
answers: the odds 70 / 22 / 8; women and men both; the installed presets out of random but still in the
picker; unique bodies for companions and major named NPCs as a separate step.

The pool is generated, not hand-made: `tools/pool/generate.py` writes
`data/Tools/BodySlide/SliderPresets/Silhouette Pool.xml` (82 presets, 41 a sex: "Plain" 18, "Rough" 17,
"Fine" 6) and its sidecar `tools/pool/pool.json` (each preset's tier, archetype, values and measurements).
Each tier is a set of archetypes given as slider ranges (`tools/pool/archetypes.py`). A candidate is BUILT
from the installed body without BodySlide (`mesh.py`: the zeroed body is the reference, S-5, and the `.tri`
holds every slider's diff) and MEASURED (`measure.py`: girths of torso, leg and arm slices, bust and belly
projection, volume). It is kept only if its measurements put it in its own tier (`tier_of`), and from the
survivors the most different are chosen, so no two bodies of an archetype look alike. A tier is therefore a
claim about the body's shape, not about the slider names: CBBE's zeroed woman is already a fantasy hourglass
(waist/hip 0.60), and a "middle" body has to be moved away from it. `generate.py --check` re-measures the
committed pool against the installed body and says which body moved tier; `--sheets` draws them.

The odds come from repetition: BodyGen picks one entry of a line uniformly (docs/bodygen-format.md), so a
"Plain" body is listed 3 times and the others once -- 54 : 17 : 6 entries, 70.1 / 22.1 / 7.8 per cent. The
line is about 1.6 KB, far under the 32,766 bytes past which the engine splits a line; the generator refuses a
longer one. Every installed preset that fits stays in the picker and can still be named by a rule; none of
them is random. A pool preset that is not a full fit of the installed body, or a sidecar naming a preset no
SliderPresets folder holds, is refused. The pool's own presets are read before Data's, so a stale deployed
copy cannot shadow the repo's. fo4-anatomy's build slider and the runtime states are left out of the pool's
XML (S-16, S-29, S-62): BodySlide gives them the set's default, as Silhouette leaves them out of every body.

The player default (S-45) stays "the most average full fit", now of the pool as weighted: `Plain F01` and
`Plain M02`. The verifier checks that the random presets are the pool's and that every random line lists
each one exactly its tier's weight; a line with one "Plain" copy short, or one "Fine" copy extra, fails it.

**Amended 2026-09-24, by poll: a floor under the bust.** The owner sent a photo of a pool woman whose chest had
gone concave, and it looked wrong: the breast physics folded a bust that small. From a render of the flattest
body (Rough F06) at two floors, the owner chose the smaller: a small but real bust. So no woman's archetype
may draw `Breasts` below 0 or `BreastsSmall` above 30. Flat, Frail, BottomHeavy, Lean, Petite, Boxy and
Average were narrowed to it. The pool was regenerated with the same seed, and every archetype still found 40
of 40 candidates in its own tier; Flat and Frail still measure as "ugly". `tests/test_pool.py` (BustFloor)
holds both the committed bodies and the archetype ranges to the floor. It was proven on the pool before the
floor, where 20 committed women failed. People who already have a pool body keep it until **Reset everyone**
(S-68) or a per-NPC Reset gives them a new one.

## S-66 — The named people get bodies of their own

Owner decision, 2026-09-24, by the same poll as S-65: companions and major named NPCs get a unique body that
matches who they are, instead of a roll from the pool. The list is a draft for the owner to veto.

`tools/pool/characters.py` holds 41 characters (24 women, 17 men), each a hand-set body in the pool's
vocabulary, a one-line vibe, and the NPC records it is for: 44 records by plugin, editor id and local form id
(Kellogg, Curie and Paladin Brandis have two). The ids were read from the plugins themselves, and
`characters.py --check` reads them again. Reading them found a defect: Fallout4.esm has TWO top groups of
NPC_ (and of LVLN, WEAP and eleven more types), and `plugin_forms.editor_ids` stopped at the first, where
Piper and Preston are not. It now reads every group (1ab3b66). Ghouls (Hancock), Nick and the robots are not
HumanRace; BodyGen's lines name HumanRace, so they are not here. Overture's companions are the vanilla ones
(Overture.esp defines no NPC).

**Amended the same day: Ivy (CompanionIvy.esm 000803), by the owner's poll.** Her lore, read from her own
plugin and her LoversLab page: an NX-2C pleasure bot built on a Courser combat chassis, carrying the brain of
Deborah, a raider boss ("as deadly as she is sexy", her creator's note; "I think my body was roughly based on
that model", of the Coursers). Of three drawn options -- an engineered hourglass over toned muscle, a
voluptuous showpiece, a lean Courser chassis -- the owner chose the first, which has both halves of what she
is. It measures "beautiful" (waist/hip 0.60). That makes 42 characters (25 women) and 45 records.
**Amended again that night, by the owner's poll:** in game she read "too slim" -- breasts 25, butt 15, thighs
and hips 10, next to the Rocket Bomb Extra Extra Extra (breasts 142, butt and thighs 120) she had rolled
before. Now a fuller hourglass over the same muscle: breasts 70, butt 60, thighs 50, hips 30, push-up 20,
7B Upper 15 (build c6d3bdcccc51). A body BodyGen gave keeps the values it was given, so she shows it after a
Reset on her (or Reset everyone).
Then, from a render of her now and two proposals, option B: "we was aiming on muscles + sexy ... do u think
woman with powerful ass muscles could have such a small ass?" -- butt 100, BigButt 75, RoundAss 80, MuscularButt 100,
AppleCheeks 60, HipBack 35, BackArch 35, hips 45, thighs 70, MuscularLegs 65, MuscularArms 70, ForearmSize 25, Back 30,
ShoulderWidth 10 (build 91ed55994014). The pool's measure calls her "Rough" now; it sorts only random bodies. The owner, in game after a Reset on her: "perfect!"

The bodies are written to `data/Tools/BodySlide/SliderPresets/Silhouette Characters.xml`; the generator reads
the sidecar `tools/pool/characters.json`. Each record becomes an npcFormID rule UNDER the user's: a rule the
config or an include already has for that record wins, however its form id is written, and the run says
whose rule it kept. A shipped include would have been simpler and wrong, because includes override the
main config (rules.load), and a user's rule for Piper would have lost to ours. A character's body is never
random (the pool is the only random source, S-65) and is in the picker like any preset. BodyGen follows an
actor's base up its template chain, so a rule on Cait's own record reaches her even though the record has a
template.

The female picker now holds 126 presets of the 128 a Papyrus array allows. A few more installed presets
and the generator refuses, and its message says how to hold some back
(`blacklistedPresetsShowInOBodyMenu`). *Superseded by S-69: the picker has no cap.*

**Amended 2026-09-25, by a rendered poll: Geneva gets her own body.** The mayor's secretary is famous among players
for being hot, and the pool had rolled her a Boxy one (Rough F16). From three renders (bombshell, sleek secretary,
pin-up) the owner chose the sleek secretary: a perky bust, a slim waist, a high round bottom. Her record is
`Fallout4.esm` `Geneva` 0x2F0A; the body measures "beautiful". Like every character's, it reaches her when BodyGen
next gives her a body: a per-NPC Reset, or Reset everyone.

## S-67 — MCM's "Back to random" is called Reset

Owner decision, 2026-09-24: "back to random should be renamed to reset bc its actually not random if its
unique companion its back to silhouete ruling this npc". Since S-66 the button's name was wrong. On Piper it
gives her own body back, not a roll. It forgets the body the player gave them, and Silhouette decides again,
as if they were met for the first time. That gives a named character their own body, someone under a name
or faction rule a new draw from that rule (S-60), and anyone else a new roll from the pool (S-65).

Only the label, its help and the message changed. The action is the same (`MenuRandom` →
`RequestRegenerate`: a new body at once, other mods' keyed morphs kept). The API's Reset (S-27, S-53:
bare now, a new body at the next load) is a different action with the same word, and MCM does not offer
it. Earlier entries and the code's comments say "Back to random"; they mean this button.

## S-68 — "Reset everyone": a fresh start, picks included, for the whole save

Owner decision, 2026-09-24, by two polls. S-65 and S-66 changed what people are GIVEN, but a body is given
once (OBody's rule, and BodyGen's): everyone already met kept the installed pin-up they had rolled, and
Piper, Cait and Ivy kept theirs, because a form-id rule is BodyGen's line and BodyGen reads a line only for
someone who has no body yet (the director leaves such a verdict alone, Director.cpp DecideBody, `kNone`).
Asked what the update should do to people already met, the owner first chose "everyone switches", then
decided against logic that fires once by itself: "better just wipe all bodies in mcm and reapply them
accordingly unique rules built in silhouette and new pool for random bodies". So it is a button. Asked
whom one press reaches: "Everyone, as met". Asked about bodies the player picked: wiped too, "who wants
fresh start rollback to silhouette only experience".

MCM > Silhouette > Bodies > **Reset everyone**, pressed twice within a minute (10 seconds at first: the owner's first try ran out while they read the first message box). Everyone is decided again
as if met for the first time: a roll, then the rules have their say -- a named character's own line, a
rule by name or faction drawing by id alone (S-60's presses forgotten), the pool for everyone else.
- **Around the player, at once**: everyone the plugin has seen this session whom Silhouette shapes (a
  distributed race, S-11; never the player or a dummy) and who wears a Silhouette body, or whom a rule or
  BodyGen now gives one. Someone blacklisted by name stays bare; a Silhouette body under another blacklist
  goes to what BodyGen's blacklist line gives (bare). Another mod's body is replaced only where BodyGen now
  gives a body -- for someone met later it cannot be dated, and is left.
- **On record, wherever they are**: every choice (the picker's, another mod's) and every picking in
  progress becomes a roll owed, which S-59 carries to their next sighting; every rule's kept draw goes back
  to the draw by id. A choice asked for before the press and not written yet -- queued, or with the bridge
  -- is replaced by that roll, so it cannot land after the press; and someone blacklisted by name who wore
  another mod's choice is rolled too, so the blacklist has its say at once. (Both found by fuzzing the
  press into the director's random sessions: 183 of 100,000 runs ended with a choice back on someone.)
- **Everyone met later**: a Silhouette body that a build older than the press made is decided again when
  they are met, once. The save remembers the press as the builds whose bodies count as made after it --
  the one current at the press, and every newer one loaded since -- so an update of Silhouette never sets
  it off again. A body the press's own build made before the press, on someone out of sight, stays: the
  rules of now made it.

The press lives in the co-save as a record of its own ('RST1', written only once pressed), not as an item
of the records: the record list refuses bytes after its last picking, so a block appended there would have
made every older plugin drop every record. An older plugin skips 'RST1' and simply does not follow the
reset. Protocol 4 (the bridge calls the new native).

## S-69 — The player picker has no cap: its lists come in parts of 128

Owner decision, 2026-09-24, by poll. The female picker stood at 126 of 128 presets, and the generator
refused a 129th. The cap is the Papyrus VM's: it grows no array past 128 entries, whether by `new` or by
`Add` (arrays a native returns are not capped). Asked what to do about it, the owner chose to lift it:
"Lists from the DLL", accepting that without Silhouette.dll the picker would show only the first 128.

It was done in Papyrus alone instead, so nothing is lost without the DLL either. `Silhouette:Player` lists
each sex's markers and names in PARTS of 128 (`FemaleMarkers0`, `FemaleMarkers1`, ...; both sexes have
the same number of parts, the shorter one's trailing parts empty). `Locate()` finds an entry across the
parts and `At()` reads one back; both count part p as the entries from p * 128 on, so every part but the
last one holding anything must be full. Census, Refresh, Show current and the NPC page's choice pass the
parts down; the regeneration window's heal without the DLL (Adopter.Heal) asks `MarkerAmong()`, and it
asks only for a body that holds a state at all, since building the parts costs one Add per preset.

`verify_bodygen.py` reads the parts back in order and refuses a part over 128 (the VM would drop its
tail), a short part before the last and a gap in the numbering: each would lose a preset or read one as
another. `tests/test_picker_parts.py` builds a 300-preset script and checks that it comes back whole; each
of its checks was proven on a broken generator or verifier (9 of 9). A 300-preset script compiles.

Left as it was: the MCM dropdown lists every preset in one list, and the census counts at most 128
different presets (a count, nothing is applied from it).

## S-70 — A save new to Silhouette gets Reset everyone by itself, and the press reaches every body

Owner decision, 2026-09-24, by poll. Loading a save from before Silhouette was installed, the owner found the
people already met kept their old bodies until Reset everyone was pressed, and some changed by themselves
while others did not. The log showed why (22:48: 39 people read, 1 body given). LooksMenu gives a body only
to someone with none stored, so the people who had none changed and the rest kept theirs; and Silhouette never
replaces a body it did not make on its own. Asked what a save new to Silhouette should do: "Do it
automatically", knowing that bodies from another mod or sliders set by hand are replaced too.

- **Once per save, by itself.** `Silhouette:Adopter`'s quest starts once in a save, the first time
  `Silhouette.esp` is in it (OnQuestInit; a window MCM opens later does not come through there). It marks the
  save as fresh, and its first scan presses Reset everyone as soon as Silhouette.dll answers (`FreshStart`),
  with one notification. A refusal (a picking in progress) is asked again at the next scan. If the plugin
  never answers in that first 24-hour window, nothing is pressed: a later install would otherwise take the
  bodies Silhouette's own files gave in the meantime. A new game comes through too, where nobody has been
  met and nothing changes.
- **The press reaches every body now (S-68 amended).** A body Silhouette did not make carries no build to
  compare with the press, so S-68 left it alone, even after a press. Now it is decided again once, at the first
  sighting since the press, where BodyGen gives that person a body and no rule by name or faction decides
  them (those are DecideBody's). That includes someone holding only other mods' keyed morphs, whom BodyGen
  never rolls (S-15's window reaches them only in its 24 hours). `RST1` holds who has been looked at since the press, after the stamps
  (`Registry::resetMet`, form ids resolved on load): sliders set by hand or another mod's body put on someone
  after that stay theirs. Everyone seen at the press counts as looked at. A new press starts the list again.
- **The record stays version 1.** A plugin refuses a record version newer than its own, so the list is
  appended after the stamps rather than bumping the version: the plugin that first wrote `RST1` stops reading
  at the stamps and follows the press as before; this one reads a record from before as "nobody yet".

`tests/main.cpp` (TestResetEveryone) covers the first sighting after the press in the same and a later
session, the hand-set body after it, someone with no BodyGen line, the second press, and the record's round
trip, load-order moves and older bytes. Each guard was proven on a broken build (mutants M1-M9 killed); M10,
the rule check moved after the roll, is equivalent: the rule's body replaces the extra roll when it lands.

## S-71 — The player is told when a change they asked for waits for another mod, and when it lands

Owner decision, 2026-09-25. The owner pressed Reset on Geneva, an AAF scene took her three seconds later, and
the new body landed only when the scene ended, five minutes on: it looked like the scene had changed her body.
Holding a change while another mod has someone busy is right (S-56); saying nothing about it was not: "would be
good to notify about such things in notifications for being more clear whats happening and when".

When an order in the urgent lane -- the player's own: the picker, MCM's page for the NPC in your sights -- is
deferred because another mod has the person busy, the plugin queues one line for the screen ("<name>: busy in
another mod's scene - the change you asked for waits until it ends"), once however often it is tried again, and
another when it lands ("<name>: the change you asked for is done"). Bulk work (Reset everyone's rolls, the
regeneration window) and other mods' requests wait without a word: they are not the player's to wait for, and
a press of Reset everyone would otherwise fill the screen. The bridge takes the lines with the new native
`NextNotice` on every poll, at most four a poll, and shows each as a notification. Protocol 5.

`tests/main.cpp` covers the line when it waits, told once over several tries, the line when it lands, and
silence for another mod's request and for bulk work; each was proven on a broken build (N1-N4).

## Seventh wave (pre-release review, 2026-09-25)

Five lenses on everything since d83d733, run on Sonnet by the owner's standing rule; each finding was traced
in the code and, where it held, fixed against a test that failed first.

- **A notice belongs to the change the player asked for (S-71 amended).** An actor's work takes the most
  urgent lane of everything it holds, so an order's lane cannot say whose change it is: a refit coming off as
  she undressed made a Reset everyone roll queued beside it read as the player's, and a scene then showed
  "the change you asked for waits". The body request now carries `asked`, set by the lane of the call that
  queued it (the urgent lane is the player's own); only such a body is said when it waits, and "done" is said
  only when that body lands -- another mod's change that took its place before it landed is not the player's.
  A touch-up is never said.
- **The met list keeps each person's NPC record (S-70 amended).** A created (0xFF) reference's id handed to
  somebody new (S-57) was taken for the person it had been, so the newcomer's body was never decided again.
  `resetMet` maps reference to record; a different record on a created reference is a first sighting. In
  `RST1` the list is now a tag ("MET2") and (reference, record) pairs; the first S-70 build's list of
  references alone is still read, with no record known.
- **The fresh start reads its own answer (S-70 amended).** `Adopter.FreshStart` compared the answer with
  `"not done: " + LastError()`, and LastError is shared by every caller: another script's call in between made a
  refusal read as done, and a save new to Silhouette was never reset. The new native `FreshStart` (protocol 5,
  not yet released) returns "" when done or why not, and logs the press itself. Its notification now says that
  other mods' bodies are replaced too.
- **The release ships the BodySlide presets.** `make-release.ps1` packed F4SE, MCM, the esp and the scripts but
  not `Tools/BodySlide/SliderPresets` (the pool and the named people), though the README said it packs the
  whole package; it now copies `Tools` and refuses to pack without both preset files.
- **Docs.** The README counted 41 named people (43 now) and did not say that adding `Silhouette.esp` back to a
  save runs the fresh start again, picks included.

Checked and sound: every native in DLL.psc is bound (69 of 69) and the three protocol numbers agree; the
crosshair's event layout and main-thread lookups; the picker's parts, their order against the menu, and all
45 character records; rolls owed across a save; the Adopter's timer after a load; new script variables on
old saves. Left as they are: a `RST1` newer than this plugin is not kept byte for byte as `REC1` is (the
record stays version 1 for good, so it cannot arise); VERSION is the owner's to set at release.
- **The bust floor holds for the named people too (S-65 and S-66 amended, owner poll).** The floor covered the
  pool's archetypes only, and eight hand-set women drew BreastsSmall past it -- the slider whose concave chest
  the physics folded: Piper 35, Cait 40, Desdemona 45, Madison Li 40, Marcy Long 50, Nisha 55, Kasumi 35, Aster
  50. Asked, the owner chose "Apply it to them": each is capped at 30, the pool's small-but-real bust, and
  `tests/test_pool.py` (BustFloor) holds `characters.json` to it as it holds the pool (it failed on exactly those
  eight first).

## S-72 — Each faction draws from a body pool of its own

Owner decision, 2026-09-25, by poll: "do separate pools per faction for they still be diverse but hold faction
specifics". Poll answers: the military (Brotherhood of Steel, Minutemen, Gunners), the outlaws (raiders,
Nuka-World's Disciples, Operators and Pack, the Triggermen) and the Institute & co (the Institute, the Railroad,
the Children of Atom -- the Commonwealth's and Far Harbor's); not the towns and settlers; women and men; ten
bodies per faction and sex; the faction's look with the main pool's mix inside.

- `tools/pool/factions.py` builds each pool exactly as the main pool is built (generate.py, which now takes a
  faction's archetypes): the main pool's archetypes in the faction's own proportions, some shifted toward its
  look -- muscle for the Brotherhood and the Pack, harder bodies for Gunners and Railroad agents, thinner limbs
  for raiders, the Disciples and the Children of Atom, softer ones for the Institute and the Triggermen. Every
  candidate is built from the installed body, measured, and kept only if it measures as its tier; the most
  different survivors are chosen. 6 plain, 3 rough, 1 fine per sex; the bust floor (S-65) holds for every range.
  220 bodies in `Silhouette Factions.xml`, named "Gunner Plain F03", "Raider Rough M01"...
- Each faction becomes a faction rule (OBody's `factionFemale` / `factionMale`, applied by the plugin, S-23),
  merged UNDER the user's own: a rule of theirs for the same faction and sex wins. The rule lists a plain body 3
  times and a rough or fine one twice -- 18 : 6 : 2 entries, 69 / 23 / 8 per cent -- since the plugin picks one
  entry by the person's id; the catalog now keeps a faction rule's repeats (it used to fold them). The gangs and
  the Triggermen come before the raiders, as the first rule whose faction the NPC carries is theirs.
- A faction rule outranks the random pool, plugin and race rules and loses to per-NPC rules, so the named
  people -- Danse, Preston, Cait -- keep their own bodies (S-66). People already met get their faction's body
  as the rule reaches them (DecideBody gives a rule's body to anyone it names), or at once with Reset everyone.
- The picker lists them like the pool's (237 women, 176 men now; S-69's parts hold any number).
  `make-release.ps1` refuses to pack without `Silhouette Factions.xml`.

`tests/test_factions.py` holds the sidecar and the XML to one run, 6/3/1 per faction and sex, the floor, the
names, the weights and order of the rules, a user's rule winning, and the shipped catalog's repeats;
`test_generator_main` holds a fresh catalog to them, which failed when the catalog folded the repeats.
`factions.py --check` re-measures every body and looks every faction up in its plugin, without case, as the
game does (Far Harbor's is `DLC03ChildrenofAtomFaction`).

## S-73 — MCM switches for the fresh start, the faction pools and the notices

Owner decision, 2026-09-25, by poll at release preparation: three things Silhouette does by itself get a switch
in MCM > Settings, "What Silhouette does by itself", each on by default.

- **Fresh start for saves new to Silhouette** (`bFreshStart`, S-70). Read by `Silhouette:Adopter.FreshStart`
  before it presses: off, it waits and is asked again at every scan, so switching it on while the save's first
  24-hour window is open still presses it. MCM's value counts only while its sentinel (`iDefaults:Meta`) says it
  was read, as for every switch: an unread key reads False and would have switched it off.
- **Faction bodies** (`bFactionPools`, S-72). The catalog marks the rules of Silhouette's own faction pools
  (`"pool": true`, optional: a catalog from before reads as none), the bridge pushes the switch with the others
  (`Configure` takes it: protocol 5, not yet released), and `Decide` leaves the marked rules out when it is off --
  the faction is then drawn from the random pool; a user's faction rule applies either way. It decides who is
  given a body from then on: a faction body already given stays until Reset.
- **Tell me when a change I asked for waits** (`bNotices`, S-71). The bridge still takes every notice from the
  plugin, so none waits to show later, and shows them only while it is on. Stored as `_hideNotices`: a variable
  added to a script is False in a save made before it, and False is "shown".

The MCM's texts were brought up to date at the same time: the Bodies page said every NPC gets "one of your
BodySlide presets" (S-65 made the pool the only random source) and the default "the most average body of your
presets"; they now name the pool, the named people and the factions' pools.

## S-74 — A release carries Silhouette's own presets and CBBE's and BodyTalk's stock ones, nothing else

Owner decision, 2026-09-26, by poll, after 0.1.0's first archive was found to carry 68 presets installed on the
author's machine -- The Rocket Bomb Body, Josie, ALSL, PLP, Oxton, That Gym Booty and more, from other pages --
baked into the templates, the catalog, every picker and every earlier build's manifest. None was ever random
(S-65), but their values were redistributed, and a player would have seen presets they never installed.
`make-release.ps1`'s own header had warned against exactly this; nothing enforced it. Asked what a release
should ship: "Own + CBBE/BodyTalk stock" -- the presets that ship with CBBE and BodyTalk themselves, which
nearly every player has.

- **The allowlist** is `tools/release_presets.json`, by origin, not by look: CBBE's own `CBBE.xml` (its stock
  bodies, the Imitation UNP and Dream Girl presets and the SevenBase ones -- all four families ship in that one
  file) and BodyTalk's `BT-*.xml`. Measured on the author's install: which mod folder each XML came from.
- **`silhouette_gen.py --write --release`** reads the package's own presets and, from the game's folders, only
  the allowlisted ones. Without `--release` it takes everything installed, as before: that is the author's
  own game, and anyone who wants their presets in the pickers.
- **`verify_bodygen.py --release`** fails a catalog holding any other preset, and `make-release.ps1` always
  passes it: a release generated without `--release` cannot be packed.
- **Manifests.** Every build's manifest holds the values of the bodies it could give (S-6, S-12). The 14 of
  the builds before this one carry the author's presets; they stay whole in the repo, and
  `tools/release_manifests.py` strips those presets' entries from the archive's copies and keeps the rest. It
  refuses to pack if the current build's own manifest carries any.
  **Amended the same day** by a dev-against-release comparison (publisher-bud, after an Anatomy regression of
  the same shape): the first cut dropped the 14 manifests whole. `Catalog::PresetForMarker` names a body only
  through its build's manifest (or the current catalog, for the current stamp), so every body an earlier
  build gave became unnamed -- no "Which body", no touch-up, no heal. The author's save held 47 such bodies,
  all of Silhouette's own presets (build 11221959). No published collection ever carried Silhouette, so no
  player had one; stripping instead of dropping keeps them named anyway. Measured against the author's tested
  dev deploy at the same time: the 348 templates, the 52 BodyGen lines, the catalog's rules, variety, ORefit
  and player sections are identical apart from the marker's value (the build stamp); ORefit falls back to the
  built-in set for an unnamed body, as for every body in dev. Checked against the author's save: the
  published archive named 0 of its 47 bodies, the stripped one all 47. An early manifest's `player` record
  also named a preset of the author's (S-10's first medoid): foreign names go from it too.
  `make-release.ps1 -Built <folder>` proves the package against a zeroed body there, passed to the verifier
  and to the tools' tests (`SILHOUETTE_BUILT`): the day the author deployed Anatomy's own rebuilt body (25,299
  vertices in its CBBE shape, against the CBBE reference's 22,708), no reference on disk matched it, and a
  release could not be verified against Data at all.
- The pickers now hold 192 presets for women (177 of Silhouette's, 15 of CBBE's) and 175 for men (168 and 7).

The repo's history keeps the earlier generated files; this stops them from shipping, not from having been
committed.

## S-75 — Every game version through Runtime Database, fail-closed where a layout differs

Owner decision, 2026-09-26, after the first Nexus comment from a player on another runtime: "we will support
all versions via https://www.nexusmods.com/fallout4/mods/108394" -- Runtime Database (Zzyxzz), with its
library CommonLibF4RD: one plugin for OG 1.10.163, NG 1.10.984 and AE 1.11.x, addresses found at run time
from `f4rd-runtime.bin` (which players install from that page). Asked how NG and AE get proved on a machine
that only has OG: a public beta -- an Optional file on Nexus for NG and AE players to test, with Runtime
Database's `.trace` / `.mapping` diagnostics; the Main file stays the OG build until they confirm.

- **The library.** `SILHOUETTE_RUNTIME_DATABASE` (CMake, ON) builds on `extern/CommonLibF4RD`; OFF builds the
  classic OG-only plugin on alandtse's CommonLibF4 (S-18). The API is the same but for four calls, kept in
  `src/Compat.h`: the NPC's sex (a plain number there), a reference's name (no GetDisplayFullName: the RD build
  names a reference by its base record, as OBody reads names for its rules), the biped slots (an enumeration
  there), and the co-save interface's constness. `F4SEPlugin_Version` is exported for NG's and AE's F4SE
  (addresses by signatures); OG's F4SE asks `F4SEPlugin_Query`, which no longer refuses other runtimes.
- **Layouts are not addresses.** Runtime Database's own guide: "runtime-aware relocations do not make class
  layouts automatically compatible". Silhouette hooks nothing and has no address of its own -- it calls the
  library's functions, listens to events (found by their RTTI names, EventSources.cpp) -- but it reads a few
  members directly: `Actor::biped` and `race`, `TESNPC::formRace`, `faceNPC` and `formSkin`, `TESRace::formSkin`,
  the race's `formEditorID`, `BGSKeywordForm::keywords`, the biped slots, `ProcessLists::highActorHandles`.
  None of them could be checked here on NG or AE. So `Game::GuardLayout` reads each once -- on the player
  and on the Vault 111 jumpsuit, when the player's body is built -- and checks it against what it must be
  (a race is a race form, an editor id is text, the jumpsuit fills body slot 33, every keyword is a
  keyword, every high process handle is an actor, every biped slot holds a form or nothing), under a fault
  guard. One that does not check out refuses the plugin for the session with one log line naming it: rules
  by name and faction, ORefit, the picker and the API are off, and BodyGen still gives every body. Never
  half on.
- **Not claimed.** The beta's page says what was proved where: OG in the author's game, NG and AE by
  testers' logs.
- **Verified in game on OG**, 2026-09-26, by the owner with the RD build (DLL d4a965b8f4c54764, Runtime
  Database installed): "runtime 1-10-163-0 (OG), addresses through Runtime Database", "layout: every member
  Silhouette reads checks out on this runtime", all three event sources attached, a save's 39 co-save
  records read back, 39 probes and refits, and on a save from before Silhouette the fresh start (S-70): 35
  body orders, 9 refits, none failed. One new line, "failed to get next record info", was CommonLibF4RD's
  wrapper warning at the co-save list's normal end; the list is now read through F4SE's own interface.
- **First AE run** (1.11.240, 2026-09-26, fallout-collection with the owner): Silhouette.dll bound, the catalog
  ready (371 presets, the player's own among them), events attached, co-save read -- but the bridge refused:
  the Anniversary LooksMenu (1.7) registers its F4SE plugin as "Fallout 4 Engine Extender", old-gen's as "F4EE",
  and the scripts asked for "F4EE" only. `API.LooksMenuLoaded()` accepts either and every script asks there.
  MCM registers "F4MCM" on both. A name by which a plugin registers is part of the runtime too.
- **Verified on AE** with 0.2.1 (fallout-collection, 1.11.240): the layout line, 4 own presets, 39 probes with
  0 failed, the picker's steps, refits off and on around scenes, no errors.
- **Amended 2026-09-27 (owner):** 0.2.1 is the Main file, for OG and AE; 0.1.1 stays among the old files for
  OG players who will not install Runtime Database. Next-gen is not a supported target -- "nobody use next
  gen ... its pointless version" -- so no tester call and no claim for it. The build still declares NG's
  layouts to F4SE because RD costs nothing there, and the layout guard would still turn a mismatch off.

## S-76 — The player's own presets, read in game: the try-on, never random

Owner decision, 2026-09-26, by poll, after players asked how to use their own BodySlide presets: "Yes, read
them in game" -- no Python, no generator, no Papyrus compiler. Fallout 4's MCM builds no dropdown list at run
time (the options are fixed in config.json, and the plugin does not rewrite a mod's files inside a mod
manager's folders), so the second poll settled how they reach the pickers: "Try-on for NPCs and yourself"
-- they join the NPC picker's Next / Previous, and the player gets the same try-on; the dropdowns keep
Silhouette's own bodies and CBBE's and BodyTalk's stock ones (S-74).

- **Read as the generator reads them** (`src/Presets.cpp`, pure, tested offline): every SliderPresets file in
  BodySlide's order, the first preset of a name winning in any case; a slider counts by its big (or both)
  value; the fit is the share of its sliders the body's `.tri` carries -- full at 95 %, partial at 50 % only
  for the body's own family; outfit-tuned copies and refit sets are left out; the values are resolved
  through the slider set the body was built with (the set's default where the preset is silent, inverted
  where the set says so), kept where the body has the morph and never a runtime state or the shaft. The
  marker is the generator's `plain_marker`. Mutants on the inversion, the defaults, the family rule and
  small-size values each fail a test.
- **The slider set travels in the catalog** (`sliderSets`, optional: a plugin that does not know it ignores
  it, a catalog from before has none and the player's presets are then not read). The generator writes the
  set it measured each body against.
- **Absolute builds only.** In a compensated build every value is relative to what the base has baked in;
  that is not guessed at run time.
- **Never random, never a rule's.** They are picker presets: the random pool stays Silhouette's (S-65).
  Their values come from the player's own files, so nothing is redistributed (S-74).
- Names and markers the catalog already has stay the catalog's. A body given one of them and saved stays
  named for as long as the preset stays installed; removed, the body stays as it is, unnamed.

## S-77 — The Diamond City pack: the people you meet, sardonic on purpose

Owner poll, 2026-09-27, "which NPC pack next": the Diamond City pack, "and rest save for future updates" --
with a direction for every named body from now on: "we need make some of unique npcs TRULY unique memorable
bodies it could also be sardonic to their stories/characters. for ex mayour of dc which is quite unpleasant
could very very fat". From renders of each body, the owner: "Ship as is".

- **Who.** Not a list from memory: the unique HumanRace people the author's last 12 saves hold bodies for.
  LooksMenu keys a body by the actor REFERENCE, so each reference was mapped through its ACHR record to the
  NPC_ it places, in the masters themselves: 46 unique people met, 7 already named. The pack is the named ones
  of the rest (Diamond City's "Resident F01"-style uniques are left to a future town pool): Polly, Cathy,
  Becky Fallon, Scarlett, John, Arturo Rodriguez, Solomon, Doctor Sun, Abbot, Sheffield, Malcolm Latimer
  (and his quest photo's record), Finn, Wayne Delancy, Parker Quinn, Winlock, Barnes, Rufus Rubins. Every
  record was read from Fallout4.esm; `characters.py --check` holds them.
- **What.** Each body is the character's story made visible, from the Fallout wiki's own words (gathered
  verbatim, 2026-09-27): McDonough ("Mankind for McDonough", threw out the ghouls) grossly obese -- volume 540
  against 340-445 for every other body; Sheffield ("doctors said I shot my liver") a swollen gut on stick
  limbs; Abbot, keeper of "the great, green guardian", a wall himself; Becky Fallon of Fallon's Basement a
  small bust over enormous hips; Parker Quinn the charge-card con man all chest on chicken legs; Malcolm
  Latimer, who pays the mayor and hires his dirty work, a pampered pear; Solomon ("balance you out") the
  least balanced man in the market; Winlock and Barnes a boss and his soft sidekick; Rufus Rubins, who works
  for room and board, the one honest wiry body in Goodneighbor. Parker Quinn's face is modelled on a real
  person, Fallout 4's lead designer; his body's joke is the scam, not the man.
- **Mayor McDonough reworked** from "well-fed and soft" (stomach 60) to the owner's "very very fat".
- **Limits, measured:** the BodyTalk thin sliders are weak, so the scrawny men (John, Solomon, Doctor Sun,
  Finn) read as scrawny rather than as themselves; the owner shipped them so. A body BodyGen already gave keeps
  its values: an NPC shows the new body after Reset on them (or Reset everyone), as S-66 found for Ivy.
- 60 named people (30 women, 30 men), 64 records; the pickers hold 196 for women and 188 for men. Build
  09f9637ce99e, stamp 653667; its manifest joins the others (S-12).

## S-78 — A sex with no body Silhouette supports is left alone, and the player is told once

A request (2026-09-30): Silhouette with one body mod installed, CBBE without BodyTalk or the other way round.
The owner's rule, word for word: "if we don't see the user's proper supported body we just do nothing on this
sex, and warn it in logs", and "better show popup in game with button OK".

- **Measured at load** (`Presets::MeasureBody`, called by `Game::CheckBodies`): the loose
  `FemaleBody.tri` / `MaleBody.tri` against the distinct sliders Silhouette's random pool sets for that sex.
  Under half carried, or no loose `.tri` with morphs, and that sex has no supported body. Another family's
  body (Fusion Girl under CBBE presets) shares a few names at most, so it lands under half; the owner's
  install carries 31 of 34 female and 25 of 34 male.
- **Left alone like a race Silhouette does not distribute to:** `Catalog::bodySupported[sex]` gates
  eligibility (`Distributed`), so nobody of that sex is probed, ruled, rolled, reset, refit or touched up.
  The NPC picker and the API refuse them and say why (`Shapeable`).
- **Told:** one `warn` line a sex in Silhouette.log with the count or the missing file, and a
  `Debug.MessageBox` (OK) once a launch from the bridge's Connect (`BodyWarning`, protocol 6).
- **Not in reach:** LooksMenu still reads Silhouette's BodyGen lines for that sex. Their sliders are ones
  the body does not have, so nothing moves; a body that shares a few of them could move a little.

## S-79 — The picker window: Scaleform, live on the character, with thumbnails

Owner poll, 2026-09-30, three answers: "a full featured window with even preview render characters with
selected shapes before apply them", picked from the options below.

- **Scaleform**, a native FO4 menu (LooksMenu's and SAM's kind): mouse and controller, OG and AE, no new
  dependency. It is built on F4SE's own menu API, not CommonLibF4's IMenu code, whose `VisitMembers` crash
  on 1.10.163 is why S-22 had no window. Dear Modding UI (ImGui, OG support unknown) and PrismaUI (a
  Chromium overlay) were offered and not chosen.
- **Preview: live on the character, plus thumbnails.** A grid of pre-rendered, non-explicit silhouette
  thumbnails for Silhouette's and the stock presets (the player's own presets get a plain card); choosing
  one puts it on the real character live, with the camera on them; Apply keeps it, Cancel restores exactly
  what they had -- the S-22 picker's Keep/Cancel machinery. A rendered 3D copy inside the window was offered
  and not chosen: FO4 has no ready offscreen render of a clone.
- **One window for NPCs and the player's character.** It replaces the MCM dropdowns and the hotkey
  cycling as the way to choose; the hotkeys stay as a shortcut.

**S-79 as built (0.3.0).** The window is `interface/src/*.as`, compiled from code with Apache Flex's mxmlc (no
Animate), registered with `UI.RegisterCustomMenu` (ScreenArcherMenu's flags plus the menu input context, 0x8) and
driven by `Silhouette:Bridge` through `UI.Invoke` and F4SE's external events; the plugin adds the picker's list,
"show this preset" and the camera. Measured in game and fixed: "tfc" is carried out a frame or more after it is
typed, so the camera moves towards what the window wants, one toggle at a time, and the bridge waits ("wait"
answers); Papyrus hands the script to another thread at every LooksMenu or plugin call, so the window works in
sessions -- open, close and Them/Me each start one, and older work stops and takes back a hold or a camera that
landed late. The NPC is held with `SetRestrained` (their AI keeps running), released on every close, switch and
load. The pictures are two atlases named by the build (`Thumbs<Sex>_<build>.dds`), mounted through F4SE's
`MountImage`; cell k is the k-th preset of the list, and an atlas of another build is never mounted. Six review
lenses before release (microscope S-79 wave 1).

## S-80 — The installer: the house FOMOD standard, first in 0.3.0

Owner, 2026-10-01: every mod's next release ships a FOMOD to `nexus-tools/docs/FOMOD-STANDARD.md`. For
Silhouette, `tools/fomod_pack.py` writes it into the release folder: the install is refused unless LooksMenu.esp
is active and the game is 1.10.163 or newer (the only requirements a FOMOD can see in both Vortex and MO2);
every top-level entry of the release is installed as it is, read from the folder; one step shows what Silhouette
does, a card each; a note appears only when AAF is missing. It is validated against the 5.0 schema Vortex itself
uses. F4SE, Runtime Database and MCM cannot be seen by a FOMOD: the plugin and the bridge say so in game, and the
Nexus page lists them. Manual installs are not supported.

**S-79 amended (0.3.1, 2026-10-02): the window's input, measured in game.** 0.3.0 shipped review-wave input
changes untested and the window took no click for a tester (Alt-F4 to get out). Fixed from the owner's tests
and the window's own log: (1) a custom menu does not take the game's controls -- an InputEnableLayer turns the
player's off while it is open; (2) the free camera handles the mouse itself -- its input is muted while held;
(3) under the free camera the game delivers NO mouse click to a menu, it turns the left button into "WorldZUp"
-- the window clicks what is under the cursor itself, as ScreenArcherMenu does; (4) F4SE keeps the window's
movie between openings -- every opening starts it again (Panel.Begin). The menu input context (0x8) and the
gamepad's cursor removal (extended flag 2) are gone; the gamepad is not claimed. Ways out: Cancel, the window's
hotkey again, `cgf "Silhouette:API.CloseWindow"`. The window logs what it receives ("window:" lines).

## S-81 — The dead are left alone (0.3.2, 2026-10-02)

Reports of pre-placed corpses showing only their head and hands (no logs yet). Re-applying morphs to a ragdolled
corpse is the lead on Silhouette's side (Anatomy-specialist's), so Silhouette writes nothing to the dead: a body,
refit or touch-up order for a dead actor is not carried out (OrderGone: it waits for a sighting, so the drain does
not spin); a probe or a snapshot still reads them. The window, the Pick hotkey and MCM's Apply/Random buttons
refuse a dead target and say why. A body LooksMenu's BodyGen gives at cell load is not ours to stop. Shipped with
Anatomy Engine 1.2.2's guard against non-finite physics: a "[physics] ... went non-finite" line in
anatomy_ocbpc.log puts a remaining case on the physics; without one, on LooksMenu's own BodyGen at cell load.

**S-79 amended (0.3.3, 2026-10-02): the Me tab lists your own presets.** A tester's own BodySlide presets went on
NPCs but the Me tab showed none: it listed the player script's presets, fixed when the release was generated,
while your own presets (S-76) are read by the plugin in game. With the plugin the Me tab now takes the plugin's
list, exactly as the NPC tab does (protocol 8: PlayerPresets, PlayerPresetIndex, PlayerBody*), and writes the
body the player script would -- the preset's values and its marker, no variety, the unkeyed layer cleared first.
Without the plugin it is the player script's list as before. Undo is unchanged (the snapshot of the player's
morphs). MCM's Bodies page still lists only the player script's presets.

## S-82 — Invisible Dead Body Fix is required (owner, 2026-10-03)

The invisible corpses (S-81) were found by a player: an old LooksMenu bug. With BodyGen on, an NPC the game places
dead gets morphs, and the game skips that corpse's 3D update unless a flag is set (taking an armour off them sets
it -- why the body appears then). Lee3310's Invisible Dead Body Fix (Nexus 93614, an F4SE plugin, old-gen/next-gen
and Anniversary builds) sets the flag as their cell loads; the player confirmed it works with Silhouette. Silhouette
gives everyone a BodyGen body, so the fix is a requirement: on the Nexus page, in the installer's setup checklist
(a FOMOD cannot see a DLL), and checked at launch -- its file among F4SE's plugins, any name holding
"deadbodyfix"; missing, a warning in Silhouette.log and once a launch in the S-78 box. Not reimplemented: its
source is not published, it works, and two copies of the same flag trick could fight. S-81 stays: Silhouette
writes nothing to the dead either way.

## S-85 — Servitron gets bodies (owner, 2026-10-06)

A player asked for Servitron (Nexus 32801, 5133p39): an Automatron robot whose parts BodySlide builds on CBBE's
sliders (47 sets, 84 sliders: Breasts, Butt, Hips, Waist...), so BodyGen's morphs move them (read from the files
by Anatomy-specialist). `ServitronRace` (Servitron.esm|000F99) joins the default `distributeRaces`, and
`rules.OPTIONAL_RACES` names it with its plugin: a player without Servitron has no such race, its two lines
match nobody, and the generator and the verifier say so instead of refusing. Both sexes' lines are written:
which sex the game gives a Servitron is not known from the plugin (its only NPC is a male-flagged dummy; the
robots come from Automatron's templates), and a man's BodyTalk sliders move nothing on CBBE parts. Its physics
is Anatomy's (a 3BBB version on Nexus 109602). The build is unchanged: only the lines and the rules hash.

## S-86 — A race's body sex, and the races the plugin draws itself (owner, 2026-10-07)

S-85 did nothing in game: the engine flags every robot male, Servitrons included, while their parts are built on
CBBE's sliders, and Servitrons are made from Automatron's templates, which BodyGen's race lines never reach (an
All line lists only NPCs without a template). The picker offered a Servitron men's presets and she got no body.
Now `rules.OPTIONAL_RACES` gives such a race the sex of the body it wears, and the catalog's `rules.racePool`
carries it with that sex's random pool, each body as often as its tier weighs. The plugin reads the actor's
sex as the body's (`Game::IsFemale`, the sighting's facts), so presets, the picker and the pools are the women's;
Decide draws the race's body after the factions, through the faction path. LooksMenu files morphs under the
actor's own flag, so the bridge writes every morph under the real sex, not the plugin's. BodyGen's lines give
the race its body sex's pool in both tables, for a Servitron some mod places without a template.

## S-87 — Servitrons take no breast sliders (owner's poll, 2026-10-08)

A Servitron's breasts are separate armour pieces (a "Boobs" shape inside a torn suit, metal caps). Anatomy
measured every template on them offline (fo4-anatomy studies/servitron_bodygen_clip.py): in about half, the
breast sliders flatten the breasts until the suit's torn edges sit in front of them -- shards across the chest in
game. The owner chose to keep shaping Servitrons without those sliders. `rules.OPTIONAL_RACES` gives a race a
`without` list (37 breast, nipple and chest morphs for Servitron), carried in `rules.racePool`. The plugin never
writes them on that race -- a body, a preview, a refit's floors, the variety -- and its touch-up reads and takes
off the values 0.3.5 wrote. Such a race has no BodyGen line any more: BodyGen cannot reach its NPCs made from
templates, and a line cannot leave morphs out.

## S-88 — Servitron's list is lifted by Anatomy's fixed torsos (2026-10-08)

The owner had Anatomy push the suit behind the breasts in its Servitron torsos; measured offline, the chest stays
clean under every template. Plain Servitron still clips (182 of 365 templates on 32801's own torso), so S-87's
list stays the default, and `rules.racePool.withoutUnless` names a file only Anatomy's package ships
(`F4SE/Plugins/Anatomy/Servitron.ini`): at launch, when it is under Data, the plugin lifts the list and says so
in Silhouette.log.
