"""F4SE/Plugins/Silhouette/catalog.json: what Silhouette.dll needs to know (decision S-19).

The generator stays the compiler. It has already measured which presets fit, what
each one writes, what is a runtime state (S-16) or the shaft (S-29) and what BodyGen
rolls per NPC (S-17, S-21); the plugin re-derives none of it and refuses a catalog
that disagrees with the BodyGen files beside it (their headers carry the same build,
stamp and rules hash).

The schema is the plugin's parser (src/Catalog.cpp), key for key; a catalog it cannot
read whole is refused whole, so check() below refuses the same things first, and the
build scripts run the parser itself on the written files (SilhouetteTests.exe --check).
"""
import hashlib
import json
import math
import os
import pathlib
import string
import struct

import plugin_forms
import rules

SCHEMA = 1
MARKER_PREFIX = 'Silhouette_'
BLACKLIST_MARKER = 'Silhouette_Blacklisted'   # S-23: stored unkeyed, moves nothing, keeps BodyGen away
REFIT_MARKER = 'Silhouette_Refit'             # S-40: under the refit keyword, names the set that is on
CHOICE_MARKER = 'Silhouette_Chosen'           # S-51: unkeyed, beside a picked or API-given body
RESERVED_MARKERS = (BLACKLIST_MARKER, REFIT_MARKER, CHOICE_MARKER)
CLOTHED_SLOTS = [33, 36, 41]                  # BODY, [U] Torso, [A] Torso (S-20)
FLOAT32_MAX = 3.4028234663852886e38

# The built-in clothed shape for a CBBE body (S-40, S-48): FLOORS under Silhouette's refit keyword.
# While she is dressed she has at least these; every other slider is her own body, so the clothed shape
# follows her body wherever it goes. NipBGone only under heavy clothes.
BUILTIN_FEMALE = [
    ('BreastsTogether', 0.3, False),
    ('PushUp', 0.2, False),
    ('NipBGone', 1.0, True),
]

# std::tolower in the "C" locale, as the plugin compares names: ASCII letters only.
_ASCII_LOWER = str.maketrans(string.ascii_uppercase, string.ascii_lowercase)


def ifold(s):
    return str(s).translate(_ASCII_LOWER)


def is_refit(name):
    """A BodySlide preset that is a clothed shape, not a body: OBody's naming."""
    n = name.casefold()
    return n.endswith('-refit') or n in ('female-refit', 'male-refit')


def plugins_txt():
    local = os.environ.get('LOCALAPPDATA')
    return pathlib.Path(local) / 'Fallout4' / 'plugins.txt' if local else None


def resolve_races(cfg, data, report):
    """Every race editor id the rules name, found in the load order -- or the run is refused. A race
    no plugin defines matches nobody: a typo in distributeRaces would give no NPC a body, and every
    check would still pass. Matched in any case, as the game and LooksMenu match them."""
    wanted = set(rules.distribute_races(cfg))
    for key in ('blacklistedRacesFemale', 'blacklistedRacesMale'):
        wanted.update(cfg.get(key, []))
    for key in ('raceFemale', 'raceMale'):
        wanted.update(cfg.get(key, {}))
    missing = []
    txt = plugins_txt()
    found = plugin_forms.resolve(data, txt, 'RACE', wanted, missing)
    unknown = sorted(w for w in wanted if w not in found)
    # S-85: a race another mod adds is listed by default; without that mod its lines wait for it.
    for w in [w for w in unknown if w.lower() in rules.OPTIONAL_RACES]:
        report.append(f'{w}: {rules.OPTIONAL_RACES[w.lower()]['plugin']} is not in this load order, so its lines match nobody here (S-85)')
        unknown.remove(w)
    if unknown:
        # A race a disabled plugin defines is not a typo: say which plugin, not "check the spelling" --
        # and which load order it was looked for in. Without a plugins.txt (Mod Organizer, Proton, no
        # LOCALAPPDATA) only the base game and Creation Club plugins count as loaded.
        inactive = plugin_forms.find_inactive(data, txt, 'RACE', unknown)
        if txt and txt.exists():
            enable = f'which is not active in the load order in {txt} -- enable it, or take the race out of the rules'
        else:
            # Without a plugins.txt the plugin may well be enabled -- where this tool did not look: Mod Organizer
            # keeps the load order in its profile and shows it only to programs it starts (wave 5 N14).
            enable = (f'and no plugins.txt was found ({txt or "LOCALAPPDATA is not set"}) to say it is enabled, so only '
                      f'the base game\'s and Creation Club plugins counted as loaded. Under Mod Organizer, run this '
                      f'from Mod Organizer, which shows it the profile\'s load order; otherwise enable the plugin, or '
                      f'take the race out of the rules')
        why = [f'{w!r} is defined by {inactive[w]}, {enable}' if w in inactive else
               f'{w!r}: no plugin in Data defines a race of that editor id -- check the spelling '
               f'(HumanRace, GhoulRace, ...)' for w in unknown]
        raise SystemExit('rules: a race the rules name matches nobody in the load order:\n  ' + '\n  '.join(why))
    report.extend(m for m in missing if 'could not read' in m)


