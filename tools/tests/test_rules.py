"""tools/rules.py: what the config may hold, and the form ids it names."""
import json
import unittest

import support
import rules


def refusal(cfg):
    """The message validate() refuses cfg with -- None when it takes it."""
    try:
        rules.validate(cfg, 'test.json')
    except SystemExit as exc:
        return str(exc)
    return None


class Validate(unittest.TestCase):
    def test_the_default_config_is_taken(self):
        self.assertIsNone(refusal(json.loads(json.dumps(rules.DEFAULT))))

    def test_an_emptied_race_list_is_refused_not_read_as_the_default(self):
        # owner, 2026-09-24: HumanRace is the default, and removing it must be deliberate -- and said.
        msg = refusal({'distributeRaces': []})
        self.assertIsNotNone(msg)
        self.assertIn('Silhouette would shape nobody', msg)
        self.assertIn('no random bodies, no rules by name or faction, no refits', msg)
        self.assertIn('["HumanRace"]', msg)
        with self.assertRaises(SystemExit):
            rules.distribute_races({'distributeRaces': []})

    def test_a_missing_race_list_is_the_default(self):
        self.assertEqual(rules.distribute_races({}), ['HumanRace'])

    def test_a_race_list_of_the_wrong_shape_says_so_before_anything_else(self):
        # "" is a string where a list belongs: that says more than "an empty name" (wave 4 L8).
        for value in ('', None, 'HumanRace', {'HumanRace': 1}, [5], [None]):
            with self.subTest(value=value):
                self.assertEqual(refusal({'distributeRaces': value}), 'test.json: distributeRaces must be a list of strings')

    def test_empty_names_plugins_and_form_ids_are_refused(self):
        for cfg in ({'distributeRaces': ['']}, {'distributeRaces': ['  ']}, {'npc': {'': ['CBBE Curvy']}},
                    {'npc': {'Piper': ['']}}, {'npcFormID': {'': {'2F1E': ['CBBE Curvy']}}},
                    {'npcFormID': {'Fallout4.esm': {'': ['CBBE Curvy']}}},
                    {'blacklistedNpcsFormID': {'Fallout4.esm': ['']}}, {'heavyWords': ['']}):
            with self.subTest(cfg=cfg):
                self.assertIn('must be free of empty names, plugins and form ids', refusal(cfg) or '')

    def test_each_key_is_refused_in_the_wrong_shape_with_its_key_named(self):
        for key, value, words in (
                ('npc', ['Piper'], 'an object of "name": ["preset", ...]'),
                ('npc', {'Piper': 5}, 'an object of "name": ["preset", ...]'),
                ('npcFormID', {'Fallout4.esm': ['2F1E']}, 'an object of "Plugin.esp": {"formid": ["preset", ...]}'),
                ('blacklistedNpcsFormID', {'Fallout4.esm': [0x2F1E]},
                 'an object of "Plugin.esp": ["formid", ...], each form id a hex string'),
                ('refitOutfitPresetsFemale', {'Dress': ['x']}, 'an object of "outfit name": "refit preset name"'),
                ('blacklistedPresetsShowInOBodyMenu', 'yes', 'true or false'),
                ('heavyWords', 'coat', 'a list of strings')):
            with self.subTest(key=key, value=value):
                self.assertEqual(refusal({key: value}), f'test.json: {key} must be {words}')

    def test_text_the_plugin_cannot_read_back_is_refused(self):
        self.assertIn('not valid Unicode', refusal({'npc': {'\ud800': ['CBBE Curvy']}}) or '')

    def test_a_heavy_word_needs_a_letter_or_a_digit(self):
        self.assertIn("('---' holds no letter or digit)", refusal({'heavyWords': ['---']}) or '')
        self.assertIsNone(refusal({'heavyWords': ['é', 'coat', 'T-60']}))

    def test_load_refuses_an_emptied_race_list_in_the_file(self):
        with support.Scratch() as root:
            f = root / rules.CONFIG_NAME
            f.write_text(json.dumps({'distributeRaces': []}), encoding='utf-8')
            with self.assertRaises(SystemExit) as caught:
                rules.load(f, [], [])
            self.assertIn('Silhouette would shape nobody', str(caught.exception))
            f.write_text(json.dumps({'npc': {}}), encoding='utf-8')
            self.assertEqual(rules.load(f, [], [])['distributeRaces'], ['HumanRace'])


class FormKey(unittest.TestCase):
    def test_hex_with_or_without_0x_and_the_load_order_byte(self):
        for text, want in (('2F1E', '002F1E'), ('0x2F1E', '002F1E'), ('0X2f1e', '002F1E'), ('00002F1E', '002F1E'),
                           ('01002F1E', '002F1E'), ('FF0A7D35', '0A7D35'), (' 2F1E ', '002F1E'), ('7', '000007')):
            with self.subTest(text=text):
                self.assertEqual(rules.form_key(text), want)

    def test_anything_else_is_no_form_id(self):
        # int(x, 16) takes '+12' and '1_2' as 0x12: neither is a form id as OBody writes one.
        for text in ('+12', '1_2', '12g', '0x', '', ' ', '123456789', '-1', '0', '0x0', '1 2', '0x0x12', 'FE000000'):
            with self.subTest(text=text):
                self.assertIsNone(rules.form_key(text))

    def test_a_light_plugin_keeps_three_digits(self):
        self.assertEqual(rules.form_key('FE00A801', light=True), '000801')
        self.assertEqual(rules.form_key('801', light=True), '000801')
        self.assertIsNone(rules.form_key('FE00A000', light=True))


class PlayerForms(unittest.TestCase):
    def test_the_player_and_both_dummies_as_form_key_writes_them(self):
        for plugin, key in (('Fallout4.esm', '7'), ('FALLOUT4.ESM', '0x00000007'), ('fallout4.esm', '000A7D34'),
                            ('Fallout4.esm', 'FF0A7D35')):
            with self.subTest(plugin=plugin, key=key):
                self.assertTrue(rules.is_player_form(plugin, rules.form_key(key)))

    def test_nobody_else(self):
        for plugin, fid in (('Fallout4.esm', '002F1E'), ('DLCCoast.esm', '000007'), ('Fallout4.esm', '7'),
                            ('Fallout4.esm', '0a7d35')):
            with self.subTest(plugin=plugin, fid=fid):
                self.assertFalse(rules.is_player_form(plugin, fid))


if __name__ == '__main__':
    unittest.main()
