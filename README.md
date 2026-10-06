# Silhouette

OBody NG's body distribution, for Fallout 4. Every NPC gets a body the first time you meet them, and
keeps it: an ordinary one most often, an unflattering one less often, a conventionally beautiful one
rarely, from Silhouette's own pool. The Commonwealth's named people -- companions and major NPCs -- each
have a body of their own.

**Status:** 0.3.4: the picker window (S-79) -- choose anyone's body, or your own, from a grid of pictures,
live on them while the camera frames them. One plugin for old-gen 1.10.163 and Anniversary 1.11.x through
Runtime Database (S-75); your own BodySlide presets in the picker (S-76); the Diamond City pack (S-77); one
body mod is enough (S-78).
Next-gen 1.10.984 is not supported: nobody plays it. The last release without Runtime Database is 0.1.1
(old-gen only). Phase 1 (the generator; LooksMenu's own BodyGen does the
work at run time) and Phase 2 -- `Silhouette.dll`, an F4SE plugin, with `Silhouette.esp` -- which adds the
rules only a running game can see, ORefit, the NPC picker, the touch-up, faction pools and an API with
OBody's names. Everything is tested offline (over 540 plugin tests and 140 tool tests) and has been played in the
author's game; the in-game checklist is [docs/phase2-test-plan.md](docs/phase2-test-plan.md). Please report
what you see. Every feature: [docs/FEATURES.md](docs/FEATURES.md). What maps where:
[docs/obody-feature-map.md](docs/obody-feature-map.md); how Phase 2 works:
[docs/phase2.md](docs/phase2.md).

## What it does that a hand-written BodyGen file does not

It **measures your install** instead of assuming it.

- **Which presets can do anything.** A preset whose sliders your body's `.tri` does not have is a
  silent no-op. Silhouette reads the `.tri` and hands out only presets that move your body — on the
  machine it was written on, 27 of 120 presets (Fusion Girl, Atomic Beauty on a CBBE body) were left
  out.
- **What your base body already has baked in.** BodyGen adds on top of the mesh on disk, so if you
  built your body with anything but Zeroed Sliders, every NPC preset stacks on it. Silhouette fits
  your built body against BodySlide's reference mesh and names the preset it was built from — exactly
  ("CBBE Chubby", 0.005% unexplained) — and tells you which zeroed preset to rebuild with. If you
  cannot rebuild, `--compensate` writes every template relative to what is baked instead, and every
  NPC still lands exactly on its preset.
- **What it wrote.** `tools/verify_bodygen.py` reads the files the way LooksMenu reads them and
  builds every body they can produce: each lands within 0.031 units of its preset.

It never re-rolls an NPC you have already met, and leaves zeroed presets out of the random pool as
OBody does.

## Who gets which body

- **Everyone: Silhouette's pool** (`Silhouette Pool.xml`, S-65). 41 bodies a sex, generated and measured
  rather than hand-picked: 18 ordinary ("Plain"), 17 unflattering ("Rough": very fat, very flat, frail,
  disproportioned) and 6 conventionally beautiful ("Fine"). A Plain body is listed three times in
  BodyGen's random line, so the odds are 70 / 22 / 8 per cent. Your installed presets are not random;
  they stay in the picker.
- **Named people: their own body** (`Silhouette Characters.xml`, S-66). 60 companions and named NPCs,
  from Piper to Porter Gage, each given a body that fits who they are -- the Diamond City pack's
  cheerfully sardonic (S-77): the mayor is enormous. A rule of yours for the same NPC
  wins.
- **Factions: a pool of their own** (`Silhouette Factions.xml`, S-72). The Brotherhood, the Minutemen,
  the Gunners, the raiders and Nuka-World's three gangs, the Triggermen, the Institute, the Railroad and the
  Children of Atom each draw from ten bodies per sex in their own look -- drilled soldiers, lean mercenaries,
  underfed raiders, soft scientists -- with the pool's odds inside: mostly plain, some rough, a rare fine
  one. The named people keep their own; a faction rule of yours wins.
- **Your rules** by form id, name, faction, plugin or race, as in OBody (below).

## Your character

Never randomised. A character with no LooksMenu body sliders gets the **most average** body of the
pool, weighted as NPCs are given it — measured: the one whose body is closest to the mean of them all
("Plain F01" and "Plain M02"). **MCM > Silhouette** lists every preset that fits your character's body:
choose one and press *Apply to my character*; *Back to the default* and *Which body do I have?* are
next to it. No plugin is involved — the menu calls a small script LooksMenu drives.

