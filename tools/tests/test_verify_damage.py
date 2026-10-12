"""tools/verify_bodygen.py FAILs a damaged package, and says why -- never a PASS, never a traceback.

Each case damages a copy of the committed package (data/ and papyrus/Silhouette/Player.psc) the way a hand
edit, a bad merge, a stale copy or a generator bug would, and runs the verifier on it as the deploy and release
scripts do. Two kinds of damage: a file changed after the generator wrote it, and the same change written
consistently -- the rules hash re-stamped in the catalog and both BodyGen headers -- which only the checks
that do not lean on the hash can catch. The untouched copy must PASS first, or no verdict means anything
(GP-4). The verifier builds every body from the game's Data: without it the cases are skipped.
"""
import concurrent.futures
import json
import re
import shutil
import subprocess
import sys
import unittest

import support
import catalog
import make_esp

LOOSE = support.LOOSE


class NotHere(Exception):
    """The committed package has nothing this case can damage (a preset kind it does not hold)."""


def data(d, rel=''):
    return d / 'data' / rel


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def edit_json(path, fn):
    doc = read_json(path)
    fn(doc)
    path.write_text(json.dumps(doc, indent=1) + '\n', encoding='utf-8')


def edit_text(path, fn):
    """A BodyGen file is cp1252 with CRLF, and the script source UTF-8: each is written back as it was."""
    encoding = 'cp1252' if path.suffix == '.ini' else 'utf-8'
    raw = path.read_bytes().decode(encoding)
    new = fn(raw)
    if new == raw:
        raise AssertionError(f'the damage did not change {path.name}')
    path.write_bytes(new.encode(encoding))


def restamp(d):
    """What a generator bug would write: the rules hash recomputed from the files as they now are."""
    cpath = data(d, support.CAT)
    cat = read_json(cpath)
    lines = [ln for ln in data(d, LOOSE / 'Silhouette_morphs.ini').read_bytes().decode('cp1252').split('\r\n') if ln]
    old, new = cat['rulesHash'], catalog.rules_hash(cat, lines)
    cat['rulesHash'] = new
    cpath.write_text(json.dumps(cat, indent=1) + '\n', encoding='utf-8')
    for name in ('Silhouette_templates.ini', 'Silhouette_morphs.ini'):
        p = data(d, LOOSE / name)
        p.write_bytes(p.read_bytes().replace(f'rules {old}.'.encode(), f'rules {new}.'.encode()))


class Package:
    """What the cases pick from the committed package: its stamp, an old manifest, a preset of each kind."""
    def __init__(self):
        cat = read_json(support.PACKAGE / support.CAT)
        self.stamp = str(cat['stamp'])
        self.race = cat['rules']['races'][0]
        olds = sorted(f.stem for f in (support.PACKAGE / support.MANIFESTS).glob('*.json') if f.stem != self.stamp)
        self.old = olds[0] if olds else None
        self.female = next((p for p in cat['presets'] if p['sex'] == 'female' and p['random']), None)
        self.male = next((p for p in cat['presets'] if p['sex'] == 'male' and p['random']), None)
        self.zeroed = next((p for p in cat['presets'] if p['sex'] == 'female' and p['zeroed']), None)

    def need(self, *what):
        for w in what:
            if getattr(self, w) is None:
                raise NotHere(f'the committed package has no {w} preset or manifest to damage')


PKG = Package()
MAN = support.MANIFESTS
MCM = support.MCM
CASES = {}      # name -> (damage(d), words one of verify's problems must hold -- a function of PKG)


def case(words):
    def deco(fn):
        CASES[fn.__name__] = (fn, words)
        return fn
    return deco


# ---------------------------------------------------------------- a file changed after the generator wrote it
@case(lambda: f'manifest {PKG.old}.json says it is stamp {PKG.stamp}')
def r4_old_manifest_claims_the_current_stamp(d):
    PKG.need('old')
    edit_json(data(d, MAN / f'{PKG.old}.json'), lambda doc: doc.update(stamp=int(PKG.stamp)))


@case(lambda: f'does not name {(PKG.zeroed or PKG.female)["name"]!r}')
def r5_current_manifest_misses_a_preset(d):
    marker = (PKG.zeroed or PKG.female)['marker']
    edit_json(data(d, MAN / f'{PKG.stamp}.json'), lambda doc: doc['templates'].pop(marker))


@case(lambda: 'Silhouette:Player.ApplyDefault() gives the female player index')
def r6_script_default_index_moved(d):
    edit_text(d / 'Player.psc', lambda s: re.sub(r'(    If female\r?\n        index = )(\d+)',
                                                 lambda m: f'{m.group(1)}{int(m.group(2)) ^ 1}', s))


