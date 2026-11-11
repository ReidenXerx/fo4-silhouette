"""Silhouette's own body pool: the bodies NPCs are drawn from at random (the owner, 2026-09-24).

    python tools/pool/generate.py [--data <Fallout 4 Data>] [--seed S] [--sheets DIR]
    python tools/pool/generate.py --check        # the committed pool, re-measured against the installed body

The owner asked for real people instead of the installed presets' pin-ups: an ordinary body most often,
an unflattering one less often, a conventionally beautiful one rarely, and variety inside each. The owner's poll:
70 / 22 / 8, women and men, the installed presets out of random but still in the picker.

Each tier is a set of archetypes (archetypes.py) given as slider ranges. A candidate is drawn from its
ranges, BUILT (mesh.py: reference + value x diff) and MEASURED (measure.py: girths and projections from
slices of the torso, a leg, an arm), and kept only if its measurements put it in its own tier (tier_of).
From the survivors, the ones most different from each other are chosen (farthest-point sampling on the
measurements), so no two bodies of an archetype look alike.

The tiers' odds come from repetition in BodyGen's random line (weighting by repetition is BodyGen's own
rule, docs/bodygen-format.md): a middle body is listed WEIGHT['middle'] times, the others once. With 18
middle, 17 ugly and 6 beautiful bodies per sex that is 54 : 17 : 6, 70.1 / 22.1 / 7.8 per cent, in 77
entries: about 1.6 KB of line, far under the 32,766 bytes past which the engine splits a line (S-65).

Writes:
    data/Tools/BodySlide/SliderPresets/Silhouette Pool.xml   the presets, every slider of the set explicit
    tools/pool/pool.json                                     the sidecar: tier and archetype of each preset
"""
import argparse
import json
import math
import pathlib
import random
import sys
from xml.sax.saxutils import quoteattr

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import base_body  # noqa: E402
import silhouette_gen  # noqa: E402
from archetypes import ARCHETYPES  # noqa: E402
from measure import measure  # noqa: E402
from mesh import Body  # noqa: E402

PRESETS = ROOT / 'data/Tools/BodySlide/SliderPresets/Silhouette Pool.xml'
SIDECAR = HERE / 'pool.json'
DATA = pathlib.Path(r'D:\SteamFreeGames\Fallout 4 AE\Data')
TIER_NAME = {'middle': 'Plain', 'ugly': 'Rough', 'beautiful': 'Fine'}
WEIGHT = {'middle': 3, 'ugly': 1, 'beautiful': 1}
# The preset's declaration, as the body's own zero presets declare it (AnatomyZero.xml, BT-Zero.xml)
SET = {'female': ('CBBE Body', ['CBBE', 'Anatomy']),
       'male': ('BodyTalk4', ['BodyTalk - Bodies', 'BodyTalk - Clothing (Automaton)', 'BodyTalk - Clothing (Base Game)',
                              'BodyTalk - Clothing (Far Harbor)', 'BodyTalk - Clothing (Nuka World)'])}
FEATURES = ['bust', 'waist', 'hip', 'thigh', 'arm', 'bustProjection', 'belly', 'butt', 'whr', 'bwr']


def tier_of(m, sex):
    """The tier a body's MEASUREMENTS put it in. The gates were set from a calibration run of every
    archetype (CBBE's zeroed woman is already a fantasy hourglass: waist/hip 0.60, bust/waist 1.58;
    BodyTalk's zeroed man 0.78 / 1.35)."""
    if sex == 'female':
        if (m['whr'] >= 0.77 or m['volume'] >= 355 or m['volume'] <= 300 or m['hip'] >= 84
                or (m['bustProjection'] <= 0.3 and m['bust'] <= 66) or m['belly'] >= 1.2):
            return 'ugly'
        if m['whr'] <= 0.64 and m['bwr'] >= 1.45 and m['bustProjection'] >= 0.6 and m['belly'] <= 0.3:
            return 'beautiful'
        return 'middle'
    if m['volume'] >= 415 or m['volume'] <= 345 or m['whr'] >= 0.86 or m['hip'] >= 88 or m['bwr'] <= 1.12:
        return 'ugly'
    if m['bwr'] >= 1.38 and m['whr'] <= 0.77:
        return 'beautiful'
    return 'middle'


def _distance(a, b, scale):
    return math.sqrt(sum(((a[k] - b[k]) / scale[k]) ** 2 for k in FEATURES))


def generate(body, sex, seed, candidates=40, log=print, archetypes=None):
    """archetypes: {tier: {archetype: (count, ranges)}} for this sex; the pool's own by default (a faction's
    pool, S-72, passes its own)."""
    rng = random.Random(f'{seed}:{sex}')
    chosen = []
    for tier, arcs in (archetypes or ARCHETYPES[sex]).items():
        for arc, (count, ranges) in arcs.items():
            pool = []
            for _ in range(candidates):
                values = {s: round(rng.uniform(lo, hi)) for s, (lo, hi) in ranges.items()}
                m = measure(body.build({s: v / 100.0 for s, v in values.items()}), body.region, sex)
                if tier_of(m, sex) == tier:
                    pool.append((values, m))
            log(f'  {sex:6} {tier:9} {arc:12} {len(pool):2}/{candidates} candidates measure as {tier}')
            if len(pool) < count:
                raise SystemExit(f'{sex} {tier}/{arc}: only {len(pool)} of {candidates} candidates measure as {tier}')
            scale = {k: (max(p[1][k] for p in pool) - min(p[1][k] for p in pool)) or 1.0 for k in FEATURES}
            centre = {k: sum(p[1][k] for p in pool) / len(pool) for k in FEATURES}
            picks = [min(pool, key=lambda p: _distance(p[1], centre, scale))]
            while len(picks) < count:
                picks.append(max(pool, key=lambda p: min(_distance(p[1], q[1], scale) for q in picks)))
            chosen += [{'tier': tier, 'archetype': arc, 'values': v, 'measure': m} for v, m in picks]
    letter = 'F' if sex == 'female' else 'M'
    counts = {}
    for c in chosen:
        counts[c['tier']] = counts.get(c['tier'], 0) + 1
        c['name'] = f"{TIER_NAME[c['tier']]} {letter}{counts[c['tier']]:02d}"
    return chosen