def build(*, stamp, build_id, mode, presets, player, states, never_in_body, variety, cfg, data,
          refit_presets, body_morphs, baked, report, pool_factions=frozenset(), slider_sets=None, race_pools=None):
    """The catalog as a dict, without its rulesHash (rules_hash() below, once the BodyGen lines exist).

    slider_sets: {sex: base_body slider set} the body was built with -- the plugin resolves the player's
              own installed presets through it at run time, as this generator does (S-76)

    presets:  [{name, sex, marker, values: [(morph, v)], random, menu, zeroed, fit, family}] in
              the order the pickers list them first (the NPC hotkeys walk this order)
    player:   {sex: preset name}          states, never_in_body: {sex: [morph]}
    variety:  {sex: [(morph, low, high, group)]}, only morphs the body carries
    refit_presets: [{name, sex, values: [(morph, v)]}] -- "<Preset>-Refit", "Female-Refit"...
    body_morphs: {sex: set of morphs the installed body carries}
    baked:    {sex: {morph: value}} the base body already has (compensated mode; empty when zeroed):
              every value here is written relative to it, the built-in floors too
    """
    resolve_races(cfg, data, report)
    have = {(p['sex'], p['name'].casefold()): p['name'] for p in presets}

    def names_for(sex, wanted, what, repeats=False):
        # repeats: a faction rule keeps a preset listed twice, as twice as likely -- the plugin picks one entry
        # of the list (Rules.cpp Pick), which is how the factions' pools weigh their tiers (S-72).
        out = []
        for n in wanted if isinstance(wanted, list) else [wanted]:
            got = have.get((sex, str(n).casefold()))
            if got is None:
                report.append(f'catalog: {what} names {n!r}, which is not a {sex} preset of this build - left out')
            elif repeats or got not in out:
                out.append(got)
        return out

    def sexes_of(wanted):
        return [s for s in ('female', 'male')
                if any((s, str(n).casefold()) in have for n in (wanted if isinstance(wanted, list) else [wanted]))]

    def bodygen_plugin(plugin, what):
        # LooksMenu reads a line whose plugin starts with "All" as an All line (tools/rules.py): BodyGen
        # never applies such a rule, so the plugin must not believe it does either.
        if str(plugin).lower().startswith('all'):
            report.append(f'catalog: {what} names plugin {plugin!r}, which starts with "All" - left out, as BodyGen leaves it')
            return False
        return True

    def ref(plugin, key, what):
        light = rules.is_light(data, plugin)
        fid = rules.form_key(key, light)
        if fid is None:
            report.append(f'catalog: {what} {plugin} {key!r} is not a form id - left out')
            return None
        return {'plugin': plugin, 'id': int(fid, 16)}

    def npc_ref(plugin, key, what):
        # The BodyGen tiers the plugin leaves alone: the lines skip the player and the character-creation
        # dummies (S-45, rules.compile_lines reports it), so the catalog must not claim a line reaches them.
        r = ref(plugin, key, what)
        return None if r is None or rules.is_player_form(plugin, f'{r["id"]:06X}') else r

    # ---- rules: the tiers BodyGen carries (so the plugin leaves them alone) and the
    # ones only the runtime can see (S-23)
    npc_form = {'female': [], 'male': []}
    for plugin, forms in cfg.get('npcFormID', {}).items():
        if not bodygen_plugin(plugin, 'npcFormID'):
            continue
        for key, wanted in forms.items():
            r = npc_ref(plugin, key, 'npcFormID')
            if r:
                for s in sexes_of(wanted):
                    npc_form[s].append(r)
    blacklisted_form = []
    for plugin, keys in cfg.get('blacklistedNpcsFormID', {}).items():
        if not bodygen_plugin(plugin, 'blacklistedNpcsFormID'):
            continue
        for key in keys:
            r = npc_ref(plugin, key, 'blacklistedNpcsFormID')
            if r:
                blacklisted_form.append(r)
    blacklisted_plugins = {s: [p for p in cfg.get(key, []) if bodygen_plugin(p, key)]
                           for s, key in (('female', 'blacklistedNpcsPluginFemale'), ('male', 'blacklistedNpcsPluginMale'))}

    npc_name = []
    for name, wanted in cfg.get('npc', {}).items():
        for s in ('female', 'male'):
            got = [have[(s, str(n).casefold())] for n in (wanted if isinstance(wanted, list) else [wanted])
                   if (s, str(n).casefold()) in have]
            if got:
                npc_name.append({'name': name, 'sex': s, 'presets': list(dict.fromkeys(got))})
        if not sexes_of(wanted):
            report.append(f'catalog: npc {name!r} names no preset of this build - left out')

    faction = []
    wanted_factions = {e for key in ('factionFemale', 'factionMale') for e in cfg.get(key, {})}
    found = plugin_forms.resolve(data, plugins_txt(), 'FACT', wanted_factions, report)
    for sex, key in (('female', 'factionFemale'), ('male', 'factionMale')):
        for edid, wanted in cfg.get(key, {}).items():
            if edid not in found:
                continue
            got = names_for(sex, wanted, f'{key} {edid!r}', repeats=True)
            if got:
                plugin, local = found[edid]
                rule = {'plugin': plugin, 'id': local, 'editorID': edid, 'sex': sex, 'presets': got}
                if (edid.casefold(), sex) in pool_factions:
                    rule['pool'] = True  # one of Silhouette's own pools (S-72): MCM's switch can leave it out (S-73)
                faction.append(rule)

    # ---- ORefit (S-20, S-40, S-42), OBody's keys and Silhouette's heavy/light lists
    def refs(key):
        out = []
        for plugin, keys in cfg.get(key, {}).items():
            for k in keys:
                r = ref(plugin, k, key)
                if r:
                    out.append(r)
        return out

    sets = []
    female_body = body_morphs.get('female', set())
    # A floor is a value of the morph layer, which a compensated body adds to what its mesh has baked
    # in: the floor that lands the body AT the built-in value is that value minus the baked one.
    female_baked = (baked or {}).get('female', {})
    builtin = [(m, round(v - female_baked.get(m, 0.0), 6), h) for m, v, h in BUILTIN_FEMALE if m in female_body]
    lower = [m for m, v, _h in builtin if v <= 0]
    if lower:
        # The floor that would land the body on the built-in shape is 0 or below, and a refit only
        # raises (S-40): a body whose own value sits below the base's is not raised there.
        report.append(f'catalog: the base body already has {", ".join(lower)} at or above the built-in clothed '
                      f'shape, so the floor there would be 0 or below, which a refit cannot hold (S-40): a body '
                      f'below the base\'s own value keeps it while dressed')
    builtin = [(m, v, h) for m, v, h in builtin if v > 0]
    missing = [m for m, _v, _h in BUILTIN_FEMALE if m not in female_body]
    if missing:
        report.append(f'catalog: the installed female body has no {", ".join(missing)}: the built-in clothed shape '
                      f'leaves {"it" if len(missing) == 1 else "them"} out')
    if builtin:
        sets.append({'name': 'builtin:female', 'sex': 'female',
                     'floors': [{'morph': m, 'value': v, 'heavyOnly': h} for m, v, h in builtin]})
    for p in refit_presets:
        if (p['sex'], ifold(p['name'])) in {(s['sex'], ifold(s['name'])) for s in sets}:
            report.append(f'catalog: refit preset {p["name"]!r} is there twice for {p["sex"]} - the first one is used')
            continue
        floors = [{'morph': m, 'value': round(v, 6), 'heavyOnly': False} for m, v in p['values'] if v > 0]
        lower = [m for m, v in p['values'] if v <= 0]
        if lower:
            report.append(f'catalog: refit preset {p["name"]!r}: {len(lower)} of its own slider(s) would sit at or '
                          f'below the body\'s own layer zero, and a refit only raises (S-40) - left out: '
                          f'{", ".join(lower[:6])}{" ..." if len(lower) > 6 else ""}')
        if not floors:
            report.append(f'catalog: refit preset {p["name"]!r} raises nothing on this body - left out, so the next '
                          f'set in line applies')
            continue
        sets.append({'name': p['name'], 'sex': p['sex'], 'floors': floors})
    set_names = {(s['sex'], ifold(s['name'])): s['name'] for s in sets}
    outfits = []
    for sex, key in (('female', 'refitOutfitPresetsFemale'), ('male', 'refitOutfitPresetsMale')):
        for outfit, preset in cfg.get(key, {}).items():
            got = set_names.get((sex, ifold(preset)))
            if got:
                outfits.append({'name': outfit, 'sex': sex, 'set': got})   # the set's own spelling
            else:
                report.append(f'catalog: {key} {outfit!r} names {preset!r}, which is not a {sex} refit set of this '
                              f'build (a BodySlide preset named "<something>-Refit" that raises something) - left out')

    doc = {
        'schema': SCHEMA, 'build': build_id, 'stamp': stamp, 'mode': mode,
        'presets': [{'name': p['name'], 'sex': p['sex'], 'marker': p['marker'],
                     'values': {m: round(v, 6) for m, v in p['values']},
                     'random': p['random'], 'menu': p['menu'], 'zeroed': bool(p.get('zeroed', not p['values'])),
                     'fit': p['fit'], 'family': p['family']} for p in presets],
        'player': {s: player[s] for s in ('female', 'male') if player.get(s)},
        'states': {s: list(states.get(s, [])) for s in ('female', 'male')},
        'neverInBody': {s: list(never_in_body.get(s, [])) for s in ('female', 'male')},
        # S-76: each morph slider's default and inversion in the set the body was built with, so the plugin
        # turns a player's own BodySlide preset into morph values exactly as base_body.resolve() does.
        'sliderSets': {s: {'set': ss['name'],
                           'sliders': {n: [round(sl['default'], 6), bool(sl['invert'])]
                                       for n, sl in sorted(ss['sliders'].items()) if sl['morph']}}
                       for s, ss in (slider_sets or {}).items() if ss},
        'variety': {s: [{'morph': m, 'low': lo, 'high': hi, 'group': grp} for m, lo, hi, grp in variety.get(s, [])]
                    for s in ('female', 'male')},
        'rules': {
            'races': rules.distribute_races(cfg),
            'npcFormID': npc_form,
            'blacklistedNpcsFormID': blacklisted_form,
            'blacklistedPlugins': blacklisted_plugins,
            'blacklistedRaces': {'female': list(cfg.get('blacklistedRacesFemale', [])),
                                 'male': list(cfg.get('blacklistedRacesMale', []))},
            'npcName': npc_name,
            'blacklistedNpcNames': list(cfg.get('blacklistedNpcs', [])),
            'faction': faction,
            # S-86: races the plugin draws itself, with the body's sex
            'racePool': list(race_pools or []),
        },
        'orefit': {
            'slots': CLOTHED_SLOTS,
            'blacklist': refs('blacklistedOutfitsFromORefitFormID'),
            'blacklistNames': list(cfg.get('blacklistedOutfitsFromORefit', [])),
            'blacklistPlugins': list(cfg.get('blacklistedOutfitsFromORefitPlugin', [])),
            'force': refs('outfitsForceRefitFormID'),
            'forceNames': list(cfg.get('outfitsForceRefit', [])),
            'outfits': outfits,
            'sets': sets,
            'heavy': {'words': list(cfg.get('heavyWords', rules.DEFAULT['heavyWords'])),
                      'items': refs('heavyOutfitsFormID'), 'names': list(cfg.get('heavyOutfits', []))},
            'light': {'items': refs('lightOutfitsFormID'), 'names': list(cfg.get('lightOutfits', []))},
        },
    }
    return doc


