"""Prove the written BodyGen files do what they claim.

Reads Silhouette_templates.ini and Silhouette_morphs.ini the way LooksMenu reads
them (f4ee/BodyGenInterface.cpp, ReadBodyMorphTemplates and ReadBodyMorphs), then
builds every body those files can put on an NPC and compares it with the body the
preset describes:

    what LooksMenu shows  = installed base mesh + sum(template value * diff)
    what the preset means = BodySlide reference mesh + sum(preset value * diff)

They must agree to within the half-float rounding of the installed mesh. The
same comparison is made for the naive, uncompensated file, which shows how far
off every NPC would be if the templates ignored what the base has baked in.

It tests the ARTIFACT, not the generator: a template LooksMenu would drop, a
reference it would not resolve, a line longer than its 0x7FFF buffer, a missing
marker or a player guard that sets something is a failure here even if the
generator believes it wrote something correct. The files around them are held to
the same standard: every manifest, the catalog's BodyGen tiers against the lines,
the menu, the hotkeys, the picker script and Silhouette.esp.

    python tools/verify_bodygen.py                # the files in this repo
    python tools/verify_bodygen.py --dir <Loose>  # any other copy
"""
import argparse
import collections
import json
import math
import pathlib
import re
import sys

import base_body
import catalog
import make_esp
import plugin_forms
import rules as rules_mod
import silhouette_gen as sg

# Morph names are compared in any case, as LooksMenu and the plugin compare them.
STATES = {m.casefold() for m in sg.STATE_MORPHS}
SHAFT = {m.casefold() for m in sg.SHAFT_MORPHS}
ANATOMY = {m.casefold() for m in sg.ANATOMY_OWNED}      # S-62: fo4-anatomy's build sliders
NEVER = STATES | SHAFT | ANATOMY

# What LooksMenu's lines name: the player record and the two character-creation dummies (S-45).
PLAYER_ID, DUMMY_IDS = 0x7, {0xA7D35: 'female', 0xA7D34: 'male'}


def papyrus_unescape(s):
    """The text a Papyrus string literal holds (sg.papyrus_string, read back)."""
    return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m.group(1), m.group(1)), s)

LINE_LIMIT = 0x7FFF        # BSResourceTextFile<0x7FFF>
MAX_ERROR = 0.1            # game units; half floats step 0.0625 above |64|
RMS_ERROR = 0.01


def explode(s, sep):
    """std::explode as LooksMenu's Utilities.cpp has it: empty pieces are dropped."""
    return [p for p in s.split(sep) if p]


def engine_lines(path, problems):
    """[(line number, text)] exactly as LooksMenu gets them.

    Both parsers loop `while (textFile.ReadLine(&str))`, and ReadLine is the
    engine's BSResourceTextFile::ReadLine: it returns the number of bytes before
    the '\n', and the loop stops on 0. So with LF endings the first EMPTY line
    ends the file -- every template and rule after it is silently never read.
    With CRLF an "empty" line is '\r' and survives. So: every line must end
    CRLF, and a zero-length line fails the file.
    """
    raw = path.read_bytes()
    segments = raw.split(b'\n')
    tail = segments.pop()                 # after the last '\n': EOF, or an unterminated line
    out = []
    for i, seg in enumerate(segments, 1):
        if len(seg) == 0:
            rest = sum(1 for x in segments[i:] if x.strip()) + (1 if tail.strip() else 0)
            problems.append(f'{path.name}:{i}: an EMPTY line -- LooksMenu stops reading here, and the '
                            f'{rest} line(s) with content after it are never read')
            return out
        if not seg.endswith(b'\r'):
            problems.append(f'{path.name}:{i}: a bare LF line ending -- one blank line away from '
                            f'ending the file for LooksMenu; write CRLF')
        if len(seg) >= LINE_LIMIT:
            problems.append(f'{path.name}:{i}: line is {len(seg)} bytes, LooksMenu reads it in '
                            f'{LINE_LIMIT - 1}-byte pieces')
        out.append((i, seg.rstrip(b'\r').decode('cp1252', errors='replace')))
    if tail:
        out.append((len(segments) + 1, tail.rstrip(b'\r').decode('cp1252', errors='replace')))
    return out


def parse_templates(path, problems):
    """{name: [alternative sets]}, each set a list of selectors, each selector a
    list of (morph, low, high). A template with any bad entry is DROPPED, as
    LooksMenu drops it."""
    out = {}
    for n, raw in engine_lines(path, problems):
        line = raw.strip()
        if not line or line[0] == '#':
            continue
        side = explode(line, '=')
        if len(side) < 2:
            problems.append(f'{path.name}:{n}: no "=", LooksMenu logs an error')
            continue
        name, rside = side[0].strip(), side[1].strip()
        sets, error = [], None
        for s in explode(rside, '/'):
            morphs = []
            for m in explode(s.strip(), ','):
                selector = []
                for sel in explode(m.strip(), '|'):
                    pair = explode(sel.strip(), '@')
                    if len(pair) < 2:
                        error = f'no "@" in {sel.strip()!r}'
                        break
                    morph, values = pair[0].strip(), pair[1].strip()
                    if not morph or not values:
                        error = f'empty morph or value in {sel.strip()!r}'
                        break
                    rng = explode(values, ':')
                    try:
                        low = float(rng[0]) if len(rng) > 1 else float(values)
                        high = float(rng[1]) if len(rng) > 1 else low
                    except ValueError:
                        error = f'value {values!r} is not a number (atof would read garbage)'
                        break
                    selector.append((morph, low, high))
                if error:
                    break
                morphs.append(selector)
            if error:
                break
            sets.append(morphs)
        if error:
            problems.append(f'{path.name}:{n}: template {name!r} DROPPED by LooksMenu: {error}')
            continue
        if name.casefold() in {k.casefold() for k in out}:
            problems.append(f'{path.name}:{n}: template {name!r} defined twice (LooksMenu compares '
                            f'names case-insensitively); the later wins')
        out[name] = sets
    return out


def parse_morphs(path, templates, problems):
    """[(left side tokens, [[template names]], line number)] in file order."""
    rules = []
    for n, raw in engine_lines(path, problems):
        line = raw.strip()
        if not line or line[0] == '#':
            continue
        side = explode(line, '=')
        if len(side) < 2:
            problems.append(f'{path.name}:{n}: no "="')
            continue
        form = [t.strip() for t in explode(side[0].strip(), '|')]
        if len(form) < 2:
            problems.append(f'{path.name}:{n}: left side needs a mod name or All, and more')
            continue
        groups = []
        for g in explode(side[1].strip(), ','):
            names = [t.strip() for t in explode(g.strip(), '|')]
            for t in names:
                if t not in templates:
                    problems.append(f'{path.name}:{n}: template {t!r} not found (LooksMenu skips it)')
            groups.append([t for t in names if t in templates])
        rules.append((form, groups, n))
    return rules


def form_id(token):
    """A line's form id as LooksMenu reads it -- strtoul in base 16, "0x" allowed, leading digits only --
    without a load-order byte; None when there are no digits (strtoul gives 0, which names nothing)."""
    m = re.match(r'\s*(?:0[xX])?([0-9A-Fa-f]+)', token)
    return int(m.group(1), 16) & 0xFFFFFF if m else None