@case(lambda: 'Silhouette:Player.Count(True) returns')
def r7_script_count_short(d):
    edit_text(d / 'Player.psc', lambda s: re.sub(r'(    If female\r?\n        Return )(\d+)',
                                                 lambda m: f'{m.group(1)}{int(m.group(2)) - 1}', s))


@case(lambda: 'Silhouette:Player.StateMorphs() lists')
def r8_script_heal_list_loses_the_shaft(d):
    edit_text(d / 'Player.psc', lambda s: re.sub(r'(out\[\d+\] = )"Penis Width"', r'\1"Erection"', s))


@case(lambda: 'it has no FLST 804')
def r9_esp_of_an_older_build(d):
    data(d, 'Silhouette.esp').write_bytes(support.esp_without(make_esp.build(), make_esp.HEALED_FORMID))


@case(lambda: "keybind 'next' calls PickerPrevious, not PickerNext")
def r10_hotkey_next_goes_back(d):
    def f(doc):
        for k in doc['keybinds']:
            if k['id'] == 'next':
                k['action']['function'] = 'PickerPrevious'
    edit_json(data(d, MCM / 'keybinds.json'), f)


@case(lambda: f'{PKG.female["marker"]}: rolls')
def r12_a_template_lacks_a_range(d):
    PKG.need('female')
    head = f'{PKG.female["marker"]}='

    def f(s):
        lines = s.split('\r\n')
        i = next(i for i, x in enumerate(lines) if x.startswith(head))
        lines[i] = re.sub(r', [^,@=]+@-?[0-9.]+:-?[0-9.]+', '', lines[i], count=1)
        return '\r\n'.join(lines)
    edit_text(data(d, LOOSE / 'Silhouette_templates.ini'), f)


@case(lambda: f'manifest {int(PKG.old) - 1}.json says it is stamp {PKG.old}')
def r14_a_manifest_renamed(d):
    PKG.need('old')
    m = data(d, MAN)
    (m / f'{PKG.old}.json').rename(m / f'{int(PKG.old) - 1}.json')


@case(lambda: f'manifest {PKG.stamp}.json: its templates are not all a marker naming a preset')
def c1_current_manifest_templates_a_list(d):
    edit_json(data(d, MAN / f'{PKG.stamp}.json'), lambda doc: doc.update(templates=list(doc['templates'])))


@case(lambda: f'manifest {PKG.stamp}.json: its templates are not all a marker naming a preset')
def c2_current_manifest_entry_a_string(d):
    edit_json(data(d, MAN / f'{PKG.stamp}.json'),
              lambda doc: doc['templates'].update({next(iter(doc['templates'])): 'CBBE Curvy'}))


@case(lambda: f'manifest {PKG.old}.json: its templates are not all a marker naming a preset')
def c3_old_manifest_templates_a_list(d):
    PKG.need('old')
    edit_json(data(d, MAN / f'{PKG.old}.json'), lambda doc: doc.update(templates=list(doc['templates'])))


@case(lambda: f'manifest {PKG.old} - Copy.json says it is stamp {PKG.old}')
def c4_a_backup_copy_of_a_manifest(d):
    PKG.need('old')
    shutil.copy2(data(d, MAN / f'{PKG.old}.json'), data(d, MAN / f'{PKG.old} - Copy.json'))


@case(lambda: f'manifest {PKG.stamp}.json: its templates are not all a marker naming a preset')
def c5_manifest_values_not_an_object(d):
    def f(doc):
        doc['templates'][next(iter(doc['templates']))]['values'] = [1, 2]
    edit_json(data(d, MAN / f'{PKG.stamp}.json'), f)


@case(lambda: 'iExtra=0.5: not a whole number')
def c6_a_setting_that_is_no_whole_number(d):
    # An i setting is a whole number to MCM; an f one may be 0.5 (tests/test_settings.py).
    p = data(d, MCM / 'settings.ini')
    p.write_text(p.read_text(encoding='utf-8') + 'iExtra=0.5\n', encoding='utf-8')


@case(lambda: "MCM hotkey 'next' has no keybind in keybinds.json")
def c7_keybinds_missing(d):
    data(d, MCM / 'keybinds.json').unlink()


@case(lambda: 'it has no refit keyword (KYWD 0x803)')
def c8_the_phase_1_esp(d):
    data(d, 'Silhouette.esp').write_bytes(support.esp_without(make_esp.build(), make_esp.REFIT_FORMID, make_esp.HEALED_FORMID))


@case(lambda: 'Silhouette.esp is not the one tools/make_esp.py builds -- rebuild it')
def c9_esp_saved_again_by_an_editor(d):
    # The same records, only the header's next free id changed -- what saving it in xEdit does.
    b = bytearray(make_esp.build())
    i = b.find(b'HEDR') + 6
    b[i + 8:i + 12] = (0x900).to_bytes(4, 'little')
    data(d, 'Silhouette.esp').write_bytes(bytes(b))


