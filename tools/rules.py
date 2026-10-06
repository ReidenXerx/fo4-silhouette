"""OBody NG's distribution rules, compiled to LooksMenu BodyGen lines.

The config is OBody's own shape, key for key, so an OBody user can read it and an
OBody include translates by renaming the file:

    data/F4SE/Plugins/Silhouette/Silhouette_presetDistributionConfig.json
    F4SE/Plugins/Silhouette/includes/*.json   (other mods' rules; alphabetical,
                                               a later file overrides an earlier)

OBody's order, from OBody::GenerateActorBody (src/Body/Body.cpp), highest first:

    1. blacklistedNpcs / blacklistedNpcsFormID          -> the NPC gets nothing
    2. npc / npcFormID                                  -> these presets
    3. blacklistedNpcsPlugin* / blacklistedRaces*       -> nothing
    4. faction*                                         -> these presets
    5. npcPlugin*                                       -> these presets
    6. race*                                            -> these presets
    7. random

BodyGen has no priorities, only "a later line overwrites an earlier one for the
same NPC" -- so the lines are written in REVERSE: random pool first, then 6, 5,
4, 3, 2, 1, and the player's own lines after everything.

What BodyGen can express, and so what is compiled today: npcFormID, npcPlugin*,
race*, and every FormID/plugin/race blacklist. `npc` and `blacklistedNpcs` match
a DISPLAY NAME and `faction*` a faction, neither of which a morphs.ini line can
name; base-game names are localised into string tables inside Fallout4 -
Interface.ba2. Those keys go into catalog.json instead (tools/catalog.py), and Silhouette.dll
applies them at run time (decision S-23).

A blacklist is a line whose only template sets nothing. That is safe here only
because it is the ONLY option on its line: an NPC with no stored morphs is
evaluated again on every load, and evaluates to nothing again, every time.

Plugin and race lines must name a race: LooksMenu matches `npc->race == nullptr
|| npc->race == filter`, so a line with no race reaches only race-less NPCs.
"""
import json
import pathlib
import re

KEYS_PRESET_MAP = ('npcFormID', 'npc', 'factionFemale', 'factionMale',
                   'npcPluginFemale', 'npcPluginMale', 'raceFemale', 'raceMale')
KEYS_LISTS = ('blacklistedNpcs', 'blacklistedNpcsPluginFemale', 'blacklistedNpcsPluginMale',
              'blacklistedRacesFemale', 'blacklistedRacesMale',
              'blacklistedPresetsFromRandomDistribution', 'distributeRaces')
KEYS_PENDING = ('npc', 'blacklistedNpcs', 'factionFemale', 'factionMale')
KEYS_PHASE2 = ('blacklistedOutfitsFromORefitFormID', 'blacklistedOutfitsFromORefit',
               'blacklistedOutfitsFromORefitPlugin', 'outfitsForceRefitFormID', 'outfitsForceRefit',
               'refitOutfitPresetsFemale', 'refitOutfitPresetsMale')
# OBody only lets includes carry these (JSONParser: obody_includes).
INCLUDE_KEYS = ('npc', 'npcFormID', 'npcPluginFemale', 'npcPluginMale')