def line_kind(form):
    """What a morphs.ini line's left side names, as ReadBodyMorphs splits it:
    ('all', gender, race) | ('plugin', plugin, gender, race) | ('form', plugin, id, gender) | None.
    A gender of None means both tables; a first token starting with "all" is an All line (_strnicmp 3)."""
    low = [t.lower() for t in form]
    if low[0][:3] == 'all':
        return ('all', low[1] if len(low) > 1 and low[1] in ('female', 'male') else None,
                low[2] if len(low) > 2 else None)
    if len(low) >= 2 and low[1] == 'all':
        return ('plugin', low[0], low[2] if len(low) > 2 and low[2] in ('female', 'male') else None,
                low[3] if len(low) > 3 else None)
    fid = form_id(form[1])
    if fid is None:
        return None
    return ('form', low[0], fid, low[2] if len(low) > 2 and low[2] in ('female', 'male') else None)


def same_values(a, b):
    if a is None:
        return False
    return set(a) == set(b) and all(abs(a[k] - b[k]) <= 1e-6 for k in a)


def split_template(sets):
    """(fixed {morph: value}, ranges [(morph, low, high)] in file order, ranges_last) for a template
    of one set of one-choice selectors -- the only shape Silhouette writes -- else None. ranges_last:
    no morph has a fixed value AFTER its own range, so LooksMenu (a later entry for the same morph
    wins) lets the range replace the preset's own value (S-21). The marker, last of all, is no range's."""
    if len(sets) != 1:
        return None
    fixed, ranges, ranges_last, ranged = {}, [], True, set()
    for selector in sets[0]:
        if len(selector) != 1:
            return None
        morph, low, high = selector[0]
        if low != high:
            ranges.append((morph, low, high))
            ranged.add(morph)
        else:
            fixed[morph] = low
            if morph in ranged:
                ranges_last = False
    return fixed, ranges, ranges_last


def fixed_values(sets):
    """The body a template describes: its fixed values. The ranges are rolled per NPC on purpose
    (S-17, S-21) and checked against catalog.json on their own."""
    split = split_template(sets)
    return split[0] if split else None


def same_ranges(got, want):
    return len(got) == len(want) and all(
        a[0] == b[0] and abs(a[1] - b[1]) <= 1e-6 and abs(a[2] - b[2]) <= 1e-6 for a, b in zip(got, want))


def apply(mesh, values, tri_shape):
    out = [list(v) for v in mesh]
    for morph, w in values.items():
        if w == 0:
            continue
        for i, (x, y, z) in tri_shape.get(morph, {}).items():
            out[i][0] += w * x
            out[i][1] += w * y
            out[i][2] += w * z
    return out


def error(a, b):
    d = [math.dist(p, q) for p, q in zip(a, b)]
    return max(d), math.sqrt(sum(x * x for x in d) / len(d))


def parse_picker_script(path):
    """{gender: {'markers': [...], 'names': [...], 'parts': {'markers': [len, ...], 'names': [...]},
    'apply': {index: {morph: value}}}} read back out of the generated Silhouette:Player source. The
    lists come in parts (FemaleMarkers0, FemaleMarkers1, ...), joined here in part order."""
    out = {'female': {'markers': [], 'names': [], 'apply': {}},
           'male': {'markers': [], 'names': [], 'apply': {}}}
    part_lists = {(g, k): {} for g in ('female', 'male') for k in ('markers', 'names')}
    current, branch = None, None
    for line in path.read_text(encoding='utf-8').splitlines():
        s = line.strip()
        if s.startswith('String[] Function') or s.startswith('String Function Apply'):
            m = re.match(r'String\[\] Function (Female|Male)(Markers|Names)(\d+)\(', s)
            if m:
                current = (m.group(1).lower(), m.group(2).lower(), int(m.group(3)))
                part_lists[current[:2]][current[2]] = []
                continue
            m = re.match(r'String Function Apply(Female|Male)\(', s)
            current = (m.group(1).lower(), 'apply') if m else None
            branch = None
            continue
        if s == 'EndFunction':
            current = None
            continue
        if not current:
            continue
        g, kind = current[:2]
        if kind in ('markers', 'names'):
            m = re.match(r'a\.Add\("(.*)", 1\)$', s)
            if m:
                part_lists[current[:2]][current[2]].append(papyrus_unescape(m.group(1)))
        else:
            m = re.match(r'(?:If|ElseIf) index == (\d+)$', s)
            if m:
                branch = int(m.group(1))
                out[g]['apply'][branch] = {}
                continue
            m = re.match(r'BodyGen\.SetMorph\(a, (True|False), "(.*)", None, (\S+)\)$', s)
            if m and branch is not None:
                out[g]['apply'][branch][papyrus_unescape(m.group(2))] = float(m.group(3))
    for (g, kind), by_part in part_lists.items():
        ordered = [by_part[p] for p in sorted(by_part)]
        out[g].setdefault('parts', {})[kind] = [len(x) for x in ordered]
        out[g].setdefault('partNumbers', {})[kind] = sorted(by_part)
        out[g][kind] = [x for part in ordered for x in part]
    return out


def picker_part_problems(g, s):
    """The VM grows no array past 128 (a longer part silently loses its tail), and Locate()/At() read part p
    as the entries from p * 128 on: parts numbered from 0 without a gap, none over 128, and every part but the
    last one holding anything full. `s` is one sex of parse_picker_script()."""
    out = []
    for kind, sizes in s['parts'].items():
        filled = [n for n in sizes if n]
        if s['partNumbers'][kind] != list(range(len(sizes))) or any(n > sg.ARRAY_LIMIT for n in sizes) or \
                any(n != sg.ARRAY_LIMIT for n in filled[:-1]) or sizes[:len(filled)] != filled:
            out.append(f'{g} picker script: the {kind} parts hold {sizes} (parts {s["partNumbers"][kind]}) -- '
                       f'each at most {sg.ARRAY_LIMIT}, every one before the last full, or a preset is lost or '
                       f'read as another')
    return out


def script_function(text, header):
    """The body of one function of the generated script, from its header line to EndFunction."""
    m = re.search(re.escape(header) + r'\s*\n(.*?)\n\s*EndFunction', text, re.S)
    return m.group(1) if m else None


def parse_settings(text, problems):
    """({"<key>:<section>": value}, sBuild) from an MCM settings.ini, each value read as MCM types it -- by the
    key's first letter: i a whole number, b 0 or 1, f any number, s text (wave 5 L9). A value MCM cannot read as
    its type is a problem: MCM would read that setting as 0."""
    defaults, menu_build, section = {}, None, None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1]
            continue
        if '=' not in line or line.startswith(';'):
            continue
        k, v = (s.strip() for s in line.split('=', 1))
        if k == 'sBuild':
            menu_build = v
            continue
        kind = k[:1]
        try:
            if kind == 'i':
                value = int(v)
            elif kind == 'b':
                value = int(v)
                if value not in (0, 1):
                    raise ValueError(v)
            elif kind == 'f':
                value = float(v)
            elif kind == 's':
                value = v
            else:
                problems.append(f'settings.ini [{section}] {k}={v}: MCM types a setting by its first letter (i, b, f '
                                f'or s), and this one has none of them')
                continue
        except ValueError:
            want = {'i': 'a whole number', 'b': '0 or 1', 'f': 'a number'}[kind]
            problems.append(f'settings.ini [{section}] {k}={v}: not {want} -- MCM would read the setting as 0')
            continue
        defaults[f'{k}:{section}'] = value
    return defaults, menu_build


