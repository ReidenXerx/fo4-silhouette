"""S-65: NPCs are drawn from Silhouette's own body pool, a "Plain" body most often, a "Fine" one rarely.

The committed pool gives the owner's odds; the random line repeats each body by its tier's weight; the pool's
presets are read before Data's; the verifier refuses a line that weights a body wrongly and a random preset
from outside the pool; and every body still measures as its tier against the installed body."""
import collections
import json
import pathlib
import sys
import unittest

import support
import silhouette_gen as sg
import verify_bodygen

sys.path.insert(0, str(support.TOOLS / 'pool'))
import generate  # noqa: E402

POOL_XML = support.PACKAGE / sg.PRESETS / 'Silhouette Pool.xml'


def preset_file(folder, name, value):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f'{name}-{value}.xml').write_text(
        f'<SliderPresets><Preset name="{name}" set="CBBE Body"><Group name="CBBE"/>'
        f'<SetSlider name="Breasts" size="big" value="{value}"/></Preset></SliderPresets>', encoding='utf-8')


class CommittedPool(unittest.TestCase):
    """What ships: the sidecar and the XML are of one run, and give the owner's 70 / 22 / 8."""

    @classmethod
    def setUpClass(cls):
        cls.pool = sg.load_pool()
        with support.Scratch() as root:          # the pool's file alone: the folder holds the characters too
            (root / POOL_XML.name).write_bytes(POOL_XML.read_bytes())
            cls.xml = {p['name']: p for p in sg.read_presets(root)}

    def test_the_odds_per_sex(self):
        for sex in ('female', 'male'):
            entries = collections.Counter()
            for e in self.pool.values():
                if e['sex'] == sex:
                    entries[e['tier']] += e['weight']
            total = sum(entries.values())
            share = {t: 100 * n / total for t, n in entries.items()}
            for tier, want in (('middle', 70), ('ugly', 22), ('beautiful', 8)):
                self.assertAlmostEqual(share[tier], want, delta=1.0, msg=f'{sex} {tier}: {share}')

    def test_the_xml_holds_every_body_of_the_sidecar_with_its_values(self):
        side = json.loads(sg.POOL_SIDECAR.read_text(encoding='utf-8'))['presets']
        self.assertEqual(set(side), set(self.xml))
        for name, p in side.items():
            got = {s: round(v * 100) for s, v in self.xml[name]['sliders'].items() if v}
            self.assertEqual(got, {s: v for s, v in p['values'].items() if v}, name)

    def test_no_body_names_a_state_the_shaft_or_the_anatomy_slider(self):
        for name, p in self.xml.items():
            self.assertEqual([s for s in p['sliders'] if sg.never_in_body(s)], [], name)


class RandomLine(unittest.TestCase):
    def test_each_body_repeated_by_its_weight(self):
        pool = {'a': {'weight': 3}, 'b': {'weight': 1}}
        rows = [('Silhouette_A', {}, {'name': 'A'}), ('Silhouette_B', {}, {'name': 'B'})]
        self.assertEqual(sg.random_line_names(rows, pool), ['Silhouette_A'] * 3 + ['Silhouette_B'])

    def test_the_pools_own_presets_come_first(self):
        with support.Scratch() as root:
            preset_file(root / 'repo', 'Plain F01', 10)
            preset_file(root / 'data', 'plain f01', 90)          # a stale deployed copy, in another case
            preset_file(root / 'data', 'Rocket', 50)
            got = {p['name']: p['sliders']['Breasts'] for p in sg.read_all_presets([root / 'repo', root / 'data'])}
            self.assertEqual(got, {'Plain F01': 0.1, 'Rocket': 0.5})


class VerifierWeights(unittest.TestCase):
    """check_weights against the committed sidecar: two real pool bodies, one of each weight."""

    def setUp(self):
        pool = sg.load_pool()
        self.plain = next(e for e in pool.values() if e['sex'] == 'female' and e['weight'] == 3)['name']
        self.fine = next(e for e in pool.values() if e['sex'] == 'female' and e['weight'] == 1)['name']
        self.cat = {'presets': [{'name': n, 'sex': 'female', 'marker': sg.plain_marker(n), 'random': True}
                                for n in (self.plain, self.fine)]}
        self.p, self.f = sg.plain_marker(self.plain), sg.plain_marker(self.fine)

    def problems(self, names, cat=None):
        out = []
        verify_bodygen.check_weights(cat or self.cat, [(5, ['female'], names)], out)
        return out

    def test_the_right_counts_pass(self):
        self.assertEqual(self.problems([self.p] * 3 + [self.f]), [])

    def test_a_plain_copy_short_fails(self):
        self.assertTrue(self.problems([self.p] * 2 + [self.f]))

    def test_a_fine_copy_extra_fails(self):
        self.assertTrue(self.problems([self.p] * 3 + [self.f] * 2))

    def test_a_random_preset_outside_the_pool_fails(self):
        cat = {'presets': self.cat['presets'] + [{'name': 'Rocket Bomb', 'sex': 'female', 'marker': 'Silhouette_Rocket',
                                                  'random': True}]}
        out = self.problems([self.p] * 3 + [self.f, 'Silhouette_Rocket'], cat)
        self.assertTrue(any('outside the body pool' in s for s in out), out)


