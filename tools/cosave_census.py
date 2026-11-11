"""Who has which Silhouette body in a save -- read from its F4SE co-save, without
touching the running game.

LooksMenu stores every actor's body morphs in the co-save (f4ee main.cpp:
SetUniqueID 'F4EE'; records STTB = string table, MRPH / MRPM = one female / male
actor's form id, each followed by MRVM = that actor's morphs). A Silhouette body
is recognised by its MARKER morph, whose value is the stamp of the build that
rolled it (decision S-12); the manifest of that stamp names the exact preset.

F4SE's co-save (Serialization.cpp): a header (signature 'F4SE', format version,
F4SE version, runtime version, plugin count), then per plugin a header (unique
id, chunk count, byte length) and its chunks (type, version, length, data).
Multi-character constants are stored as little-endian integers, so 'F4SE' is the
bytes "ES4F".

    python tools/cosave_census.py                    # the newest save
    python tools/cosave_census.py <file.f4se>
"""
import collections
import json
import pathlib
import struct
import sys

SAVES = pathlib.Path.home() / 'Documents/My Games/Fallout4/Saves'
MANIFESTS = [pathlib.Path(r'D:\SteamFreeGames\Fallout 4 AE\Data\F4SE\Plugins\Silhouette\manifests'),
             pathlib.Path(__file__).resolve().parent.parent / 'data/F4SE/Plugins/Silhouette/manifests']

# Silhouette's own names that are not a body's marker (src/Catalog.cpp KindOf, compared as the plugin
# does, ASCII case folded): the refit marker lives under the refit keyword (S-40), the choice marker
# beside a picked or API-given body (S-51), and the blacklist marker keeps a body bare (S-23).
REFIT_MARKER, CHOICE_MARKER, BLACKLIST_MARKER = 'silhouette_refit', 'silhouette_chosen', 'silhouette_blacklisted'


def kind_of(name):
    n = name.lower()
    if n in (REFIT_MARKER, CHOICE_MARKER, BLACKLIST_MARKER):
        return n
    return 'body' if len(name) > len('Silhouette_') and n.startswith('silhouette_') else None


def fourcc(v):
    return struct.pack('>I', v).decode('latin1')


def read_cosave(path):
    """{plugin id: [(chunk type, version, bytes)]}"""
    b = path.read_bytes()
    # Measured on a real co-save: the HEADER signature is the literal bytes "F4SE",
    # while plugin ids and chunk types are little-endian integers ("EE4F" is F4EE,
    # "BTTS" is STTB) -- the two are not written the same way.
    if b[:4] != b'F4SE':
        raise ValueError(f'{path}: not an F4SE co-save ({b[:4]!r})')
    _sig, fmt, f4se, runtime, nplugins = struct.unpack_from('<5I', b, 0)
    o, plugins = 20, {}
    for _ in range(nplugins):
        uid, nchunks, length = struct.unpack_from('<3I', b, o)
        o += 12
        end = o + length
        chunks = []
        while o < end:
            t, ver, ln = struct.unpack_from('<3I', b, o)
            o += 12
            chunks.append((fourcc(t), ver, b[o:o + ln]))
            o += ln
        plugins[fourcc(uid)] = chunks
        o = end
    return plugins


def looksmenu_bodies(chunks):
    """-> ({(sex, actor form id): {morph: {keyword form id: value}}}, strings)"""
    strings, bodies, current = [], {}, None
    for kind, ver, data in chunks:
        if kind == 'STTB':
            (n,) = struct.unpack_from('<I', data, 0)
            o = 4
            for _ in range(n):
                (ln,) = struct.unpack_from('<H', data, o)
                o += 2
                strings.append(data[o:o + ln].decode('latin1'))
                o += ln
        elif kind in ('MRPH', 'MRPM'):
            (fid,) = struct.unpack_from('<I', data, 0)
            current = ('F' if kind == 'MRPH' else 'M', fid)
        elif kind == 'MRVM' and current:
            (n,) = struct.unpack_from('<I', data, 0)
            o, morphs = 4, {}
            for _ in range(n):
                sid, nkeys = struct.unpack_from('<2I', data, o)
                o += 8
                keys = {}
                for _ in range(nkeys):
                    kw, val = struct.unpack_from('<If', data, o)
                    o += 8
                    keys[kw] = val
                name = strings[sid] if sid < len(strings) else f'#{sid}'
                morphs[name] = keys
            bodies[current] = morphs
            current = None
    return bodies, strings