def check_picker(args, templates, player, problems, stamp, cat):
    cat_by_marker = {p['marker'].casefold(): p for p in (cat or {}).get('presets', [])}
    mcm = args.dir.parent.parent.parent.parent.parent / 'MCM/Config/Silhouette'
    psc = args.psc
    if not (mcm / 'config.json').exists() or not psc.exists():
        problems.append(f'picker files missing ({mcm}\\config.json or {psc})')
        return
    config = json.loads((mcm / 'config.json').read_text(encoding='utf-8-sig'))
    options, buttons, hotkeys, switches = {}, [], [], []
    for page in config['pages']:
        for c in page['content']:
            if c.get('type') == 'dropdown':
                options[c['id']] = c['valueOptions']['options']
            if c.get('type') == 'switcher':
                switches.append(c['id'])
            if c.get('type') == 'button':
                buttons.append(c['action'])
            if c.get('type') == 'hotkey':
                hotkeys.append(c['id'])
    defaults, menu_build = parse_settings((mcm / 'settings.ini').read_text(encoding='utf-8-sig'), problems)
    text = psc.read_text(encoding='utf-8')
    m = re.search(r'String Function Build\(\) Global\s+Return "([^"]*)"', text)
    script_build = m.group(1) if m else None
    m = re.search(r'Float Function Stamp\(\) Global\s+Return (\d+)\.0', text)
    script_stamp = float(m.group(1)) if m else None
    if script_build is None or script_build != menu_build:
        problems.append(f'the script is build {script_build}, the menu {menu_build}: ApplyChosen would '
                        f'refuse -- install the generated files together')
    if stamp is not None and script_stamp != stamp:
        problems.append(f'the script stamps markers {script_stamp}, the BodyGen files {stamp}')

    # (1) What no body may hold, as the script lists it for the regeneration window's heal without
    # Silhouette.dll: exactly the generator's list, in its order (S-16, S-29).
    body = script_function(text, 'String[] Function StateMorphs() Global')
    listed = [papyrus_unescape(x) for x in re.findall(r'out\[\d+\] = "((?:[^"\\]|\\.)*)"', body or '')]
    size = re.search(r'new String\[(\d+)\]', body or '')
    if listed != list(sg.NEVER_IN_BODY) or not size or int(size.group(1)) != len(listed):
        problems.append(f'Silhouette:Player.StateMorphs() lists {listed}, not {list(sg.NEVER_IN_BODY)}: without '
                        f'Silhouette.dll the heal would leave a state or the shaft in a body (S-16, S-29)')

    script = parse_picker_script(psc)
    # (4) Count() is each list's length: a shorter one hides a preset, a longer one reads past the list.
    count = re.search(r'Int Function Count\(Bool female\) Global\s+If female\s+Return (\d+)\s+EndIf\s+Return (\d+)', text)
    counted = {'female': int(count.group(1)), 'male': int(count.group(2))} if count else {}
    for g in ('female', 'male'):
        if counted.get(g) != len(script[g]['names']):
            problems.append(f'Silhouette:Player.Count({g == "female"}) returns {counted.get(g)}, the {g} list holds '
                            f'{len(script[g]["names"])} presets')
    apply_default = script_function(text, 'Function ApplyDefault() Global') or ''
    m = re.search(r'Int index = (-?\d+)\s+If female\s+index = (-?\d+)', apply_default)
    default_of = {'male': int(m.group(1)), 'female': int(m.group(2))} if m else {}
    if not m:
        problems.append('Silhouette:Player.ApplyDefault() names no default index')
    for g in ('female', 'male'):
        sid = next((k for k in options if k.startswith(f'i{g.capitalize()}_')), None)
        # (the NPC page's dropdowns, iNpc<Sex>_..., are checked below)
        s = script[g]
        if sid is None and not s['names']:
            print(f'{g} picker: no preset of this sex fits the body, so the menu offers none')
            continue
        sid = sid or f'i{g.capitalize()}_?:Player'
        if options.get(sid) != s['names']:
            problems.append(f'MCM {sid} lists {len(options.get(sid, []))} presets, the script {len(s["names"])} '
                            f'-- the menu would apply a different preset than it shows')
        if len(s['markers']) != len(s['names']) or sorted(s['apply']) != list(range(len(s['names']))):
            problems.append(f'{g} picker script: markers, names and branches do not line up')
            continue
        problems += picker_part_problems(g, s)
        d = defaults.get(sid)
        if d is None or not 0 <= d < len(s['names']):
            problems.append(f'MCM default {sid}={d} is not an entry of the menu')
        elif g in player and [x for grp in player[g][1] for x in grp]:
            t = [x for grp in player[g][1] for x in grp][0]
            fixed = fixed_values(templates[t]) or {}
            # With no preset that fits fully the player gets the bare body (the template that sets
            # nothing), and the menu's default is simply its first entry (L4 F7).
            if any(fixed.values()) and s['markers'][d] not in fixed:
                problems.append(f'MCM default {sid} is {s["markers"][d]!r}, but the player template {t!r} '
                                f'BodyGen gives carries {[k for k in fixed if k.startswith("Silhouette_")]}')
        # (4) "Back to the default" gives what the menu's default is -- the catalog's player default --
        # or, with no default, the bare body: -1.
        want = (cat or {}).get('player', {}).get(g)
        got = default_of.get(g)
        if want:
            if got is None or not 0 <= got < len(s['names']) or s['names'][got] != want or (d is not None and got != d):
                problems.append(f'Silhouette:Player.ApplyDefault() gives the {g} player index {got}, the menu\'s '
                                f'default is {d} and the catalog\'s player default {want!r}')
        elif got is not None and got != -1:
            problems.append(f'Silhouette:Player.ApplyDefault() gives the {g} player index {got}, but no {g} preset is '
                            f'the default (the bare body: -1)')
        agree = 0
        for i, marker in enumerate(s['markers']):
            got_values = s['apply'][i]
            if got_values.get(marker) != script_stamp:
                problems.append(f'picker {g} #{i} {s["names"][i]!r}: sets no marker with the build\'s '
                                f'stamp')
            # Every branch, not only the ones the random pool has a template for: a picker-only preset
            # (a zeroed one) is compared with the body the plugin gives by the same marker.
            cp = cat_by_marker.get(marker.casefold())
            if cat is not None and (cp is None or cp['sex'] != g or cp['name'] != s['names'][i] or not same_values(
                    cp['values'], {k: v for k, v in got_values.items() if k != marker})):
                problems.append(f'picker {g} {s["names"][i]!r} ({marker}) is not the catalog\'s preset of that marker: '
                                f'the menu would give another body than the plugin does')
            if marker in templates:
                want_values = fixed_values(templates[marker])
                if want_values is None or set(want_values) != set(got_values) or any(
                        abs(want_values[k] - got_values[k]) > 1e-6 for k in want_values):
                    problems.append(f'picker {g} {s["names"][i]!r} applies different values than '
                                    f'its BodyGen template {marker}')
                else:
                    agree += 1
        print(f'{g} picker: {len(s["names"])} presets in the menu; {agree} match their BodyGen '
              f'template exactly; default {s["names"][d] if d is not None and 0 <= d < len(s["names"]) else "?"!r}')
    # Every button and hotkey calls something that exists, with no arguments: a global of the
    # generated Silhouette:Player, or a method of Silhouette:Bridge on Silhouette.esp's 0x802.
    globals_ = set(re.findall(r'^Function (\w+)\(\) Global$', text, re.M))
    api_src = (sg.ROOT / 'papyrus/Silhouette/API.psc').read_text(encoding='utf-8')
    api_globals = set(re.findall(r'^Function (\w+)\(\) Global$', api_src, re.M))
    bridge_src = (sg.ROOT / 'papyrus/Silhouette/Bridge.psc').read_text(encoding='utf-8')
    methods = set(re.findall(r'^Function (\w+)\(\)\s*$', bridge_src, re.M))

    def callable_(a):
        if a.get('params'):
            return False
        if a.get('type') == 'CallGlobalFunction' and a.get('script') == 'Silhouette:Player':
            return a.get('function') in globals_
        if a.get('type') == 'CallGlobalFunction' and a.get('script') == 'Silhouette:API':
            return a.get('function') in api_globals
        if a.get('type') == 'CallFunction' and a.get('form') == sg.BRIDGE_FORM:
            return a.get('function') in methods
        return False

    for a in buttons:
        if not callable_(a):
            problems.append(f'MCM button calls {a} -- neither a Silhouette:Player global nor a '
                            f'Silhouette:Bridge method on {sg.BRIDGE_FORM}')
    keyfile = mcm / 'keybinds.json'
    keybinds = json.loads(keyfile.read_text(encoding='utf-8-sig'))['keybinds'] if keyfile.exists() else []
    bound = {k['id']: k['action'] for k in keybinds}
    for h in hotkeys:
        if h not in bound:
            problems.append(f'MCM hotkey {h!r} has no keybind in keybinds.json: pressing it would do nothing')
    for kid, a in bound.items():
        if not callable_(a):
            problems.append(f'keybind {kid!r} calls {a} -- not a Silhouette:Bridge method on {sg.BRIDGE_FORM}')
    # (6) Each hotkey does what its name says: the function the generator gives that id.
    meant = {kid: fn for kid, _text, fn in sg.HOTKEYS}
    for kid, fn in meant.items():
        if kid not in bound:
            problems.append(f'keybinds.json has no {kid!r} hotkey, which calls Silhouette:Bridge.{fn}')
        elif bound[kid].get('function') != fn:
            problems.append(f'keybind {kid!r} calls {bound[kid].get("function")}, not {fn}: that hotkey would do '
                            f'something else than its name says')
    for kid in bound:
        if kid not in meant:
            problems.append(f'keybinds.json binds {kid!r}, which is none of Silhouette\'s hotkeys')
    if sorted(hotkeys) != sorted(meant):
        problems.append(f'the MCM lists hotkeys {sorted(hotkeys)}, Silhouette has {sorted(meant)}')

    # The NPC page offers exactly the player's lists, and NpcChoice reads the same ids.
    for g in ('female', 'male'):
        nid = next((k for k in options if k.startswith(f'iNpc{g.capitalize()}_')), None)
        if nid is None:
            if script[g]['names']:
                problems.append(f'the NPC page has no {g} dropdown, but {len(script[g]["names"])} {g} presets '
                                f'fit: "Give them this preset" could never offer one')
            continue
        if options[nid] != script[g]['names']:
            problems.append(f'MCM {nid} lists other presets than the player picker: NpcChoice would give '
                            f'a different preset than the menu shows')
        d = defaults.get(nid)
        if d is None or not 0 <= d < len(script[g]['names']):
            problems.append(f'MCM default {nid}={d} is not an entry of the menu')
        if f'"{nid}")' not in text:
            problems.append(f'Silhouette:Player.NpcChoice does not read {nid}')
    # Every switch the menu shows, read from the menu itself: a switch added later cannot miss its default (S-73).
    for key in switches:
        if key not in defaults:
            problems.append(f'settings.ini has no default for {key}: MCM would read it as off')
    # The sentinel the bridge reads before it believes MCM at all (a key MCM never loaded reads as 0).
    if defaults.get('iDefaults:Meta') != 1:
        problems.append('settings.ini has no [Meta] iDefaults=1: the bridge would never trust MCM\'s switches, and '
                        'the settings in the menu would do nothing')


