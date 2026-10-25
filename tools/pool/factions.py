"""Body pools per faction (S-72): each faction's people are drawn from ten bodies of their own per sex, in the
faction's look, still varied -- mostly plain, some rough, a rare fine one, as the main pool (the owner, 2026-09-25).

    python tools/pool/factions.py [--data <Fallout 4 Data>] [--sheets DIR]
    python tools/pool/factions.py --check    # every body still measures as its tier, every faction is still there

Each faction's archetypes are the main pool's (archetypes.py) in the faction's own proportions, some shifted
toward its look: muscle for the Brotherhood and the Pack, thinner limbs for raiders and the Children of Atom,
softer bodies for the Institute and the Triggermen. A candidate is built, measured and kept only if it measures
as its tier, exactly as for the main pool (generate.py), and the most different survivors are chosen.

Per sex and faction: 6 plain bodies listed WEIGHT['middle'] times in the faction's rule, 3 rough and 1 fine
listed WEIGHT[...] times -- 18 : 6 : 2 entries, 69 / 23 / 8 per cent, the main pool's odds. The rule picks one
entry by the person's id (src/Rules.cpp Pick), so repetition is the weighting, as in BodyGen's random line.

The generator (silhouette_gen.py) gives each faction its rule (factionFemale / factionMale) UNDER the user's
own: a rule of the config for the same faction and sex wins. A faction rule outranks the random pool, plugin
and race rules and loses to per-NPC rules (S-23), so the named people keep their own bodies (S-66).

Writes:
    data/Tools/BodySlide/SliderPresets/Silhouette Factions.xml   the presets
    tools/pool/factions.json                                     the sidecar the generator reads
"""
import argparse
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import generate  # noqa: E402
import plugin_forms  # noqa: E402
from archetypes import ARCHETYPES, CAP_F, LIMIT  # noqa: E402
from measure import measure  # noqa: E402
from mesh import Body  # noqa: E402

PRESETS = ROOT / 'data/Tools/BodySlide/SliderPresets/Silhouette Factions.xml'
SIDECAR = HERE / 'factions.json'
F4, NW, FH = 'Fallout4.esm', 'DLCNukaWorld.esm', 'DLCCoast.esm'
WEIGHT = {'middle': 3, 'ugly': 2, 'beautiful': 2}
COUNTS = {'middle': 6, 'ugly': 3, 'beautiful': 1}
# The owner's floor under a woman's bust (S-65): no faction's range goes below it.
FLOOR = {'Breasts': ('lo', 0), 'BreastsSmall': ('hi', 30)}

# Shifts toward a faction's look, added to an archetype's ranges ((lo, hi) in percent; a slider the archetype
# does not name starts at 0).
MUSCLE_F = {'MuscularArms': (15, 30), 'MuscularLegs': (15, 30), 'MuscularButt': (10, 30)}
HARD_F = {'MuscularArms': (10, 20), 'MuscularLegs': (10, 20)}
THIN_F = {'LegsThin': (10, 20), 'Arms': (10, 20)}
SOFT_F = {'ChubbyArms': (10, 20), 'Belly': (5, 15)}
MUSCLE_M = {'BTAbDefinition': (15, 30), 'BTBiceps': (10, 25), 'BTShoulders': (10, 20)}
HARD_M = {'BTAbDefinition': (10, 20), 'BTBiceps': (5, 15)}
THIN_M = {'BTThinArm': (10, 20), 'ThinThigh': (10, 20)}
SOFT_M = {'BTStomachFat': (10, 20), 'BTChubbyArm': (10, 20)}


def a(base, count, shift=None):
    """An archetype of the main pool, `count` bodies of it, shifted toward the faction's look."""
    return (base, count, shift or {})


