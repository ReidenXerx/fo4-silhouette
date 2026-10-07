# Silhouette — every feature

The full list, for anyone writing about Silhouette (a mod page, a review) or deciding whether it fits a load
order. Each item names where it is settled: `S-#` is a decision in [decisions.md](decisions.md).

## Bodies for everyone: the pool

- Every NPC gets a body the first time you meet them, and keeps it for the rest of the save; an NPC already met
  is never re-rolled by itself.
- The random bodies come from Silhouette's **own pool**, not from your installed presets: 41 bodies per sex,
  82 in all (S-65).
- Three tiers per sex: 18 **Plain** (ordinary), 17 **Rough** (unflattering: very fat, very flat, frail,
  disproportioned) and 6 **Fine** (conventionally beautiful).
- Odds of **70 / 22 / 8 %**: a Plain body is listed three times in BodyGen's random line (54 : 17 : 6 entries).
- The pool is generated, not hand-picked: each body is built from the installed body, **measured** (girths,
  bust and belly projection, volume) and kept only if it measures as its tier; the most different candidates
  are chosen, so no two look alike.
- A **bust floor** keeps every woman's bust small but real (no concave chest the breast physics would fold),
  in the pool, the faction pools and the named characters alike (S-65 amendment, seventh wave).
- The pickers hold Silhouette's own bodies and the presets CBBE and BodyTalk themselves ship (CBBE Curvy,
  Slim, Athletic ..., the Imitation and SevenBase presets; BT - Average, Muscular ...): 196 for women, 188 for
  men (S-74). Your other installed BodySlide presets join the NPC picker's Next / Previous by themselves
  (S-76); in the MCM dropdowns only when you run the generator yourself. None is ever random. Zeroed presets (`CBBE Zeroed Sliders`, `BT - Zero`) are left out of random, told by their
  measured values (S-8).
- Presets whose sliders your body's `.tri` does not have are left out (they would do nothing); partial fits
  count only for the body family they declare (S-4). `--no-partial` keeps only full fits.
- Your built base body is measured on every run; a body not built zeroed is named exactly (for example "CBBE
  Chubby") with the zeroed preset to rebuild with, and `--compensate` writes every template relative to it
  when you cannot rebuild (S-5).
- A marker morph per template makes every roll permanent and records which body an NPC got; each build's
  manifest names every body it can give, and old manifests are kept (S-6, S-12).
- Which races get bodies is a config key (`distributeRaces`, HumanRace and Servitron's ServitronRace by
  default, S-85). Servitrons are flagged male by the game and wear women's parts: Silhouette treats them as
  women and draws their bodies itself, since BodyGen never reaches robots made from templates (S-86); an empty list is refused
  rather than read as "everyone" (S-11, S-61).

## The named people

- **60 companions and named NPCs** (30 women, 30 men) each have a body of their own that fits who they are
  (S-66): Piper, Cait, Curie, Magnolia, Desdemona, Glory, Haylen, Madison Li, Mama Murphy, Marcy Long,
  Fahrenheit, Irma, Mags Black, Nisha, Kasumi Nakano, Ronnie Shaw, Myrna, Doctor Amari, Trudy, Ellie Perkins,
  Cricket, Carla, Captain Avery, Aster, **Ivy** (CompanionIvy), **Geneva**, Polly, Cathy, Becky Fallon and
  Scarlett; Preston Garvey, Paladin Danse,
  MacCready, Deacon, Arthur Maxson, X6-88, Old Longfellow, Porter Gage, Sturges, Kellogg, Travis Miles, Mayor
  McDonough, Vadim Bobrov, Moe Cronin, Father, Paladin Brandis, Tinker Tom, John, Arturo Rodriguez, Solomon, Doctor Sun, Abbot,
  Sheffield, Malcolm Latimer, Finn, Wayne Delancy, Parker Quinn, Winlock, Barnes and Rufus Rubins.
- **The Diamond City pack** (S-77): the people the author's saves meet most, each drawn from their story and
  allowed to be sardonic -- the mayor of "Mankind for McDonough" grossly obese, Sheffield's ruined liver on
  stick limbs, Becky Fallon's "basement", a con man all chest on chicken legs.
- Bound to their NPC records by plugin and form id (64 records: Curie, Kellogg, Brandis and Malcolm Latimer have two each);
  a rule of yours for the same record wins.
- Ghouls (Hancock), synths of the old models (Nick) and robots are not HumanRace and have none.

## Factions: a pool of their own

- **Eleven factions** draw from ten bodies per sex in their own look (S-72): the **Brotherhood of Steel**
  (drilled soldiers, heavy knights, softer scribes), the **Minutemen** (sturdy, farm-strong), the **Gunners**
  (lean and hard), **raiders** (underfed, wiry, chem-worn, the odd brute), Nuka-World's **Disciples**
  (knife-lean), **Operators** (sleek, better fed) and **Pack** (feral, muscled), the **Triggermen** (well fed,
  a little soft), the **Institute** (soft, sedentary), the **Railroad** (lean, quick) and the **Children of
  Atom** (gaunt, ascetic; the Commonwealth's and Far Harbor's).
- 220 bodies, each measured like the pool's; per faction and sex 6 plain, 3 rough, 1 fine, at the pool's
  odds (69 / 23 / 8).
- A faction outranks the random pool, plugins and races, and loses to per-NPC rules: Danse, Preston and Cait
  keep their own bodies. The gangs and the Triggermen are matched before the raiders.
- Your own faction rule for the same faction wins; the whole feature can be switched off in MCM (S-73).

## Your rules (OBody's config)

- OBody NG's own JSON keys and priority: per-NPC blacklist (form id, name) → per-NPC preset (`npcFormID`,
  `npc`) → plugin and race blacklists → faction → plugin → race → random (S-11, S-23).