## People other mods marked first

LooksMenu only gives a body to someone who holds no body morphs at all, so an NPC another mod
already marked (an AAF morph left behind by a scene) never gets one. With `Silhouette.esp` enabled
(a light plugin, no load-order slot), for 24 in-game hours after Silhouette first loads, such people
around you are given a body and the other mod's morphs are kept; MCM shows the window and can open a
new one.

## With the plugin (Phase 2)

`Silhouette.dll` decides; a Papyrus script on `Silhouette.esp` carries its decisions out through
LooksMenu, a moment after they are made (LooksMenu is reached through Papyrus, about a frame a value).
Without the DLL, everything above keeps working.

- **Rules by name and faction, and a name blacklist** — OBody's keys and OBody's priority, in
  `F4SE/Plugins/Silhouette/Silhouette_presetDistributionConfig.json`, which lists every key, written out:
  the defaults, plus a few entries for known mods (Eli's armour compendium: which of its armours count as
  heavy). The plugin reads what the generator compiles from it, not the file: after editing it, run the
  generator and install what it writes. A name is the NPC record's, as OBody reads it. BodyGen carries
  every rule it can (form ids, plugins, races); the plugin applies the rest. Someone a rule gave one of
  several presets keeps it while the rule still lists it; MCM's *Reset* draws from the rule again.
- **ORefit** — while someone is dressed: breasts held together and lifted, and nipples flattened under
  heavy clothes -- armour, jackets, coats, told by the item's name (`heavyWords` in the config; items can
  be named heavy or light too). The flattening shows only on outfits whose meshes carry the refit sliders
  (mod outfits built with them; no vanilla heavy outfit does). It only ever raises a slider, under a keyword of
  its own: the moment they undress they are exactly their own body. Removing `Silhouette.esp` takes every
  clothed shape off by itself, and without a working DLL the bridge takes them off the people around you.
  Nobody in power armour is refit. `<Preset>-Refit` BodySlide presets and OBody's outfit lists work as in
  OBody.
- **The picker window** (S-79) — aim at someone and press its hotkey (MCM > Silhouette > *The NPC in your
  sights*), or use the button on that page (aimed at in the half minute before) or on the *Bodies* page (you):
  a window beside them shows every preset that fits their body as a grid of pictures, filtered (People,
  Plain, Rough, Fine, Yours). A click puts one on them live while the camera frames them and they stand still
  (their AI keeps running: quests and companions carry on); your own controls are off while it is open.
  *Apply* keeps it; *Cancel*, the window's hotkey again, or the console (`cgf "Silhouette:API.CloseWindow"`)
  closes it and puts back exactly what they had. *Them* / *Me* switches to your own body. The pictures show
  each preset on the author's zeroed bodies, one scale a sex; the live try-on shows yours.