class BustFloor(unittest.TestCase):
    """Owner poll 2026-09-24: no woman's bust goes below a small but real one -- the physics folded a concave
    chest. Breasts at least 0 and BreastsSmall at most 30, in what ships and in what the next run can draw."""

    def test_no_committed_woman_is_below_the_floor(self):
        side = json.loads(sg.POOL_SIDECAR.read_text(encoding='utf-8'))['presets']
        for name, p in side.items():
            if p['sex'] == 'female':
                with self.subTest(name):
                    self.assertGreaterEqual(p['values'].get('Breasts', 0), 0)
                    self.assertLessEqual(p['values'].get('BreastsSmall', 0), 30)

    def test_no_named_woman_is_below_the_floor(self):
        # Owner poll 2026-09-25 (seventh wave): the named people's hand-set bodies are held to it too.
        side = json.loads(sg.CHARACTERS_SIDECAR.read_text(encoding='utf-8'))['characters']
        for name, c in side.items():
            if c['sex'] == 'female':
                with self.subTest(name):
                    self.assertGreaterEqual(c['values'].get('Breasts', 0), 0)
                    self.assertLessEqual(c['values'].get('BreastsSmall', 0), 30)

    def test_no_female_archetype_can_draw_below_the_floor(self):
        import archetypes
        for tier, kinds in archetypes.FEMALE.items():
            for kind, (_, ranges) in kinds.items():
                with self.subTest(f'{tier} {kind}'):
                    self.assertGreaterEqual(ranges.get('Breasts', (0, 0))[0], 0)
                    self.assertLessEqual(ranges.get('BreastsSmall', (0, 0))[1], 30)


class InsideTheRange(unittest.TestCase):
    """S-96 (alasdairn 2026-10-25): Cathy shipped a chest folded into a shelf on stick limbs. Every shipped value is
    inside its slider's 0..100, and a woman's sag, flatness and thin limbs stay under archetypes.CAP_F."""

    def check(self, name, sex, values):
        import archetypes
        caps = archetypes.CAP_F if sex == 'female' else {}
        for s, v in values.items():
            with self.subTest(f'{name} {s}'):
                self.assertGreaterEqual(v, archetypes.LIMIT[0])
                self.assertLessEqual(v, caps.get(s, archetypes.LIMIT[1]))

    def test_every_pool_and_faction_body(self):
        for sidecar in (sg.POOL_SIDECAR, sg.POOL_SIDECAR.parent / 'factions.json'):
            for name, p in json.loads(sidecar.read_text(encoding='utf-8'))['presets'].items():
                self.check(name, p['sex'], p['values'])

    def test_every_named_body(self):
        for name, c in json.loads(sg.CHARACTERS_SIDECAR.read_text(encoding='utf-8'))['characters'].items():
            self.check(name, c['sex'], c['values'])

    def test_no_archetype_can_draw_outside(self):
        import archetypes
        for sex, tiers in archetypes.ARCHETYPES.items():
            for tier, kinds in tiers.items():
                for kind, (_, ranges) in kinds.items():
                    self.check(f'{sex} {tier} {kind}', sex, {f'{s} lo': lo for s, (lo, _hi) in ranges.items()})
                    self.check(f'{sex} {tier} {kind}', sex, {s: hi for s, (_lo, hi) in ranges.items()})


@support.needs_data
class StillItsTier(unittest.TestCase):
    """generate.py --check: every committed body, rebuilt from the installed body, measures as its tier."""

    def test_every_body_measures_as_its_tier(self):
        self.assertEqual(generate.check(support.game_data()), 0)


class Characters(unittest.TestCase):
    """S-66: the named people's own bodies, bound to their records under the user's own rules."""

    @classmethod
    def setUpClass(cls):
        cls.side = json.loads(sg.CHARACTERS_SIDECAR.read_text(encoding='utf-8'))['characters']

    def test_a_users_rule_for_the_record_wins_however_it_is_written(self):
        cfg = {'npcFormID': {'Fallout4.esm': {'0x00002f1e': ['My Piper']}}}
        bound, kept = sg.merge_characters(cfg)
        self.assertEqual(cfg['npcFormID']['Fallout4.esm']['0x00002f1e'], ['My Piper'])
        self.assertNotIn('002F1E', cfg['npcFormID']['Fallout4.esm'])
        self.assertEqual(kept, ['Piper Wright (Fallout4.esm 002F1E)'])
        self.assertEqual(bound, sum(len(c['forms']) for c in self.side.values()) - 1)

    def test_every_record_has_its_line_in_the_package(self):
        lines = (support.PACKAGE / support.LOOSE / 'Silhouette_morphs.ini').read_text(encoding='utf-8').splitlines()
        for name, c in self.side.items():
            label = 'Female' if c['sex'] == 'female' else 'Male'
            for plugin, _edid, fid in c['forms']:
                self.assertIn(f'{plugin}|{fid}|{label}={sg.plain_marker(name)}', lines, name)

    def test_the_xml_holds_every_character_with_its_values_and_none_is_random(self):
        xml = {p['name']: p for p in sg.read_presets(POOL_XML.parent) if p['name'] in self.side}
        self.assertEqual(set(xml), set(self.side))
        for name, c in self.side.items():
            got = {s: round(v * 100) for s, v in xml[name]['sliders'].items() if v}
            self.assertEqual(got, {s: v for s, v in c['values'].items() if v}, name)
        cat = json.loads((support.PACKAGE / support.CAT).read_text(encoding='utf-8-sig'))
        random = {p['name'] for p in cat['presets'] if p.get('random')}
        self.assertEqual(random & set(self.side), set())


@support.needs_data
class CharactersStillTheirRecords(unittest.TestCase):
    def test_every_record_is_where_it_was(self):
        import characters
        self.assertEqual(characters.check(support.game_data()), 0)


if __name__ == '__main__':
    unittest.main()