def check_manifests(groot, cat, stamp, problems):
    """(F1) Every manifest a body can be read through: named by its stamp, the stamp its build's, one file
    per stamp -- the plugin keys manifests by stamp, and a second claim to one hides the other build. And
    the current build's manifest names every preset the plugin can give. -> {stamp: manifest}"""
    folder = groot / sg.MANIFESTS
    found = {}
    for f in sorted(folder.glob('*.json')) if folder.is_dir() else []:
        try:
            doc = json.loads(f.read_text(encoding='utf-8-sig'))
            # Text that is no Unicode (a lone surrogate, written escaped): Python reads it, the plugin's parser
            # refuses the whole file (wave 5, lens 3 L8 -- tools/tests/test_parser_parity.py M13).
            json.dumps(doc, ensure_ascii=False).encode('utf-8')
            st, build = doc['stamp'], doc['build']
            templates = doc['templates']
        except (OSError, ValueError, KeyError, TypeError) as exc:
            problems.append(f'manifest {f.name}: not readable as a manifest ({exc!r}) -- the bodies of its build '
                            f'could not be named or healed')
            continue
        if type(st) is not int or not 0 < st < 1 << 24:
            problems.append(f'manifest {f.name}: stamp {st!r} is not 1 .. 2^24-1')
            continue
        if f.stem != str(st):
            problems.append(f'manifest {f.name} says it is stamp {st}: a manifest is named by its stamp, and this '
                            f'one would stand for another build than its name says')
        if not (isinstance(build, str) and re.fullmatch(r'[0-9a-f]{12}', build) and (int(build[:6], 16) or 1) == st):
            problems.append(f'manifest {f.name}: stamp {st} is not the first 24 bits of its build {build!r}')
        # Everything read below reads it as {marker: {preset, values: {morph: number}}}: one of another shape is
        # said once and left out, not read into a traceback (wave 4 L4).
        if not isinstance(templates, dict) or not all(
                isinstance(m, str) and isinstance(e, dict) and isinstance(e.get('preset'), str)
                and isinstance(e.get('values', {}), dict)
                and all(isinstance(k, str) and isinstance(v, (int, float)) and not isinstance(v, bool)
                        for k, v in e.get('values', {}).items())
                for m, e in templates.items()):
            problems.append(f'manifest {f.name}: its templates are not all a marker naming a preset and its values -- '
                            f'the bodies of its build could not be named or healed')
            continue
        # The plugin's parser (Catalog.cpp ParseManifest) reads a gender, where there is one, as exactly "female"
        # or "male", and skips the whole file on anything else (wave 5 L8).
        wrong = sorted(m for m, e in templates.items() if 'gender' in e and e['gender'] not in ('female', 'male'))
        if wrong:
            problems.append(f'manifest {f.name}: {wrong[0]} has gender {templates[wrong[0]]["gender"]!r} -- the plugin reads '
                            f'only "female" or "male" and skips the whole file, so the bodies of its build could not be '
                            f'named or healed')
            continue
        if st in found:
            problems.append(f'manifests {found[st][0].name} and {f.name} both say stamp {st}: the plugin keeps one, '
                            f'and the bodies of the other build would be named and healed by the wrong one')
            continue
        found[st] = (f, doc)
    if stamp and cat is not None:
        current = found.get(int(stamp))
        if current is not None:
            entries = {m.casefold(): e for m, e in current[1].get('templates', {}).items()}
            for p in cat.get('presets', []):
                e = entries.get(p['marker'].casefold())
                if e is None or e.get('preset') != p['name'] or e.get('gender', p['sex']) != p['sex']:
                    problems.append(f'the manifest of stamp {int(stamp)} does not name {p["name"]!r} ({p["marker"]}): '
                                    f'a {p["sex"]} body the plugin gives of this build could not be read back')
    return {st: doc for st, (_f, doc) in found.items()}


def check_esp(groot, problems):
    """(5) Silhouette.esp beside the files is this build's, byte for byte: the bridge's quest, the window's
    lists, and the refit keyword LooksMenu files every refit under (S-40)."""
    esp = groot / 'Silhouette.esp'
    if not esp.exists():
        problems.append(f'no Silhouette.esp in {groot}: the bridge, the refit keyword and the window live in it')
        return
    if esp.read_bytes() != make_esp.build():
        why = make_esp.check(esp)
        problems.append(f'{esp.name} is not the one tools/make_esp.py builds'
                        + (f': {"; ".join(why)}' if why else ' -- rebuild it (python tools/make_esp.py data/Silhouette.esp)'))