# key: (name tag, [(plugin, faction editor id)], look,
#       {sex: {tier: [a(base archetype, count, shift)]}})   -- 6 middle, 3 ugly, 1 beautiful per sex
FACTIONS = {
    'bos': ('BoS', [(F4, 'BrotherhoodofSteelFaction')],
            'a drilled army, well fed: fit soldiers and heavy knights, softer scribes',
            {'female': {'middle': [a('Sturdy', 2, MUSCLE_F), a('Lean', 2, HARD_F), a('Average', 2)],
                        'ugly': [a('Boxy', 2, HARD_F), a('Apple', 1)],
                        'beautiful': [a('Athletic', 1, HARD_F)]},
             'male': {'middle': [a('Stocky', 2, MUSCLE_M), a('Lean', 2, HARD_M), a('Average', 2)],
                      'ugly': [a('BeerBelly', 1), a('Moobs', 1), a('Frail', 1)],
                      'beautiful': [a('Muscular', 1)]}}),
    'minutemen': ('Minutemen', [(F4, 'MinutemenFaction')],
                  'farmers turned militia: sturdy, work-strong, ordinary',
                  {'female': {'middle': [a('Sturdy', 2), a('Average', 2), a('WideHips', 1), a('Busty', 1)],
                              'ugly': [a('BottomHeavy', 1), a('Saggy', 1), a('Apple', 1)],
                              'beautiful': [a('Pear', 1)]},
                   'male': {'middle': [a('Stocky', 2), a('Dad', 2), a('Average', 2)],
                            'ugly': [a('BeerBelly', 1), a('Pear', 1), a('SkinnyFat', 1)],
                            'beautiful': [a('Broad', 1)]}}),
    'gunner': ('Gunner', [(F4, 'GunnerFaction')],
               'professional mercenaries: lean and hard',
               {'female': {'middle': [a('Lean', 3, HARD_F), a('Sturdy', 2, HARD_F), a('Average', 1, HARD_F)],
                           'ugly': [a('Boxy', 1), a('Frail', 1), a('Flat', 1)],
                           'beautiful': [a('Athletic', 1)]},
                'male': {'middle': [a('Wiry', 2, HARD_M), a('Lean', 2, HARD_M), a('Stocky', 2, HARD_M)],
                         'ugly': [a('Frail', 1), a('SkinnyFat', 1), a('BeerBelly', 1)],
                         'beautiful': [a('Athlete', 1)]}}),
    # The gangs and the Triggermen before the raiders: a rule is the first whose faction the NPC carries.
    'disciples': ('Disciple', [(NW, 'DLC04GangDisciplesFaction')],
                  'the Disciples: knife-lean, sinewy, all tendon',
                  {'female': {'middle': [a('Lean', 4, THIN_F), a('Average', 2, THIN_F)],
                              'ugly': [a('Frail', 2), a('Flat', 1)],
                              'beautiful': [a('Slender', 1)]},
                   'male': {'middle': [a('Wiry', 4, HARD_M), a('Lean', 2, THIN_M)],
                            'ugly': [a('Frail', 2), a('SkinnyFat', 1)],
                            'beautiful': [a('Swimmer', 1)]}}),
    'operators': ('Operator', [(NW, 'DLC04GangOperatorsFaction')],
                  'the Operators: sleek, styled, better fed than the other gangs',
                  {'female': {'middle': [a('Average', 2), a('Busty', 2), a('Lean', 2)],
                              'ugly': [a('Saggy', 1), a('BottomHeavy', 1), a('Apple', 1)],
                              'beautiful': [a('Hourglass', 1)]},
                   'male': {'middle': [a('Average', 2), a('Lean', 2), a('Soft', 2)],
                            'ugly': [a('Moobs', 1), a('Pear', 1), a('BeerBelly', 1)],
                            'beautiful': [a('Classic', 1)]}}),
    'pack': ('Pack', [(NW, 'DLC04GangPackFaction')],
             'the Pack: wild, feral, muscled',
             {'female': {'middle': [a('Sturdy', 4, MUSCLE_F), a('Lean', 2, MUSCLE_F)],
                         'ugly': [a('Boxy', 2, MUSCLE_F), a('BottomHeavy', 1)],
                         'beautiful': [a('Athletic', 1, MUSCLE_F)]},
              'male': {'middle': [a('Stocky', 3, MUSCLE_M), a('Wiry', 1, MUSCLE_M), a('Lean', 2, MUSCLE_M)],
                       'ugly': [a('BeerBelly', 1), a('Pear', 1), a('Obese', 1)],
                       'beautiful': [a('Broad', 1)]}}),
    'triggermen': ('Triggerman', [(F4, 'TriggermanFaction')],
                   'Skinny Malone\'s mob: well fed and a little soft',
                   {'female': {'middle': [a('Average', 2, SOFT_F), a('Busty', 2), a('Soft', 2)],
                               'ugly': [a('Obese', 1), a('Saggy', 1), a('Apple', 1)],
                               'beautiful': [a('Hourglass', 1)]},
                    'male': {'middle': [a('Dad', 2), a('Stocky', 2), a('Soft', 2)],
                             'ugly': [a('Obese', 1), a('BeerBelly', 1), a('Moobs', 1)],
                             'beautiful': [a('Broad', 1)]}}),
    'raiders': ('Raider', [(F4, 'RaiderFaction')],
                'raiders: underfed, wiry, chem-worn, and the odd brute',
                {'female': {'middle': [a('Lean', 4, THIN_F), a('Sturdy', 2, HARD_F)],
                            'ugly': [a('Frail', 2), a('Boxy', 1)],
                            'beautiful': [a('Slender', 1)]},
                 'male': {'middle': [a('Wiry', 3, THIN_M), a('Lean', 2, THIN_M), a('Stocky', 1, HARD_M)],
                          'ugly': [a('Frail', 2), a('SkinnyFat', 1)],
                          'beautiful': [a('Lean', 1)]}}),
    'institute': ('Institute', [(F4, 'InstituteFaction')],
                  'the Institute: well fed, sedentary, soft',
                  {'female': {'middle': [a('Soft', 2), a('Average', 3, SOFT_F), a('Busty', 1)],
                              'ugly': [a('Obese', 1), a('Saggy', 1), a('Apple', 1)],
                              'beautiful': [a('Slender', 1)]},
                   'male': {'middle': [a('Soft', 2), a('Dad', 2), a('Average', 2, SOFT_M)],
                            'ugly': [a('Obese', 1), a('Moobs', 1), a('SkinnyFat', 1)],
                            'beautiful': [a('Classic', 1)]}}),
    'railroad': ('Railroad', [(F4, 'RailroadFaction')],
                 'Railroad agents: lean and quick',
                 {'female': {'middle': [a('Lean', 3, HARD_F), a('Average', 2), a('Sturdy', 1)],
                             'ugly': [a('Frail', 1), a('Flat', 1), a('Boxy', 1)],
                             'beautiful': [a('Athletic', 1)]},
                  'male': {'middle': [a('Lean', 3, HARD_M), a('Wiry', 2), a('Average', 1)],
                           'ugly': [a('Frail', 1), a('SkinnyFat', 1), a('BeerBelly', 1)],
                           'beautiful': [a('Swimmer', 1)]}}),
    'atom': ('Atom', [(F4, 'ChildrenOfAtomFaction'), (FH, 'DLC03ChildrenofAtomFaction')],
             'the Children of Atom: gaunt, ascetic, radiation-worn',
             {'female': {'middle': [a('Lean', 4, THIN_F), a('Average', 2, THIN_F)],
                         'ugly': [a('Frail', 2), a('Saggy', 1)],
                         'beautiful': [a('Slender', 1)]},
              'male': {'middle': [a('Lean', 3, THIN_M), a('Wiry', 3, THIN_M)],
                       'ugly': [a('Frail', 2), a('SkinnyFat', 1)],
                       'beautiful': [a('Lean', 1)]}}),
}
TIER_NAME = {'middle': 'Plain', 'ugly': 'Rough', 'beautiful': 'Fine'}


