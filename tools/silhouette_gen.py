"""Silhouette, Phase 1: turn your BodySlide presets into LooksMenu BodyGen files.

Every NPC gets one of your presets the first time it loads, and keeps it for the
rest of that save. The distribution itself is done by LooksMenu's own BodyGen,
which is already switched on in f4ee.ini and was simply never fed. This tool
writes what feeds it; there is no runtime code in Phase 1 at all.

WHAT MAKES IT BETTER THAN SHIPPING A FIXED BODYGEN FILE -- everything is MEASURED
against the bodies you actually have installed:

1. Which presets can do anything at all. LooksMenu applies a morph by name and
   only morphs present in the body's .tri can move a vertex, so a preset whose
   sliders are absent from your .tri is a silent no-op. On the machine this was
   written on, 27 of 120 presets (Fusion Girl, Atomic Beauty) would have done
   nothing on a CBBE body. They are left out rather than handed out.

2. What your base body already has baked into it (tools/base_body.py). BodyGen
   adds morphs ON TOP of the mesh on disk, so a body built from any preset but
   Zeroed Sliders makes every NPC preset stack on it -- the classic "why are all
   my NPCs enormous". Measured on the machine this was written on: FemaleBody was
   built from "CBBE Chubby" and MaleBody from "BT - Average", and nothing said so.
   The tool names the baked preset exactly and tells you to rebuild zeroed -- the
   owner's choice (S-5) and the standard setup. For an install that cannot be
   rebuilt, --compensate writes every template RELATIVE to what is baked (target
   minus baked, per slider), so every NPC still lands exactly on its preset:
   LooksMenu applies a morph as vertex += diff * value with no clamp
   (BodyMorphInterface.cpp ApplyMorph) and parses values with atof.

Everything below is taken from sources, not from documentation summaries:

- BodyGen grammar: LooksMenu's own parser, f4ee/BodyGenInterface.cpp.
    templates.ini   Name = Morph@value, Morph@low:high, ...   (',' = all applied)
    morphs.ini      All|Female|HumanRace = T1|T2|...          ('|' = one at random)
  A LATER line OVERWRITES an earlier one for the same NPC, so rule priority is
  simply file order. A morph that evaluates to 0 is SKIPPED before SetMorph.
- An NPC with NO stored morphs is evaluated AGAIN on every load (ActorUpdateManager
  only runs BodyGen when GetMorphMap is empty). So a template that sets nothing
  re-rolls the NPC each load: a zero template is not "keep the base", it is "roll
  again next time". Every Silhouette template therefore also sets one MARKER
  morph named after itself -- no body has it, so it moves nothing
  (BodyMorphInterface::SetMorph stores any name), but it makes the roll permanent
  and records which preset the NPC got, for Phase 2 to read back.
- The one place a re-roll is wanted is the player, who must never be given a
  body: `Fallout4.esm|7` is the Player record, and its template sets nothing.
  Without that line, a player with no LooksMenu body sliders is randomised on the
  next load -- `All|...|HumanRace` includes the Player record.
- `All|Female` WITHOUT a race matches only NPCs with no race at all
  (GetFilteredNPCList compares the NPC's race to a null filter). Always name one.
- Loose files: LooksMenu reads Data/F4SE/Plugins/F4EE/BodyGen/Loose/*_templates.ini
  and *_morphs.ini whatever plugins are loaded, AFTER the per-plugin folders. So
  Phase 1 needs no .esp, and cannot collide with another BodyGen mod's files.
- Slider values: see tools/base_body.py -- big value, else the slider set's
  default (BodyTalk 4 has 26 sliders defaulting to 100), inverted if flagged.

    python tools/silhouette_gen.py                  # measure and report only
    python tools/silhouette_gen.py --write          # also write the BodyGen files
    python tools/silhouette_gen.py --no-partial     # random pool = full fits only
    python tools/silhouette_gen.py --compensate     # for a base that is NOT zeroed
"""
import argparse
import collections
import hashlib
import json
import math
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

import base_body
import catalog
import rules

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_DATA = pathlib.Path(r'D:\GOGGames\Fallout 4 GOTY\Data')
MANIFESTS = pathlib.Path('F4SE/Plugins/Silhouette/manifests')    # below a mod folder / data root
CATALOG = pathlib.Path('F4SE/Plugins/Silhouette/catalog.json')
# Silhouette's own body pool (tools/pool, S-65): the only presets NPCs are drawn from at random, each listed
# in the random line as many times as its tier's weight. Every other preset stays in the picker.
POOL_SIDECAR = ROOT / 'tools/pool/pool.json'
# ...and the named people's own bodies (tools/pool/characters.py, S-66), bound to their NPC records.
CHARACTERS_SIDECAR = ROOT / 'tools/pool/characters.json'
# ...and the factions' own pools (tools/pool/factions.py, S-72), each a faction rule weighted by repetition.
FACTIONS_SIDECAR = ROOT / 'tools/pool/factions.json'
PRESETS = pathlib.Path('Tools/BodySlide/SliderPresets')          # below a mod folder / data root
# A public build's only presets besides the package's own: CBBE's and BodyTalk's stock (S-74).
RELEASE_PRESETS = ROOT / 'tools/release_presets.json'
LINE_LIMIT = 32766      # bytes the engine's ReadLine gives before it splits a line (docs/bodygen-format.md)


def reconfigure_output():
    """A name the console's code page cannot hold must not end the run: piped into another program
    (deploy-dev.ps1 and make-release.ps1 read the verifier's last lines), stdout is cp1252 here, and one
    Cyrillic preset name crashed a legitimate build (L4 F6). Such a character prints as an escape."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors='backslashreplace')
        except (AttributeError, ValueError):
            pass

FULL_FIT = 0.95      # this share of a preset's sliders must exist on the body
PARTIAL_FIT = 0.50   # below this a preset does too little to be worth handing out
BODIES = {'female': 'FemaleBody', 'male': 'MaleBody'}

# A preset's `set` attribute is NOT what the preset is for. It is whichever slider
# set happened to be open in BodySlide when the preset was saved. Measured: seven
# CBBE body presets on the machine this was written on carry sets like
# "POP AR Vest D" and "Jumpsuit_Vault" while declaring <Group name="CBBE"/> --
# they are bodies whose authors had an outfit open. An earlier version of this
# tool classed them as outfits by set name and threw seven good bodies away.
#
# The authored signal is <Group>: 116 of 120 presets declare the body families
# they are for. A group naming an outfit or a clothing pack is not a family.
NOT_A_FAMILY = re.compile(
    r'outfit|clothing|costume|dress|corset|suit|jumpsuit|armor|armour|\bxy -|2pac', re.I)

# A preset tuned for BUILDING OUTFITS rather than for a nude body: a second copy
# of a body style with the shape eased off. Handing them out as well would double
# the weight of every style that ships one. Kept aside -- ORefit may want them.
CLOTHED_VARIANT = re.compile(r'\(outfit\)|clothing|clothed|for outfit|\boutfit\b', re.I)

# Every character LooksMenu's template parser splits on (BodyGenInterface.cpp:
# '=' then '/' then ',' then '|' then '@').
BODYGEN_SEPARATORS = re.compile(r'[=/,|@]')

PLAYER_GUARD = rules.UNSHAPED

# Morphs that something else drives at RUNTIME, so they are states, not shapes (decision S-16).
# AAF raises Erection/CErection under its own keyword for a scene and takes them away after, and
# Animated Fannies-style scripts do the same with the Penetrate sliders. A preset that sets one
# would put it in the UNKEYED layer, where nothing ever takes it away. That happened with
# Sirius_Male_preset (Erection 100%): two males carried a permanent erection (co-save, 2026-09-23).
STATE_MORPHS = ('Erection', 'Erection Up', 'Erection Down', 'CErection', 'VaginaPenetrate', 'AnusPenetrate',
                'VaginaSpread', 'ButtcheeksSpread')

# Per-woman genital shape variety (S-17): {morph: (low, high)} from tools/genital_shapes.json, appended to
# every female template as Morph@low:high so LooksMenu rolls each woman her own shape. Only morphs the
# body has; never a runtime state (S-16).
GENITAL_SHAPES_FILE = pathlib.Path(__file__).resolve().parent / 'genital_shapes.json'


def genital_shapes():
    import json
    if not GENITAL_SHAPES_FILE.exists():
        return {}
    ranges = json.loads(GENITAL_SHAPES_FILE.read_text(encoding='utf-8')).get('ranges', {})
    out = {}
    for morph, (low, high) in ranges.items():
        if is_state(morph):
            raise SystemExit(f'{GENITAL_SHAPES_FILE.name}: {morph} is a runtime state (S-16), not a shape')
        out[morph] = (float(low), float(high))
    return out


# Nipple and ball variety (S-21): {sex: {group: {morph: [low, high]}}} from tools/variety.json, rolled per NPC
# the same way. variety_ranges() is the ONE loader for both range files.
VARIETY_FILE = pathlib.Path(__file__).resolve().parent / 'variety.json'

# Never rolled, whatever a range file says. AnusBack is an alignment control (S-17); the shaft's length,
# width and tip decide where fo4-anatomy's collision sits (S-21, owner: "wouldn't it harm alignment of
# penis-hole-mouth?" -- it would).
NEVER_VARIED = ('AnusBack', 'Penis Length', 'Penis Width', 'TipShape', 'BTPenisLengthErectv2',
                'BTPenisLengthFlaccid', 'BTPenisWidthv2', 'BTPenisTipShapeRounded', 'BTShaftRootSize',
                'BTUrethraCurve', 'BTSmoothPenisErect', 'BTSmoothPenisFlaccid')
VARIETY_GROUPS = ('nipples', 'genitals')

# The shaft (S-29, owner): never part of a body, as the runtime states are not (S-16). Animations aim the
# penis bones and fo4-anatomy's collision is sized to BodyTalk's current shaft, so a wider or longer one
# clips; two installed male presets set Penis Width. AnusBack stays a preset's own business.
SHAFT_MORPHS = tuple(m for m in NEVER_VARIED if m != 'AnusBack')
# S-62: sliders fo4-anatomy's body build owns and sets. Their default is baked into the base body a
# zeroed build produces, and a runtime value (fo4-anatomy's, under its own keyword) ADDS to it -- so
# Silhouette never writes one, never measures a body by one, and its own layer's value stays 0.
ANATOMY_OWNED = ('AnatomyOpening',)
# Never written into a template, the player's picker, the catalog or a refit set.
NEVER_IN_BODY = STATE_MORPHS + SHAFT_MORPHS + ANATOMY_OWNED

# LooksMenu and the plugin read morph names in any case ("erection" is Erection to them): every
# comparison with these lists folds the case (L4 F8).
_STATES_FOLDED = {m.casefold() for m in STATE_MORPHS}
_NEVER_VARIED_FOLDED = {m.casefold() for m in NEVER_VARIED}
_ANATOMY_FOLDED = {m.casefold() for m in ANATOMY_OWNED}
_NEVER_IN_BODY_FOLDED = {m.casefold() for m in NEVER_IN_BODY}


def is_state(morph):
    return morph.casefold() in _STATES_FOLDED


def never_varied(morph):
    return morph.casefold() in _NEVER_VARIED_FOLDED


def anatomy_owned(morph):
    return morph.casefold() in _ANATOMY_FOLDED


def never_in_body(morph):
    return morph.casefold() in _NEVER_IN_BODY_FOLDED


def why_never(morph):
    """Why a morph is never part of a body, in one clause."""
    if is_state(morph):
        return 'a state other mods drive at runtime (S-16)'
    if anatomy_owned(morph):
        return 'fo4-anatomy\'s build slider, its default baked into the base (S-62)'
    return 'the shaft, never part of a body (S-29)'

# S-45: the templates the player and the character-creation dummies are given -- the default preset's
# values and marker, none of the ranges.
PLAYER_TEMPLATE = {'female': 'Silhouette_PlayerFemale', 'male': 'Silhouette_PlayerMale'}


def variety_ranges():
    """{'female': {morph: (low, high, group)}, 'male': {...}}: every range BodyGen rolls per NPC --
    S-17's genital shapes (genital_shapes.json, women) and S-21's nipples and balls (variety.json).
    Refuses a runtime state (S-16), a morph that is never varied, a morph BOTH files name, and a
    range whose low is above its high."""
    out = {'female': {}, 'male': {}}
    for morph, (low, high) in genital_shapes().items():
        out['female'][morph] = (low, high, 'genitals')
    if VARIETY_FILE.exists():
        doc = json.loads(VARIETY_FILE.read_text(encoding='utf-8'))
        for g in ('female', 'male'):
            for group, ranges in doc.get(g, {}).items():
                if group not in VARIETY_GROUPS:
                    raise SystemExit(f'{VARIETY_FILE.name}: group {group!r} is not one of {VARIETY_GROUPS}')
                for morph, (low, high) in ranges.items():
                    if is_state(morph):
                        raise SystemExit(f'{VARIETY_FILE.name}: {morph} is a runtime state (S-16), not a shape')
                    if morph in out[g]:
                        raise SystemExit(f'{morph} is in both {GENITAL_SHAPES_FILE.name} and {VARIETY_FILE.name}: '
                                         f'one file must own each range')
                    out[g][morph] = (float(low), float(high), group)
    for g, ranges in out.items():
        for morph, (low, high, _group) in ranges.items():
            if never_varied(morph):
                raise SystemExit(f'{morph} is never rolled (S-17, S-21): take it out of the range files')
            if anatomy_owned(morph):
                raise SystemExit(f'{morph} is {why_never(morph)}: never rolled -- take it out of the range files')
            if not (math.isfinite(low) and math.isfinite(high) and low < high):
                raise SystemExit(f'{morph}: range {low}..{high} rolls nothing -- low must be below high')
    return out


# --------------------------------------------------------------------------
# reading
# --------------------------------------------------------------------------

def read_presets(folder):
    """Every <Preset>, read the way BodySlide reads it (SliderPresets.cpp):
    recursive, first preset of a name wins, size="big" or "both" gives the big
    value, and a SetSlider with no size is ignored. Values come out 0..1.

    Names are compared in any case: the engine keeps one spelling per string, so to
    LooksMenu and the plugin two presets named alike are one (the first is used)."""
    out, seen_names = [], {}
    for f in sorted(folder.rglob('*.xml')):
        try:
            root = ET.parse(f).getroot()
        except ET.ParseError as exc:
            print(f'  skipped unparseable preset file {f.name}: {exc}')
            continue
        for p in root.iter('Preset'):
            name = p.get('name')
            if not name:
                continue
            if name.casefold() in seen_names:
                if seen_names[name.casefold()] != name:
                    print(f'  skipped preset {name!r} ({f.name}): the same name as {seen_names[name.casefold()]!r} '
                          f'in another case, and the game reads them as one')
                continue
            seen_names[name.casefold()] = name
            big, small_only = {}, set()
            for s in p.iter('SetSlider'):
                slider, size = s.get('name'), (s.get('size') or '').lower()
                try:
                    value = float(s.get('value')) / 100.0
                except (TypeError, ValueError):
                    continue
                if not math.isfinite(value):
                    print(f'  skipped slider {slider!r} of {name!r}: {s.get("value")!r} is not a number')
                    continue
                if size in ('big', 'both'):
                    big[slider] = value
                elif size == 'small':
                    small_only.add(slider)
            groups = [g.get('name', '') for g in p.findall('Group')]
            out.append({
                'name': name,
                'set': p.get('set', ''),
                'families': sorted({g for g in groups if g and not NOT_A_FAMILY.search(g)}),
                'file': str(f.relative_to(folder)),
                'sliders': big,
                # BodySlide builds these at the set's default for Fallout 4.
                'small_only': len(small_only - set(big)),
            })
    return out


# --------------------------------------------------------------------------
# classifying
# --------------------------------------------------------------------------

def classify(preset, female_morphs, male_morphs):
    """-> gender, fit (0..1) and kind (body | clothed-variant | empty)."""
    sliders = set(preset['sliders'])
    if not sliders:
        return {'gender': None, 'fit': 0.0, 'kind': 'empty'}
    f_fit = len(sliders & female_morphs) / len(sliders)
    m_fit = len(sliders & male_morphs) / len(sliders)
    gender = 'female' if f_fit >= m_fit else 'male'
    kind = 'clothed-variant' if CLOTHED_VARIANT.search(preset['name']) else 'body'
    return {'gender': gender, 'fit': max(f_fit, m_fit), 'kind': kind}


def installed_family(presets, gender):
    """The body family the installed body belongs to, read off the data.

    Whichever family the FULL-fit body presets of this gender most often declare.
    On the machine this was written on that is "CBBE" for women and a BodyTalk
    family for men -- but nothing here names a body, so a Fusion Girl or Atomic
    Beauty install gets the right answer with no configuration.
    """
    counts = collections.Counter()
    for p in presets:
        if p['gender'] == gender and p['kind'] == 'body' and p['fit'] >= FULL_FIT:
            counts.update(p['families'])
    return counts.most_common(1)[0][0] if counts else None


def band(preset, family):
    """full | partial | other-family | none.

    A PARTIAL fit counts only when the preset is for this body's family, or
    declares no family at all and so leaves it to measurement. Sharing a slider
    NAME with a different body is not sharing a SHAPE: measured, a Fusion Girl
    preset overlaps a CBBE body by 53% through coincident names. True Wasteland
    Body presets overlap 77-83% and declare CBBE, because TWB is built on CBBE --
    those names do mean the same shapes.
    """
    fit = preset['fit']
    if fit >= FULL_FIT:
        return 'full'
    if fit < PARTIAL_FIT:
        return 'none'
    declared = preset['families']
    if declared and family not in declared:
        return 'other-family'
    return 'partial'


# --------------------------------------------------------------------------
# writing
# --------------------------------------------------------------------------

def plain_marker(name):
    # A template name is split on '=' and looked up as an exact string. Keep it to
    # characters that survive both files: letters, digits and underscores.
    safe = re.sub(r'[^A-Za-z0-9]+', '_', name).strip('_')
    return f'Silhouette_{safe}' if safe else ''


def manifest_folders(root, data):
    """The folders every reader of a build's history takes manifests from, in the order a copy of one name
    is taken: the output root's own, then the game's Data's -- a build deployed once is a build whose bodies
    are in saves, even when this run writes somewhere fresh or a manifest was deleted here (wave 4 L6).
    Computed once and handed to every reader, so no reader can be left looking at one of them (wave 5)."""
    return [pathlib.Path(root) / MANIFESTS, pathlib.Path(data) / MANIFESTS]


def manifest_files(*folders):
    """{file name, casefolded: path} of every manifest in the folders -- the first folder's copy of a name
    wins (manifest_folders())."""
    files = {}
    for folder in folders:
        folder = pathlib.Path(folder) if folder else None
        if folder and folder.is_dir():
            for f in sorted(folder.glob('*.json')):
                files.setdefault(f.name.casefold(), f)
    return files


def unreadable_in_data(path, exc):
    """What is said of a manifest in the game's Data that cannot be read: it is skipped, not fatal. Only the
    mod manager puts files there, and the plugin skips such a file too (wave 5)."""
    return (f'{path}: not a manifest this tool can read ({exc!r}) -- skipped. It is in the game\'s Data, where '
            f'only the mod manager puts files: remove it, or restore it, in the Silhouette mod\'s staging folder')


def manifest_history(*folders, notes=None):
    """{preset name, casefolded: Counter(marker: manifests recording it)} -- what every manifest in the
    folders (manifest_files) says each preset's marker was. A manifest of the FIRST folder (the output root)
    that cannot be read is refused: without it the markers of that build could not be kept (assign_markers),
    and git can restore it. One only the game's Data holds is skipped, and said in `notes`."""
    names = collections.defaultdict(collections.Counter)
    first = pathlib.Path(folders[0]).resolve() if folders and folders[0] else None
    for _name, f in sorted(manifest_files(*folders).items()):
        try:
            doc = json.loads(f.read_text(encoding='utf-8-sig'))
            recorded = collections.Counter()
            for marker, entry in doc['templates'].items():
                recorded[(str(entry['preset']).casefold(), marker)] += 1
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
            if first is not None and f.parent.resolve() != first:
                if notes is not None:
                    notes.append(unreadable_in_data(f, exc))
                continue
            raise SystemExit(f'{f}: not a manifest this tool can read ({exc!r}) -- restore it (git), since it '
                             f'says what the bodies of its build are')
        for (name, marker), n in recorded.items():
            names[name][marker] += n
    return names