CONFIG_NAME = 'Silhouette_presetDistributionConfig.json'
DEFAULT = {
    'npcFormID': {}, 'npc': {}, 'factionFemale': {}, 'factionMale': {},
    'npcPluginFemale': {}, 'npcPluginMale': {}, 'raceFemale': {}, 'raceMale': {},
    'blacklistedNpcs': [], 'blacklistedNpcsFormID': {},
    'blacklistedNpcsPluginFemale': [], 'blacklistedNpcsPluginMale': [],
    'blacklistedRacesFemale': [], 'blacklistedRacesMale': [],
    'blacklistedPresetsFromRandomDistribution': [],
    'blacklistedPresetsShowInOBodyMenu': True,
    # ORefit (S-20), OBody's keys: outfits that never refit, outfits that always do, and
    # outfits that bring their own refit preset (a BodySlide preset named "<X>-Refit").
    'blacklistedOutfitsFromORefitFormID': {}, 'blacklistedOutfitsFromORefit': [],
    'blacklistedOutfitsFromORefitPlugin': [],
    'outfitsForceRefitFormID': {}, 'outfitsForceRefit': [],
    'refitOutfitPresetsFemale': {}, 'refitOutfitPresetsMale': {},
    # Silhouette's additions. Which races take part in random distribution: OBody
    # distributes to every NPC race; in Fallout 4 only races that wear the human body
    # should, and the base game has one. Never empty (owner, 2026-09-24): the shipped
    # config lists HumanRace, and a list emptied by hand is refused, not read as the default.
    # ServitronRace (S-85): a mod's race, in OPTIONAL_RACES below.
    'distributeRaces': ['HumanRace', 'ServitronRace'],
    # What counts as heavy clothes, which flatten the nipples under ORefit (S-48): an item
    # whose NAME holds one of these words or phrases, as a whole word, in any case --
    # plainly armour, a plate over the chest, or a jacket or coat. Anything the name does
    # not say is light: nipples flattened where they should show are worse than nipples
    # showing where they should not (owner, 2026-09-23). Single items named heavy or light
    # by form id ({plugin: [ids]}) or by exact name win over the words. The plates and the
    # longer coats came from measuring 1,426 dressing items (wave 3, owner 2026-09-24).
    'heavyWords': ['armor', 'armour', 'armored', 'armoured', 'cuirass', 'kevlar', 'carapace', 'torso',
                   'chest piece', 'chestpiece', 'chestplate', 'breastplate', 'plate',
                   'jacket', 'coat', 'overcoat', 'greatcoat', 'battlecoat', 'dreadcoat', 'longcoat',
                   'raincoat', 'trenchcoat', 'duster', 'parka'],
    'heavyOutfitsFormID': {}, 'heavyOutfits': [],
    'lightOutfitsFormID': {}, 'lightOutfits': [],
}

# The player (Fallout4.esm 0x7) and the two character-creation dummies (0xA7D34, 0xA7D35): their
# own lines come last in Silhouette_morphs.ini, so a rule naming them never reaches them (S-45).
PLAYER_FORMS = {'000007', '0A7D34', '0A7D35'}


def is_player_form(plugin, fid):
    """The player or a character-creation dummy, as form_key() writes the id: no rule reaches them,
    so neither the BodyGen lines nor the catalog may claim one does (S-45)."""
    return str(plugin).lower() == 'fallout4.esm' and fid in PLAYER_FORMS


# S-85 (owner, 2026-10-06, a player's request): races another mod adds, whose NPCs wear a body Silhouette can
# shape, listed by default. Servitron (Nexus 32801, Servitron.esm|000F99 ServitronRace): an Automatron robot
# built from parts BodySlide builds on CBBE's sliders (47 sets, Breasts, Butt, Hips, Waist...), so BodyGen's
# morphs move them. Such a race may be missing from the load order: its lines then match nobody, which is
# right for a player without the mod, so the generator and the verifier say so instead of refusing. Editor id,
# folded -> the plugin that defines it.
OPTIONAL_RACES = {'servitronrace': 'Servitron.esm'}


EMPTY_RACES = ('distributeRaces is empty: Silhouette would shape nobody -- no random bodies, no rules by name or '
               'faction, no refits. The default is ["HumanRace", "ServitronRace"]: put it back, or list the races (by editor id) '
               'that wear the human body')


def distribute_races(cfg):
    """The races random distribution gives a body to. Never empty: an emptied list is refused, not
    read as the default (owner, 2026-09-24); a config without the key has DEFAULT's."""
    races = cfg.get('distributeRaces', DEFAULT['distributeRaces'])
    if not races:
        raise SystemExit(f'config: {EMPTY_RACES}')
    return list(races)

# The shape every key must have. A value of the wrong shape is refused with its key named: the
# plugin refuses a catalog that does not parse, and a race list written as a string would give
# nobody a body -- both only discovered in the game otherwise.
PRESET_MAPS = ('npc', 'factionFemale', 'factionMale', 'npcPluginFemale', 'npcPluginMale', 'raceFemale', 'raceMale')
STRING_LISTS = ('blacklistedNpcs', 'blacklistedNpcsPluginFemale', 'blacklistedNpcsPluginMale',
                'blacklistedRacesFemale', 'blacklistedRacesMale', 'blacklistedPresetsFromRandomDistribution',
                'distributeRaces', 'blacklistedOutfitsFromORefit', 'blacklistedOutfitsFromORefitPlugin',
                'outfitsForceRefit', 'heavyOutfits', 'lightOutfits', 'heavyWords')