def rules_hash(doc, morph_lines):
    """12 hex digits naming everything the rules do: the BodyGen lines (who BodyGen gives what) and
    the catalog (what the plugin does). Both BodyGen files' headers and the catalog carry it, and the
    plugin refuses files whose hashes differ: the build names the bodies, this names the rules (S-19)."""
    body = {k: v for k, v in doc.items() if k != 'rulesHash'}
    text = json.dumps({'lines': [line for line in morph_lines if not line.startswith('#')], 'catalog': body},
                      sort_keys=True)
    return hashlib.sha1(text.encode('utf-8')).hexdigest()[:12]


def f32(v):
    """v as the float32 the plugin keeps (static_cast<float>): inf when it does not fit one."""
    try:
        return struct.unpack('<f', struct.pack('<f', v))[0]
    except (OverflowError, struct.error, TypeError):
        return math.inf


# Every key the plugin's parser reads, per object. The parser ignores an unknown key outside the
# per-sex objects -- a field the generator added and the plugin never reads -- so check() refuses it.
SEXES = ('female', 'male')
TOP_KEYS = ('schema', 'build', 'stamp', 'mode', 'rulesHash', 'states', 'neverInBody', 'presets', 'player',
            'variety', 'rules', 'orefit', 'sliderSets')
PRESET_KEYS = ('name', 'sex', 'marker', 'values', 'random', 'menu', 'zeroed', 'fit', 'family')
VARIETY_KEYS = ('morph', 'low', 'high', 'group')
RULE_KEYS = ('racePool', 'races', 'npcFormID', 'blacklistedNpcsFormID', 'blacklistedPlugins', 'blacklistedRaces', 'npcName',
             'blacklistedNpcNames', 'faction')