def check_weights(cat, pool_lines, problems):
    """(2b) S-65: the random presets are the pool's, and each random line lists every one of them as many
    times as its tier's weight -- repetition is how BodyGen weights (docs/bodygen-format.md)."""
    try:
        side = sg.load_pool()
    except SystemExit as exc:
        problems.append(f'pool: {exc}')
        return
    weight = {}
    for p in cat.get('presets', []):
        if not p.get('random'):
            continue
        e = side.get(p['name'].casefold())
        if e is None or e['sex'] != p['sex']:
            problems.append(f'{p["name"]!r} is a random {p["sex"]} preset outside the body pool (S-65)')
            continue
        weight[(p['sex'], p['marker'].casefold())] = e['weight']
    for n, genders, names in pool_lines:
        got = collections.Counter(t.casefold() for t in names)
        want = {m: w for (g, m), w in weight.items() if g in genders}
        wrong = sorted(f'{m} x{got.get(m, 0)} (weight {w})' for m, w in want.items() if got.get(m, 0) != w)
        if wrong:
            problems.append(f'Silhouette_morphs.ini:{n}: the random line lists {len(wrong)} template(s) other than '
                            f'their tier\'s weight (S-65): {", ".join(wrong[:4])}')


def check_lines(args, rules, templates, cat, problems):
    """(2) The random pool: each sex's lines give that sex's presets, never a zeroed one, the catalog's
    random presets exactly, and every race any line names exists in the load order. (7) The catalog's
    BodyGen tiers -- the ones the plugin leaves to BodyGen -- are exactly the lines that carry them."""
    sex_of = {p['marker'].casefold(): p['sex'] for p in (cat or {}).get('presets', [])}
    sex_of.update({t.casefold(): g for g, t in sg.PLAYER_TEMPLATE.items()})
    by_marker = {p['marker'].casefold(): p for p in (cat or {}).get('presets', [])}
    unshaped = rules_mod.UNSHAPED.casefold()
    # The pool is written first: the All lines before any rule line (the player's and the dummies' own
    # lines are not rules, wherever they stand).
    def own_line(kind):
        return kind[0] == 'form' and kind[1] == 'fallout4.esm' and (kind[2] == PLAYER_ID or kind[2] in DUMMY_IDS)
    first_rule = next((n for form, _g, n in rules
                       if (line_kind(form) or ('?',))[0] != 'all' and not own_line(line_kind(form) or ('?',))), None)
    rule_start = min([n for n, raw in engine_lines(args.dir / 'Silhouette_morphs.ini', [])
                      if raw.strip().startswith('# Rules from')] + ([first_rule] if first_rule else []), default=None)
    races, lines_tiers, pool_races, pool, pool_lines = set(), set(), {}, {}, []
    # S-86: a race with a pool of its own wears one sex's body in both tables.
    body_of = {e['race'].lower(): e['sex'] for e in (cat or {}).get('rules', {}).get('racePool', [])}
    for form, groups, n in rules:
        kind = line_kind(form)
        names = [t for grp in groups for t in grp]
        if kind is None:
            problems.append(f'Silhouette_morphs.ini:{n}: {"|".join(form)!r} names no form id LooksMenu can read')
            continue
        gender = kind[1] if kind[0] == 'all' else kind[2] if kind[0] == 'plugin' else kind[3]
        if kind[0] == 'all' and kind[2] and kind[2].lower() in body_of:
            problems.append(f'Silhouette_morphs.ini:{n}: {kind[2]} draws its bodies in the plugin (S-87) and has no line')
        for t in names:
            s = sex_of.get(t.casefold())
            if gender and s and s != gender:
                problems.append(f'Silhouette_morphs.ini:{n}: the {gender} line gives {t}, a {s} preset')
        race = kind[2] if kind[0] == 'all' else kind[3] if kind[0] == 'plugin' else None
        if race:
            races.add(race)
        blacklist = bool(names) and all(t.casefold() == unshaped for t in names)
        if kind[0] == 'all' and race and not blacklist and (rule_start is None or n < rule_start):
            for g in ([gender] if gender else ['female', 'male']):
                pool_races.setdefault(g, set()).add(race)
                pool.setdefault(g, set()).update(t.casefold() for t in names)
            pool_lines.append((n, [gender] if gender else ['female', 'male'], names))
            for t in names:
                p = by_marker.get(t.casefold())
                if p is not None and (p.get('zeroed') or not p.get('random')):
                    problems.append(f'Silhouette_morphs.ini:{n}: the random pool gives {t}, which the catalog calls '
                                    f'{"zeroed" if p.get("zeroed") else "not random"} -- a zeroed preset is never '
                                    f'handed out at random')
        # (7) The tiers BodyGen carries, as the catalog must list them. The player's and the dummies' own
        # lines are S-45's, checked with the player.
        if own_line(kind):
            continue
        if kind[0] == 'form' and blacklist:
            lines_tiers.add(('blacklistedNpcsFormID', None, kind[1], kind[2]))
        elif kind[0] == 'form' and not blacklist:
            for g in ([kind[3]] if kind[3] else ['female', 'male']):
                lines_tiers.add(('npcFormID', g, kind[1], kind[2]))
        elif kind[0] == 'plugin' and blacklist:
            for g in ([kind[2]] if kind[2] else ['female', 'male']):
                lines_tiers.add(('blacklistedPlugins', g, kind[1], None))
        elif kind[0] == 'all' and blacklist and race:
            for g in ([gender] if gender else ['female', 'male']):
                lines_tiers.add(('blacklistedRaces', g, race, None))
    if cat is None:
        return
    for g in ('female', 'male'):
        want = {p['marker'].casefold() for p in cat.get('presets', []) if p['sex'] == g and p.get('random')}
        got = pool.get(g, set())
        if got != want:
            extra, lost = sorted(got - want)[:4], sorted(want - got)[:4]
            problems.append(f'the {g} random pool is not the catalog\'s random presets: '
                            f'{"lines add " + str(extra) if extra else ""}{"; " if extra and lost else ""}'
                            f'{"lines lack " + str(lost) if lost else ""}')
        if got and pool_races.get(g, set()) != {r.lower() for r in cat['rules'].get('races', [])
                                                if r.lower() not in body_of}:
            problems.append(f'the {g} random pool is given to races {sorted(pool_races.get(g, set()))}, the catalog '
                            f'distributes to {cat["rules"].get("races")} (distributeRaces)')
    check_weights(cat, pool_lines, problems)
    cat_tiers = set()
    r = cat.get('rules', {})
    for g in ('female', 'male'):
        cat_tiers.update(('npcFormID', g, e['plugin'].lower(), e['id']) for e in r.get('npcFormID', {}).get(g, []))
        cat_tiers.update(('blacklistedPlugins', g, p.lower(), None) for p in r.get('blacklistedPlugins', {}).get(g, []))
        cat_tiers.update(('blacklistedRaces', g, x.lower(), None) for x in r.get('blacklistedRaces', {}).get(g, []))
    cat_tiers.update(('blacklistedNpcsFormID', None, e['plugin'].lower(), e['id'])
                     for e in r.get('blacklistedNpcsFormID', []))

    def say(t):
        return f'{t[0]} {t[1] or "both"} {t[2]}' + (f' {t[3]:06X}' if isinstance(t[3], int) else '')
    for t in sorted(cat_tiers - lines_tiers, key=str):
        problems.append(f'catalog.json says BodyGen carries {say(t)}, but no line of Silhouette_morphs.ini does: '
                        f'the plugin would leave those NPCs to a rule that never runs')
    for t in sorted(lines_tiers - cat_tiers, key=str):
        problems.append(f'Silhouette_morphs.ini carries {say(t)}, which catalog.json does not list: the plugin '
                        f'would apply its own rules over what BodyGen gave them')
    # Every race a line names must be in the load order, or the line matches nobody (as resolve_races
    # refuses it in the generator).
    report = []
    found = plugin_forms.resolve(args.data, catalog.plugins_txt(), 'RACE', races, report)
    for race in sorted(races - {x.lower() for x in found} - set(rules_mod.OPTIONAL_RACES)):  # S-85: a mod's race may be absent
        problems.append(f'Silhouette_morphs.ini names race {race!r}, which no plugin in the load order defines: those '
                        f'lines match nobody')