- Keys BodyGen can carry work even without the plugin: `raceFemale/Male`, `npcPluginFemale/Male`,
  `npcFormID`, every form-id, plugin and race blacklist. The plugin adds `npc` (by name), `factionFemale/Male`
  and `blacklistedNpcs` (by name).
- `blacklistedPresetsFromRandomDistribution`, `blacklistedPresetsShowInOBodyMenu`, and `includes/*.json`
  (OBody's includes: applied in name order, the later file wins).
- A rule with several presets draws one per person and keeps it while the rule lists it (S-52); a Reset draws
  a different one (S-60).
- The shipped config writes every key out at its default, plus entries for Eli's Armour Compendium (which of
  its armours count as heavy) (S-61).

## Picking bodies in game

- **The dead are left alone** (S-81): Silhouette changes no corpse's body -- no faction body, no refit, no
  touch-up on someone dead; the window, the Pick hotkey and MCM's buttons say so instead. A body LooksMenu's
  BodyGen gives them as the cell loads is LooksMenu's.
- **Corpses keep their bodies** (S-82): Invisible Dead Body Fix (Nexus 93614) is required -- without it LooksMenu's
  BodyGen leaves corpses the game places with only head and hands. Silhouette says so at launch when it is missing.

- **The picker window** (S-79): opened by a hotkey on the NPC in your sights, by MCM's buttons (on the NPC
  aimed at in the half minute before, or on you), or from the console (`cgf "Silhouette:API.OpenWindow"`).
  A grid of pictures of every preset that fits them -- Silhouette's, CBBE's and BodyTalk's stock ones, yours
  as plain cards -- with filters (All, People, Plain, Rough, Fine, Yours). A click or the arrows tries one on
  live; *Apply* keeps it, *Cancel* or any other close puts back exactly what they had. *Them* / *Me* switches
  between the NPC and your own body; your own BodySlide presets are on both tabs.
  - The camera: the game's free camera stands in front of them, the window beside them, and goes back when it
    closes. A free camera you already had on is left alone; where the camera's fields do not check out on a
    game version, it is not moved at all (S-75's rule).
  - The NPC stands still while picked (the game's own "restrained": their AI keeps running, so quests and
    companion routines go on) -- not someone in a scene, in combat or busy in AAF; let go on every close,
    switch and load.
  - Input: the mouse. Under the free camera the game delivers no mouse click to a menu -- it turns the left
    button into the camera's "WorldZUp" control -- so the window clicks whatever is under the cursor itself
    (as ScreenArcherMenu does). The player's own controls are off while it is open (an InputEnableLayer), and
    the free camera's own input too. Closed by Cancel, the window's hotkey again, or the console
    (`cgf "Silhouette:API.CloseWindow"`). Every opening starts from nothing (F4SE keeps the window between
    openings).
  - The pictures: every preset drawn front and side on the author's zeroed bodies, one scale a sex, two
    2048x2048 atlases named by the build -- an atlas of another build shows no picture rather than the wrong
    one. The live try-on shows your own body.

- **Your own BodySlide presets** join the NPC picker's Next / Previous by themselves, read from BodySlide's
  SliderPresets folder when the game starts, exactly as BodySlide builds them for your body -- no Python, no
  generator (S-76). They are never random. `Silhouette.log` says how many joined and which were left out, and
  why (made for another body, fits too little, an outfit copy).

- **Your character** is never randomised: the pool's most average body (Plain F01 / Plain M02) unless you
  choose one in MCM; *Apply to my character*, *Back to the default*, *Which body do I have?* (S-7, S-10). This
  part needs no plugin.
- **The NPC picker** (plugin): five hotkeys, unbound until you set them — *Pick* the NPC you aim at (close
  enough to talk to), *Next* / *Previous* to try every preset on them live (about a second each, their own
  nipple and genital variety kept), *Keep*, *Cancel* (exactly what they had, hand edits too) (S-22, S-64).
- A picking survives a save (a save mid-preview loads as a Cancel; up to 64 kept) (S-47); a kept choice is
  marked in LooksMenu itself, so a save made without the plugin keeps it (S-51).
- MCM page *The NPC in your sights*: give a preset from a list, *Reset* them, or ask *Which body do they have?*
  — for the one picked, or the last one aimed at in the half minute before.
- The pickers have no length limit (Papyrus caps an array at 128; the lists come in parts) (S-69).

## For the people around you

- *Count the bodies around me* (a census, each person written to the Papyrus log), *Refresh the people around
  me* (their preset again with this build's values), *Give the people around me new bodies*.
- **Reset** on one NPC: a named character gets their own body back, a rule draws again, anyone else rolls
  from the pool; other mods' keyed morphs (AAF, pregnancy) stay (S-67).
- **Reset everyone**: a fresh start for the whole save, your picks included; the people around you change at
  once, everyone else when met; the save remembers it, and a later update does not set it off again; it
  reaches bodies Silhouette did not make, once each. Two presses within a minute (S-68, S-70). The log
  names each person: what they wore, what decides them, the body they got, and who waits for another mod.
- **Fresh start**: a save that never had Silhouette gets *Reset everyone* by itself on its first load, with a
  notification; a switch in MCM turns it off (S-70, S-73).

## Clothing: ORefit

- While someone is dressed: breasts held together and lifted; under heavy clothes the nipples flattened
  (S-40, S-42).
- Heavy is told by the item's **name** (armor, jacket, coat, parka, breastplate, kevlar and more, whole words
  only), with lists to name items heavy or light by form id or name (S-48).
- It only ever raises a slider, under a keyword of its own: the moment they undress they are exactly their own
  body; removing `Silhouette.esp` takes every clothed shape off by itself (S-27, S-40).
- `<Preset>-Refit`, `Female-Refit` / `Male-Refit` BodySlide presets and OBody's outfit keys
  (`blacklistedOutfitsFromORefit…`, `outfitsForceRefit…`, `refitOutfitPresetsFemale/Male`) (S-20, S-26).
- Nobody in power armour is refit, nor your character, nor anyone blacklisted or bare (S-41).
- The flattening shows only on outfits whose meshes carry the refit sliders (mod outfits built with them; no
  vanilla or DLC heavy outfit does).

## Variety

- Every person their own nipples (size, areola, tip, length; for men nipple size and width) and, on the
  anatomy bodies, genital shape for women and ball size for men (S-17, S-21).
- Never the shaft, and never a runtime state (erection, openings) — those belong to scenes (S-16, S-29).
- Two MCM switches; bodies an older build gave are **touched up** once with the variety they lack (S-44).

## Other mods

- **AAF-aware**: anyone in a scene, busy or locked, waits; their change lands when the scene ends (S-56).
- Other mods' morphs under their own keyword are never touched (S-27, S-41).
- A 24-hour **regeneration window** gives a body to people another mod marked before Silhouette arrived,
  keeping that mod's morphs; MCM shows it and opens a new one (S-15).
- **Notifications** when a change you asked for waits for another mod's scene, and when it lands; a switch
  turns them off (S-71, S-73).

## Safe saves

- Silhouette's co-save keeps only intent (who chose what); the bodies are LooksMenu's (S-43).
- A body cut short by a save is finished at the next load (S-58); a roll or reset asked for before a save is
  carried out after it (S-59).
- A created NPC's id handed to somebody new is not taken for the person it was (S-57).
- The plugin and the scripts check they speak the same protocol and refuse to half-work otherwise (S-18).

## For mod authors

- `Silhouette:API` with OBody NG's names (`GetPresetAssignedToActor`, `ApplyPresetByName`, `GenActor`,
  `ResetActorOBodyMorphs`, `ReapplyActorOBodyMorphs`, `SetORefit`, `SetNippleRand`, `SetGenitalRand`, ...),
  plus `IsHeavilyDressed` for mods that raise nipples, `LastError` and `ShowStatus`; safe to call without the
  plugin.