def assign_markers(presets, history=None):
    """Each preset's marker, which is also its template's name.

    A preset keeps the marker a manifest already records for it (history, manifest_history() of the
    folder the build is written to). A body's marker is read through its own build's manifest by the
    plugin, but Silhouette:Player (Census, Refresh, "Which body") and the regeneration window's heal
    read the CURRENT build's markers whatever the stamp -- so a marker must go on meaning the preset it
    always meant, and a newcomer never takes one history gives another name (L4 F2). Were one marker
    recorded for two presets still on disk, it stays with the one most manifests name.

    A preset with no history gets the plain marker, unless it comes out the same in any case as another
    newcomer's ("Body 1" and "Body-1"), holds nothing plain ("Тело"), lands on a name Silhouette
    reserves or on a marker history gives someone else: then the first 6 hex digits of its name's hash
    as well. LooksMenu and the plugin read names case-insensitively, so each must stand for one thing."""
    history = history or {}
    fold = catalog.ifold
    reserved = {fold(n) for n in (PLAYER_GUARD, *PLAYER_TEMPLATE.values(), *catalog.RESERVED_MARKERS)}
    current = {p['name'].casefold() for p in presets}
    owners = collections.defaultdict(collections.Counter)      # fold(marker) -> {name: manifests}
    for name, markers in history.items():
        for m, n in markers.items():
            owners[fold(m)][name] += n
    # A marker two presets still on disk were given: the one most manifests name keeps it.
    claimant = {k: max(sorted(c for c in names if c in current), key=lambda c: names[c], default=None)
                for k, names in owners.items()}
    taken = {}
    fresh = []
    for p in presets:
        name = p['name'].casefold()
        got = history.get(name, {})
        # The plain spelling first, then the one most manifests hold.
        for m in sorted(got, key=lambda m: (m != plain_marker(p['name']), -got[m], m)):
            if fold(m) not in reserved and claimant.get(fold(m)) == name and fold(m) not in taken:
                p['marker'] = m
                taken[fold(m)] = name
                break
        else:
            fresh.append(p)
    counts = collections.Counter(fold(plain_marker(p['name'])) for p in fresh)
    for p in fresh:
        name = p['name'].casefold()
        m = plain_marker(p['name'])
        k = fold(m)
        if not m or counts[k] > 1 or k in reserved or k in taken or set(owners.get(k, {})) - {name}:
            digest = hashlib.sha1(p['name'].encode('utf-8')).hexdigest()
            for width in range(6, 41, 2):
                m = f'{plain_marker(p["name"]) or "Silhouette"}_{digest[:width]}'
                if fold(m) not in taken and not set(owners.get(fold(m), {})) - {name}:
                    break
        p['marker'] = m
        taken[fold(m)] = name


def previous_markers(folders, catalogs, notes=None):
    """({preset name, casefolded: marker}, manifest path) of the build a package holds now: the first of
    `catalogs` (the output root's catalog.json, then the game's Data's) that names a stamp, read through that
    stamp's manifest in `folders` (manifest_folders()). ({}, None) when there is none -- and when a catalog
    names a stamp that no manifest there records, `notes` says so: a move since that build cannot be said, and
    an empty answer must not read as "nothing moved" (wave 6). A manifest naming one preset under two markers
    (only a hand-edited one does) is said too, and its first is compared."""
    unmatched = []
    for cat_path in catalogs:
        try:
            stamp = json.loads(pathlib.Path(cat_path).read_text(encoding='utf-8-sig'))['stamp']
        except (OSError, ValueError, KeyError, TypeError):
            continue
        for folder in folders:
            f = pathlib.Path(folder) / f'{stamp}.json'
            try:
                templates = json.loads(f.read_text(encoding='utf-8-sig'))['templates']
                named = {}
                for m, e in templates.items():
                    if not isinstance(e, dict):
                        continue
                    key = str(e['preset']).casefold()
                    if key in named:
                        if notes is not None:
                            notes.append(f'{f} names {e["preset"]!r} under two markers ({named[key]}, {m}): the '
                                         f'first is the one compared')
                        continue
                    named[key] = m
                return named, f
            except (OSError, ValueError, KeyError, TypeError, AttributeError):
                continue
        unmatched.append((cat_path, stamp))
    if notes is not None:
        for cat_path, stamp in unmatched:
            notes.append(f'{cat_path} records build stamp {stamp}, but no manifest of that stamp is in '
                         f'{", ".join(map(str, folders))}: markers that moved since that build cannot be said')
    return {}, None


def marker_moves(presets, previous, history):
    """[(preset, marker it had, marker it gets, manifests recording each)] for every preset whose marker is not
    the one the build a package holds now gave it (previous_markers()). A marker is how every body of it is
    read -- Census, Refresh and "Which body" read the current build's markers -- so a move must be said, not
    found later: the history votes, and since wave 4 the game's Data votes too (wave 5 L6)."""
    moves = []
    for p in presets:
        name = p['name'].casefold()
        old, new = previous.get(name), p.get('marker')
        if old and new and old != new:
            got = history.get(name, {})
            moves.append((p['name'], old, new, got.get(old, 0), got.get(new, 0)))
    return moves


def body_values(target, morphs):
    """What a body of a preset holds, as the "most average" measurement sees it: the sliders the body carries,
    never a state, the shaft or fo4-anatomy's build slider -- none of them is written into a body (S-16, S-29,
    S-62), so none of them may pull the average either."""
    return {k: v for k, v in target.items() if k in morphs and not never_in_body(k)}


def release_stock():
    """The presets a public build may carry besides the package's own (S-74), casefolded: the ones CBBE and
    BodyTalk themselves ship (tools/release_presets.json)."""
    d = json.loads(RELEASE_PRESETS.read_text(encoding='utf-8'))
    return {n.casefold() for names in d['stock'].values() for n in names}


def read_all_presets(folders, release=False):
    """read_presets over several folders, the first folder's preset winning a name (any case): the pool this
    repo holds is read before the copy a deploy put in the game's Data. With release (S-74), the other folders
    give only CBBE's and BodyTalk's stock presets: a public build never carries the presets of the machine it
    was made on."""
    stock = release_stock() if release else None
    out, seen = [], set()
    for i, folder in enumerate(folders):
        for p in read_presets(folder):
            key = p['name'].casefold()
            if key in seen or (stock is not None and i > 0 and key not in stock):
                continue
            seen.add(key)
            out.append(p)
    return out


def load_pool(path=POOL_SIDECAR):
    """{preset name casefolded: {'name', 'sex', 'tier', 'weight'}} of the pool's sidecar (S-65)."""
    try:
        side = json.loads(pathlib.Path(path).read_text(encoding='utf-8'))
        weights = side['weights']
        return {n.casefold(): {'name': n, 'sex': p['sex'], 'tier': p['tier'], 'weight': int(weights[p['tier']])}
                for n, p in side['presets'].items()}
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise SystemExit(f'{path}: not a readable pool sidecar ({exc}) -- run tools/pool/generate.py')


def merge_characters(cfg, path=CHARACTERS_SIDECAR):
    """S-66: each character's body as an npcFormID rule for its records, UNDER the user's own -- a rule the
    config or an include already has for a record wins, however its form id is written. -> (bound, kept)."""
    try:
        side = json.loads(pathlib.Path(path).read_text(encoding='utf-8'))['characters']
    except (OSError, ValueError, KeyError) as exc:
        raise SystemExit(f'{path}: not a readable characters sidecar ({exc}) -- run tools/pool/characters.py')
    bound, kept = 0, []
    for name, c in side.items():
        for plugin, _edid, fid in c['forms']:
            forms = cfg.setdefault('npcFormID', {}).setdefault(plugin, {})
            theirs = set()
            for k in forms:
                try:
                    theirs.add(int(str(k), 16) & 0xFFFFFF)
                except ValueError:
                    pass
            if int(fid, 16) in theirs:
                kept.append(f'{name} ({plugin} {fid})')
                continue
            forms[fid] = [name]
            bound += 1
    return bound, kept