FORM_LISTS = ('blacklistedNpcsFormID', 'blacklistedOutfitsFromORefitFormID', 'outfitsForceRefitFormID',
              'heavyOutfitsFormID', 'lightOutfitsFormID')
NAME_MAPS = ('refitOutfitPresetsFemale', 'refitOutfitPresetsMale')


def has_word(text):
    """A letter or digit in it, as the plugin splits a name into words: ASCII letters and digits, and
    every byte of a character beyond ASCII (src/Catalog.cpp Words)."""
    return any((c.isascii() and c.isalnum()) or not c.isascii() for c in text)


def encodable(text):
    """Written as UTF-8 (JSON) and read back by the plugin: a lone surrogate would be refused whole."""
    try:
        text.encode('utf-8')
        return True
    except UnicodeEncodeError:
        return False


def validate(cfg, source):
    """Refuses, naming the key, any value that is not the shape OBody's config gives it -- and any
    empty name or plugin, or text the plugin could not read back. The shape is checked first:
    "distributeRaces": "" is a string where a list belongs, which says more than "an empty name"."""
    def bad(key, what):
        raise SystemExit(f'{source}: {key} must be {what}')

    def names(v):
        return isinstance(v, str) or (isinstance(v, list) and all(isinstance(x, str) for x in v))

    def texts(v):
        if isinstance(v, str):
            yield v
        elif isinstance(v, list):
            for x in v:
                yield from texts(x)
        elif isinstance(v, dict):
            for k, x in v.items():
                yield k
                yield from texts(x)

    for key, v in cfg.items():
        if key in PRESET_MAPS:
            if not isinstance(v, dict) or not all(isinstance(k, str) and names(x) for k, x in v.items()):
                bad(key, 'an object of "name": ["preset", ...]')
        elif key == 'npcFormID':
            if not isinstance(v, dict) or not all(
                    isinstance(plugin, str) and isinstance(forms, dict)
                    and all(isinstance(k, str) and names(x) for k, x in forms.items())
                    for plugin, forms in v.items()):
                bad(key, 'an object of "Plugin.esp": {"formid": ["preset", ...]}')
        elif key in STRING_LISTS:
            if not isinstance(v, list) or not all(isinstance(x, str) for x in v):
                bad(key, 'a list of strings')
        elif key in FORM_LISTS:
            if not isinstance(v, dict) or not all(
                    isinstance(plugin, str) and isinstance(ids, list) and all(isinstance(i, str) for i in ids)
                    for plugin, ids in v.items()):
                bad(key, 'an object of "Plugin.esp": ["formid", ...], each form id a hex string')
        elif key in NAME_MAPS:
            if not isinstance(v, dict) or not all(isinstance(k, str) and isinstance(x, str) for k, x in v.items()):
                bad(key, 'an object of "outfit name": "refit preset name"')
        elif key == 'blacklistedPresetsShowInOBodyMenu':
            if not isinstance(v, bool):
                bad(key, 'true or false')
        for t in texts(v):
            if not isinstance(t, str):
                continue
            if not encodable(t):
                bad(key, f'text the plugin can read (found {t!r}, which is not valid Unicode)')
            if not t.strip() and key not in ('blacklistedPresetsShowInOBodyMenu',):
                bad(key, 'free of empty names, plugins and form ids')
        if key == 'heavyWords':
            for w in v:
                if not has_word(w):
                    bad(key, f'words or phrases ({w!r} holds no letter or digit)')
        if key == 'distributeRaces' and not v:
            raise SystemExit(f'{source}: {EMPTY_RACES}')