def slider_names(data, slider_set):
    for s in base_body.read_slider_sets(pathlib.Path(data) / 'Tools/BodySlide'):
        if s['name'] == slider_set:
            # a runtime state, the shaft, or a slider fo4-anatomy's build owns is never part of a body
            # (S-16, S-29, S-62): left to the set's default, as Silhouette leaves it out of every template
            return sorted(n for n in s['sliders'] if not silhouette_gen.never_in_body(n))
    raise SystemExit(f'no slider set {slider_set!r} in {data}\\Tools\\BodySlide')


def preset_xml(bodies, sliders_of):
    """Every slider of the set written, the ones a body does not name at 0: BodySlide builds an omitted
    slider at the set's DEFAULT, and 26 BodyTalk sliders default to 100 (base_body.py)."""
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', '<SliderPresets>',
             '    <!-- Silhouette Pool: generated by fo4-silhouette tools/pool/generate.py. Edit archetypes.py, not this. -->']
    for sex, rows in bodies.items():
        set_name, groups = SET[sex]
        for c in rows:
            lines.append(f'    <Preset name={quoteattr(c["name"])} set={quoteattr(set_name)}>')
            lines += [f'        <Group name={quoteattr(g)}/>' for g in groups]
            for s in sliders_of[sex]:
                v = c['values'].get(s, 0)
                lines.append(f'        <SetSlider name={quoteattr(s)} size="small" value="{v}"/>')
                lines.append(f'        <SetSlider name={quoteattr(s)} size="big" value="{v}"/>')
            lines.append('    </Preset>')
    lines.append('</SliderPresets>')
    return '\n'.join(lines) + '\n'


def sidecar(bodies, seed):
    return {'about': 'Silhouette Pool: the tier of each preset in "Silhouette Pool.xml" (tools/pool/generate.py).',
            'seed': seed, 'weights': WEIGHT,
            'presets': {c['name']: {'sex': sex, 'tier': c['tier'], 'archetype': c['archetype'],
                                    'values': c['values'],
                                    'measure': {k: round(c['measure'][k], 3) for k in FEATURES + ['volume']}}
                        for sex, rows in bodies.items() for c in rows}}


def load_sidecar(path=SIDECAR):
    return json.loads(pathlib.Path(path).read_text(encoding='utf-8')) if pathlib.Path(path).exists() else None


def check(data):
    """Re-measure the committed pool against the installed body: every body must still measure as its tier."""
    side = load_sidecar()
    if not side:
        raise SystemExit(f'no {SIDECAR}')
    bad = 0
    for sex in ('female', 'male'):
        body = Body(data, sex)
        for name, p in side['presets'].items():
            if p['sex'] != sex:
                continue
            m = measure(body.build({s: v / 100.0 for s, v in p['values'].items()}), body.region, sex)
            if tier_of(m, sex) != p['tier']:
                bad += 1
                print(f'  {name}: now measures as {tier_of(m, sex)}, not {p["tier"]}')
    print('pool check: ' + ('every body measures as its tier' if not bad else f'{bad} body(ies) moved tier'))
    return bad


def sheets(bodies, bodies_by_sex, folder):
    from render import render_sheet
    folder = pathlib.Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    for sex, rows in bodies.items():
        for tier in ('beautiful', 'middle', 'ugly'):
            render_sheet(bodies_by_sex[sex], [c for c in rows if c['tier'] == tier], folder / f'{sex}_{tier}.png')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--data', type=pathlib.Path, default=DATA)
    ap.add_argument('--seed', default='silhouette-pool-1')
    ap.add_argument('--sheets', type=pathlib.Path, help='also draw a review sheet per sex and tier here')
    ap.add_argument('--check', action='store_true', help='re-measure the committed pool; write nothing')
    args = ap.parse_args()
    if args.check:
        return 1 if check(args.data) else 0
    bodies, models, sliders_of = {}, {}, {}
    for sex in ('female', 'male'):
        models[sex] = Body(args.data, sex)
        sliders_of[sex] = slider_names(args.data, models[sex].slider_set)
        bodies[sex] = generate(models[sex], sex, args.seed)
    PRESETS.parent.mkdir(parents=True, exist_ok=True)
    PRESETS.write_text(preset_xml(bodies, sliders_of), encoding='utf-8')
    SIDECAR.write_text(json.dumps(sidecar(bodies, args.seed), indent=1) + '\n', encoding='utf-8')
    for sex, rows in bodies.items():
        n = {t: sum(1 for c in rows if c['tier'] == t) for t in WEIGHT}
        entries = {t: n[t] * WEIGHT[t] for t in WEIGHT}
        total = sum(entries.values())
        print(f'{sex}: ' + ', '.join(f'{n[t]} {t} x{WEIGHT[t]} = {100 * entries[t] / total:.1f}%' for t in ('middle', 'ugly', 'beautiful')))
    print(f'wrote {PRESETS.relative_to(ROOT)} and {SIDECAR.relative_to(ROOT)}')
    if args.sheets:
        sheets(bodies, models, args.sheets)
        print(f'review sheets in {args.sheets}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
