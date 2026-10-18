"""The player picker's lists in parts (owner poll 2026-09-24: lift the picker's 128 cap).

The Papyrus VM grows no array past 128 entries, by `new` or by Add, so Silhouette:Player lists each sex's
presets in parts of 128 and Locate()/At() read part p as the entries from p * 128 on. What the script lists
comes back whole and in order past 128; the verifier refuses a part over 128, a short part before the last
and a gap in the numbering -- each would lose a preset or read one as another."""
import pathlib
import re
import unittest

import support
import silhouette_gen as sg
import verify_bodygen as vb


def entry(i, sex):
    return {'marker': f'Silhouette_{sex}_{i:03d}', 'display': f'{sex} {i:03d}', 'values': [('Breasts', 0.1)]}


def picker(female, male):
    return {'female': [entry(i, 'F') for i in range(female)], 'male': [entry(i, 'M') for i in range(male)]}


def written(root, p):
    path = root / 'Player.psc'
    sg.write_papyrus(path, p, {'female': 0, 'male': 0}, 123, 'abc')
    return path


class Parts(unittest.TestCase):

    def test_the_window_never_names_a_part(self):
        # S-93: Bridge passed two parts to At/Locate/PresetOf; 275 men made three and its compile failed. It asks the
        # generated wrappers instead, which take every part the build has.
        bridge = (support.REPO / 'papyrus' / 'Silhouette' / 'Bridge.psc').read_text(encoding='utf-8')
        self.assertEqual(re.findall(r'(?:Names|Markers)\d\(|Player\.(?:At|Locate|PresetOf)\(', bridge), [])
        with support.Scratch() as root:
            text = written(root, picker(300, 5)).read_text(encoding='utf-8')
        for fn in ('String Function Joined(Bool female)', 'String Function WornBy(Actor akActor, Bool female)',
                   'Int Function IndexOf(String s, Bool female)'):
            self.assertIn(fn, text)
        self.assertIn('At(i, n0, n1, n2)', text)

    def test_three_hundred_come_back_whole_and_in_order(self):
        p = picker(300, 5)
        with support.Scratch() as root:
            s = vb.parse_picker_script(written(root, p))
        for sex in ('female', 'male'):
            self.assertEqual(s[sex]['markers'], [e['marker'] for e in p[sex]])
            self.assertEqual(s[sex]['names'], [e['display'] for e in p[sex]])
            self.assertEqual(sorted(s[sex]['apply']), list(range(len(p[sex]))))
            self.assertEqual(vb.picker_part_problems(sex, s[sex]), [])
        self.assertEqual(s['female']['parts']['names'], [128, 128, 44])
        self.assertEqual(s['male']['parts']['names'], [5, 0, 0])

    def test_the_part_count_follows_the_longer_list(self):
        for female, want in ((0, [0]), (128, [128]), (129, [128, 1]), (256, [128, 128]), (257, [128, 128, 1])):
            with self.subTest(female), support.Scratch() as root:
                s = vb.parse_picker_script(written(root, picker(female, 3)))
                self.assertEqual(s['female']['parts']['markers'], want)
                self.assertEqual(len(s['male']['parts']['markers']), len(want))

    def test_locate_and_at_count_each_part_from_its_first_entry(self):
        with support.Scratch() as root:
            text = written(root, picker(300, 5)).read_text(encoding='utf-8')
        locate = vb.script_function(text, 'Int Function Locate(String s, String[] a0, String[] a1, String[] a2) Global')
        at = vb.script_function(text, 'String Function At(Int k, String[] a0, String[] a1, String[] a2) Global')
        self.assertEqual(re.findall(r'k = (a\d)\.Find', locate), ['a0', 'a1', 'a2'])
        self.assertEqual([int(x) for x in re.findall(r'Return (\d+) \+ k', locate)], [0, 128, 256])
        self.assertEqual(re.findall(r'Return (a\d)\[k - (\d+)\]', at), [('a0', '0'), ('a1', '128'), ('a2', '256')])
        self.assertEqual([int(x) for x in re.findall(r'ElseIf k < (\d+)', at)], [128, 256, 384])

    def test_no_generated_array_is_built_past_the_limit(self):
        with support.Scratch() as root:
            text = written(root, picker(300, 5)).read_text(encoding='utf-8')
        for body in re.findall(r'String\[\] Function \w+\(\) Global\n(.*?)\nEndFunction', text, re.S):
            self.assertLessEqual(body.count('.Add('), sg.ARRAY_LIMIT)


class VerifierRefuses(unittest.TestCase):
    """Each damage the verifier must name: the script is the one thing between the menu and the body."""

    def damaged(self, edit, female=300):
        with support.Scratch() as root:
            path = written(root, picker(female, 5))
            path.write_text(edit(path.read_text(encoding='utf-8')), encoding='utf-8')
            s = vb.parse_picker_script(path)
        return vb.picker_part_problems('female', s['female'])

    def test_a_part_over_the_limit(self):
        # The first entry of part 1 moved to the end of part 0: 129 there, and the VM drops it.
        def edit(t):
            line = '    a.Add("F 128", 1)\n'
            t = t.replace(line, '', 1)
            return t.replace('    a.Add("F 127", 1)\n', '    a.Add("F 127", 1)\n' + line, 1)
        self.assertTrue(any('names parts hold [129, 127, 44]' in p for p in self.damaged(edit)), self.damaged(edit))

    def test_a_single_part_over_the_limit(self):
        # Nothing before it to be short: only the limit itself names this one.
        out = self.damaged(lambda t: t.replace('    a.Add("F 000", 1)\n', '    a.Add("F 000", 1)\n' * 2, 1), female=128)
        self.assertTrue(any('names parts hold [129]' in p for p in out), out)

    def test_a_short_part_before_the_last(self):
        out = self.damaged(lambda t: t.replace('    a.Add("F 005", 1)\n', '', 1))
        self.assertTrue(any('names parts hold [127, 128, 44]' in p for p in out), out)

    def test_a_gap_in_the_numbering(self):
        # The sizes still read right ([128, 128, 44]); only the numbering is wrong.
        out = self.damaged(lambda t: t.replace('String[] Function FemaleNames2()', 'String[] Function FemaleNames3()'))
        self.assertTrue(any('parts [0, 1, 3]' in p for p in out), out)


if __name__ == '__main__':
    unittest.main()
