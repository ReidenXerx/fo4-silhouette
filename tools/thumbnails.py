"""The picker window's thumbnails (S-79): every preset the pickers offer, drawn front and side on the installed
body, one atlas per sex (2048 wide; 2048 tall, or 4096 when a sex's list outgrows it, S-93), BC1 DDS for Data/Textures/Silhouette/Thumbs<Sex>_<build>.dds.

    python tools/thumbnails.py [--data <game Data>] [--jobs 4] [--texconv <texconv.exe>]

The build is in the file name: the window mounts the atlas of the build its list comes from, so an atlas of
another build is simply not found, and the cards show no picture rather than the wrong one. Atlases of other
builds are removed from the output folder. index.json records the build, the cell geometry the window's
constants must match (interface/src/Panel.as), and each atlas's SHA-256 -- scripts/make-release.ps1 checks all
three.

Cell k of a sex's atlas is the k-th preset the catalog offers that sex in its menu (its order in catalog.json),
which is also the k-th entry of the NPC picker's list and of Silhouette:Player's: the window finds a preset's
picture by its place in the list, so the order is checked against the picker script before anything is
written. The player's own presets (S-76) are read in game and have none.

Shaded like the review sheets (tools/pool/render.py) and nude -- the owner, 2026-09-30: nude renders can ship.
Every body of a sex is drawn at one scale, so widths compare across the grid. Needs Pillow and texconv
(DirectXTex; xEdit and DynDOLOD ship it).
"""
import argparse
import json
import math
import pathlib
import re
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / 'tools' / 'pool'), str(ROOT / 'tools')]

CATALOG = ROOT / 'data' / 'F4SE' / 'Plugins' / 'Silhouette' / 'catalog.json'
PLAYER = ROOT / 'papyrus' / 'Silhouette' / 'Player.psc'
OUT = ROOT / 'build' / 'textures' / 'Silhouette'
WORK = ROOT / 'build' / 'thumbnails'
# texconv (DirectXTex): --texconv, else on PATH, else where xEdit and DynDOLOD keep a copy on the author's machine.
TEXCONV = [pathlib.Path(r'D:\xEdit.4.1.5f\Edit Scripts\Texconvx64.exe'),
           pathlib.Path(r'D:\DynDOLOD\Edit Scripts\Texconvx64.exe')]

CELL_W, CELL_H, SS = 96, 160, 3  # one thumbnail, and the supersampling it is drawn at
ATLAS = 2048          # the width, which fixes the columns the window cuts by (Panel.as ATLAS_COLS)
TALL = (2048, 4096)   # the height: the smaller that holds the list (S-93: 207 named people outgrew 2048)
COLS = ATLAS // CELL_W


def rows_of(height):
    return height // CELL_H


def height_for(count):
    return next((h for h in TALL if count <= COLS * rows_of(h)), None)
BACK = (22, 22, 26)
SKIN = (214, 176, 156)
FILE = {'female': 'ThumbsFemale', 'male': 'ThumbsMale'}  # + "_<build>.dds"

_body = {}


def menu_presets(catalog, sex):
    return [p for p in catalog['presets'] if p.get('menu') and p['sex'] == sex]


def script_names(sex):
    """The names Silhouette:Player lists for a sex, in its order (its Names0, Names1 ... functions)."""
    text = PLAYER.read_text(encoding='utf-8')
    names = []
    for part in range(8):
        m = re.search(rf'String\[\] Function {sex.capitalize()}Names{part}\(\) Global(.*?)EndFunction', text, re.S)
        if not m:
            break
        names += re.findall(r'a\.Add\("((?:[^"\\]|\\.)*)", 1\)', m.group(1))
    return names


def _load(data, sex):
    if sex not in _body:
        from mesh import Body
        body = Body(data, sex)
        arms = {i for i, r in enumerate(body.region) if r == 'arm'}
        body.side_tris = [t for t in body.tris if not arms.intersection(t)]
        _body[sex] = body
    return _body[sex]


def _views(img_size, verts, tris, side_tris, scale, zmin):
    """Front (left) and side (right, from the body's left: it faces +y), shaded, far triangles first. The side
    view leaves the arms off: one hangs across the profile the side view is there to show."""
    from PIL import Image, ImageDraw
    w, h = img_size
    img = Image.new('RGB', (w, h), BACK)
    d = ImageDraw.Draw(img)
    for cx, view in ((0.27, 'front'), (0.74, 'side')):
        V = (0.0, 1.0, 0.0) if view == 'front' else (1.0, 0.0, 0.0)
        L = (0.4 * V[0] + 0.3, 0.8 * V[1] + 0.4 * V[0], 0.55)
        ll = math.sqrt(sum(c * c for c in L))
        L = tuple(c / ll for c in L)
        polys = []
        for a, b, c in (tris if view == 'front' else side_tris):
            A, B, C = verts[a], verts[b], verts[c]
            ux, uy, uz = B[0] - A[0], B[1] - A[1], B[2] - A[2]
            vx, vy, vz = C[0] - A[0], C[1] - A[1], C[2] - A[2]
            nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
            nl = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
            depth = sum(P[0] * V[0] + P[1] * V[1] for P in (A, B, C))
            if view == 'front':
                pts = [(w * cx - P[0] * scale, h - 6 * SS - (P[2] - zmin) * scale) for P in (A, B, C)]
            else:
                pts = [(w * cx + P[1] * scale, h - 6 * SS - (P[2] - zmin) * scale) for P in (A, B, C)]
            lit = abs(nx * L[0] + ny * L[1] + nz * L[2]) / nl
            polys.append((depth, pts, 0.3 + 0.7 * lit))
        polys.sort(key=lambda t: t[0])
        for _d, pts, sh in polys:
            d.polygon(pts, fill=tuple(int(c * sh) for c in SKIN))
    return img


