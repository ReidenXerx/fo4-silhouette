"""S-76: the plugin reads the player's own presets exactly as the generator would have written them.

The plugin's C++ (src/Presets.cpp, reached through SilhouetteTests.exe --presets, the same ReadInstalled the game
calls) against the generator's own functions -- classify, band, base_body.resolve, morph_values, plain_marker -- on
the presets installed in the game's Data that the committed catalog does not carry: every name, sex, fit, marker
and value must agree.
"""
import json
import subprocess
import unittest

import support
import base_body
import silhouette_gen as sg


def generator_view(data, cat):
    """What the generator makes of Data's own presets, by the plugin's rules: never a preset the catalog has."""
    morphs = {}
    for g, body in sg.BODIES.items():
        tri = data / f'Meshes/Actors/Character/CharacterAssets/{body}.tri'
        morphs[g] = set().union(*base_body.read_tri(tri).values()) if tri.exists() else set()
    own = {(p['sex'], p['name'].casefold()) for p in cat['presets']}
    family = {}
    for g in sg.BODIES:
        counts = {}
        for p in cat['presets']:
            if p['sex'] == g and p['fit'] == 'full' and p['family']:
                counts[p['family']] = counts.get(p['family'], 0) + 1
        family[g] = min(counts, key=lambda f: (-counts[f], f)) if counts else None
    sets = {g: {'sliders': {n: {'default': d, 'invert': inv, 'morph': True} for n, (d, inv) in s['sliders'].items()}}
            for g, s in cat.get('sliderSets', {}).items()}
    out = {}
    for p in sg.read_all_presets([data / sg.PRESETS]):
        c = sg.classify(p, morphs['female'], morphs['male'])
        if c['kind'] != 'body' or p['name'].casefold().endswith('-refit'):
            continue
        g = c['gender']
        if not morphs[g] or (g, p['name'].casefold()) in own:
            continue
        p.update(c)
        band = sg.band(p, family[g])
        if band not in ('full', 'partial') or g not in sets:
            continue
        marker = sg.plain_marker(p['name'])
        values = sg.morph_values(marker, base_body.resolve(p, sets[g]), {}, morphs[g], p['name'])
        out[p['name']] = {'sex': g, 'fit': band, 'marker': marker, 'values': dict(values)}
    return out


@support.needs_data
class Parity(unittest.TestCase):
    def test_the_plugin_reads_installed_presets_as_the_generator_would(self):
        data = support.game_data()
        catalog = support.PACKAGE / support.CAT
        cat = json.loads(catalog.read_text(encoding='utf-8'))
        if not cat.get('sliderSets'):
            self.skipTest('the committed catalog carries no slider sets')
        run = subprocess.run([str(support.tests_exe()), '--presets', str(data), str(catalog)],
                             capture_output=True, text=True, encoding='utf-8', timeout=300)
        self.assertEqual(run.returncode, 0, run.stderr)
        plugin = {a['name']: a for a in json.loads(run.stdout)['added']}
        expected = generator_view(data, cat)
        self.assertEqual(sorted(plugin), sorted(expected), 'the same presets join the pickers')
        for name, want in expected.items():
            got = plugin[name]
            with self.subTest(preset=name):
                self.assertEqual((got['sex'], got['fit'], got['marker']), (want['sex'], want['fit'], want['marker']))
                self.assertEqual(sorted(got['values']), sorted(want['values']), 'the same morphs')
                for m, v in want['values'].items():
                    self.assertAlmostEqual(got['values'][m], v, places=5, msg=m)


if __name__ == '__main__':
    unittest.main()