def merge_factions(cfg, path=FACTIONS_SIDECAR, pools=None):
    """S-72: each faction's pool as its faction rule (factionFemale / factionMale), UNDER the user's own -- a
    rule the config or an include already has for the faction and sex wins. Each body is listed as many times
    as its tier weighs: the rule picks one entry by the person's id (src/Rules.cpp Pick), so repetition is the
    weighting, as in BodyGen's random line. The rules go in the sidecar's order, after the user's: the first
    rule whose faction the NPC carries is theirs. -> (rules added, [faction (sex) kept for the user's rule])."""
    try:
        side = json.loads(pathlib.Path(path).read_text(encoding='utf-8'))
        weights = side['weights']
        lists = collections.defaultdict(list)
        for name, p in side['presets'].items():
            lists[(p['faction'], p['sex'])] += [name] * int(weights[p['tier']])
        factions = side['factions']
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise SystemExit(f'{path}: not a readable factions sidecar ({exc}) -- run tools/pool/factions.py')
    added, kept = 0, []
    for key, f in factions.items():
        for sex, cfg_key in (('female', 'factionFemale'), ('male', 'factionMale')):
            names = lists.get((key, sex))
            if not names:
                continue
            rules_of = cfg.setdefault(cfg_key, {})
            for _plugin, edid in f['factions']:
                if any(str(k).casefold() == edid.casefold() for k in rules_of):
                    kept.append(f'{edid} ({sex})')
                    continue
                rules_of[edid] = list(names)
                added += 1
                if pools is not None:
                    pools.add((edid.casefold(), sex))  # the catalog marks it one of Silhouette's own (S-73)
    return added, kept


def faction_preset_names(path=FACTIONS_SIDECAR):
    """Every preset the factions' sidecar names (S-72); none when there is no sidecar."""
    try:
        return list(json.loads(pathlib.Path(path).read_text(encoding='utf-8'))['presets'])
    except (OSError, ValueError, KeyError):
        return []


def random_line_names(pool_rows, pool):
    """The template names of one sex's random line, each as many times as its tier's weight: BodyGen picks
    one entry of the line uniformly, so repetition is the weighting (docs/bodygen-format.md)."""
    return [n for n, _v, p in pool_rows for _ in range(pool[p['name'].casefold()]['weight'] if pool else 1)]


def template_name(preset):
    return preset.get('marker') or plain_marker(preset['name'])


def target_values(preset, base):
    """{morph: value 0..1} this preset puts the body at, as BodySlide would build it."""
    if base['set']:
        return base_body.resolve(preset, base['set'])
    return dict(preset['sliders'])      # no set to read defaults from: as written


def template_text(name, values, stamp, ranges=()):
    """A template's right-hand side: the morphs LooksMenu must add to the base body
    to reach the preset, then the marker. `values` comes from morph_values().

    Only morphs the body has (others move nothing and only bloat the co-save) and
    only non-zero differences. The marker's VALUE is the generation stamp: LooksMenu
    stores any non-zero value and moves no vertex for a name no .tri has, so the
    value is free to say which generation of the files rolled this NPC -- the
    manifest of that generation names the exact preset and its values (S-12).
    """
    return ([f'{m}@{fmt(v)}' for m, v in values] + [f'{m}@{fmt(lo)}:{fmt(hi)}' for m, (lo, hi) in ranges]
            + [f'{name}@{stamp}'])


def judge_base(b):
    """A measured base (base_body.measure), judged for Silhouette. S-62: a base whose baked values are ALL
    never part of a body -- fo4-anatomy's build slider at its set default -- is zeroed for Silhouette:
    nothing of Silhouette's is baked in, absolute files land every body exactly, and nothing needs
    rebuilding. What the build itself baked in stays visible as 'owned' (describe() prints it)."""
    b = dict(b, owned={})
    if b['status'] != 'preset' or not b.get('baked'):
        return b
    live = {m: v for m, v in b['baked'].items() if abs(v) >= 5e-5}
    # The build's own values stay in the base whatever the mode: morph_values() neither sets nor
    # compensates a never-in-body morph, so the body a preset means includes them (verify_bodygen).
    b['owned'] = {m: v for m, v in live.items() if never_in_body(m)}
    if live and len(b['owned']) == len(live):
        b.update(status='zeroed', baked={})
    return b


def describe(base):
    if base['status'] == 'zeroed' and base.get('owned'):
        return (f'{base["note"]}  ({100 * (base["unexplained"] or 0):.3f}% unexplained) -- nothing of Silhouette\'s '
                f'baked in: ' + ', '.join(f'{m} {fmt(v)}' for m, v in sorted(base['owned'].items()))
                + f' is the build\'s own ({why_never(next(iter(base["owned"])))})')
    if base['status'] == 'zeroed':
        return f'{base["note"]} -- nothing baked in'
    if base['status'] == 'preset':
        return f'{base["note"]}  ({100 * base["unexplained"]:.3f}% unexplained)'
    return base['note']


def built_roots(args):
    """Where the built bodies are looked for: every --built folder, then Data."""
    return list(args.built or []) + [args.data]


def ini_bytes(name, lines):
    """A BodyGen file's bytes: CRLF (see below), in cp1252 -- the code page the game keeps a
    plugin's file name in, which LooksMenu compares a line's plugin with byte for byte. A
    comment may lose a character cp1252 cannot hold; a line LooksMenu reads may not: a plugin
    or morph it would misspell is refused, naming the line (L4 F11)."""
    out = bytearray()
    for line in lines:
        try:
            data = line.encode('cp1252')
        except UnicodeEncodeError:
            if not line.startswith('#'):
                raise SystemExit(f'{name}: {line[:100]!r} holds a character the game\'s code page (cp1252) cannot, '
                                 f'so LooksMenu would never match it -- rename that plugin or preset')
            data = line.encode('cp1252', errors='replace')
        out += data + b'\r\n'
    return bytes(out)


def fmt(v):
    """A float both LooksMenu (atof) and the Papyrus compiler read: fixed point,
    never exponent notation, no trailing zeros."""
    s = f'{v:.4f}'.rstrip('0').rstrip('.')
    return '0' if s in ('', '-0') else s


def morph_values(name, target, baked, morphs_on_body, preset_name):
    """[(morph, value)] LooksMenu must add to the base body to reach `target`,
    without the marker. Shared by the BodyGen templates and the player picker so
    the two can never disagree about what a preset is. A morph whose NAME holds a
    BodyGen separator (= / , | @) would be cut in two by LooksMenu's parser and is
    skipped with a warning; spaces are fine ("7B Lower" is a real master slider)."""
    out = []
    for morph in sorted(set(target) | set(baked)):
        if morph not in morphs_on_body:
            continue
        if BODYGEN_SEPARATORS.search(morph):
            print(f'  skipped morph {morph!r} in {preset_name!r}: its name holds a BodyGen separator')
            continue
        if never_in_body(morph):
            # neither set nor compensated: the base's own state, shaft or fo4-anatomy's build value is not
            # ours to change either. An anatomy-owned slider sits at its set default in every preset that
            # does not name it, so it is reported once with the base (main), not preset by preset.
            if abs(target.get(morph, 0.0)) >= 5e-5 and not anatomy_owned(morph):
                print(f'  left out {morph!r} in {preset_name!r}: {why_never(morph)}')
            continue
        # Exactly the number the template file will carry: the plugin's bodies, the picker, the
        # manifest and the catalog then all say the same thing to the last digit.
        v = float(fmt(target.get(morph, 0.0) - baked.get(morph, 0.0)))
        if v == 0.0:
            continue
        out.append((morph, v))
    return out


# --------------------------------------------------------------------------
# the player picker: an MCM menu and the script it calls
# --------------------------------------------------------------------------

MOD = 'Silhouette'
SCRIPT = 'Silhouette:Player'


def papyrus_string(s):
    """A Papyrus string literal saying exactly s: the menu shows a name as the catalog holds it, and
    the picker hands that same name to the plugin (L4 F9)."""
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t') + '"'


def list_hash(entries):
    """8 hex digits naming one exact option list. It goes into the MCM setting id,
    so a choice saved against a different list is never read as an index into
    this one (MCM keeps changed values in Data/MCM/Settings/<mod>.ini forever)."""
    text = '\n'.join(f'{e["marker"]}|{e["display"]}' for e in entries)
    return hashlib.sha1(text.encode('utf-8')).hexdigest()[:8]


def setting_id(g, entries):
    return f'i{g.capitalize()}_{list_hash(entries)}'


BRIDGE_FORM = 'Silhouette.esp|802'   # tools/make_esp.py: the quest that runs Silhouette:Bridge

# The NPC picker's hotkeys (S-22) and the picker window's (S-79): MCM keybind id -> (what it does,
# Silhouette:Bridge function).
HOTKEYS = (
    ('window', 'Open the picker window on the NPC in your sights, or on you', 'OpenWindow'),
    ('pick', 'Pick the NPC in your sights', 'PickerPick'),
    ('next', 'Try the next preset on them', 'PickerNext'),
    ('previous', 'Try the previous preset on them', 'PickerPrevious'),
    ('keep', 'Keep the preset they are trying on', 'PickerKeep'),
    ('cancel', 'Put back the body they had', 'PickerCancel'),
)


def npc_setting_id(g, entries):
    return f'iNpc{g.capitalize()}_{list_hash(entries)}'