def load(config_path, include_dirs, report):
    """The merged config: defaults, then the main file, then includes."""
    cfg = json.loads(json.dumps(DEFAULT))
    if config_path.exists():
        try:
            main = json.loads(config_path.read_text(encoding='utf-8-sig'))
        except json.JSONDecodeError as exc:
            raise SystemExit(f'{config_path}: not valid JSON ({exc})')
        if not isinstance(main, dict):
            raise SystemExit(f'{config_path}: the config must be a JSON object')
        known = {k: v for k, v in main.items() if k in cfg or k in KEYS_PHASE2}
        for k in main:
            if k not in known:
                report.append(f'config: unknown key {k!r} ignored')
        validate(known, config_path.name)
        cfg.update(known)
    files = {}
    for d in include_dirs:
        if d and d.is_dir():
            for f in d.glob('*.json'):
                files[f.name.lower()] = f            # a later folder's copy wins
    for name in sorted(files):
        f = files[name]
        try:
            inc = json.loads(f.read_text(encoding='utf-8-sig'))
        except json.JSONDecodeError as exc:
            report.append(f'include {f.name}: not valid JSON, skipped ({exc})')
            continue
        if not isinstance(inc, dict):
            report.append(f'include {f.name}: not a JSON object, skipped')
            continue
        for k in list(inc):
            if k not in INCLUDE_KEYS:
                report.append(f'include {f.name}: key {k!r} is not allowed in an include (OBody allows '
                              f'{", ".join(INCLUDE_KEYS)}), ignored')
                del inc[k]
        validate(inc, f'include {f.name}')
        for k, v in inc.items():
            if k == 'npcFormID':
                for plugin, forms in v.items():
                    cfg[k].setdefault(plugin, {}).update(forms)
            else:
                cfg[k].update(v)
    for k in KEYS_PENDING:
        if cfg.get(k):
            report.append(f'config: {k} has {len(cfg[k])} entr{"y" if len(cfg[k]) == 1 else "ies"} -- '
                          f'matched by name/faction at run time by Silhouette.dll (catalog.json)')
    for k in KEYS_PHASE2:
        if cfg.get(k):
            report.append(f'config: {k} is ORefit, applied at run time by Silhouette.dll (catalog.json)')
    return cfg


def is_light(data, plugin):
    """.esl, or the TES4 header's light flag (0x200) on an .esp/.esm."""
    if plugin.lower().endswith('.esl'):
        return True
    p = data / plugin
    try:
        with open(p, 'rb') as f:
            head = f.read(12)
    except OSError:
        return False
    return head[:4] == b'TES4' and bool(int.from_bytes(head[8:12], 'little') & 0x200)


def form_key(text, light=False):
    """OBody writes FormIDs as hex, with or without 0x, often with the load-order
    byte(s); LooksMenu wants the id WITHOUT them (it adds the plugin's own). ->
    6 hex digits, or None if the key is not hex or cannot be right.

    A light plugin's id is its last THREE digits: LooksMenu builds
    0xFE000000 | light index << 12 | (id & 0xFFFFFF), so anything above 0xFFF
    lands in another plugin's range (OBody's DiscardFormDigits keeps 3 digits for
    light mods too). An xEdit-style 'FE00A801' is local id 801.

    Strict: 1 to 8 hex digits after an optional 0x, and nothing else but the spaces around them.
    Python's int() would also take '+12' and '1_2' as 0x12, which is no form id as OBody writes one."""
    t = str(text).strip().lower().removeprefix('0x')
    if not re.fullmatch(r'[0-9a-f]{1,8}', t):
        return None
    v = int(t, 16)
    v &= 0xFFF if light else 0xFFFFFF
    return f'{v:06X}' if v else None