def render(job):
    """(data, sex, index, values, scale, zmin) -> (sex, index, PNG bytes of one cell)"""
    import io
    from PIL import Image
    data, sex, index, values, scale, zmin = job
    body = _load(data, sex)
    img = _views((CELL_W * SS, CELL_H * SS), body.build(values), body.tris, body.side_tris, scale, zmin)
    buf = io.BytesIO()
    img.resize((CELL_W, CELL_H), Image.LANCZOS).save(buf, 'PNG')
    return sex, index, buf.getvalue()


def texconv(given):
    import shutil
    if given:
        return pathlib.Path(given)
    for name in ('texconv', 'texconv.exe', 'Texconvx64.exe'):
        found = shutil.which(name)
        if found:
            return pathlib.Path(found)
    for t in TEXCONV:
        if t.exists():
            return t
    sys.exit('no texconv: get it from https://github.com/microsoft/DirectXTex/releases and pass --texconv, or put it '
             'on PATH')


def main():
    import generate
    from PIL import Image
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', default=str(generate.DATA))
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--texconv', help='texconv.exe (DirectXTex); default: on PATH')
    a = ap.parse_args()
    tool = texconv(a.texconv)

    catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
    jobs, lists = [], {}
    for sex in ('female', 'male'):
        presets = menu_presets(catalog, sex)
        names = [p['name'] for p in presets]
        script = script_names(sex)
        if names != script:
            first = next((i for i, (x, y) in enumerate(zip(names, script)) if x != y), min(len(names), len(script)))
            sys.exit(f'{sex}: the catalog offers {len(names)} presets, Silhouette:Player {len(script)}, first '
                     f'difference at {first} -- regenerate (tools/silhouette_gen.py --write) and try again')
        if height_for(len(names)) is None:
            sys.exit(f'{sex}: {len(names)} presets, the tallest atlas holds {COLS * rows_of(TALL[-1])}')
        lists[sex] = names
        body = _load(a.data, sex)
        zs = [p[2] for p in body.ref]
        zmin, zmax = min(zs), max(zs)
        scale = (CELL_H - 12) * SS / (zmax - zmin)
        jobs += [(a.data, sex, i, p['values'], scale, zmin) for i, p in enumerate(presets)]

    WORK.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    atlas = {sex: Image.new('RGB', (ATLAS, height_for(len(names))), BACK) for sex, names in lists.items()}
    import io
    with ProcessPoolExecutor(max_workers=a.jobs) as pool:
        for n, (sex, i, png) in enumerate(pool.map(render, jobs, chunksize=4), 1):
            atlas[sex].paste(Image.open(io.BytesIO(png)), ((i % COLS) * CELL_W, (i // COLS) * CELL_H))
            if n % 50 == 0:
                print(f'  {n}/{len(jobs)}', flush=True)
    import hashlib
    build = catalog['build']
    for old in OUT.glob('Thumbs*'):
        old.unlink()  # another build's pictures: the window would never mount them
    hashes = {}
    for sex, img in atlas.items():
        stem = f'{FILE[sex]}_{build}'
        png = WORK / f'{stem}.png'
        img.save(png)
        # BC1, one mip: the window shows each cell at about its own size, and Scaleform does no mip selection.
        subprocess.run([str(tool), '-nologo', '-y', '-f', 'BC1_UNORM', '-m', '1', '-o', str(OUT), str(png)],
                       check=True, stdout=subprocess.DEVNULL)
        written = next((f for f in OUT.iterdir() if f.name.lower() == f'{stem}.dds'.lower()), None)
        if written is None:
            sys.exit(f'texconv wrote no {stem}.dds')
        dds = OUT / f'{stem}.dds'
        written.replace(dds)  # texconv keeps the case it likes
        hashes[dds.name] = hashlib.sha256(dds.read_bytes()).hexdigest()
        print(f'{sex}: {len(lists[sex])} thumbnails -> {dds}')
    # Written last: a run that fails before this keeps the last index, which names the atlases it made.
    (WORK / 'index.json').write_text(json.dumps({'build': build, 'cell': [CELL_W, CELL_H], 'cols': COLS,
                                                 'atlases': hashes, 'lists': lists}, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main()