@case(lambda: 'settings.ini has no [Meta] iDefaults=1')
def w1_settings_sentinel_removed(d):
    edit_text(data(d, MCM / 'settings.ini'), lambda s: re.sub(r'\[Meta\]\r?\niDefaults=1\r?\n', '', s))


@case(lambda: 'settings.ini has no default for bFactionPools:General')
def w3_a_new_switch_without_its_default(d):
    # S-73: every switch of the menu needs its default, the ones added after the verifier's first list too.
    edit_text(data(d, MCM / 'settings.ini'), lambda s: re.sub(r'bFactionPools=1\r?\n', '', s))


@case(lambda: 'settings.ini has no [Meta] iDefaults=1')
def w2_settings_sentinel_zero(d):
    edit_text(data(d, MCM / 'settings.ini'), lambda s: s.replace('iDefaults=1', 'iDefaults=0'))


@case(lambda: f'manifest {PKG.stamp}.json: its templates are not all a marker naming a preset')
def c10_current_manifest_value_a_string(d):
    # A number written as text: the verifier compares the values as numbers (wave 5, lens 3 L7).
    def f(doc):
        values = next(e['values'] for e in doc['templates'].values() if e.get('values'))
        values[next(iter(values))] = '0.5'
    edit_json(data(d, MAN / f'{PKG.stamp}.json'), f)


@case(lambda: f'manifest {PKG.old}.json: its templates are not all a marker naming a preset')
def c11_old_manifest_value_a_bool(d):
    PKG.need('old')

    def f(doc):
        values = next(e['values'] for e in doc['templates'].values() if e.get('values'))
        values[next(iter(values))] = True
    edit_json(data(d, MAN / f'{PKG.old}.json'), f)


@case(lambda: 'broken.json: not readable as a manifest')
def w3_a_manifest_that_is_no_json(d):
    data(d, MAN / 'broken.json').write_text('{"stamp": 5, "templates": {', encoding='utf-8')


@case(lambda: "not the catalog's preset of that marker")
def w4_current_manifest_value_changed(d):
    PKG.need('female')

    def f(doc):
        values = doc['templates'][PKG.female['marker']]['values']
        k = next(iter(values))
        values[k] = round(values[k] + 0.25, 6)
    edit_json(data(d, MAN / f'{PKG.stamp}.json'), f)


# ---------------------------------------------------------------- the same change, written consistently
def pool_line_edit(fn):
    def edit(s):
        head = f'All|Female|{PKG.race}='
        if head not in s:
            raise NotHere(f'no {head} line')
        return fn(s, head)
    return edit


@case(lambda: f'the female line gives {PKG.male["marker"]}, a male preset')
def g1_a_male_template_in_the_female_pool(d):
    PKG.need('male')
    edit_text(data(d, LOOSE / 'Silhouette_morphs.ini'),
              pool_line_edit(lambda s, head: s.replace(head, f'{head}{PKG.male["marker"]}|', 1)))
    restamp(d)


@case(lambda: f'the random pool gives {PKG.zeroed["marker"]}, which the catalog calls zeroed')
def g2_a_zeroed_preset_in_the_random_pool(d):
    PKG.need('zeroed', 'female')
    z = PKG.zeroed['marker']

    def template(s):
        lines = s.split('\r\n')
        i = next(i for i, x in enumerate(lines) if x.startswith(f'{PKG.female["marker"]}='))
        ranges = ', '.join(e for e in lines[i].split('=', 1)[1].split(', ') if ':' in e.split('@', 1)[1])
        lines.insert(i, f'{z}={ranges}, {z}@{PKG.stamp}')
        return '\r\n'.join(lines)
    edit_text(data(d, LOOSE / 'Silhouette_templates.ini'), template)
    edit_text(data(d, LOOSE / 'Silhouette_morphs.ini'), pool_line_edit(lambda s, head: s.replace(head, f'{head}{z}|', 1)))
    edit_json(data(d, support.CAT),
              lambda doc: next(p for p in doc['presets'] if p['marker'] == z).update(random=True))
    restamp(d)


@case(lambda: 'which no plugin in the load order defines')
def g3_a_race_misspelt_in_the_pool(d):
    edit_text(data(d, LOOSE / 'Silhouette_morphs.ini'),
              pool_line_edit(lambda s, head: s.replace(head, head.replace(f'|{PKG.race}=', f'|{PKG.race}x='), 1)))
    restamp(d)


@case(lambda: 'catalog.json neverInBody (female) lacks Penis Width, TipShape')
def g6_never_in_body_loses_the_shaft(d):
    def f(doc):
        for s in ('female', 'male'):
            doc['neverInBody'][s] = [m for m in doc['neverInBody'][s] if m not in ('Penis Width', 'TipShape')]
    edit_json(data(d, support.CAT), f)
    restamp(d)