def ranges_of(sex, tier, base, shift):
    """The archetype's ranges with the faction's shift added, the bust floor kept for a woman (S-65), every
    slider inside its range and a woman's caps (S-96)."""
    ranges = dict(ARCHETYPES[sex][tier][base][1])
    for s, (lo, hi) in shift.items():
        blo, bhi = ranges.get(s, (0, 0))
        ranges[s] = (blo + lo, bhi + hi)
    if sex == 'female':
        for s, (side, limit) in FLOOR.items():
            if s in ranges:
                lo, hi = ranges[s]
                ranges[s] = (max(lo, limit), max(hi, limit)) if side == 'lo' else (min(lo, limit), min(hi, limit))
    # S-96: a shift never pushes a slider past its own range, nor a woman past the chest and limb caps
    caps = CAP_F if sex == 'female' else {}
    for s, (lo, hi) in ranges.items():
        top = caps.get(s, LIMIT[1])
        ranges[s] = (min(max(lo, LIMIT[0]), top), min(max(hi, LIMIT[0]), top))
    return ranges


def archetypes_of(key, sex):
    """{tier: {label: (count, ranges)}} for generate.generate, labels unique within a tier."""
    spec = FACTIONS[key][3][sex]
    out = {}
    for tier in ('beautiful', 'middle', 'ugly'):
        arcs = {}
        for base, count, shift in spec[tier]:
            label = base if base not in arcs else f'{base}{len(arcs)}'
            arcs[label] = (count, ranges_of(sex, tier, base, shift))
        if sum(c for c, _r in arcs.values()) != COUNTS[tier]:
            raise SystemExit(f'{key} {sex} {tier}: {sum(c for c, _r in arcs.values())} bodies, not {COUNTS[tier]}')
        out[tier] = arcs
    return out