OREFIT_KEYS = ('slots', 'blacklist', 'blacklistNames', 'blacklistPlugins', 'force', 'forceNames', 'outfits', 'sets',
               'heavy', 'light')
KIND_NAMES = {str: 'a string', bool: 'true or false', int: 'an integer', float: 'a number', list: 'a list',
              dict: 'an object'}


def check(doc):
    """What the plugin's parser (src/Catalog.cpp ParseCatalog) refuses, refused here first, key for key --
    and a little more: what it would accept but could never mean (a range or a floor that one float32
    cannot tell from nothing, a key it would silently ignore). Every refusal is a SystemExit naming the
    place, never a crash: tools/verify_bodygen.py reports it as a problem of the file on disk."""
    def fail(msg):
        raise SystemExit(f'catalog: {msg}')

    def need(v, kind, where):
        kinds = kind if isinstance(kind, tuple) else (kind,)
        if not isinstance(v, kinds) or (isinstance(v, bool) and bool not in kinds):
            what = 'a number' if set(kinds) == {int, float} else ' or '.join(KIND_NAMES.get(k, k.__name__) for k in kinds)
            fail(f'{where}: expected {what}, got {type(v).__name__}')
        return v

    def at(obj, key, where):
        need(obj, dict, where)
        if key not in obj:
            fail(f'{where}: missing "{key}"')
        return obj[key]

    def only(obj, keys, where):
        need(obj, dict, where)
        extra = sorted(str(k) for k in obj if k not in keys)
        if extra:
            fail(f'{where}: unknown key {extra[0]!r} (the plugin reads {", ".join(keys)})')
        return obj

    def per_sex(obj, where):
        only(obj, SEXES, where)
        for s in SEXES:
            at(obj, s, where)
        return obj

    def number(v, where):
        # Num(): a JSON number (an integer too), finite as a double AND as the float32 the plugin keeps.
        need(v, (int, float), where)
        f = f32(v)
        if not math.isfinite(f):
            fail(f'{where}: {v!r} is not a finite float32')
        return f

    def sex_of(v, where):
        if v not in SEXES:
            fail(f'{where}: sex must be "female" or "male", not {v!r}')
        return v

    def strings(v, where):
        for x in need(v, list, where):
            need(x, str, where)
        return v

    def ref(r, where):
        need(r, dict, where)
        if not need(at(r, 'plugin', where), str, f'{where}.plugin'):
            fail(f'{where}: an empty plugin name')
        i = at(r, 'id', where)
        if type(i) is not int or i < 0:
            fail(f'{where}: id {i!r} must be a non-negative integer')
        if i > 0xFFFFFF:
            fail(f'{where}: id {i:X} carries a load-order byte')
        return r

    def ref_list(v, where):
        for r in need(v, list, where):
            only(r, ('plugin', 'id'), where)
            ref(r, where)
        return v

    # Everywhere: text the plugin can read back (JSON is UTF-8; a lone surrogate is refused whole), and
    # numbers finite as the float the plugin keeps (it refuses 1e39, 10**40 and NaN).
    def walk(v, where):
        if isinstance(v, str):
            if not rules.encodable(v):
                fail(f'{where}: {v!r} is not valid Unicode')
        elif isinstance(v, (int, float)) and not isinstance(v, bool):
            if isinstance(v, float) and not math.isfinite(v) or abs(v) > FLOAT32_MAX:
                fail(f'{where}: {v!r} is not a finite float32')
        elif isinstance(v, dict):
            for k, x in v.items():
                walk(k, where)
                walk(x, f'{where}.{k}')
        elif isinstance(v, list):
            for x in v:
                walk(x, where)

    walk(doc, 'catalog')
    only(doc, TOP_KEYS, 'catalog')
    schema = at(doc, 'schema', 'catalog')
    if type(schema) is not int or schema != SCHEMA:
        fail(f'schema must be the integer {SCHEMA}, not {schema!r}')
    stamp = at(doc, 'stamp', 'catalog')
    if type(stamp) is not int or not 0 < stamp < 1 << 24:
        fail(f'stamp {stamp!r} must be an integer 1 .. 2^24-1 (a marker holds it as a float32, exactly)')
    for k in ('build', 'mode', 'rulesHash'):
        need(at(doc, k, 'catalog'), str, k)
    if not doc['rulesHash']:
        fail('rulesHash is empty')
    never = {s: {ifold(m) for m in strings(at(per_sex(at(doc, 'neverInBody', 'catalog'), 'neverInBody'), s,
                                              'neverInBody'), f'neverInBody.{s}')} for s in SEXES}
    states = per_sex(at(doc, 'states', 'catalog'), 'states')
    for s in SEXES:
        for m in strings(states[s], f'states.{s}'):
            if ifold(m) not in never[s]:
                fail(f'the runtime state {m!r} is missing from neverInBody (S-16)')
    seen, markers = set(), set()
    for p in need(at(doc, 'presets', 'catalog'), list, 'presets'):
        need(p, dict, 'presets')
        where = f'preset {p.get("name")!r}'
        only(p, PRESET_KEYS, where)
        name = need(at(p, 'name', where), str, f'{where}.name')
        sex = sex_of(at(p, 'sex', where), where)
        marker = need(at(p, 'marker', where), str, where)
        if len(marker) <= len(MARKER_PREFIX) or ifold(marker[:len(MARKER_PREFIX)]) != ifold(MARKER_PREFIX) \
                or ifold(marker) in {ifold(r) for r in RESERVED_MARKERS}:
            fail(f'{where}: marker {marker!r} is not a body marker ("{MARKER_PREFIX}...", not a reserved one)')
        if ifold(marker) in markers:
            fail(f'{where}: marker {marker!r} is used by another preset too')
        markers.add(ifold(marker))
        for m, v in need(at(p, 'values', where), dict, f'{where}.values').items():
            if ifold(m) in never[sex]:
                fail(f'{where}: {m!r} is never part of a body (S-16, S-29)')
            number(v, f'{where}.values.{m}')
        for k in ('random', 'menu', 'zeroed'):
            need(at(p, k, where), bool, f'{where}.{k}')
        for k in ('fit', 'family'):
            need(at(p, k, where), str, f'{where}.{k}')
        # The engine's string pool keeps one spelling per name, whatever the case (L3 F12): two
        # names that differ only in case are one preset to the plugin.
        key = (sex, ifold(name))
        if key in seen:
            fail(f'{where} listed twice for {sex} (names are compared in any case)')
        seen.add(key)
    # S-76, optional: the slider set each body was built with, for the player's own presets at run time.
    for s, ss in only(need(doc.get('sliderSets', {}), dict, 'sliderSets'), SEXES, 'sliderSets').items():
        only(need(ss, dict, f'sliderSets.{s}'), ('set', 'sliders'), f'sliderSets.{s}')
        need(at(ss, 'set', f'sliderSets.{s}'), str, f'sliderSets.{s}.set')
        for n, pair in need(at(ss, 'sliders', f'sliderSets.{s}'), dict, f'sliderSets.{s}.sliders').items():
            if not (isinstance(pair, list) and len(pair) == 2 and type(pair[1]) is bool):
                fail(f'sliderSets.{s}.sliders.{n} must be [default, invert]')
            number(pair[0], f'sliderSets.{s}.sliders.{n}')
    player = only(at(doc, 'player', 'catalog'), SEXES, 'player')
    for s, name in player.items():
        if (s, ifold(need(name, str, f'player.{s}'))) not in seen:
            fail(f'the {s} player default {name!r} is not a preset of this build')
    variety = per_sex(at(doc, 'variety', 'catalog'), 'variety')
    for s in SEXES:
        names = set()
        for v in need(variety[s], list, f'variety.{s}'):
            only(v, VARIETY_KEYS, 'variety')
            morph = need(at(v, 'morph', 'variety'), str, 'variety.morph')
            where = f'variety {morph!r}'
            low, high = number(at(v, 'low', where), f'{where}.low'), number(at(v, 'high', where), f'{where}.high')
            group = need(at(v, 'group', where), str, f'{where}.group')
            if not low < high:
                fail(f'{where}: the range {v["low"]}..{v["high"]} rolls nothing (low must be below high, as the '
                     f'float32 the plugin keeps)')
            if group not in ('nipples', 'genitals'):
                fail(f'{where}: group {group!r}')
            if ifold(morph) in never[s]:
                fail(f'{where} is never part of a body, so never rolled (S-16, S-29)')
            if ifold(morph) in names:
                fail(f'{where} listed twice for {s}')
            names.add(ifold(morph))
    r = only(at(doc, 'rules', 'catalog'), RULE_KEYS, 'rules')
    if not strings(at(r, 'races', 'rules'), 'rules.races'):
        fail('rules.races is empty: no race would be shaped (distributeRaces)')
    for key in ('npcFormID', 'blacklistedPlugins', 'blacklistedRaces'):
        per_sex(at(r, key, 'rules'), f'rules.{key}')
        for s in SEXES:
            (ref_list if key == 'npcFormID' else strings)(r[key][s], f'rules.{key}.{s}')
    ref_list(at(r, 'blacklistedNpcsFormID', 'rules'), 'rules.blacklistedNpcsFormID')
    strings(at(r, 'blacklistedNpcNames', 'rules'), 'rules.blacklistedNpcNames')
    for rule in need(at(r, 'npcName', 'rules'), list, 'rules.npcName'):
        only(rule, ('name', 'sex', 'presets'), 'rules.npcName')
        where = f'rules.npcName {need(at(rule, "name", "rules.npcName"), str, "rules.npcName.name")!r}'
        sex = sex_of(at(rule, 'sex', where), where)
        for n in strings(at(rule, 'presets', where), where):
            if (sex, ifold(n)) not in seen:
                fail(f'{where} names {n!r}, not a {sex} preset of this build')
    for rule in need(at(r, 'faction', 'rules'), list, 'rules.faction'):
        only(rule, ('plugin', 'id', 'editorID', 'sex', 'presets', 'pool'), 'rules.faction')
        if 'pool' in rule:
            need(rule['pool'], bool, 'rules.faction.pool')  # optional (S-73): the plugin reads it only when true/false
        ref(rule, 'rules.faction')
        where = f'rules.faction {need(at(rule, "editorID", "rules.faction"), str, "rules.faction.editorID")!r}'
        sex = sex_of(at(rule, 'sex', where), where)
        for n in strings(at(rule, 'presets', where), where):
            if (sex, ifold(n)) not in seen:
                fail(f'{where} names {n!r}, not a {sex} preset of this build')
    for rule in need(r.get('racePool', []), list, 'rules.racePool'):  # S-86, optional
        only(rule, ('race', 'sex', 'presets', 'without', 'withoutUnless'), 'rules.racePool')
        strings(rule.get('without', []), 'rules.racePool.without')  # S-87, optional
        need(rule.get('withoutUnless', ''), str, 'rules.racePool.withoutUnless')  # S-88, optional
        where = f'rules.racePool {need(at(rule, "race", "rules.racePool"), str, "rules.racePool.race")!r}'
        sex = sex_of(at(rule, 'sex', where), where)
        names = strings(at(rule, 'presets', where), where)
        if not names:
            fail(f'{where}: no presets')
        for n in names:
            if (sex, ifold(n)) not in seen:
                fail(f'{where} names {n!r}, not a {sex} preset of this build')
    o = only(at(doc, 'orefit', 'catalog'), OREFIT_KEYS, 'orefit')
    for slot in need(at(o, 'slots', 'orefit'), list, 'orefit.slots'):
        if type(slot) is not int or not 30 <= slot <= 61:
            fail(f'orefit.slots: {slot!r} is not a biped slot 30..61')
    for k in ('blacklist', 'force'):
        ref_list(at(o, k, 'orefit'), f'orefit.{k}')
    for k in ('blacklistNames', 'blacklistPlugins', 'forceNames'):
        strings(at(o, k, 'orefit'), f'orefit.{k}')
    set_keys = set()
    for st in need(at(o, 'sets', 'orefit'), list, 'orefit.sets'):
        only(st, ('name', 'sex', 'floors'), 'orefit.sets')
        where = f'refit set {need(at(st, "name", "orefit.sets"), str, "orefit.sets.name")!r}'
        key = (sex_of(at(st, 'sex', where), where), ifold(st['name']))
        if key in set_keys:
            fail(f'{where} listed twice for {st["sex"]}')
        set_keys.add(key)
        for f in need(at(st, 'floors', where), list, where):
            only(f, ('morph', 'value', 'heavyOnly'), where)
            m = need(at(f, 'morph', where), str, where)
            if not m or ifold(m) == ifold(REFIT_MARKER) or (len(m) > len(MARKER_PREFIX) and ifold(m[:len(MARKER_PREFIX)]) == ifold(MARKER_PREFIX)):
                fail(f'{where}: {m!r} is not a body slider')
            if ifold(m) in never[st['sex']]:
                fail(f'{where}: {m!r} is never part of a body (S-16, S-29)')
            # Compared as the float32 the plugin keeps: 1e-50 is 0 there, and a refit only raises (S-40).
            if not number(at(f, 'value', where), where) > 0:
                fail(f'{where}: {m!r} at {f["value"]}: a floor must be above 0 as a float32, a refit only raises (S-40)')
            need(at(f, 'heavyOnly', where), bool, where)
    for ot in need(at(o, 'outfits', 'orefit'), list, 'orefit.outfits'):
        only(ot, ('name', 'sex', 'set'), 'orefit.outfits')
        where = f'orefit.outfits {need(at(ot, "name", "orefit.outfits"), str, "orefit.outfits.name")!r}'
        sex = sex_of(at(ot, 'sex', where), where)
        if (sex, ifold(need(at(ot, 'set', where), str, where))) not in set_keys:
            fail(f'{where}: refit set {ot["set"]!r} is not in this catalog')
    heavy = only(at(o, 'heavy', 'orefit'), ('words', 'items', 'names'), 'orefit.heavy')
    for w in strings(at(heavy, 'words', 'orefit.heavy'), 'orefit.heavy.words'):
        if not rules.has_word(w):
            fail(f'orefit.heavy.words: {w!r} holds no word (S-48)')
    light = only(at(o, 'light', 'orefit'), ('items', 'names'), 'orefit.light')
    for k, v in (('heavy', heavy), ('light', light)):
        ref_list(at(v, 'items', f'orefit.{k}'), f'orefit.{k}.items')
        strings(at(v, 'names', f'orefit.{k}'), f'orefit.{k}.names')


def write(path, doc):
    check(doc)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=1, sort_keys=False, allow_nan=False) + '\n', encoding='utf-8')