def compile_lines(cfg, resolve_presets, data, report):
    """-> (lines, needed_templates): BodyGen lines in the order they must be
    written (after the random pool), and the template names they reference.

    resolve_presets(names, gender or None) -> {'female': [template], 'male': [...]}
    of templates for the preset names that exist and fit that gender's body,
    reporting the ones that do not.
    """
    lines, needed = [], set()
    distribute = distribute_races(cfg)

    def plugin_ok(plugin):
        if plugin.lower().startswith('all'):
            # _strnicmp(name, "all", 3): LooksMenu reads such a line as an All line.
            report.append(f'rules: plugin {plugin!r} starts with "All", which LooksMenu reads as an '
                          f'All line -- its rules cannot work and are skipped')
            return False
        if not (data / plugin).exists():
            report.append(f'rules: plugin {plugin!r} is not in Data -- LooksMenu will skip its lines')
        return True

    def emit(left, templates):
        if templates:
            lines.append(f'{left}=' + '|'.join(templates))
            needed.update(templates)

    # 6. race presets
    for g, key, label in (('female', 'raceFemale', 'Female'), ('male', 'raceMale', 'Male')):
        for race, names in cfg.get(key, {}).items():
            emit(f'All|{label}|{race}', resolve_presets(names, g).get(g))
    # 5. plugin presets -- every race that takes part, since a line needs one
    for g, key, label in (('female', 'npcPluginFemale', 'Female'), ('male', 'npcPluginMale', 'Male')):
        for plugin, names in cfg.get(key, {}).items():
            if not plugin_ok(plugin):
                continue
            races = sorted(set(distribute) | set(cfg.get(f'race{label}', {})))
            for race in races:
                emit(f'{plugin}|All|{label}|{race}', resolve_presets(names, g).get(g))
    # 4. faction presets: pending (reported by load)
    # 3. plugin and race blacklists
    for g, key, label in (('female', 'blacklistedNpcsPluginFemale', 'Female'),
                          ('male', 'blacklistedNpcsPluginMale', 'Male')):
        for plugin in cfg.get(key, []):
            if not plugin_ok(plugin):
                continue
            races = sorted(set(distribute) | set(cfg.get(f'race{label}', {})))
            for race in races:
                emit(f'{plugin}|All|{label}|{race}', [UNSHAPED])
    for g, key, label in (('female', 'blacklistedRacesFemale', 'Female'),
                          ('male', 'blacklistedRacesMale', 'Male')):
        for race in cfg.get(key, []):
            emit(f'All|{label}|{race}', [UNSHAPED])
    # 2. per-NPC presets by FormID. A preset list may hold both sexes' presets;
    # each sex's table gets its own.
    for plugin, forms in cfg.get('npcFormID', {}).items():
        if not plugin_ok(plugin):
            continue
        light = is_light(data, plugin)
        for key, names in forms.items():
            fid = form_key(key, light)
            if fid is None:
                report.append(f'rules: npcFormID {plugin} {key!r} is not a FormID, skipped')
                continue
            if is_player_form(plugin, fid):
                report.append(f'rules: npcFormID {plugin} {key!r} is the player or a character-creation dummy: '
                              f'their own lines come last and are never randomised (S-45), so this rule never '
                              f'reaches them -- pick the player\'s body in MCM instead')
                continue
            by_g = resolve_presets(names, None)
            for g, label in (('female', 'Female'), ('male', 'Male')):
                emit(f'{plugin}|{fid}|{label}', by_g.get(g))
    # 1. per-NPC blacklists by FormID
    for plugin, keys in cfg.get('blacklistedNpcsFormID', {}).items():
        if not plugin_ok(plugin):
            continue
        light = is_light(data, plugin)
        for key in keys:
            fid = form_key(key, light)
            if fid is None:
                report.append(f'rules: blacklistedNpcsFormID {plugin} {key!r} is not a FormID, skipped')
                continue
            if is_player_form(plugin, fid):
                report.append(f'rules: blacklistedNpcsFormID {plugin} {key!r} is the player or a character-creation '
                              f'dummy: their own lines come last (S-45), so this blacklist never reaches them')
                continue
            emit(f'{plugin}|{fid}', [UNSHAPED])
    return lines, needed


# The template that sets nothing. An NPC it is given stays UNSHAPED: it has no
# stored morphs, so LooksMenu evaluates it again on every load and it evaluates
# to nothing again. Safe only as the sole option on its line.
UNSHAPED = 'Silhouette_Unshaped'


def write_default(path):
    """A config with every key present and its default value written out -- the rules empty, the
    heavy words and distributeRaces ["HumanRace"] spelled in full -- so a user sees each option and
    what it holds without reading this file (owner, 2026-09-24). Only ever written where there is
    no config yet: the generator never writes into a config it reads."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(DEFAULT, indent=2) + '\n', encoding='utf-8')