- **The NPC picker hotkeys** — unbound until you set them in MCM > Silhouette > *The NPC in your
  sights*: aim at someone close enough to talk to, *Pick*, *Next*/*Previous* to try every preset on them
  live, *Keep* or *Cancel*. The MCM page *The NPC in your sights* gives a preset, *Reset* (Silhouette decides
  their body again: a named character's own body, a rule's draw, or a new roll from the pool), or names
  the one they have. A choice is kept like a rule's, and marked in LooksMenu, so a save made without the
  DLL keeps it (MCM's *Refresh* pressed without the DLL gives the body again without the mark).
- **Reset everyone** (MCM > Silhouette > Bodies, pressed twice) — a fresh start for the whole save: every
  body Silhouette gave, the ones you picked too, is decided again as if everyone were met for the first
  time. The people around you change at once, everyone else the next time you meet them; the save
  remembers it, and a later update of Silhouette does not set it off again. After updating to the pool,
  this is how people you had already met get it. It reaches bodies Silhouette did not make too (from
  before it was installed, or another mod's), each once: at the first sighting since the press.
- **A save that has never had Silhouette** gets *Reset everyone* by itself, once, on its first load with
  Silhouette: without it, everyone you had already met would keep the body they had before, since a body is
  given only to someone who has none. Like the button, it replaces other mods' bodies and sliders set by hand
  on the people it reaches; a notification says when it has been done.
- **Notifications** — when a change you asked for (a Reset, a preset given, a picker try) has to wait
  because another mod has them busy in a scene, a notification says so, and another when it is done.
- **Switches** (MCM > Silhouette > Settings, *What Silhouette does by itself*, each on by default): the fresh
  start for saves new to Silhouette, the faction bodies, and the notifications. Off, a new save keeps its
  old bodies until you press *Reset everyone*; factions draw from the common pool (your own faction rules
  apply either way); changes wait without a word.
- **Touch-up** — bodies an older build gave get the nipple and genital variety they lack, and lose any
  shaft slider (never part of a body); a value you take off afterwards stays off.
- **API** — `Silhouette:API`, OBody NG's function names (`GetPresetAssignedToActor`,
  `ApplyPresetByName`, `GenActor`, `ResetActorOBodyMorphs`, `SetORefit`, ...) and its events
  (`OnActorGenerated`, `OnActorNaked`, `OnActorRemovingClothes`, `OnORefitChanged`) on the bridge quest,
  plus `IsHeavilyDressed` for mods that raise nipples.

Requirements, in addition: Fallout 4 **1.10.163** with F4SE 0.6.23 (the plugin refuses other
runtimes), the **Microsoft Visual C++ 2015-2022 Redistributable 14.40 or newer** (x64), `Silhouette.esp`
enabled (light, no load-order slot), MCM for the picker. Build: `scripts/build-plugin.ps1` (the DLL, 400+
offline tests, the tools' own tests, and the plugin's own parser run on the generated files),
`scripts/build-papyrus.ps1`, then `scripts/deploy-dev.ps1` with the game closed -- it stages in place and
names any new file that waits for Vortex's Deploy. MCM > Silhouette > *How is Silhouette doing?* says what is
loaded and what is missing; the plugin logs to `Documents\My Games\Fallout4\F4SE\Silhouette.log`.

Removing it: disable `Silhouette.esp` and remove the files. Bodies stay as they are (they are
LooksMenu's); every clothed shape goes with the esp. Adding `Silhouette.esp` back to that save later makes it
a save new to Silhouette again: *Reset everyone* runs by itself once more, your picks included. **Never go back by installing an older
`Silhouette.esp`**: one without the refit keyword makes LooksMenu keep every clothed shape as the body
itself, for good. Remove Silhouette entirely instead, or remove only `Silhouette.dll` (the bridge then
takes the clothed shapes off).

## Requirements

- Fallout 4 -- old-gen 1.10.163 or Anniversary 1.11.x -- with the F4SE for that version,
  and **Runtime Database** ([Nexus 108394](https://www.nexusmods.com/fallout4/mods/108394)).
- **LooksMenu**, BodyGen enabled in `Data/F4SE/Plugins/f4ee.ini`
  (`[BodyMorph] bEnable=1`, `bEnableBodyGen=1` — the default).
- **BodySlide**, with your body — CBBE for women, BodyTalk for men — and your outfits built from a
  **zeroed** preset ("CBBE Zeroed Sliders", "BT - Zero") with **Build Morphs** ticked. One of the two is
  enough: a sex without its body is left alone, and Silhouette.log and a message box say so (S-78).
- **MCM** (Mod Configuration Menu) for the settings and the pickers.
- **Invisible Dead Body Fix** ([Nexus 93614](https://www.nexusmods.com/fallout4/mods/93614), the build for
  your game version). With BodyGen on, corpses the game places show only their head and hands without it --
  LooksMenu's old bug, not Silhouette's. Silhouette.log and a message box say when it is missing (S-82).
- **Vortex or Mod Organizer 2.** The archive has an installer that refuses to install without LooksMenu;
  manual installs are not supported.
- Python 3 only to run the generator yourself (standard library only) -- never to play.

## Install

1. Build your body and your outfits in BodySlide from a zeroed preset, with Build Morphs ticked.
2. Install Runtime Database (Nexus 108394), then the release archive, with your mod manager, and enable
   `Silhouette.esp` (a light plugin).
3. Load a save. A save that never had Silhouette gets *Reset everyone* by itself once, with a notification; a
   new game needs nothing.
4. Set the picker window's hotkey (and, if you like, the old picker hotkeys) in MCM > Silhouette > *The NPC
   in your sights*.

The archive is generated ready to play: Silhouette's own bodies, and in the pickers also the presets CBBE and
BodyTalk ship (S-74); your other installed presets join the NPC picker by themselves (S-76). You need the
generator below only to put them in the MCM dropdowns,
to change the rules in `Silhouette_presetDistributionConfig.json`, or for a body you cannot build zeroed.

## Use (the generator)

```
python tools/silhouette_gen.py                 # measure and report, write nothing
python tools/silhouette_gen.py --write         # write the BodyGen files, the MCM menu, the script source
powershell scripts/build-papyrus.ps1           # compile the character picker
python tools/verify_bodygen.py                 # prove the written files do what they claim
python tools/audit_builds.py                   # which bodies AND outfits are zeroed, and which are not
```

All three read the built meshes from Data by default. If BodySlide builds somewhere else (its
`OutputDataPath`), pass `--built <folder>` (repeatable) to check a rebuild **before** it is
deployed.

Install the `data/` folder as a mod, with `Silhouette.dll` under `F4SE/Plugins/` and every compiled script
from `build/papyrus/Silhouette/` under `Scripts/Silhouette/` -- the Player, Adopter, Bridge, API and DLL
scripts, since `Silhouette.esp`'s quests run them. `scripts/deploy-dev.ps1` stages exactly this;
`scripts/make-release.ps1` packs it, with this README and the licence under `F4SE/Plugins/Silhouette/` (S-63). **Run the generator
again whenever you add presets, rebuild a body
in BodySlide, or edit `Silhouette_presetDistributionConfig.json`** -- the game never reads that file
itself, only what the generator makes of it.

A build for release is `--write --release`: besides Silhouette's own presets, only the stock ones CBBE and
BodyTalk ship (`tools/release_presets.json`), never the ones installed on the machine that made it;
`make-release.ps1` refuses anything else (S-74).

Options: `--no-partial` hands out only presets that fit fully; `--compensate` writes templates
relative to a base that is not zeroed; `--data` points at another `Data` folder; `--report file.json`
writes the full classification.

## Building from source

The release archive is everything a player needs; this is for building it yourself.

- **Windows**, **Visual Studio 2022 Build Tools** with the C++ workload (it brings CMake), and
  **[vcpkg](https://github.com/microsoft/vcpkg)** with `VCPKG_ROOT` pointing at it.
- The CommonLibF4 submodule: `git submodule update --init --recursive`.
- **Python 3** (the generator and its tests use the standard library; the body-pool tools in `tools/pool`
  also need Pillow for their review sheets).
- The **Papyrus compiler** from the Creation Kit, and the base game's script sources. Beside them, a folder
  (`-F4se`) holding the base's `ScriptObject.psc` with F4SE's `RegisterForExternalEvent` and
  `UnregisterForExternalEvent` appended -- the picker window's events need them; F4SE's whole
  `ScriptObject.psc` does not work in its place (the bridge's custom events stop compiling).
- For the picker window: **Apache Flex SDK 4.16.1** on **Java**, with `playerglobal.swc` for Flash Player 11.2
  (setup in `scripts/build-interface.ps1`); **Pillow** and **texconv** (DirectXTex) for its pictures; **lxml**
  for the installer's schema check.

```
powershell scripts/build-plugin.ps1     # Silhouette.dll, the offline tests, the tools' tests
powershell scripts/build-papyrus.ps1 -Base <script sources> -F4se <that folder> -Compiler <PapyrusCompiler.exe>
powershell scripts/build-interface.ps1  # the picker window, build/interface/SilhouetteMenu.swf
python tools/thumbnails.py              # its pictures, one atlas a sex (--texconv if it is not on PATH)
powershell scripts/make-release.ps1     # the archive and its installer (refuses to pack unless every check passes)
```

The scripts' default paths (the game's `Data`, the compiler, the staging folder) are the author's; pass
your own with the parameters each script lists at its top, or `--data` to the Python tools.

## How it decides

| | |
| --- | --- |
| Full fit | ≥ 95% of the preset's sliders exist on your body |
| Partial fit | 50–95%, and the preset declares your body's family (read from the full fits) or none |
| Left out | the rest; outfit-tuned variants ("(Outfit)", "Clothed"); zeroed presets |
| Base body | the reference mesh and preset that reproduce your built `.nif` exactly |

The LooksMenu and BodySlide behaviour all of this relies on is written up, from their source, in
[docs/bodygen-format.md](docs/bodygen-format.md). Decisions and their evidence:
[docs/decisions.md](docs/decisions.md).

## Credits

OBody NG (Aietos and contributors) for the design this follows. LooksMenu (expired6978) and
BodySlide (ousnius, Caliente) for the tools it feeds — this project reads their source to get their
behaviour right, and ships none of it.

## Licence

GPL-3.0.