def manifests():
    """{stamp: manifest}. Data's copy of a stamp wins over this checkout's: it is the one the game reads."""
    out = {}
    for d in MANIFESTS:
        for f in d.glob('*.json') if d.exists() else []:
            m = json.loads(f.read_text(encoding='utf-8-sig'))     # a BOM an editor added is not a new format
            out.setdefault(int(m['stamp']), m)
    return out


def main():
    # A preset or NPC name the console's code page cannot hold must not end the census (L4 F6).
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors='backslashreplace')
        except (AttributeError, ValueError):
            pass
    path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else max(SAVES.glob('*.f4se'), key=lambda p: p.stat().st_mtime)
    plugins = read_cosave(path)
    if 'F4EE' not in plugins:
        print(f'{path.name}: no LooksMenu data in this co-save')
        return 1
    bodies, _ = looksmenu_bodies(plugins['F4EE'])
    known = manifests()
    rows, per_preset, other = [], collections.Counter(), 0
    blacklisted, writing, baked_refit = [], [], []
    for (sex, fid), morphs in sorted(bodies.items()):
        # Her own layer only (key 0): a marker under another keyword is not her body's.
        own = {n: keys.get(0, 0.0) for n, keys in morphs.items() if keys.get(0, 0.0) > 0}
        if any(kind_of(n) == REFIT_MARKER for n in own):
            # The refit marker belongs under the refit keyword. In her own layer it means LooksMenu loaded a
            # Silhouette.esp without that keyword and moved every refit value into her body (wave 3 L5-H1).
            baked_refit.append(fid)
        if any(kind_of(n) == BLACKLIST_MARKER for n in own):
            blacklisted.append(fid)
        marks = [(n, v) for n, v in own.items() if kind_of(n) == 'body']
        if not marks:
            other += 0 if fid in blacklisted else 1
            rows.append((fid, sex, None, None, len(morphs)))
            continue
        name, value = marks[0]
        if value < 1.0:
            # S-58: a body marker below 1 is "pending" -- the body was being written when the game saved,
            # and the next probe gives it again. A stamp is never below 1.
            writing.append(fid)
            per_preset[f'{name} (being written, S-58)'] += 1
            rows.append((fid, sex, f'{name} (being written)', None, len(morphs)))
            continue
        stamp = int(round(value))
        m = known.get(stamp)
        preset = m['templates'].get(name, {}).get('preset') if m else None
        per_preset[preset or name] += 1
        rows.append((fid, sex, preset or name, stamp, len(morphs)))
    print(f'{path.name}  ({path.stat().st_size} bytes): {len(bodies)} actors hold LooksMenu body morphs')
    print(f'  Silhouette bodies: {sum(per_preset.values())} in {len(per_preset)} presets; '
          f'blacklisted (kept bare): {len(blacklisted)}; other body sliders: {other}')
    if writing:
        print(f'  being written when the game saved (S-58, given again when met): {len(writing)} -- '
              + ', '.join(f'{f:08X}' for f in writing[:8]) + (' ...' if len(writing) > 8 else ''))
    if baked_refit:
        print(f'  !! {len(baked_refit)} actor(s) hold Silhouette_Refit in their OWN layer: a Silhouette.esp without the '
              f'refit keyword was loaded, and LooksMenu moved their refit values into their bodies for good -- '
              + ', '.join(f'{f:08X}' for f in baked_refit[:8]) + (' ...' if len(baked_refit) > 8 else ''))
    stamps = collections.Counter(r[3] for r in rows if r[3])
    for st, n in stamps.items():
        print(f'  stamp {st}: {n} actor(s) -- manifest {"found" if st in known else "MISSING"}')
    player = [r for r in rows if r[0] == 0x14]
    if player:
        print(f'  the player (00000014): {player[0][2] or "no Silhouette marker"} '
              f'({player[0][4]} morphs)')
    print('  most common presets:')
    for p, n in per_preset.most_common(12):
        print(f'    {n:3}  {p}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