- Events `OnActorGenerated`, `OnActorNaked`, `OnActorRemovingClothes`, `OnORefitChanged`.

## Tools

- `silhouette_gen.py` (measure and write), `verify_bodygen.py` (rebuilds every body LooksMenu can make and
  checks each lands on its preset), `audit_builds.py` (which bodies and outfits are built zeroed),
  `cosave_census.py` (who has which body in a save, offline), and the pool tools (`pool/generate.py`,
  `pool/characters.py`, `pool/factions.py`, each with `--check` and `--sheets`).
- MCM *How is Silhouette doing?* and `Silhouette.log` say what is loaded, what is waiting and what is
  missing.

## Requirements

- **Vortex or Mod Organizer 2**: the archive's installer refuses to install without LooksMenu active; manual
  installs are not supported.

- Fallout 4 **old-gen 1.10.163 or Anniversary 1.11.x** with the F4SE for that version, and
  **Runtime Database** (Nexus 108394), which finds the game's functions on each (S-75). Proved in game on
  old-gen 1.10.163 and Anniversary 1.11.240. Next-gen 1.10.984 is not supported (nobody plays it). A version whose classes are laid out differently
  from what the plugin reads turns the plugin off with one log line, never half on; BodyGen still gives
  every body. **LooksMenu** with BodyGen on, **MCM**, the Visual C++ 2015-2022 Redistributable 14.40 or newer, and your body and outfits built in
  **BodySlide** from a zeroed preset with Build Morphs ticked. `Silhouette.esp` is a light plugin (no
  load-order slot).
- **One body mod is enough** (S-78). Silhouette's women are made for CBBE, its men for BodyTalk. A sex whose
  body Silhouette does not support -- no BodySlide-built `.tri`, or one carrying under half of the pool's
  sliders (another body family's) -- is left alone: no rolls, rules, resets, refits or picks. Silhouette.log
  says why, and a message box says it once a launch.

## Limits

- ORefit only raises a slider; it cannot lower one (LooksMenu keeps the highest value per keyword).
- Heavy clothes are told by English names; a localized game needs its words added to `heavyWords`.
- Faction rules read an NPC record's own factions, as OBody does, not its templates' (S-23).
- The picker window is a Scaleform menu opened through F4SE's own custom-menu support -- none of CommonLibF4's menu
  code, whose crash on 1.10.163 is why there was none before (S-22, S-79). The camera can frame someone inside a
  wall in a tight room: it is placed in front of them without a collision test.