def release_problems(cat, own, stock):
    """S-74: a public build carries the package's own presets and CBBE's and BodyTalk's stock ones, nothing
    else -- the catalog of one generated on a machine with other presets installed would ship theirs."""
    foreign = [p['name'] for p in (cat or {}).get('presets', [])
               if p['name'] not in own and p['name'].casefold() not in stock]
    if not foreign:
        return []
    more = f' and {len(foreign) - 8} more' if len(foreign) > 8 else ''
    return [f'a release would carry presets that are neither Silhouette\'s own nor CBBE\'s or BodyTalk\'s stock: '
            f'{", ".join(foreign[:8])}{more} -- regenerate with silhouette_gen.py --write --release (S-74)']


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--data', type=pathlib.Path, default=sg.DEFAULT_DATA)
    ap.add_argument('--built', type=pathlib.Path, action='append', default=None,
                    help='a folder BodySlide built into (repeatable), searched before Data')
    ap.add_argument('--dir', type=pathlib.Path,
                    default=sg.ROOT / 'data/F4SE/Plugins/F4EE/BodyGen/Loose',
                    help='the folder holding Silhouette_templates.ini -- or a mod folder / Data '
                         'folder above it, where F4SE/Plugins/F4EE/BodyGen/Loose is looked for')
    ap.add_argument('--psc', type=pathlib.Path, default=sg.ROOT / 'papyrus/Silhouette/Player.psc',
                    help='the generated picker script source to check against the menu')
    ap.add_argument('--release', action='store_true',
                    help='a public build (S-74): read the presets as silhouette_gen.py --release does, and fail any '
                         'catalog preset that is neither the package\'s own nor CBBE\'s or BodyTalk\'s stock')
    args = ap.parse_args()
    sg.reconfigure_output()
    roots = sg.built_roots(args)
    body_file = {b: base_body.locate(roots, f'Meshes/Actors/Character/CharacterAssets/{b}.nif')
                 for b in sg.BODIES.values()}
    for b, f in body_file.items():
        if f is None or not f.with_suffix('.tri').exists():
            raise SystemExit(f'no {b}.nif + .tri in {", ".join(map(str, roots))}')
        print(f'{b}: {f.parent}')

    # A mod folder or Data given instead of the BodyGen folder itself (the first plumbing pass
    # crashed on exactly that): look below it before giving up with a sentence, not a traceback.
    if not (args.dir / 'Silhouette_templates.ini').exists():
        below = args.dir / 'F4SE/Plugins/F4EE/BodyGen/Loose'
        if (below / 'Silhouette_templates.ini').exists():
            args.dir = below
        else:
            raise SystemExit(f'no Silhouette_templates.ini in {args.dir} or in {below}')
    print(f'checking {args.dir}')
    groot = args.dir.parent.parent.parent.parent.parent

    problems = []
    templates = parse_templates(args.dir / 'Silhouette_templates.ini', problems)
    rules = parse_morphs(args.dir / 'Silhouette_morphs.ini', templates, problems)
    print(f'{len(templates)} templates parse, {len(rules)} rules')

    # A runtime state in a template lands in the unkeyed layer, where nothing ever takes it
    # away again: a permanent erection, a permanently opened body (decision S-16). In any case:
    # "erection" is Erection to LooksMenu.
    for t, sets in templates.items():
        states = sorted({m for s in sets for sel in s for m, _, _ in sel if m.casefold() in STATES})
        if states:
            problems.append(f'template {t} sets {", ".join(states)}: a runtime state baked into a body for good')
        shaft = sorted({m for s in sets for sel in s for m, _, _ in sel if m.casefold() in SHAFT})
        if shaft:
            problems.append(f'template {t} sets {", ".join(shaft)}: the shaft is never part of a body (S-29)')
        owned = sorted({m for s in sets for sel in s for m, _, _ in sel if m.casefold() in ANATOMY})
        if owned:
            problems.append(f'template {t} sets {", ".join(owned)}: fo4-anatomy\'s build slider, never part of a body '
                            f'-- its default is baked into the base, and a value here would add to it (S-62)')

    # The catalog the plugin reads, and what both files' headers say about the run that wrote them.
    cfile = groot / 'F4SE/Plugins/Silhouette/catalog.json'
    cat = None
    if not cfile.exists():
        problems.append(f'no catalog.json ({cfile}): Silhouette.dll would refuse to start')
    else:
        try:
            cat = json.loads(cfile.read_text(encoding='utf-8-sig'))
        except ValueError as exc:
            problems.append(f'catalog.json is not JSON the plugin can read ({exc})')
    headers = {}
    for name in ('Silhouette_templates.ini', 'Silhouette_morphs.ini'):
        head = (args.dir / name).read_text(encoding='ascii', errors='replace').splitlines()[:12]
        found = next((re.search(r'Build (\w+), marker stamp (\d+) \(\w+\), rules (\w+)\.', h) for h in head
                      if 'Build ' in h and 'marker stamp ' in h), None)
        if not found:
            problems.append(f'{name}: its header states no build, stamp and rules: the plugin refuses it')
            continue
        headers[name] = (found.group(1), int(found.group(2)), found.group(3))
    if cat is not None:
        # The strict checks the generator ran, run on what is on disk -- first, so nothing below reads a
        # catalog of the wrong shape.
        try:
            catalog.check(cat)
        except SystemExit as exc:
            problems.append(f'catalog.json: {exc}')
            cat = None
    if cat is not None:
        for name, (b, st, r) in headers.items():
            if (b, st, r) != (cat['build'], cat['stamp'], cat.get('rulesHash')):
                problems.append(f'{name} is build {b} stamp {st} rules {r}, catalog.json build {cat["build"]} '
                                f'stamp {cat["stamp"]} rules {cat.get("rulesHash")}: the plugin refuses the pair')
        # The hash names what the rules ARE: recomputed from the lines LooksMenu reads and the catalog the
        # plugin reads, a file edited after the generator wrote them no longer agrees with its header.
        lines = [text for _n, text in engine_lines(args.dir / 'Silhouette_morphs.ini', [])]
        if catalog.rules_hash(cat, lines) != cat.get('rulesHash'):
            problems.append('catalog.json or Silhouette_morphs.ini changed after the generator wrote them: their rules '
                            'no longer hash to the rules the headers name -- run the generator again')
        for p in cat.get('presets', []):
            bad = sorted(m for m in p.get('values', {}) if m.casefold() in NEVER)
            if bad:
                problems.append(f'catalog.json preset {p["name"]!r} carries {", ".join(bad)}: never part of a body '
                                f'(S-16, S-29, S-62)')
        # (1) Everything the generator never writes into a body, the plugin must refuse too: a catalog whose
        # neverInBody lost a name would let a body keep it, and heal nothing (S-16, S-29).
        for s in ('female', 'male'):
            listed = {m.casefold() for m in cat['neverInBody'][s]}
            lost = [m for m in sg.NEVER_IN_BODY if m.casefold() not in listed]
            if lost:
                problems.append(f'catalog.json neverInBody ({s}) lacks {", ".join(lost)}: the plugin would let a body '
                                f'hold {"it" if len(lost) == 1 else "them"} and never heal it (S-16, S-29)')

    # ---- which templates each gender's pool holds, and what the player gets.
    # LooksMenu lets a later line overwrite an earlier one per NPC, so the player's
    # table entry is whatever the LAST line naming them says.
    def sets_nothing(t):
        vals = fixed_values(templates[t])
        return vals is not None and not any(vals.values())

    # Every marker carries the build's stamp (its value); the manifest of that
    # stamp says what each marker means.
    stamps = {fixed_values(v).get(k) for k, v in templates.items()
              if fixed_values(v) and fixed_values(v).get(k)}
    stamp = next(iter(stamps)) if len(stamps) == 1 else None
    if len(stamps) != 1:
        problems.append(f'markers carry {len(stamps)} different stamps ({sorted(stamps)[:4]}): one build '
                        f'writes one stamp')
    manifests = check_manifests(groot, cat, stamp, problems)
    manifest = None
    if stamp:
        manifest = manifests.get(int(stamp))
        if manifest is None:
            problems.append(f'no manifest for stamp {int(stamp)} ({groot / sg.MANIFESTS}): NPCs rolled by these files '
                            f'could never be interpreted')
        else:
            print(f'stamp {int(stamp)}, build {manifest.get("build")}, {manifest.get("mode")}, '
                  f'{len(manifest.get("templates", {}))} templates in its manifest; {len(manifests)} manifests in all')
            # Every entry is what the plugin will believe a body of this build IS: the catalog's preset of
            # the same marker, value for value -- the heal (S-29) reads its morphs from here.
            if cat is not None:
                if manifest.get('build') != cat['build']:
                    problems.append(f'the manifest of stamp {int(stamp)} is build {manifest.get("build")}, the catalog '
                                    f'{cat["build"]}')
                by_marker = {p['marker'].casefold(): p for p in cat.get('presets', [])}
                for marker, entry in manifest.get('templates', {}).items():
                    cp = by_marker.get(marker.casefold())
                    bad = sorted(m for m in entry.get('values', {}) if m.casefold() in NEVER)
                    if bad:
                        problems.append(f'manifest {marker}: carries {", ".join(bad)}, never part of a body (S-16, S-29, S-62)')
                    if cp is None or cp['name'] != entry.get('preset') or not same_values(entry.get('values'), cp['values']):
                        problems.append(f'manifest {marker}: not the catalog\'s preset of that marker -- the plugin would '
                                        f'name or heal bodies of this build wrongly')

    handed_out = set()          # every template any line can give an NPC
    # (3) What the player and the two character-creation dummies get: the LAST line that reaches each,
    # their ids read as numbers the way LooksMenu reads them (000007 is the player too).
    reach = {}
    for form, groups, n in rules:
        for grp in groups:
            if len(grp) > 1 and any(sets_nothing(t) for t in grp):
                problems.append(f'morphs line {n}: a template that sets nothing shares a choice with '
                                f'others -- an NPC that rolls it is rolled again on the next load, and '
                                f'again, until it lands on one that sets something')
            handed_out.update(t for t in grp if not sets_nothing(t))
        kind = line_kind(form)
        if kind is None:
            continue
        hit = []
        # All|G|HumanRace and Fallout4.esm|All|G|HumanRace reach the player record and both dummies (all
        # HumanRace, no template), Fallout4.esm|7 the player, Fallout4.esm|A7D35 / A7D34 a dummy.
        if kind[0] == 'all' and kind[2] == 'humanrace' or kind[0] == 'plugin' and kind[1] == 'fallout4.esm' and kind[3] == 'humanrace':
            g = kind[1] if kind[0] == 'all' else kind[2]
            for x in ([g] if g else ['female', 'male']):
                hit += [('player', x), ('dummy', x)]
        elif kind[0] == 'form' and kind[1] == 'fallout4.esm' and kind[2] == PLAYER_ID:
            hit += [('player', x) for x in ([kind[3]] if kind[3] else ['female', 'male'])]
        elif kind[0] == 'form' and kind[1] == 'fallout4.esm' and kind[2] in DUMMY_IDS:
            x = DUMMY_IDS[kind[2]]
            if kind[3] in (None, x):
                hit.append(('dummy', x))
        for h in hit:
            reach[h] = (n, groups)
    player = {g: reach[('player', g)] for g in ('female', 'male') if ('player', g) in reach}
    player_templates = set()
    for g in ('female', 'male'):
        if g not in player:
            continue
        n, groups = player[g]
        options = [t for grp in groups for t in grp]
        if len(groups) != 1 or len(options) != 1:
            problems.append(f'morphs line {n}: a {g} player is RANDOMISED among {len(options)} templates')
            continue
        player_templates.add(options[0])
        split = split_template(templates[options[0]])
        vals = split[0] if split else None
        markers = [k for k in (vals or {}) if k.startswith('Silhouette_')]
        default = next((p for p in (cat or {}).get('presets', []) if p['sex'] == g
                        and p['name'] == (cat or {}).get('player', {}).get(g)), None)
        if vals is None:
            problems.append(f'morphs line {n}: the {g} player template is not fixed-valued')
        elif split[1]:
            problems.append(f'morphs line {n}: the {g} player template rolls {", ".join(r[0] for r in split[1])} '
                            f'-- the player is never randomised (S-45)')
        elif not any(vals.values()):
            print(f'{g} player: {options[0]}, the bare body (no preset fits fully) (line {n})')
        elif len(markers) != 1 or vals[markers[0]] != stamp:
            problems.append(f'morphs line {n}: the {g} player template has no marker and would re-roll')
        elif default is None or default['marker'] != markers[0] or not same_values(
                default['values'], {k: v for k, v in vals.items() if k != markers[0]}):
            problems.append(f'morphs line {n}: the {g} player template is not the catalog\'s player default '
                            f'{(cat or {}).get("player", {}).get(g)!r} exactly')
        else:
            print(f'{g} player: {options[0]} = {default["name"]!r}, no ranges (line {n})')
        d = reach.get(('dummy', g))
        mine = options[0]
        if d is None:
            problems.append(f'no line for the {g} character-creation dummy: a new game would clone its '
                            f'RANDOM roll onto the player')
        elif [t for grp in d[1] for t in grp] != [mine]:
            problems.append(f'morphs line {d[0]}: the last line reaching the {g} dummy gives '
                            f'{[t for grp in d[1] for t in grp]}, the player {mine!r} -- a new game would clone the '
                            f'dummy\'s')

    # (2) + (7): the pool, every race, and the catalog's BodyGen tiers against the lines.
    check_lines(args, rules, templates, cat, problems)
    # (5) Silhouette.esp.
    check_esp(groot, problems)

    # ---- every template handed out must be fixed-valued and carry its own marker
    tris = {g: base_body.read_tri(body_file[b].with_suffix('.tri')) for g, b in sg.BODIES.items()}
    morphs_of = {g: set().union(*t.values()) for g, t in tris.items()}
    # The package's own presets (the body pool, S-65) first, then the game's -- as the generator reads them.
    presets = sg.read_all_presets([groot / sg.PRESETS, args.data / sg.PRESETS], release=args.release)
    if args.release:
        own = {p['name'] for p in sg.read_presets(groot / sg.PRESETS)}
        problems.extend(release_problems(cat, own, sg.release_stock()))
    # The markers exactly as the generator derived them: from the manifests beside these files and in the
    # game's Data (L4 F2, wave 4 L6) -- the one folder list the generator reads too (wave 5). A manifest only
    # Data holds that cannot be read is said and skipped, as the generator does: the package is not at fault.
    notes = []
    try:
        history = sg.manifest_history(*sg.manifest_folders(groot, args.data), notes=notes)
    except SystemExit as exc:
        problems.append(f'manifests: {exc}')
        history = {}
    for n in notes:
        print(f'note: {n}')
    sg.assign_markers(presets, history)
    for p in presets:
        p.update(sg.classify(p, morphs_of['female'], morphs_of['male']))
    by_template = {sg.template_name(p): p for p in presets}
    cat_by_marker = {p['marker']: p for p in (cat or {}).get('presets', [])}
    want_ranges = {g: [(v['morph'], v['low'], v['high']) for v in (cat or {}).get('variety', {}).get(g, [])]
                   for g in ('female', 'male')}
    pool = {'female': [], 'male': []}
    # The player's own templates are the player's and the dummies' (checked above), never an NPC's roll.
    for t in sorted(handed_out - player_templates - set(sg.PLAYER_TEMPLATE.values())):
        split = split_template(templates[t])
        vals = split[0] if split else None
        p = by_template.get(t)
        g = p['gender'] if p else None
        cp = cat_by_marker.get(t)
        if split and g in want_ranges and cat is not None:
            got = split[1]
            if not same_ranges(got, want_ranges[g]):
                problems.append(f'{t}: rolls {[r[0] for r in got]}, catalog.json says a {g} body rolls '
                                f'{[r[0] for r in want_ranges[g]]} (S-17, S-21)')
            elif not split[2]:
                problems.append(f'{t}: a range comes before a fixed value, so the preset\'s own value would '
                                f'win over the roll (S-21)')
        if vals is not None and cat is not None and (cp is None or not same_values(
                cp['values'], {k: v for k, v in vals.items() if k != t})):
            problems.append(f'{t}: catalog.json gives this body other values than BodyGen does -- a body the '
                            f'plugin gives would differ from the same preset rolled by BodyGen')
        if vals is None:
            problems.append(f'{t}: not a single fixed-value set; cannot verify')
        elif vals.get(t) != stamp:
            problems.append(f'{t}: no marker "{t}@<stamp>" -- a roll that sets nothing re-rolls every load')
        elif manifest is not None and not same_values(manifest.get('templates', {}).get(t, {}).get('values'),
                                                      {k: v for k, v in vals.items() if k != t}):
            problems.append(f'{t}: its values differ from the manifest of stamp {int(stamp)}')
        elif p is None or p['gender'] not in pool:
            problems.append(f'{t}: no preset of that name to compare with')
        elif t in morphs_of[p['gender']]:
            problems.append(f'{t}: the marker names a real morph and would move the body')
        else:
            pool[p['gender']].append(t)

    # ---- build every body and compare it with its preset

    worst = {}
    for g, body in sg.BODIES.items():
        own = [p for p in presets if p['gender'] == g and p['kind'] != 'empty']
        base = sg.judge_base(base_body.measure(args.data, body, own, roots))
        print(f'\n{g}: {sg.describe(base)}')
        if base['set'] is None:
            problems.append(f'{g}: base body cannot be measured, so nothing can be verified')
            continue
        ref_all = base_body.read_shapes(args.data / 'Tools/BodySlide/ShapeData'
                                        / base['set']['data_folder'] / base['set']['source_file'])
        built_all = base_body.read_shapes(body_file[body])
        shapes = [n for n in built_all if n in ref_all and n in tris[g]]
        rows = []
        for t in pool[g]:
            p = by_template.get(t)
            vals = fixed_values(templates[t])
            if p is None or vals is None:
                problems.append(f'{t}: no preset of that name to compare with')
                continue
            # the BODY a preset describes: a runtime state or the shaft it happens to set is not part
            # of it (S-16, S-29) -- and a slider fo4-anatomy's build owns is the base's, at the value the
            # build baked in, whatever the preset says (S-62)
            target = {m: v for m, v in base_body.resolve(p, base['set']).items() if not sg.never_in_body(m)}
            e_max = e_rms = n_max = n_rms = 0.0
            for s in shapes:
                shown = apply(built_all[s], vals, tris[g][s])
                meant = apply(ref_all[s], {**target, **base['owned']}, tris[g][s])
                naive = apply(built_all[s], target, tris[g][s])
                a, b = error(shown, meant)
                c, d = error(naive, meant)
                e_max, e_rms = max(e_max, a), max(e_rms, b)
                n_max, n_rms = max(n_max, c), max(n_rms, d)
            rows.append((e_rms, e_max, n_rms, n_max, t, p['name']))
        rows.sort(reverse=True)
        print(f'  {"template":44} {"max":>7} {"rms":>7}   uncompensated max / rms')
        for e_rms, e_max, n_rms, n_max, t, name in rows[:6]:
            print(f'  {t[:44]:44} {e_max:7.4f} {e_rms:7.4f}   {n_max:7.3f} / {n_rms:5.3f}')
        if len(rows) > 6:
            print(f'  ... {len(rows) - 6} more, all better than the rows above')
        if rows:
            worst[g] = rows[0]
            bad = [r for r in rows if r[1] > MAX_ERROR or r[0] > RMS_ERROR]
            # Every body off by exactly the uncompensated error means one cause, not
            # 58: absolute files on a base that still has a preset baked in.
            if base['status'] == 'preset' and bad and all(abs(r[0] - r[2]) < 1e-3 for r in bad):
                problems.append(f'{g}: all {len(bad)} bodies land rms {bad[0][0]:.3f} off because {body} '
                                f'still has "{base["preset"]}" baked in and these files are absolute. '
                                f'Rebuild {body} zeroed (the generator prints how), then verify again.')
                bad = []
            for r in bad:
                problems.append(f'{r[4]}: lands {r[1]:.3f} units (rms {r[0]:.4f}) off its preset')
            print(f'  {len(rows)} bodies built; worst lands {rows[0][1]:.4f} units off (rms {rows[0][0]:.4f}); '
                  f'uncompensated, the average NPC would be off by rms '
                  f'{sum(r[2] for r in rows) / len(rows):.3f}')

    # ---- the player picker: the MCM menu, its defaults and the generated script
    # must agree with each other and with the templates above.
    check_picker(args, templates, player, problems, stamp, cat)

    print()
    if problems:
        print(f'FAIL - {len(problems)} problem(s):')
        for pr in problems:
            print(f'  {pr}')
        return 1
    # Only what was checked above (wave 4 L7): the plugin names and form ids on a line are NOT looked up.
    print('PASS - LooksMenu parses every template and line, every template a line names exists, and every race a '
          'line names is in the load order (a line\'s plugin names and form ids are not looked up); every roll is '
          'permanent; the player and the character-creation dummies get the default and are never rolled; no '
          'template or catalog preset holds a runtime state, the shaft or fo4-anatomy\'s build slider, and the '
          'catalog and the picker script list all three for the heal; the random pool is the catalog\'s random '
          'presets, each sex its own, lists each body of the pool as many times as its tier weighs (S-65), and '
          'rolls exactly its catalog ranges; the catalog\'s BodyGen tiers are the '
          'lines\'; every manifest is its stamp\'s and the current one names every preset; the menu, the hotkeys, '
          'the picker script and the settings sentinel agree; Silhouette.esp is the one tools/make_esp.py builds; '
          'the plugin gives the same bodies BodyGen does, and every body lands on its preset.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