@case(lambda: 'catalog.json says BodyGen carries npcFormID female fallout4.esm 0ABC12, but no line')
def g7_the_catalog_claims_a_line_there_is_not(d):
    # An id no character's body is bound to: Piper's 002F1E and then Geneva's 002F0A were used here until each got
    # a body of her own (S-66), so this one is not a record at all -- the line is never looked up.
    edit_json(data(d, support.CAT),
              lambda doc: doc['rules']['npcFormID']['female'].append({'plugin': 'Fallout4.esm', 'id': 0xABC12}))
    restamp(d)


@case(lambda: 'catalog.json says BodyGen carries blacklistedPlugins female dlcrobot.esm, but no line')
def g9_a_blacklist_line_dropped(d):
    edit_json(data(d, support.CAT), lambda doc: doc['rules']['blacklistedPlugins']['female'].append('DLCRobot.esm'))
    restamp(d)


@case(lambda: 'rules.randomPool.female is not the female random line at its weights (S-91)')
def g9b_the_random_pool_copy_drops_a_body(d):
    # A race the config adds in game draws from this copy: one body short is a pool that is not BodyGen's.
    edit_json(data(d, support.CAT), lambda doc: doc['rules']['randomPool']['female'].pop())
    restamp(d)


@case(lambda: 'the female player template rolls')
def g10_a_player_line_with_a_padded_id_last(d):
    # LooksMenu reads the id with strtoul: 000007 is the player too.
    PKG.need('female')
    edit_text(data(d, LOOSE / 'Silhouette_morphs.ini'),
              lambda s: s + f'Fallout4.esm|000007|Female={PKG.female["marker"]}\r\n')
    restamp(d)


@case(lambda: 'the last line reaching the female dummy gives')
def g11_the_dummy_line_before_the_pool(d):
    # A later All line overwrites it: a new game clones a random roll onto the player.
    def f(s, head):
        lines = s.split('\r\n')
        line = lines.pop(next(i for i, x in enumerate(lines) if x.startswith('Fallout4.esm|0A7D35|Female=')))
        lines.insert(next(i for i, x in enumerate(lines) if x.startswith(head)), line)
        return '\r\n'.join(lines)
    edit_text(data(d, LOOSE / 'Silhouette_morphs.ini'), pool_line_edit(f))
    restamp(d)


def fresh(where):
    shutil.copytree(support.PACKAGE, where / 'data')
    shutil.copy2(support.PSC, where / 'Player.psc')
    return where


def verify(d):
    """(exit code, whole output, problem lines)"""
    p = subprocess.run([sys.executable, str(support.TOOLS / 'verify_bodygen.py'), '--data', str(support.game_data()),
                        '--dir', str(data(d, LOOSE)), '--psc', str(d / 'Player.psc'), *support.built_args()],
                       capture_output=True, text=True, encoding='utf-8', errors='replace', env=support.ENV,
                       creationflags=support.LOW_PRIORITY, timeout=900)
    out = p.stdout + p.stderr
    problems = []
    if '\nFAIL - ' in out:
        problems = [x.strip() for x in out[out.rfind('\nFAIL - '):].splitlines()[2:] if x.strip()]
    return p.returncode, out, problems


@support.needs_data
class Damage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = support.Scratch()
        cls.root = cls.scratch.__enter__()
        cls.baseline = verify(fresh(cls.root / 'd0'))

    @classmethod
    def tearDownClass(cls):
        cls.scratch.__exit__(None, None, None)

    def test_the_committed_package_passes(self):
        rc, out, problems = self.baseline
        self.assertEqual((rc, problems), (0, []), out[-3000:])
        self.assertIn('\nPASS - ', out)

    def test_every_damage_fails_with_its_reason(self):
        if self.baseline[0] != 0:
            self.skipTest('the committed package does not PASS here (test_the_committed_package_passes says why)')
        runs, skipped = {}, {}
        for n, (name, (damage, _words)) in enumerate(CASES.items()):
            d = fresh(self.root / f'c{n}')      # short: the BodyGen paths are long already
            try:
                damage(d)
            except NotHere as exc:
                skipped[name] = str(exc)
                continue
            runs[name] = d
        with concurrent.futures.ThreadPoolExecutor(support.jobs()) as pool:
            results = dict(zip(runs, pool.map(verify, runs.values())))
        for name, (_damage, words) in CASES.items():
            with self.subTest(case=name):
                if name in skipped:
                    self.skipTest(skipped[name])
                rc, out, problems = results[name]
                self.assertNotIn('Traceback', out, out[-2000:])
                self.assertEqual(rc, 1, out[-2000:])
                self.assertIn('\nFAIL - ', out)
                want = words()
                self.assertTrue(any(want in p for p in problems), f'no problem says {want!r}; verify said:\n  '
                                + '\n  '.join(problems))


if __name__ == '__main__':
    unittest.main()