def write_mcm(folder, picker, default_index, average, build):
    """MCM/Config/Silhouette: a dropdown per sex and buttons that call global
    functions of Silhouette:Player -- so no plugin is needed. The dropdown shape
    (ModSettingInt + options) and CallGlobalFunction buttons are the ones already
    working in this install (CommonwealthEncounterDirector, Rapport)."""
    def button(text, help_, function):
        return {'type': 'button', 'text': text, 'help': help_,
                'action': {'type': 'CallGlobalFunction', 'script': SCRIPT, 'function': function,
                           'params': []}}

    def bridge_button(text, help_, function):
        return {'type': 'button', 'text': text, 'help': help_,
                'action': {'type': 'CallFunction', 'form': BRIDGE_FORM, 'function': function, 'params': []}}

    needs = ' Needs Silhouette.dll and Silhouette.esp.'
    content = [
        {'type': 'text', 'text': 'Every NPC gets a body the first time you meet them, and keeps it: most '
                                 'people one of Silhouette\'s own pool (mostly ordinary, some rough, a rare '
                                 'fine one), the named characters their own, and the Brotherhood, raiders, '
                                 'Gunners and the other factions one of their faction\'s pool. Your own '
                                 'BodySlide presets are all in the lists below. Your character gets the most '
                                 'average body of the pool unless you choose one here.'},
        {'type': 'section', 'text': 'Your character'},
    ]
    for g, label in (('female', 'If your character is female'), ('male', 'If your character is male')):
        if not picker[g]:
            continue
        content.append({
            'type': 'dropdown', 'id': f'{setting_id(g, picker[g])}:Player', 'text': label,
            'help': f'{len(picker[g])} presets that fit your {g} body. Nothing changes until you '
                    f'press "Apply to my character".',
            'valueOptions': {'sourceType': 'ModSettingInt',
                             'options': [e['display'] for e in picker[g]]},
        })
    content.append(bridge_button(
        'Try presets on in the picker window',
        'Every preset that fits your body, with filters; a click puts one on you live, Apply keeps it, Cancel '
        'puts back what you had. It opens when you close this menu.', 'MenuOpenWindowMe'))
    avg = ' / '.join(average[g] for g in ('female', 'male') if average.get(g))
    content += [
        button('Apply to my character',
               'Gives your character the preset chosen above for their sex. It replaces the body '
               'sliders LooksMenu and BodyGen set, including ones you set in LooksMenu yourself; '
               'body morphs other mods add are left alone. Close the menu to see it.',
               'ApplyChosen'),
        button('Back to the default', f'The most average body of the pool: {avg}.', 'ApplyDefault'),
        button('Which body do I have?', 'Names the preset Silhouette last gave your character.',
               'ShowCurrent'),
        {'type': 'section', 'text': 'Everyone else'},
        button('Count the bodies around me',
               'How many people nearby have a Silhouette body, and in how many different presets. '
               'Each one, with its preset, is written to the Papyrus log.', 'Census'),
        button('Refresh the people around me',
               'Everyone nearby keeps their preset but gets its values from this build again -- for '
               'after you edited a preset or rebuilt your bodies. Body morphs other mods add are '
               'left alone. With Silhouette.dll each keeps their own nipple and genital variety; '
               'without it the preset\'s values alone are written.', 'Refresh'),
        button('Give the people around me new bodies',
               'Everyone nearby (never your character) rolls a new body, as if met for the first '
               'time, and the rules by name and faction get their say: someone a rule covers draws '
               'again from its presets. With Silhouette.dll, body morphs other mods keep under their '
               'own keyword are kept, and anyone in an AAF scene gets theirs when the scene ends; '
               'without it those morphs are cleared too. Cannot be undone.',
               'Reroll'),
        # S-68 (owner poll): the whole save, not only who is near, and the bodies you picked too.
        bridge_button('Reset everyone',
                      'A fresh start: forgets every body Silhouette gave -- the ones you picked too -- and '
                      'decides them all again, as if everyone were met for the first time. Named characters '
                      'get their own body, the rules by name and faction draw again, everyone else rolls a '
                      'new body from the pool. People around you change at once; everyone else the next time '
                      'you meet them (the save remembers the reset). Body morphs other mods keep under their '
                      'own keyword are kept. Press it twice to confirm. Cannot be undone.' + needs,
                      'MenuResetEveryone'),
        {'type': 'section', 'text': 'Regeneration window'},
        {'type': 'text', 'text': 'LooksMenu only shapes people who hold no body morphs at all, so '
                                 'someone another mod already marked (an AAF morph left by a scene) '
                                 'never gets a body. For 24 in-game hours after Silhouette first '
                                 'loads, such people around you are given one, and the other mod\'s '
                                 'morphs are kept. Needs Silhouette.esp.'},
        button('How is the regeneration window doing?',
               'Open or closed, the in-game hours left, and how many people it has given a body.',
               'RegenerationStatus'),
        button('Open a new 24-hour regeneration window',
               'Starts another 24 in-game hours of giving bodies to people other mods marked first. '
               'People it already handled are not handled twice.', 'OpenRegenerationWindow'),
    ]
    window_help = ('A window beside them lists every preset that fits their body, with filters; a click puts one '
                   'on them live, Apply keeps it, Cancel puts back what they had. It opens when you close this '
                   'menu.')
    npcs = [
        {'type': 'text', 'text': 'Aim at someone, then open this menu -- or Pick them with the hotkey below '
                                 'and open it any time. Anyone you give a preset keeps it: the rules by name '
                                 'and faction no longer change them.' + needs},
        {'type': 'section', 'text': 'The picker window'},
        bridge_button('Open the picker window',
                      'On the NPC you aimed at in the half minute before opening this menu, or on you when you '
                      'aimed at nobody. ' + window_help + ' The hotkey below opens it at once.' + needs,
                      'MenuOpenWindow'),
        {'type': 'section', 'text': 'Their body'},
    ]
    for g, label in (('female', 'If they are female'), ('male', 'If they are male')):
        if not picker[g]:
            continue
        npcs.append({
            'type': 'dropdown', 'id': f'{npc_setting_id(g, picker[g])}:Picker', 'text': label,
            'help': f'{len(picker[g])} presets that fit a {g} body. Nothing changes until you press '
                    f'"Give them this preset".',
            'valueOptions': {'sourceType': 'ModSettingInt', 'options': [e['display'] for e in picker[g]]},
        })
    npcs += [
        bridge_button('Give them this preset',
                      'The preset chosen above for their sex, on the NPC you picked -- or, with nobody '
                      'picked, the last NPC you aimed at in the half minute before opening the menu. Their '
                      'own nipple and genital variety comes with it while those switches are on (Settings). '
                      'Close the menu to see it.', 'MenuApply'),
        bridge_button('Reset',
                      'Forget the body you gave them: Silhouette decides again, as if they were met for the '
                      'first time. A named character gets their own body back; someone a rule by name or '
                      'faction covers draws again from that rule\'s presets -- a different one whenever the '
                      'rule lists more than one -- and keeps the new draw; anyone else rolls a new body from '
                      'the pool. Body morphs other mods keep under their own keyword (AAF, pregnancy) are '
                      'kept. In the middle of an AAF scene it waits for the scene to end.',
                      'MenuRandom'),
        bridge_button('Which body do they have?', 'The preset their body carries, and who chose it.',
                      'MenuWhich'),
        {'type': 'section', 'text': 'Hotkeys: try presets on them in the world'},
        {'type': 'text', 'text': 'Pick someone, then Next and Previous put each preset on them in turn '
                                 '(a second or so each: LooksMenu is reached through Papyrus, one value '
                                 'a frame, and the picker goes before any other work). Keep makes it '
                                 'theirs; Cancel puts back exactly what they had.'},
    ]
    npcs += [{'type': 'hotkey', 'id': kid, 'text': text, 'help': text + '.' + needs}
             for kid, text, _fn in HOTKEYS]

    settings = [
        {'type': 'section', 'text': 'While they are dressed'},
        {'type': 'switcher', 'id': 'bORefit:General', 'text': 'ORefit',
         'help': 'A clothed shape while someone is dressed: breasts held together and lifted, and under '
                 'heavy clothes -- armour, jackets, coats, told by the item\'s name -- the nipple sliders '
                 'flattened. That flattening only shows on heavy outfits whose meshes carry the nipple '
                 'sliders (mod outfits built with them in BodySlide); no vanilla or DLC heavy outfit does, so '
                 'on those it changes nothing you can see. It only ever raises a slider, so a body that is already '
                 'fuller keeps its own, and the moment they undress they are exactly their own body again. '
                 'Your character is never refit, nor anyone in power armour. Removing Silhouette.esp takes '
                 'every clothed shape off by itself, and without a working Silhouette.dll the shapes left '
                 'on people are taken off as you meet them.' + needs,
         'valueOptions': {'sourceType': 'ModSettingBool'}},
        {'type': 'section', 'text': 'Variety in the bodies Silhouette gives'},
        {'type': 'text', 'text': 'BodyGen rolls every NPC their own nipples (and genital shape for women, '
                                 'ball size for men) from the generated files, always. These two decide '
                                 'the same for bodies Silhouette itself gives -- the rules, the picker, '
                                 'other mods -- and for the touch-up of bodies it gave before: whether the '
                                 'variety such a body is missing gets added. Taking a runtime state or the '
                                 'shaft out of a body happens either way.'},
        {'type': 'switcher', 'id': 'bNippleRand:General', 'text': 'Nipple variety',
         'help': 'Each person their own nipple size, areola and tip.', 'valueOptions': {'sourceType': 'ModSettingBool'}},
        {'type': 'switcher', 'id': 'bGenitalRand:General', 'text': 'Genital variety',
         'help': 'Each woman her own genital shape, each man his own ball size. Never the shaft.',
         'valueOptions': {'sourceType': 'ModSettingBool'}},
        {'type': 'section', 'text': 'What Silhouette does by itself'},
        {'type': 'switcher', 'id': 'bFreshStart:General', 'text': 'Fresh start for saves new to Silhouette',
         'help': 'The first time a save loads with Silhouette, everyone you already met still has the body they '
                 'had before, since a body is only given to someone who has none. On, Silhouette presses Reset '
                 'everyone for that save by itself, once -- replacing other mods\' bodies and sliders set by hand '
                 'on the people it reaches too. Off, they keep them until you press Reset everyone yourself; '
                 'switched on in the save\'s first 24 in-game hours, it still happens.' + needs,
         'valueOptions': {'sourceType': 'ModSettingBool'}},
        {'type': 'switcher', 'id': 'bFactionPools:General', 'text': 'Faction bodies',
         'help': 'The Brotherhood, the Minutemen, Gunners, raiders, Nuka-World\'s gangs, the Triggermen, the '
                 'Institute, the Railroad and the Children of Atom each draw from a pool of their own, in their '
                 'own look. Off, they draw from the same pool as everyone else; your own faction rules apply '
                 'either way. It decides who is given a body from now on: a faction body someone already has '
                 'stays until Reset.' + needs,
         'valueOptions': {'sourceType': 'ModSettingBool'}},
        {'type': 'switcher', 'id': 'bNotices:General', 'text': 'Tell me when a change I asked for waits',
         'help': 'When a change you asked for -- a Reset, a preset given, a picker try -- has to wait because '
                 'another mod has them busy in a scene, a notification says so, and another when it is done.'
                 + needs,
         'valueOptions': {'sourceType': 'ModSettingBool'}},
        {'type': 'section', 'text': 'Silhouette'},
        {'type': 'button', 'text': 'How is Silhouette doing?',
         'help': 'Which build is loaded, what it is listening to, and how much work is waiting -- and '
                 'what is missing when something is.',
         'action': {'type': 'CallGlobalFunction', 'script': 'Silhouette:API', 'function': 'ShowStatus',
                    'params': []}},
    ]

    config = {'modName': MOD, 'displayName': MOD, 'minMcmVersion': 1,
              'pages': [{'pageDisplayName': 'Bodies', 'content': content},
                        {'pageDisplayName': 'The NPC in your sights', 'content': npcs},
                        {'pageDisplayName': 'Settings', 'content': settings}]}
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'config.json').write_text(json.dumps(config, indent=2) + '\n', encoding='utf-8')
    keybinds = {'modName': MOD, 'keybinds': [
        {'id': kid, 'desc': f'Silhouette: {text[0].lower()}{text[1:]}',
         'action': {'type': 'CallFunction', 'form': BRIDGE_FORM, 'function': fn, 'params': []}}
        for kid, text, fn in HOTKEYS]}
    (folder / 'keybinds.json').write_text(json.dumps(keybinds, indent=2) + '\n', encoding='utf-8')
    ini = ['; GENERATED by tools/silhouette_gen.py. The defaults are the most average preset.',
           '[Player]', f'sBuild={build}']
    ini += [f'{setting_id(g, picker[g])}={default_index.get(g, 0)}' for g in ('female', 'male')
            if picker[g]]
    ini += ['[Picker]']
    ini += [f'{npc_setting_id(g, picker[g])}={default_index.get(g, 0)}' for g in ('female', 'male')
            if picker[g]]
    ini += ['[General]', 'bORefit=1', 'bNippleRand=1', 'bGenitalRand=1', 'bFreshStart=1', 'bFactionPools=1',
            'bNotices=1']
    # A key on NO control: MCM answers false/0 for a key it never loaded, so the bridge believes MCM's
    # switches only when this reads 1 -- this file was read, and every answer is a setting, not a gap
    # (wave 4 lens 2 L3; the "settings read" sentinel of mcm-settings-not-globals).
    ini += ['; Read by Silhouette:Bridge before it trusts any value above; no menu control shows it. Do not remove.',
            '[Meta]', 'iDefaults=1']
    (folder / 'settings.ini').write_text('\n'.join(ini) + '\n', encoding='utf-8')


# The Papyrus VM grows no array past 128 entries, by `new` or by Add (arrays a native returns are not
# capped). The picker's lists are therefore generated in PARTS of this size: part p holds entries
# p * ARRAY_LIMIT on, every part but the last one of a sex is full, and Locate()/At() count on both.
ARRAY_LIMIT = 128