def build(data, log=print):
    """-> ({sex: [body]}, models, sliders_of); each body: name, faction, tier, archetype, values, measure."""
    bodies, models, sliders_of = {'female': [], 'male': []}, {}, {}
    for sex in bodies:
        models[sex] = Body(data, sex)
        sliders_of[sex] = generate.slider_names(data, models[sex].slider_set)
    for key, (tag, _factions, _look, _spec) in FACTIONS.items():
        for sex in bodies:
            log(f'{tag} {sex}:')
            rows = generate.generate(models[sex], sex, f'silhouette-faction-{key}-1', log=log,
                                     archetypes=archetypes_of(key, sex))
            letter = 'F' if sex == 'female' else 'M'
            counts = {}
            for c in rows:
                counts[c['tier']] = counts.get(c['tier'], 0) + 1
                c['name'] = f'{tag} {TIER_NAME[c["tier"]]} {letter}{counts[c["tier"]]:02d}'
                c['faction'] = key
            bodies[sex] += rows
    return bodies, models, sliders_of


def sidecar(bodies):
    return {'about': 'Silhouette Factions: the faction, tier and archetype of each preset in "Silhouette Factions.xml" '
                     '(tools/pool/factions.py, S-72).',
            'weights': WEIGHT,
            'factions': {key: {'tag': tag, 'factions': [[pl, edid] for pl, edid in factions], 'look': look}
                         for key, (tag, factions, look, _spec) in FACTIONS.items()},
            'presets': {c['name']: {'faction': c['faction'], 'sex': sex, 'tier': c['tier'], 'archetype': c['archetype'],
                                    'values': c['values'],
                                    'measure': {k: round(c['measure'][k], 3) for k in generate.FEATURES + ['volume']}}
                        for sex, rows in bodies.items() for c in rows}}


def check(data):
    """Every committed body still measures as its tier, and every faction is still in its plugin. -> problems."""
    side = json.loads(SIDECAR.read_text(encoding='utf-8'))
    bad = 0
    for sex in ('female', 'male'):
        body = Body(data, sex)
        for name, p in side['presets'].items():
            if p['sex'] != sex:
                continue
            m = measure(body.build({s: v / 100.0 for s, v in p['values'].items()}), body.region, sex)
            if generate.tier_of(m, sex) != p['tier']:
                bad += 1
                print(f'  {name}: now measures as {generate.tier_of(m, sex)}, not {p["tier"]}')
    ids = {}
    for key, f in side['factions'].items():
        for pl, edid in f['factions']:
            if pl not in ids:  # editor ids are compared as the game does: without case
                ids[pl] = {k.casefold() for k in plugin_forms.editor_ids(pathlib.Path(data) / pl, 'FACT')}
            if edid.casefold() not in ids[pl]:
                bad += 1
                print(f'  {key}: {pl} has no faction {edid}')
    print('factions check: ' + ('every body measures as its tier, every faction is there' if not bad
                                else f'{bad} problem(s)'))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--data', type=pathlib.Path, default=generate.DATA)
    ap.add_argument('--sheets', type=pathlib.Path, help='also draw a review sheet per faction and sex here')
    ap.add_argument('--check', action='store_true', help='re-measure and look the factions up; write nothing')
    args = ap.parse_args()
    if args.check:
        return 1 if check(args.data) else 0
    bodies, models, sliders_of = build(args.data)
    PRESETS.parent.mkdir(parents=True, exist_ok=True)
    PRESETS.write_text(generate.preset_xml(bodies, sliders_of).replace(
        'Silhouette Pool: generated by fo4-silhouette tools/pool/generate.py. Edit archetypes.py',
        'Silhouette Factions: generated by fo4-silhouette tools/pool/factions.py. Edit factions.py'), encoding='utf-8')
    SIDECAR.write_text(json.dumps(sidecar(bodies), indent=1) + '\n', encoding='utf-8')
    print(f'wrote {PRESETS.relative_to(ROOT)} and {SIDECAR.relative_to(ROOT)}: '
          f'{sum(len(r) for r in bodies.values())} bodies in {len(FACTIONS)} factions')
    if args.sheets:
        from render import render_sheet
        args.sheets.mkdir(parents=True, exist_ok=True)
        for key in FACTIONS:
            for sex, rows in bodies.items():
                render_sheet(models[sex], [c for c in rows if c['faction'] == key], args.sheets / f'{key}_{sex}.png')
        print(f'review sheets in {args.sheets}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