def picker_parts(picker):
    """How many parts every list of the picker script has: the same for both sexes, at least one."""
    return max(1, *(-(-len(picker[g]) // ARRAY_LIMIT) for g in ('female', 'male')))


def write_papyrus(path, picker, default_index, stamp, build):
    """Silhouette:Player -- the functions the MCM buttons call. The preset values
    are generated into it because Papyrus cannot read the preset files; the same
    morph_values() made the BodyGen templates, so a preset means one thing in both.

    Only the unkeyed morph layer is ever cleared (RemoveMorphsByKeyword with None:
    the key BodyGen, LooksMenu's own sliders and Silhouette all write), so body
    morphs another mod keeps under its own keyword survive. The one exception is
    Reroll, which has to go through RegenerateMorphs and says so in its help.
    """
    ids = {g: setting_id(g, picker[g]) for g in ('female', 'male')}
    npc_ids = {g: npc_setting_id(g, picker[g]) for g in ('female', 'male')}
    parts = picker_parts(picker)
    params = ', '.join(f'String[] m{p}, String[] n{p}' for p in range(parts))
    each = lambda fmt_: ', '.join(fmt_.format(p=p) for p in range(parts))
    lists = {cap: each(cap + 'Markers{p}(), ' + cap + 'Names{p}()') for cap in ('Female', 'Male')}
    L = [
        'Scriptname Silhouette:Player Hidden',
        '{GENERATED by tools/silhouette_gen.py from your BodySlide presets. Do not edit:',
        ' run the generator again after adding presets or rebuilding a body.',
        ' Called by the MCM menu. Needs LooksMenu (BodyGen) and MCM.}',
        '',
        '; Every argument is passed explicitly: the decompiled base sources carry no defaults.',
        f'; Build {build}; marker stamp {stamp}.',
        '',
        f'String Function Build() Global',
        f'    Return "{build}"',
        'EndFunction',
        '',
        f'Float Function Stamp() Global',
        f'    Return {stamp}.0',
        'EndFunction',
        '',
        '; Never part of a Silhouette body: the morphs other mods drive at runtime (S-16), the',
        '; shaft (S-29) and fo4-anatomy\'s build slider (S-62). Without Silhouette.dll the',
        '; regeneration window takes them out of bodies Silhouette gave; with it, the plugin heals',
        '; what a body\'s template once wrote.',
        'String[] Function StateMorphs() Global',
        f'    String[] out = new String[{len(NEVER_IN_BODY)}]',
        *[f'    out[{i}] = {papyrus_string(m)}' for i, m in enumerate(NEVER_IN_BODY)],
        '    Return out',
        'EndFunction',
        '',
        'Bool Function IsFemale(Actor akActor) Global',
        '    Return akActor.GetLeveledActorBase().GetSex() == 1',
        'EndFunction',
        '',
        '; The preset chosen on the NPC page (Silhouette:Bridge.MenuApply), "" for a choice',
        '; this build of the menu does not have.',
        'String Function NpcChoice(Bool female) Global',
        '    If !MCM.IsInstalled()',
        '        Return ""',
        '    EndIf',
        '    Int index = -1',
        '    If female',
        f'        index = MCM.GetModSettingInt("{MOD}", "{npc_ids["female"]}:Picker")',
        '    Else',
        f'        index = MCM.GetModSettingInt("{MOD}", "{npc_ids["male"]}:Picker")',
        '    EndIf',
        '    If index < 0 || index >= Count(female)',
        '        Return ""',
        '    EndIf',
        '    If female',
        f'        Return At(index, {each("FemaleNames{p}()")})',
        '    EndIf',
        f'    Return At(index, {each("MaleNames{p}()")})',
        'EndFunction',
        '',
        'Int Function Count(Bool female) Global',
        '    If female',
        f'        Return {len(picker["female"])}',
        '    EndIf',
        f'    Return {len(picker["male"])}',
        'EndFunction',
        '',
        '; Clears the unkeyed layer only, applies preset `index`, reshapes the 3D.',
        '; Returns the preset name, or "" for an index this build does not have.',
        'String Function Give(Actor akActor, Bool female, Int index) Global',
        '    If index < 0 || index >= Count(female)',
        '        Return ""',
        '    EndIf',
        '    BodyGen.RemoveMorphsByKeyword(akActor, female, None)',
        '    String name = ""',
        '    If female',
        '        name = ApplyFemale(akActor, index)',
        '    Else',
        '        name = ApplyMale(akActor, index)',
        '    EndIf',
        '    BodyGen.UpdateMorphs(akActor)',
        '    Return name',
        'EndFunction',
        '',
        'Function ApplyChosen() Global',
        '    If !MCM.IsInstalled()',
        '        Debug.MessageBox("Silhouette: MCM\'s script is missing, so the choice cannot be read. Install F4SE Menu Framework or MCM.")',
        '        Return',
        '    EndIf',
        f'    String menu = MCM.GetModSettingString("{MOD}", "sBuild:Player")',
        '    If menu != "" && menu != Build()',
        '        Debug.MessageBox("Silhouette: the menu and the script come from different builds. Install the generated files together, then choose again.")',
        '        Return',
        '    EndIf',
        '    Actor player = Game.GetPlayer()',
        '    Bool female = IsFemale(player)',
        '    Int index = -1',
        '    If female',
        f'        index = MCM.GetModSettingInt("{MOD}", "{ids["female"]}:Player")',
        '    Else',
        f'        index = MCM.GetModSettingInt("{MOD}", "{ids["male"]}:Player")',
        '    EndIf',
        '    If index < 0 || index >= Count(female)',
        '        Debug.MessageBox("Silhouette: that choice is not in this build of the menu. Nothing was changed.")',
        '        Return',
        '    EndIf',
        '    Debug.MessageBox("Your body is now " + Give(player, female, index) + ".")',
        'EndFunction',
        '',
        '; The most average preset -- the same one the Fallout4.esm|7 lines in',
        '; Silhouette_morphs.ini give a character with no body sliders. A sex with no preset',
        '; that fits fully has none: the bare body built in BodySlide, as BodyGen gives it.',
        'Function ApplyDefault() Global',
        '    Actor player = Game.GetPlayer()',
        '    Bool female = IsFemale(player)',
        '    Int index = ' + str(default_index.get('male', -1)),
        '    If female',
        '        index = ' + str(default_index.get('female', -1)),
        '    EndIf',
        '    If index < 0',
        '        BodyGen.RemoveMorphsByKeyword(player, female, None)',
        '        BodyGen.UpdateMorphs(player)',
        '        Debug.MessageBox("Your body is now the bare body you built in BodySlide.")',
        '        Return',
        '    EndIf',
        '    Debug.MessageBox("Your body is now " + Give(player, female, index) + ".")',
        'EndFunction',
        '',
        '; The preset Silhouette last gave an actor, read back from its marker: the name,',
        '; "" when it holds no body sliders at all, or "*" when it holds sliders that',
        '; Silhouette did not set. A marker counts only while it holds a value: removing',
        '; a keyword empties a morph but leaves its name listed until the next load.',
        f'String Function PresetOf(Actor akActor, Bool female, {params}) Global',
        '    String[] morphs = BodyGen.GetMorphs(akActor, female)',
        '    Int count = 0',
        '    If morphs',
        '        count = morphs.Length',
        '    EndIf',
        '    If count == 0',
        '        Return ""',
        '    EndIf',
        '    Int i = 0',
        '    While i < count',
        f'        Int k = Locate(morphs[i], {each("m{p}")})',
        '        If k >= 0 && BodyGen.GetMorph(akActor, female, morphs[i], None) > 0.0',
        f'            Return At(k, {each("n{p}")})',
        '        EndIf',
        '        i += 1',
        '    EndWhile',
        '    Return "*"',
        'EndFunction',
        '',
        f'; The picker\'s lists come in {parts} part(s) of at most {ARRAY_LIMIT}: the Papyrus VM grows no array',
        '; past that. Part p holds the entries from p * 128 on; every part but the last is full.',
        f'Int Function Locate(String s, {each("String[] a{p}")}) Global',
        '    Int k = -1',
        *[line for p in range(parts) for line in (
            f'    k = a{p}.Find(s, 0)',
            '    If k >= 0',
            f'        Return {p * ARRAY_LIMIT} + k',
            '    EndIf')],
        '    Return -1',
        'EndFunction',
        '',
        '; Entry `k` of a list given in parts, "" past its end.',
        f'String Function At(Int k, {each("String[] a{p}")}) Global',
        '    If k < 0',
        '        Return ""',
        *[line for p in range(parts) for line in (
            f'    ElseIf k < {(p + 1) * ARRAY_LIMIT}',
            f'        If k - {p * ARRAY_LIMIT} < a{p}.Length',
            f'            Return a{p}[k - {p * ARRAY_LIMIT}]',
            '        EndIf',
            '        Return ""')],
        '    EndIf',
        '    Return ""',
        'EndFunction',
        '',
        '; The first of `morphs` that is one of this build\'s markers, "" for none. The regeneration',
        '; window\'s heal without Silhouette.dll (Silhouette:Adopter, S-16) asks it.',
        'String Function MarkerAmong(Bool female, String[] morphs) Global',
        '    If !morphs',
        '        Return ""',
        '    EndIf',
        *[f'    String[] m{p}' for p in range(parts)],
        '    If female',
        *[f'        m{p} = FemaleMarkers{p}()' for p in range(parts)],
        '    Else',
        *[f'        m{p} = MaleMarkers{p}()' for p in range(parts)],
        '    EndIf',
        '    Int i = 0',
        '    While i < morphs.Length',
        f'        If Locate(morphs[i], {each("m{p}")}) >= 0',
        '            Return morphs[i]',
        '        EndIf',
        '        i += 1',
        '    EndWhile',
        '    Return ""',
        'EndFunction',
        '',
        'Function ShowCurrent() Global',
        '    Actor player = Game.GetPlayer()',
        '    Bool female = IsFemale(player)',
        '    String preset = ""',
        '    If female',
        f'        preset = PresetOf(player, True, {lists["Female"]})',
        '    Else',
        f'        preset = PresetOf(player, False, {lists["Male"]})',
        '    EndIf',
        '    If preset == "*"',
        '        ; One of your own presets, put on through the window\'s Me tab: the plugin\'s catalog names it (S-76).',
        '        String named = Silhouette:API.MarkerPreset(player)',
        '        If named != ""',
        '            preset = named',
        '        EndIf',
        '    EndIf',
        '    If preset == ""',
        '        Debug.MessageBox("Your character has no body sliders: the bare body you built in BodySlide.")',
        '    ElseIf preset == "*"',
        '        Debug.MessageBox("Your body was not set by Silhouette: it holds LooksMenu body sliders of its own.")',
        '    Else',
        '        Debug.MessageBox("Silhouette last gave you: " + preset + ".")',
        '    EndIf',
        'EndFunction',
        '',
        '; The regeneration window lives in Silhouette.esp (decision S-15); everything',
        '; else here works without it.',
        'Silhouette:Adopter Function Adopter() Global',
        '    If !Game.IsPluginInstalled("Silhouette.esp")',
        '        Return None',
        '    EndIf',
        '    Return Game.GetFormFromFile(0x800, "Silhouette.esp") as Silhouette:Adopter',
        'EndFunction',
        '',
        'Function RegenerationStatus() Global',
        '    Silhouette:Adopter q = Adopter()',
        '    If !q',
        '        Debug.MessageBox("Silhouette: the regeneration window needs Silhouette.esp enabled in your load order.")',
        '    ElseIf q.IsOpen()',
        '        Debug.MessageBox("Silhouette: the regeneration window is open for " + (q.HoursLeft() as Int) + " more in-game hours. " + q.Adopted() + " people given a body so far.")',
        '    Else',
        '        Debug.MessageBox("Silhouette: the regeneration window is closed. " + q.Adopted() + " people were given a body.")',
        '    EndIf',
        'EndFunction',
        '',
        'Function OpenRegenerationWindow() Global',
        '    Silhouette:Adopter q = Adopter()',
        '    If !q',
        '        Debug.MessageBox("Silhouette: the regeneration window needs Silhouette.esp enabled in your load order.")',
        '        Return',
        '    EndIf',
        '    q.OpenWindow()',
        '    Debug.MessageBox("Silhouette: regeneration window open for the next 24 in-game hours.")',
        'EndFunction',
        '',
        '; Everyone nearby, never the player.',
        'Actor[] Function Nearby() Global',
        '    Actor player = Game.GetPlayer()',
        '    Keyword npc = Game.GetFormFromFile(0x13794, "Fallout4.esm") as Keyword  ; ActorTypeNPC',
        '    ObjectReference[] found = player.FindAllReferencesWithKeyword(npc, 4096.0)',
        '    Actor[] out = new Actor[0]',
        '    Int count = 0',
        '    If found',
        '        count = found.Length',
        '    EndIf',
        '    Int i = 0',
        '    While i < count',
        '        Actor a = found[i] as Actor',
        '        If a && a != player && out.Length < 128',
        '            out.Add(a, 1)',
        '        EndIf',
        '        i += 1',
        '    EndWhile',
        '    Return out',
        'EndFunction',
        '',
        '; Who around the player has which body: a summary in a message box, and one',
        '; line per actor in the Papyrus log ("Silhouette census: <ref form id, decimal>',
        '; <F|M> <preset>"). From the console: cgf "Silhouette:Player.Census"',
        'Function Census() Global',
        '    Actor[] people = Nearby()',
        *[f'    String[] {v}{p} = {cap}{kind}{p}()' for p in range(parts)
          for v, cap, kind in (('fm', 'Female', 'Markers'), ('fn', 'Female', 'Names'),
                               ('mm', 'Male', 'Markers'), ('mn', 'Male', 'Names'))],
        '    Int shaped = 0',
        '    Int own = 0',
        '    String[] distinct = new String[0]',
        '    Int i = 0',
        '    While i < people.Length',
        '        Actor a = people[i]',
        '        Bool female = IsFemale(a)',
        '        String preset = ""',
        '        String sex = "M"',
        '        If female',
        f'            preset = PresetOf(a, True, {each("fm{p}, fn{p}")})',
        '            sex = "F"',
        '        Else',
        f'            preset = PresetOf(a, False, {each("mm{p}, mn{p}")})',
        '        EndIf',
        '        Debug.Trace("Silhouette census: " + a.GetFormID() + " " + sex + " " + preset, 0)',
        '        If preset == "*"',
        '            own += 1',
        '        ElseIf preset != ""',
        '            shaped += 1',
        '            If distinct.Find(preset, 0) < 0 && distinct.Length < 128',
        '                distinct.Add(preset, 1)',
        '            EndIf',
        '        EndIf',
        '        i += 1',
        '    EndWhile',
        '    Debug.MessageBox("Silhouette: " + people.Length + " people around you. " + shaped + " have a Silhouette body, in " + distinct.Length + " different presets. " + own + " hold body sliders of their own. The rest have none: not a race Silhouette shapes, blacklisted, or not generated yet.")',
        'EndFunction',
        '',
        '; Everyone nearby keeps the preset Silhouette gave them, with its values from this build.',
        '; With Silhouette.dll ready the plugin gives it again, their own variety kept (S-17, S-21),',
        '; as bulk work behind anything the player or another mod asked for (S-55); without it --',
        '; or with its catalog refused -- the preset alone is written, and the rolled variety is lost.',
        'Function Refresh() Global',
        '    Actor[] people = Nearby()',
        *[f'    String[] {v}{p} = {cap}{kind}{p}()' for p in range(parts)
          for v, cap, kind in (('fm', 'Female', 'Markers'), ('fn', 'Female', 'Names'),
                               ('mm', 'Male', 'Markers'), ('mn', 'Male', 'Names'))],
        '    Bool plugin = Silhouette:API.IsReady()',
        '    Int done = 0',
        '    Int i = 0',
        '    While i < people.Length',
        '        Actor a = people[i]',
        '        Bool female = IsFemale(a)',
        '        String preset = ""',
        '        Int index = -1',
        '        If female',
        f'            preset = PresetOf(a, True, {each("fm{p}, fn{p}")})',
        f'            index = Locate(preset, {each("fn{p}")})',
        '        Else',
        f'            preset = PresetOf(a, False, {each("mm{p}, mn{p}")})',
        f'            index = Locate(preset, {each("mn{p}")})',
        '        EndIf',
        '        If index >= 0',
        '            If plugin',
        '                If Silhouette:DLL.RequestReapply(a.GetFormID(), Silhouette:API.MarkerPreset(a), 2) == ""',
        '                    done += 1',
        '                EndIf',
        '            ElseIf Give(a, female, index) != ""',
        '                done += 1',
        '            EndIf',
        '        EndIf',
        '        i += 1',
        '    EndWhile',
        '    If plugin',
        '        Debug.MessageBox("Silhouette: " + done + " of " + people.Length + " people around you get their preset again, with this build\'s values -- over the next moments. The rest have no Silhouette body, or one this build no longer has.")',
        '    Else',
        '        Debug.MessageBox("Silhouette: " + done + " of " + people.Length + " people around you have their preset again, with this build\'s values. The rest have no Silhouette body, or one this build no longer has.")',
        '    EndIf',
        'EndFunction',
        '',
        '; Everyone nearby rolls again, as if met for the first time. With Silhouette.dll ready the',
        '; plugin does it as bulk work (S-55): it knows who it rolled, the rules get their say, other',
        '; mods\' keyed morphs stay, and anyone in an AAF scene waits for it to end. Without it',
        '; RegenerateMorphs is the only way to run BodyGen for an actor again, and it clears every key.',
        'Function Reroll() Global',
        '    Actor[] people = Nearby()',
        '    Bool plugin = Silhouette:API.IsReady()',
        '    Int done = 0',
        '    Int i = 0',
        '    While i < people.Length',
        '        If plugin',
        '            If Silhouette:DLL.RequestRegenerate(people[i].GetFormID(), 2) == ""',
        '                done += 1',
        '            EndIf',
        '        Else',
        '            BodyGen.RegenerateMorphs(people[i], True)',
        '            done += 1',
        '        EndIf',
        '        i += 1',
        '    EndWhile',
        '    If plugin',
        '        Debug.MessageBox("Silhouette: " + done + " of " + people.Length + " people around you get a new body -- over the next moments.")',
        '    Else',
        '        Debug.MessageBox("Silhouette: " + done + " of " + people.Length + " people around you have a new body.")',
        '    EndIf',
        'EndFunction',
    ]
    for g, cap, female in (('female', 'Female', 'True'), ('male', 'Male', 'False')):
        entries = picker[g]
        for kind, key in (('Markers', 'marker'), ('Names', 'display')):
            for p in range(parts):
                L += ['', f'String[] Function {cap}{kind}{p}() Global', '    String[] a = new String[0]']
                L += [f'    a.Add({papyrus_string(e[key])}, 1)' for e in entries[p * ARRAY_LIMIT:(p + 1) * ARRAY_LIMIT]]
                L += ['    Return a', 'EndFunction']
        L += ['', '; Sets the values only; Give() clears the layer first and reshapes after.',
              f'String Function Apply{cap}(Actor a, Int index) Global']
        for i, e in enumerate(entries):
            L.append(f'    {"If" if i == 0 else "ElseIf"} index == {i}')
            for morph, v in e['values']:
                L.append(f'        BodyGen.SetMorph(a, {female}, {papyrus_string(morph)}, None, {fmt(v)})')
            L.append(f'        BodyGen.SetMorph(a, {female}, {papyrus_string(e["marker"])}, None, {stamp}.0)')
            L.append(f'        Return {papyrus_string(e["display"])}')
        if entries:
            L.append('    EndIf')
        L += ['    Return ""', 'EndFunction']
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(L) + '\n', encoding='utf-8')


def catalog_presets(pools, extra, picker):
    """Every preset the plugin can give, as catalog.build() wants them: the pickers' lists
    first and in their order (the NPC hotkeys walk it), then the rest of the random pool,
    then the presets only the rules name."""
    out, seen = [], set()

    def add(sex, p, marker, values, menu, zeroed):
        key = (sex, p['name'].casefold())
        if key in seen:
            return
        seen.add(key)
        out.append({'name': p['name'], 'sex': sex, 'marker': marker, 'values': list(values),
                    'random': any(n == marker for n, _v, _p in pools[sex]), 'menu': menu, 'zeroed': zeroed,
                    'fit': p.get('band', 'full'), 'family': (p.get('families') or [''])[0]})

    by_display = {}
    for g in ('female', 'male'):
        for _n, _v, p in pools[g]:
            by_display[(g, p['name'])] = p
        for _n, (_v, p) in extra.items():
            by_display[(p['gender'], p['name'])] = p
    for g in ('female', 'male'):
        for e in picker[g]:
            p = by_display.get((g, e['display']), {'name': e['display'], 'band': e['band'],
                                                   'families': e.get('families', [])})
            add(g, p, e['marker'], e['values'], True, is_zeroed(e['target']))
    for g in ('female', 'male'):
        for name, values, p in pools[g]:
            add(g, p, name, values, False, p.get('zeroed', False))
    for name, (values, p) in sorted(extra.items()):
        add(p['gender'], p, name, values, False, p.get('zeroed', False))
    return out


def refit_sets(refit_presets, base, baked, morphs_of):
    """OBody's refit presets ("<Preset>-Refit", "Female-Refit", "Male-Refit"), as the floors the
    refit keyword holds while dressed -- written the way the templates are, so a refit and a body
    mean the same thing by the same number.

    Only presets that fit the installed body as a body preset must (a Fusion Girl refit on a CBBE
    body moves nothing it means to), and only the sliders a refit preset SETS: resolved against its
    slider set, every slider it leaves alone comes out at the set's default -- a Male-Refit naming
    only BTChest would otherwise hold 25 floors, BTBallSize at 1.0 among them (L4 F4). The same goes
    for what a compensated base has baked in: a slider the refit preset never sets is no floor of its,
    however the base holds it (wave 3 L4 F5 -- a baked Butt made Female-Refit raise Butt)."""
    out = []
    for p in refit_presets:
        g = p['gender']
        if g not in morphs_of:
            continue
        if p.get('band') not in ('full', 'partial'):
            print(f'  refit preset {p["name"]!r} does not fit the installed {g} body ({p.get("band")}) -- left out')
            continue
        target = {k: v for k, v in target_values(p, base[g]).items() if k in morphs_of[g] and k in p['sliders']}
        own_baked = {k: v for k, v in baked[g].items() if k in target}
        out.append({'name': p['name'], 'sex': g,
                    'values': morph_values(template_name(p), target, own_baked, morphs_of[g], p['name'])})
    return out


def is_zeroed(target):
    """A preset that puts nothing on the body: no value it would WRITE -- a state or the shaft it sets
    is never written (S-16, S-29) -- reaches the body, measured absolutely (L4 F5, F6)."""
    return not any(abs(v) >= 5e-5 for m, v in target.items()
                   if not never_in_body(m) and not BODYGEN_SEPARATORS.search(m))


def manifest_templates(pools, extra, picker):
    """{marker: {preset, gender, file, values}}: every body this generation can give, by its marker."""
    templates = {}
    for g in ('female', 'male'):
        for name, values, p in pools[g]:
            templates[name] = {'preset': p['name'], 'gender': g, 'file': p['file'], 'values': dict(values)}
    for name, (values, p) in extra.items():
        templates[name] = {'preset': p['name'], 'gender': p['gender'], 'file': p['file'],
                           'values': dict(values)}
    for g in ('female', 'male'):
        for e in picker[g]:
            templates.setdefault(e['marker'], {'preset': e['display'], 'gender': g,
                                               'values': dict(e['values'])})
    return templates


def first_difference(manifest, templates):
    """What first tells a manifest's bodies from these, in words -- None when it names exactly these bodies:
    each marker the same preset, sex and values (its descriptions -- the bases, a preset's file -- aside).
    The build hash covers markers and values but not a preset's display name, so the same build can meet
    its own manifest under a preset renamed in case only ("CBBE Curvy" -> "CBBE curvy")."""
    had = manifest.get('templates') if isinstance(manifest, dict) else None
    if not isinstance(had, dict):
        return 'the manifest names no bodies it can be compared by'
    for m in sorted(set(had) | set(templates), key=lambda k: (str(k).casefold(), str(k))):
        a, b = had.get(m), templates.get(m)
        if b is None:
            what = repr(a.get('preset')) if isinstance(a, dict) else f'{a!r}, no body at all'
            return f'the manifest names {m} ({what}), which this run no longer gives'
        if a is None:
            return f'this run gives {m} ({b.get("preset")!r}), which the manifest does not name'
        if not isinstance(a, dict):
            return f'{m} ({b.get("preset")!r}): the manifest holds {a!r} there, not a body'
        if a.get('preset') != b.get('preset'):
            if str(a.get('preset')).casefold() == str(b.get('preset')).casefold():
                return (f'{m}: the manifest says preset {a.get("preset")!r}, this run {b.get("preset")!r} -- only '
                        f'the case of the name differs, so rename it back')
            return f'{m}: the manifest says preset {a.get("preset")!r}, this run {b.get("preset")!r} -- rename it back'
        if a.get('gender') != b.get('gender'):
            return f'{m} ({b.get("preset")!r}): the manifest says {a.get("gender")}, this run {b.get("gender")}'
        va, vb = a.get('values') or {}, b.get('values') or {}
        if not isinstance(va, dict):
            return f'{m} ({b.get("preset")!r}): the manifest\'s values are {va!r}, not a body\'s sliders'
        if va != vb:
            morph = next((k for k in sorted(set(va) | set(vb), key=str) if va.get(k) != vb.get(k)), '?')
            return (f'{m} ({b.get("preset")!r}): {morph} is {va.get(morph)!r} in the manifest, {vb.get(morph)!r} '
                    f'in this run')
    return None


def same_bodies(manifest, templates):
    """A manifest names exactly these bodies (first_difference finds nothing)."""
    return first_difference(manifest, templates) is None


def refuse_stamp_clash(stamp, build, templates, *folders, notes=None):
    """Refuses, before anything is written, a stamp a manifest in the folders already gives another build, or
    this build with other bodies than `templates` (manifest_templates()). The stamp is 24 bits of the build's
    hash, and a body's marker carries only the stamp: two builds sharing one could never be told apart, and
    this run would overwrite the other's manifest -- the only thing that says what its bodies are. main()
    passes manifest_folders(): the output root's, then the game's Data's -- a build deployed once has bodies
    in saves.

    The output root's copy is this build's record: unreadable, or other bodies, is refused. Data's copy is
    what was deployed: unreadable is said in `notes` and skipped, and one that differs from a root copy that
    agrees with this run is said too (the next deploy writes the root's over it) -- telling the user to rename
    a preset back could never satisfy both copies (wave 5)."""
    copies = []      # (first folder?, path, manifest) of every readable copy of this stamp's manifest
    for i, folder in enumerate(folders):
        taken = pathlib.Path(folder) / f'{stamp}.json'
        if not taken.exists():
            continue
        try:
            recorded = json.loads(taken.read_text(encoding='utf-8-sig'))
            if not isinstance(recorded, dict):
                raise ValueError(f'a {type(recorded).__name__}, not a manifest')
        except (OSError, ValueError) as exc:
            if i == 0:
                raise SystemExit(f'{taken}: not a manifest this tool can read ({exc!r}) -- restore it (git), since it '
                                 f'says what the bodies of its build are. Nothing was written.')
            if notes is not None:
                notes.append(unreadable_in_data(taken, exc) + '; the next deploy writes this build\'s over it')
            continue
        other = recorded.get('build')
        if other != build:
            raise SystemExit(f'build {build} has marker stamp {stamp}, and so has build {other} ({taken}): bodies of '
                             f'the two could not be told apart. Change anything in the presets or the ranges '
                             f'(a new build hash), then run this again.')
        copies.append((i == 0, taken, recorded))
    root = next((taken for first, taken, _r in copies if first), None)
    for first, taken, recorded in copies:
        # This build again: its manifest stays as it was (write_manifest), so it must name these bodies.
        why = first_difference(recorded, templates)
        if not why:
            continue
        if not first and root is not None:
            if notes is not None:
                notes.append(f'{taken} and {root} are two manifests of stamp {stamp} that differ -- {why}. This run '
                             f'agrees with {root}, and the next deploy writes it over the one in Data.')
            continue
        agrees = [t for f, t, r in copies if not f and first_difference(r, templates) is None]
        raise SystemExit(f'{taken} records build {build} with other bodies than this run gives -- {why}. A '
                         f'manifest is never rewritten, and nothing was written: undo that change'
                         + (f' ({agrees[0]} names exactly these bodies: restore the checkout\'s copy from it)'
                            if agrees else '')
                         + ', or change anything in the presets or the ranges to get a new build, then run this again.')


def refuse_foreign_config(out_cfg, cfg_file):
    """Refuses, before anything is written, an output root whose config is not the one this run compiles
    (cfg_file; with no cfg_file, every key at its default). The package ships the config it was made from
    (owner, 2026-09-24; wave 4 L8), and one already there that this run does not read is somebody's -- perhaps
    edited by hand in the belief that it is read -- so it is neither shipped as if compiled nor replaced."""
    if not out_cfg.exists() or (cfg_file.exists() and out_cfg.resolve() == cfg_file.resolve()):
        return
    source = cfg_file if cfg_file.exists() else 'every key at its default'

    def parsed(path):
        try:
            return json.loads(path.read_text(encoding='utf-8-sig'))
        except (OSError, ValueError):
            return None
    # The same settings are the same config, however it is laid out: another line ending (git's autocrlf) or
    # indent is no hand edit (wave 5).
    have = parsed(out_cfg)
    want = parsed(cfg_file) if cfg_file.exists() else json.loads(json.dumps(rules.DEFAULT))
    if have is not None and have == want:
        return
    if isinstance(have, dict) and isinstance(want, dict):
        key = next((k for k in sorted(set(have) | set(want)) if have.get(k) != want.get(k)), None)
        why = f'{key!r} is {have.get(key)!r} there and {want.get(key)!r} in {source}'
    else:
        why = 'it is not a JSON object like the one compiled' if have is None or not isinstance(have, dict) else \
              f'{source} is not a JSON object'
    raise SystemExit(f'{out_cfg} is not the config this run compiles ({source}) -- {why} -- and the package ships '
                     f'the config it was made from. Pass --config {out_cfg} to compile that one, or move it away '
                     f'to ship the compiled one. Nothing was written.')


def write_manifest(folder, stamp, build, mode, base, pools, extra, picker, player):
    """F4SE/Plugins/Silhouette/manifests/<stamp>.json: what every marker of this
    generation means -- the exact preset name (the marker only keeps a sanitised
    one), its file, and the values written. Never deleted: an NPC rolled by this
    generation carries this stamp for the rest of that save.

    Never rewritten either (deploy-dev.ps1 refuses a committed one edited): this build generated
    again leaves its manifest as it was -- main() has checked before writing anything that it
    names the same bodies, whatever its descriptions (the bases, a preset's file) say today.
    -> whether the file was written."""
    path = folder / f'{stamp}.json'
    if path.exists():
        return False
    doc = {'format': MANIFEST_FORMAT, 'stamp': stamp, 'build': build, 'mode': mode,
           'bases': {g: describe(base[g]) for g in ('female', 'male')},
           'player': player, 'templates': manifest_templates(pools, extra, picker)}
    folder.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    return True


MANIFEST_FORMAT = 1


def generation(mode, pools, extra, picker, variety):
    """(stamp, build). The build is a hash of everything the files say -- mode, every
    template's values, and the ranges every template of a sex rolls (S-17, S-21); the
    stamp is its first 24 bits as an integer, exact in the float32 LooksMenu stores
    (every integer below 2^24 is)."""
    parts = {'format': MANIFEST_FORMAT, 'mode': mode, 't': {},
             'r': {g: [[m, lo, hi] for m, (lo, hi) in variety.get(g, [])] for g in ('female', 'male')}}
    for g in ('female', 'male'):
        for name, values, _p in pools[g]:
            parts['t'][name] = values
        for e in picker[g]:
            parts['t'].setdefault(e['marker'], e['values'])
    for name, (values, _p) in extra.items():
        parts['t'][name] = values
    text = json.dumps(parts, sort_keys=True, default=lambda v: round(v, 6))
    build = hashlib.sha1(text.encode('utf-8')).hexdigest()[:12]
    stamp = int(build[:6], 16) or 1
    return stamp, build


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--data', type=pathlib.Path, default=DEFAULT_DATA)
    ap.add_argument('--built', type=pathlib.Path, action='append', default=None,
                    help='a folder BodySlide built into (repeatable), searched before Data for '
                         'the body meshes -- to check a rebuild before it is deployed')
    ap.add_argument('--write', action='store_true',
                    help='write the BodyGen files, the MCM menu and the picker script')
    ap.add_argument('--out', type=pathlib.Path, default=None,
                    help='the mod folder to write into (default: this repo\'s data/)')
    ap.add_argument('--psc', type=pathlib.Path, default=None,
                    help='where the generated picker script source goes (default: this repo\'s '
                         'papyrus/Silhouette/Player.psc; required with --out)')
    ap.add_argument('--config', type=pathlib.Path, default=None,
                    help='the rules file (default: the output root\'s own F4SE/Plugins/Silhouette/'
                         'Silhouette_presetDistributionConfig.json, else this repo\'s data/ one; includes/ '
                         'beside it)')
    ap.add_argument('--no-partial', action='store_true',
                    help='random pool uses full fits only (owner default: include partial)')
    ap.add_argument('--compensate', action='store_true',
                    help='write templates relative to what the base has baked in, for a base '
                         'that is NOT zeroed (default: absolute, for a zeroed base - S-5)')
    ap.add_argument('--report', type=pathlib.Path, default=None, help='also write a JSON report')
    ap.add_argument('--characters', type=pathlib.Path, default=CHARACTERS_SIDECAR,
                    help='the characters\' sidecar (tools/pool/characters.py): each named NPC\'s own body (S-66)')
    ap.add_argument('--factions', type=pathlib.Path, default=FACTIONS_SIDECAR,
                    help='the factions\' sidecar (tools/pool/factions.py): each faction\'s own pool, as its rule (S-72)')
    ap.add_argument('--pool', type=pathlib.Path, default=POOL_SIDECAR,
                    help='the body pool\'s sidecar (tools/pool/generate.py): its presets are the random pool, '
                         'weighted by tier (S-65)')
    ap.add_argument('--release', action='store_true',
                    help='a public build: besides the package\'s own presets, only the stock ones CBBE and BodyTalk '
                         'ship (tools/release_presets.json, S-74) -- never the presets installed on this machine')
    args = ap.parse_args()
    reconfigure_output()

    # Everything that depends only on the arguments is refused here, before anything is measured or written.
    root = args.out or (ROOT / 'data')
    if args.write and args.out and args.psc is None:
        # The package goes elsewhere; the script source would still land in this checkout (wave 5 L11).
        raise SystemExit(f'--out {args.out} writes the package there, and the picker script source would still '
                         f'go to {ROOT / "papyrus/Silhouette/Player.psc"} in this checkout: pass --psc <file> too.')
    psc = args.psc or ROOT / 'papyrus/Silhouette/Player.psc'
    if args.config and not args.config.is_file():
        # A config named and not there is a typo, not a wish for the defaults.
        raise SystemExit(f'--config {args.config}: no such file')
    # The config a package is made from is its own (S-61): with --out, the output root's when it has one --
    # the repo's otherwise, copied into it below (wave 5 Q3).
    out_cfg = root / 'F4SE/Plugins/Silhouette' / rules.CONFIG_NAME
    cfg_file = args.config or (out_cfg if out_cfg.exists() else ROOT / 'data/F4SE/Plugins/Silhouette' / rules.CONFIG_NAME)
    if args.write:
        refuse_foreign_config(out_cfg, cfg_file)

    roots = built_roots(args)
    tris = {}
    for g, b in BODIES.items():
        tri = base_body.locate(roots, f'Meshes/Actors/Character/CharacterAssets/{b}.tri')
        if tri is None:
            raise SystemExit(f'no {b}.tri in {", ".join(map(str, roots))} -- build the body with '
                             f'"Build Morphs" ticked')
        print(f'{b}: {tri.parent}')
        tris[g] = base_body.read_tri(tri)
    morphs_of = {g: set().union(*t.values()) for g, t in tris.items()}
    pool = load_pool(args.pool)
    presets = read_all_presets([ROOT / 'data' / PRESETS, args.data / PRESETS], release=args.release)
    # The folder the manifests go to, and the game's: every marker either records stays its preset's (L4 F2,
    # wave 4 L6) -- a fresh --out, or a manifest deleted here, must not bring the old renames back. One list,
    # for every reader (wave 5).
    folders = manifest_folders(root, args.data)
    notes = []
    history = manifest_history(*folders, notes=notes)
    for n in notes:
        print(f'  note: {n}')
    assign_markers(presets, history)
    move_notes = []
    previous, previous_file = previous_markers(folders, [root / CATALOG, args.data / CATALOG], notes=move_notes)
    for n in move_notes:
        print(f'  note: {n}')
    moves = marker_moves(presets, previous, history)
    if moves:
        print(f'markers that move from the build {previous_file} records -- bodies of it are read by marker, so '
              f'these presets\' bodies will be read as another preset\'s or none:')
        for name, old, new, n_old, n_new in moves:
            print(f'  {name!r}: {old} -> {new} (manifests recording {old}: {n_old}, {new}: {n_new})')

    for p in presets:
        p.update(classify(p, morphs_of['female'], morphs_of['male']))
    family = {g: installed_family(presets, g) for g in BODIES}

    print(f'installed bodies: female {len(morphs_of["female"])} morphs, '
          f'male {len(morphs_of["male"])} morphs')
    print(f'installed family, read off the full-fit presets: '
          f'female={family["female"]!r} male={family["male"]!r}')
    print(f'presets read: {len(presets)}\n')

    # ---- what is baked into each base body
    base = {}
    print('base bodies, measured against BodySlide\'s reference meshes:')
    for g, body in BODIES.items():
        own = [p for p in presets if p['gender'] == g and p['kind'] != 'empty']
        base[g] = judge_base(base_body.measure(args.data, body, own, roots))
        print(f'  {g:6} {describe(base[g])}')
    # S-62: a slider fo4-anatomy's build owns is left out of every body, the ones a preset sets too.
    naming = sorted(p['name'] for p in presets if any(anatomy_owned(s) for s in p['sliders']))
    if naming:
        print(f'  presets that set {"/".join(ANATOMY_OWNED)}, left out of their bodies (S-62): {", ".join(naming)}')
    baked, unready = {}, []
    for g in BODIES:
        b = base[g]
        baked[g] = {}
        if b['status'] == 'zeroed':
            continue
        if args.compensate and b['baked'] is not None:
            baked[g] = b['baked']
            continue
        unready.append(g)
        zero = next((p['name'] for p in presets if p['gender'] == g and p['kind'] == 'body'
                     and b['set'] and not any(base_body.resolve(p, b['set']).values())), None)
        print(f'  !! {g}: {BODIES[g]} is NOT zeroed, and BodyGen adds every preset on top of it.')
        print(f'     Rebuild it in BodySlide BEFORE deploying: body "{b["set"]["name"] if b["set"] else "?"}", '
              f'preset "{zero or "a Zeroed Sliders preset"}", "Build Morphs" ticked -- and batch-build')
        print(f'     your outfits with the same preset. Then run this and verify_bodygen.py again.')
        if args.compensate:
            print(f'     (--compensate cannot help: the baked shape matches no preset on disk.)')
    print()

    # ---- the rules (OBody's config keys; tools/rules.py): cfg_file, chosen at the top
    report = []
    cfg = rules.load(cfg_file, [cfg_file.parent / 'includes',
                                args.data / 'F4SE/Plugins/Silhouette/includes'], report)
    not_random = {n.casefold() for n in cfg.get('blacklistedPresetsFromRandomDistribution', [])}
    bound, kept = merge_characters(cfg, args.characters)
    print(f'characters (S-66): {bound} NPC record(s) given their own body'
          + (f'; your own rules kept for {", ".join(kept)}' if kept else ''))
    pool_factions = set()
    faction_rules, faction_kept = merge_factions(cfg, args.factions, pool_factions)
    print(f'factions (S-72): {faction_rules} faction rule(s) drawing from their own pool'
          + (f'; your own rules kept for {", ".join(faction_kept)}' if faction_kept else ''))

    # ---- the pools
    pools = {'female': [], 'male': []}
    buckets = collections.defaultdict(list)
    zeroed, held_back, picker_only = [], [], []
    for p in presets:
        if p['kind'] != 'empty':
            p['band'] = band(p, family[p['gender']])
    for p in presets:
        if p['kind'] == 'empty':
            buckets['empty'].append(p)
            continue
        if catalog.is_refit(p['name']):
            buckets['refit'].append(p)       # OBody's clothed shapes: ORefit's, never a body (S-20)
            continue
        if p['kind'] == 'clothed-variant':
            buckets['clothed-variant'].append(p)
            continue
        g = p['gender']
        b = p['band']
        buckets[f'{g}-{b}'].append(p)
        if p['name'].casefold() in pool:
            if b != 'full' or g != pool[p['name'].casefold()]['sex']:
                raise SystemExit(f'pool preset {p["name"]!r} is a {b} fit of the installed {g} body, not a full '
                                 f'fit of the {pool[p["name"].casefold()]["sex"]} one -- regenerate the pool '
                                 f'(tools/pool/generate.py) against the body you have')
        elif b in ('full', 'partial'):
            picker_only.append(p)        # S-65: out of random, still in the picker
            continue
        if not (b == 'full' or (b == 'partial' and not args.no_partial)):
            continue
        if p['name'].casefold() in not_random:
            held_back.append(p)
            continue
        target = {k: v for k, v in target_values(p, base[g]).items() if k in morphs_of[g]}
        if is_zeroed(target):
            # A zeroed preset. OBody NG blacklists these from random distribution
            # by default ("Zeroed Sliders", "HIMBO Zero for OBody"); here that is
            # decided by the values it would write, not the name, so "CBBE Zeroed
            # Sliders" and "BT - Zero" are caught too -- and so is a preset whose only
            # values are a state or the shaft, which are never written.
            zeroed.append(p)
            continue
        name = template_name(p)
        pools[g].append((name, morph_values(name, target, baked[g], morphs_of[g], p['name']), p))

    order = ['female-full', 'female-partial', 'female-other-family', 'female-none',
             'male-full', 'male-partial', 'male-other-family', 'male-none',
             'clothed-variant', 'refit', 'empty']
    for key in order:
        rows = buckets.get(key, [])
        if not rows:
            continue
        print(f'{key:20} {len(rows):3}')
        for p in rows[:5]:
            fams = ','.join(p.get('families', [])[:2]) or '-'
            print(f'    {100*p["fit"]:5.1f}%  {p["name"][:46]:46}  [{fams}]')
        if len(rows) > 5:
            print(f'    ... and {len(rows) - 5} more')

    if zeroed:
        print('\nleft out of random distribution as zeroed presets (OBody does the same): '
              + ', '.join(p['name'] for p in zeroed))
    small_only = sum(p['small_only'] for p in presets)
    if small_only:
        print(f'sliders with only a "small" value (a FO4 build uses the default): {small_only}')

    if held_back:
        print('held back from random distribution by the config: ' + ', '.join(p['name'] for p in held_back))
    if picker_only:
        print(f'installed presets in the picker only, never random (S-65): {len(picker_only)}')
    have = {p['name'].casefold() for p in presets}
    missing = sorted(e['name'] for k, e in pool.items() if k not in have)
    if missing:
        raise SystemExit(f'the pool names presets no SliderPresets folder holds: {", ".join(missing[:6])} -- '
                         f'data/{PRESETS.as_posix()}/Silhouette Pool.xml and {POOL_SIDECAR.name} are of two runs')
    missing = sorted(n for n in faction_preset_names(args.factions) if n.casefold() not in have)
    if missing:
        raise SystemExit(f'the factions\' pools name presets no SliderPresets folder holds: {", ".join(missing[:6])} -- '
                         f'data/{PRESETS.as_posix()}/Silhouette Factions.xml and {FACTIONS_SIDECAR.name} are of two runs')

    for g in BODIES:
        names = random_line_names(pools[g], pool)
        tiers = collections.Counter(pool[p['name'].casefold()]['tier'] for n, _v, p in pools[g]
                                    for _ in range(pool[p['name'].casefold()]['weight']))
        print(f'\n{g} random pool: {len(pools[g])} template(s) in {len(names)} entries -- '
              + ', '.join(f'{t} {100 * c / len(names):.1f}%' for t, c in sorted(tiers.items())) if names else
              f'\n{g} random pool: empty')

    # ---- rule lines, and templates for presets only the rules name
    by_name = {p['name'].casefold(): p for p in presets if p['kind'] != 'empty'}
    extra = {}                                       # template name -> (values, preset)

    def resolve_presets(names, gender):
        out = {'female': [], 'male': []}
        for n in names if isinstance(names, list) else [names]:
            p = by_name.get(str(n).casefold())
            if p is None:
                report.append(f'rules: no preset named {n!r}, skipped')
                continue
            if catalog.is_refit(p['name']):
                report.append(f'rules: {p["name"]!r} is a refit preset, a clothed shape for ORefit and never '
                              f'a body - skipped')
                continue
            g = p['gender']
            if gender and g != gender:
                report.append(f'rules: {p["name"]!r} is a {g} preset, not usable for {gender} NPCs')
                continue
            if p['band'] not in ('full', 'partial'):
                report.append(f'rules: {p["name"]!r} does not fit the installed {g} body '
                              f'({p["band"]}), skipped')
                continue
            name = template_name(p)
            if not any(name == n2 for n2, _l, _p in pools[g]) and name not in extra:
                target = {k: v for k, v in target_values(p, base[g]).items() if k in morphs_of[g]}
                p['zeroed'] = is_zeroed(target)
                extra[name] = (morph_values(name, target, baked[g], morphs_of[g], p['name']), p)
            out[g].append(name)
        return out

    rule_lines, _needed = rules.compile_lines(cfg, resolve_presets, args.data, report)
    # The rules only the plugin applies (S-23) name presets too, and every one of them needs a
    # template and a catalog entry -- even one held back from random distribution and the menu.
    for _name, wanted in cfg.get('npc', {}).items():
        resolve_presets(wanted, None)
    for key, g in (('factionFemale', 'female'), ('factionMale', 'male')):
        for _edid, wanted in cfg.get(key, {}).items():
            resolve_presets(wanted, g)
    distribute = rules.distribute_races(cfg)
    print(f'\nrules: {len(rule_lines)} line(s), {len(extra)} extra template(s); random distribution '
          f'races: {", ".join(distribute)}')
    for r in report:
        print(f'  {r}')

    # ---- the player: never randomised; the most average body unless they pick
    # one in MCM (owner, 2026-09-23). The picker offers every preset that fits,
    # zeroed ones included -- OBody shows blacklisted presets in its menu too.
    picker, average, default_index = {}, {}, {}
    for g in BODIES:
        entries = []
        for p in presets:
            if p['gender'] != g or p['kind'] != 'body' or catalog.is_refit(p['name']):
                continue
            if not (p['band'] == 'full' or (p['band'] == 'partial' and not args.no_partial)):
                continue
            if p['name'].casefold() in not_random and not cfg.get('blacklistedPresetsShowInOBodyMenu', True):
                continue
            target = {k: v for k, v in target_values(p, base[g]).items() if k in morphs_of[g]}
            name = template_name(p)
            entries.append({'display': p['name'], 'marker': name, 'band': p['band'], 'target': target,
                            'families': p['families'],
                            'values': morph_values(name, target, baked[g], morphs_of[g], p['name'])})
        entries.sort(key=lambda e: e['display'].casefold())
        picker[g] = entries
        shape = max(tris[g], key=lambda s: len(tris[g][s]))
        # The average is of the BODIES the pool gives (body_values).
        # Weighted as the random line weights it (S-65): the mean of the bodies NPCs are actually given.
        pool_values = [(p['name'], body_values(target_values(p, base[g]), morphs_of[g])) for _n, _l, p in pools[g]
                       for _ in range(pool[p['name'].casefold()]['weight'])]
        full = [(n, v) for n, v in pool_values
                if next(p for p in presets if p['name'] == n)['band'] == 'full']
        best = base_body.most_average(pool_values, tris[g][shape], full)
        if best:
            average[g] = best[0]
            default_index[g] = next(i for i, e in enumerate(entries) if e['display'] == best[0])
            print(f'{g} player default, the most average full fit: {best[0]!r} '
                  f'({best[1]:.3f} rms from the pool\'s mean body)')
        else:
            print(f'{g}: no full-fit preset to be the player default -- the player keeps the base body')

    ranges = variety_ranges()
    mode = 'compensated' if any(baked[g] for g in BODIES) else 'absolute'
    # What BodyGen rolls per NPC (S-17, S-21): only morphs the body carries get a range, since a range on
    # a morph the .tri lacks moves nothing. Written like every value -- relative to what the base has
    # baked in when compensating. Part of what the files say, so part of the build.
    variety = {g: sorted((mm, (float(fmt(lo - baked[g].get(mm, 0.0))), float(fmt(hi - baked[g].get(mm, 0.0)))))
                         for mm, (lo, hi, _grp) in ranges[g].items() if mm in morphs_of[g])
               for g in BODIES}

    # S-45: the player and the character-creation dummies are never randomised. They name a template of
    # their own -- the default preset's values and marker, none of the ranges -- so a new character is
    # the most average preset exactly, and "Back to the default" gives the same body.
    player_template = {}
    for g in BODIES:
        if g in average:
            e = picker[g][default_index[g]]
            player_template[g] = (PLAYER_TEMPLATE[g], e['values'], e['marker'])
    chosen = {g: player_template[g][0] if g in player_template else PLAYER_GUARD for g in BODIES}

    # LooksMenu looks template names up case-insensitively (F4EEFixedString == is _stricmp) and the
    # plugin reads markers the same way: two names that differ only in case are one. Every template name
    # and every marker must stand for exactly one thing, the names Silhouette reserves included.
    owners = collections.defaultdict(set)
    for n, what in ((PLAYER_GUARD, 'the template that sets nothing'),
                    (catalog.BLACKLIST_MARKER, 'the blacklist marker'), (catalog.REFIT_MARKER, 'the refit marker')):
        owners[catalog.ifold(n)].add(what)
    for g in BODIES:
        if g in player_template:
            owners[catalog.ifold(player_template[g][0])].add(f'the {g} player template')
        for n, _v, p in pools[g]:
            owners[catalog.ifold(n)].add(f'{g} preset {p["name"]!r}')
        for e in picker[g]:
            owners[catalog.ifold(e['marker'])].add(f'{g} preset {e["display"]!r}')
    for n, (_v, p) in extra.items():
        owners[catalog.ifold(n)].add(f'{p["gender"]} preset {p["name"]!r}')
    clash = sorted(f'{k}: {" / ".join(sorted(v))}' for k, v in owners.items() if len(v) > 1)
    if clash:
        raise SystemExit('names LooksMenu and the plugin would read as one:\n  ' + '\n  '.join(clash))

    for g, label in (('female', 'Female'), ('male', 'Male')):
        for race in distribute:
            size = len(f'All|{label}|{race}='.encode('utf-8')) + len('|'.join(random_line_names(pools[g], pool)).encode('utf-8'))
            if size > LINE_LIMIT:
                raise SystemExit(f'the {g} random line for {race} is {size} bytes; the engine splits a line past '
                                 f'{LINE_LIMIT} and the rest would be read as another line')

    stamp, build = generation(mode, pools, extra, picker, variety)
    print(f'\nbuild {build}, marker stamp {stamp} ({mode})')

    if not args.write:
        print('\n(measure only - pass --write to produce the BodyGen files)')
    else:
        # A stamp another build holds, or this build with other bodies, in the output root or the game's
        # Data (wave 4 L6): refused before anything is written. The same folder list the history read.
        clash_notes = []
        refuse_stamp_clash(stamp, build, manifest_templates(pools, extra, picker), *folders, notes=clash_notes)
        for n in clash_notes:
            print(f'  note: {n}')
        out = root / 'F4SE/Plugins/F4EE/BodyGen/Loose'
        tfile = out / 'Silhouette_templates.ini'
        mfile = out / 'Silhouette_morphs.ini'

        # Order IS priority: LooksMenu lets a later line overwrite an earlier one.
        # The broad random pool goes first; rules and blacklists go below it, in
        # the reverse of OBody's priority (tools/rules.py).
        m = ['# Silhouette - generated. Later lines override earlier ones for the same NPC.',
             '# Always name a race: "All|Female" alone matches only NPCs that have none.',
             '#']
        for g, label in (('female', 'Female'), ('male', 'Male')):
            for race in distribute:
                # S-86: a race whose body is one sex's gets that sex's pool in BOTH tables -- LooksMenu picks the
                # table by the sex flag, and a Servitron is flagged male.
                body = rules.OPTIONAL_RACES.get(race.lower(), {}).get('body', g)
                if pools[body]:
                    m.append(f'All|{label}|{race}=' + '|'.join(random_line_names(pools[body], pool)))
        if rule_lines:
            m += ['#', '# Rules from Silhouette_presetDistributionConfig.json and includes,',
                  '# lowest priority first: race, plugin, blacklists, FormID, FormID blacklists.']
            m += rule_lines
        m += ['#',
              '# The player (Fallout4.esm 0x7) is NEVER randomised (S-45): without these lines the All',
              '# lines above would include them. A character with no body sliders gets the most average',
              '# of your presets, with none of the ranges; MCM > Silhouette picks another. LooksMenu',
              '# itself skips a character that already has body sliders.',
              f'Fallout4.esm|7|Female={chosen["female"]}',
              f'Fallout4.esm|7|Male={chosen["male"]}',
              '#',
              '# ...and neither are the two character-creation dummies. A new game shows',
              '# MQ101PlayerSpouseMale (A7D34) and MQ101PlayerSpouseFemale (A7D35) at the',
              '# mirror; on confirm, LooksMenu CLONES the chosen one\'s body morphs onto the',
              '# player (CloneBodyMorphs -> CloneMorphs), past the player line above. Both',
              '# are HumanRace with no template, so the All lines would roll them. They get',
              '# the player\'s default, and the spouse in the intro wears it too.',
              f'Fallout4.esm|0A7D35|Female={chosen["female"]}',
              f'Fallout4.esm|0A7D34|Male={chosen["male"]}']

        cat_report = []
        cat = catalog.build(
            stamp=stamp, build_id=build, mode=mode,
            presets=catalog_presets(pools, extra, picker),
            player={g: average[g] for g in BODIES if g in average},
            states={g: [mm for mm in STATE_MORPHS if mm in morphs_of[g]] for g in BODIES},
            never_in_body={g: list(NEVER_IN_BODY) for g in BODIES},
            variety={g: [(mm, lo, hi, ranges[g][mm][2]) for mm, (lo, hi) in variety[g]] for g in BODIES},
            cfg=cfg, data=args.data,
            refit_presets=refit_sets(buckets.get('refit', []), base, baked, morphs_of),
            body_morphs=morphs_of, baked=baked, report=cat_report, pool_factions=pool_factions,
            race_pools=[{'race': race, 'sex': rules.OPTIONAL_RACES[race.lower()]['body'],
                         'presets': [p['name'] for _n, _v, p in pools[rules.OPTIONAL_RACES[race.lower()]['body']]
                                     for _ in range(pool[p['name'].casefold()]['weight'] if pool else 1)]}
                        for race in distribute if race.lower() in rules.OPTIONAL_RACES],
            slider_sets={g: base[g]['set'] for g in BODIES if base.get(g) and base[g].get('set')})
        rules_id = catalog.rules_hash(cat, m)
        cat['rulesHash'] = rules_id
        catalog.check(cat)
        # The same words on both files, which the plugin and the build scripts read (S-19).
        header = f'Build {build}, marker stamp {stamp} ({mode}), rules {rules_id}.'
        m.insert(1, f'# {header}')

        # NO EMPTY LINES, and CRLF. LooksMenu reads each line with the engine's own
        # BSResourceTextFile::ReadLine, which returns the number of bytes before the
        # '\n'; its loop stops on 0. With LF, the first empty line ENDS THE FILE.
        # (With CRLF an "empty" line is '\r' and survives, which is the only reason
        # it ever worked.) Spacing is done with '#'.
        t = ['# Silhouette - generated by tools/silhouette_gen.py. Do not edit by hand:',
             '# regenerate after adding presets or rebuilding a body in BodySlide.',
             f'# {len(pools["female"])} female, {len(pools["male"])} male templates. {header}',
             '#',
             '# Measured base bodies:']
        for g in BODIES:
            t.append(f'#   {g}: {describe(base[g])}')
        if mode == 'compensated':
            t += ['#',
                  '# --compensate: values are RELATIVE to that baked shape (target minus',
                  '# baked), so every NPC ends at exactly its preset. Rebuild a body with a',
                  '# different preset and these values are wrong until this tool is run again.']
        else:
            t += ['#',
                  '# Values are ABSOLUTE: written for bodies built with zeroed sliders.']
        for g in unready:
            t.append(f'# !! {BODIES[g]} is NOT zeroed yet: rebuild it zeroed before deploying.')
        t += ['#',
              '# The last morph of every template is a marker named after the template. No',
              '# body has it, so it moves nothing; it makes the roll permanent (an NPC with',
              '# no stored morphs is re-rolled on every load). Its VALUE is this build\'s',
              f'# stamp: F4SE/Plugins/Silhouette/manifests/{stamp}.json says what it means.',
              '#',
              f'{PLAYER_GUARD}={PLAYER_GUARD}@0',
              '#']
        for g, who in (('female', 'woman'), ('male', 'man')):
            if variety[g]:
                t.append(f'# Variety rolled per {who} (S-17, S-21): '
                         + ', '.join(f'{mm} {fmt(lo)}..{fmt(hi)}' for mm, (lo, hi) in variety[g]))
            missing = sorted(mm for mm in ranges[g] if mm not in morphs_of[g])
            if missing:
                t.append(f'#   not on this {g} body, so never rolled: {", ".join(missing)}')
            if variety[g] or missing:
                t.append('#')
        for g in BODIES:
            t.append(f'# --- {g} ---')
            for name, values, p in pools[g]:
                t.append(f'# {p["name"]}  {100*p["fit"]:.0f}% fit  families={p["families"]}')
                t.append(f'{name}={", ".join(template_text(name, values, stamp, variety[g]))}')
            t.append('#')
        if extra:
            t.append('# --- presets only the rules hand out ---')
            for name, (values, p) in sorted(extra.items()):
                t.append(f'# {p["name"]}  {p["gender"]}  {100*p["fit"]:.0f}% fit')
                t.append(f'{name}={", ".join(template_text(name, values, stamp, variety.get(p["gender"], [])))}')
            t.append('#')
        if player_template:
            t.append('# --- the player and the character-creation dummies: the default, never rolled (S-45) ---')
            for g in BODIES:
                if g in player_template:
                    name, values, marker = player_template[g]
                    t.append(f'# {average[g]}  {g}')
                    t.append(f'{name}={", ".join(template_text(marker, values, stamp))}')
        assert all(line.strip() for line in t), 'an empty line would end the file for LooksMenu'
        assert all(line.strip() for line in m), 'an empty line would end the file for LooksMenu'

        # Both encoded before either is written: a refusal leaves no half-written pair.
        tbytes, mbytes = ini_bytes(tfile.name, t), ini_bytes(mfile.name, m)
        out.mkdir(parents=True, exist_ok=True)
        tfile.write_bytes(tbytes)
        mfile.write_bytes(mbytes)
        print(f'\nwrote {tfile}\nwrote {mfile}')

        # The config ships beside what it produced (owner, 2026-09-24), and it is the one this run compiled
        # (wave 4 L8): an output root without one gets the config this run read -- or, with none, every key at
        # its default. One the root already holds is that same config, byte for byte (refused above if not).
        if not out_cfg.exists():
            if cfg_file.exists():
                out_cfg.parent.mkdir(parents=True, exist_ok=True)
                out_cfg.write_bytes(cfg_file.read_bytes())
                print(f'wrote {out_cfg}: the config this run compiled ({cfg_file})')
                if (cfg_file.parent / 'includes').is_dir():
                    print(f'  ({cfg_file.parent / "includes"} was compiled too, and is not copied: ship it beside the '
                          f'config yourself if the package should carry it)')
            else:
                rules.write_default(out_cfg)
                print(f'wrote {out_cfg} (every key, at its default)')
        write_mcm(root / 'MCM/Config' / MOD, picker, default_index, average, build)
        write_papyrus(psc, picker, default_index, stamp, build)
        wrote_manifest = write_manifest(root / MANIFESTS, stamp, build, mode, base, pools, extra, picker,
                                        {g: {'template': chosen[g], 'preset': average.get(g, '')} for g in BODIES})
        catalog.write(root / 'F4SE/Plugins/Silhouette/catalog.json', cat)
        for line in cat_report:
            print(f'  {line}')
        print(f'wrote {root / "F4SE/Plugins/Silhouette"}\\catalog.json ({len(cat["presets"])} presets, '
              f'{len(cat["rules"]["npcName"])} name rule(s), {len(cat["rules"]["faction"])} faction rule(s), '
              f'{len(cat["orefit"]["sets"])} refit set(s), rules {rules_id})')
        print(f'wrote {root / "MCM/Config" / MOD}\\config.json + settings.ini')
        print(f'wrote {root / MANIFESTS}\\{stamp}.json' if wrote_manifest else
              f'kept {root / MANIFESTS}\\{stamp}.json as it was: it already records this build, body for body')
        print(f'wrote {psc}  (compile: scripts/build-papyrus.ps1)')

    if args.report:
        args.report.write_text(json.dumps({
            'base': {g: {k: (v['name'] if k == 'set' and v else v)
                         for k, v in base[g].items() if k != 'baked'} for g in BODIES},
            'presets': [{k: p.get(k) for k in
                         ('name', 'set', 'families', 'file', 'gender', 'fit', 'kind', 'band')}
                        for p in presets],
        }, indent=2), encoding='utf-8')
        print(f'wrote {args.report}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
