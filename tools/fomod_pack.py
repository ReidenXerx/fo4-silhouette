"""Silhouette's FOMOD installer, written into a release folder (nexus-tools/docs/FOMOD-STANDARD.md, the owner's
house standard of 2026-10-01; Silhouette 0.3.0 is the first release under it).

    python tools/fomod_pack.py <release folder> <version>

What it writes, in <release folder>/fomod/:
- ModuleConfig.xml: the install refused unless LooksMenu.esp is active and the game is 1.10.163 or newer (the only
  requirements a FOMOD can see in both Vortex and MO2 -- F4SE, Runtime Database and MCM are checked by the plugin
  in game and listed on the Nexus page); every top-level entry of the release folder installed as it is (read
  from the folder, so the installer and the archive cannot disagree). Pages, as the standard has them (rule 4 and
  4a, owner 2026-10-01): first "Checking your setup", ONE option holding the whole checklist; then one page per
  feature, its card and text shown without a click (MO2 highlights a page's first control, Vortex shows the first
  selected option of the first group -- so one Required option, alone in its group); last, a note shown only when
  AAF is missing.
- info.xml, images/*.jpg (the cards at 1000 px), and screenshot.png (MO2 shows that, not moduleImage).

Then it validates ModuleConfig.xml against tools/fomod/ModuleConfig5.0.xsd -- the schema Vortex itself validates
with (Nexus-Mods/fomod-installer, XmlScript5.0.xsd, GPL-3.0 like Silhouette; one stray space in a type name,
type=" xs:string", taken out: .NET reads past it, lxml does not) -- and checks that every image and
every source the installer names exists. Any failure exits non-zero: scripts/make-release.ps1 packs nothing.
"""
import html
import pathlib
import sys

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
XSD = ROOT / 'tools' / 'fomod' / 'ModuleConfig5.0.xsd'
IMG = ROOT / 'docs' / 'img'
SCHEMA = 'http://qconsulting.ca/fo3/ModConfig5.0.xsd'  # exactly: Vortex reads the version out of this text

# What Silhouette does: (name, picture in docs/img, plain-text description). Facts as docs/FEATURES.md has them.
FEATURES = [
    ('The picker window', 'window.jpg',
     'Aim at anyone, or pick yourself, and choose a body from a grid of pictures: it goes on them live while the '
     'camera frames them and they stand still. Apply keeps it; Cancel puts back exactly what they had. Open it with '
     'its hotkey or from MCM, and pick with the mouse.'),
    ('Bodies for everyone', 'pool.jpg',
     'Every NPC gets a body the first time you meet them, and keeps it: Silhouette\'s own pool, 41 bodies a sex -- '
     'mostly ordinary, some rough, a rare fine one.'),
    ('The named people', 'gallery.jpg',
     '60 named people have a body of their own, drawn from their stories; the Diamond City pack is sardonic on '
     'purpose.'),
    ('Faction bodies', 'factions.jpg',
     'The Brotherhood, the Minutemen, Gunners, raiders, the Institute and the other factions each draw from a pool '
     'in their own look. A switch in MCM turns it off.'),
    ('Clothes: ORefit', 'cloth.jpg',
     'While someone is dressed, the body is held together and lifted; under heavy clothes it is flattened. The '
     'moment they undress they are exactly their own body again.'),
    ('Variety', 'variety.jpg',
     'Every person their own small details within measured ranges, as OBody does. Two switches in MCM.'),
    ('Your own presets', 'ownpresets.jpg',
     'Your BodySlide presets join the picker by themselves, read the way BodySlide builds them. They are never '
     'handed out at random.'),
    ('One file for Old-Gen and Anniversary', 'runtimes.jpg',
     'One plugin for Fallout 4 1.10.163 and Anniversary 1.11.x, through Runtime Database. Old saves get a fresh '
     'start by themselves.'),
]
# Rule 4a: every requirement on one option, so the whole list shows at once. "found" is plain fact on this page: the
# install is refused before it when LooksMenu is missing.
SETUP = ('Your setup',
         'LooksMenu: found -- its BodyGen gives every body.\n'
         'F4SE: check this yourself -- runs every DLL mod (f4se.silverlock.org).\n'
         'Runtime Database: check this yourself -- finds the game\'s functions on old-gen and Anniversary (Nexus 108394).\n'
         'MCM: check this yourself -- the settings, the hotkeys and the picker buttons.\n'
         'Invisible Dead Body Fix: check this yourself -- without it, BodyGen leaves corpses the game places with only '
         'head and hands (Nexus 93614, the build for your game version).\n'
         'BodySlide: check this yourself -- your body (CBBE for women, BodyTalk for men; one is enough) and your '
         'outfits built from a zeroed preset with Build Morphs ticked.\n'
         'Silhouette checks the rest in game and says what is missing.')
AAF_NOTE = ('AAF is not active',
            'Without AAF, Silhouette cannot tell when someone is in an AAF scene, so a body change asked for then is '
            'not held back until the scene ends. Everything else works.')


def esc(text):
    return html.escape(text, quote=True)


def option(name, description, image=None, flag='shown'):
    picture = f'\n              <image path="fomod\\images\\{image}"/>' if image else ''
    return f'''            <plugin name="{esc(name)}">
              <description>{esc(description)}</description>{picture}
              <conditionFlags><flag name="{flag}">1</flag></conditionFlags>
              <typeDescriptor><type name="Required"/></typeDescriptor>
            </plugin>'''


def module_config(entries):
    installs = []
    for e in entries:
        kind = 'folder' if e.is_dir() else 'file'
        installs.append(f'    <{kind} source="{esc(e.name)}" destination="{esc(e.name)}" priority="0"/>')
    def page(step, group, opt, visible=''):
        return f'''    <installStep name="{esc(step)}">{visible}
      <optionalFileGroups order="Explicit">
        <group name="{esc(group)}" type="SelectAll">
          <plugins order="Explicit">
{opt}
          </plugins>
        </group>
      </optionalFileGroups>
    </installStep>'''
    pages = [page('Checking your setup', 'Requirements', option(SETUP[0], SETUP[1], flag='setup'))]
    pages += [page(n, n, option(n, d, img)) for n, img, d in FEATURES]
    pages.append(page('Note: AAF', 'Read this', option(AAF_NOTE[0], AAF_NOTE[1], flag='note_aaf'), '''
      <visible>
        <dependencies operator="Or">
          <fileDependency file="AAF.esm" state="Missing"/>
          <fileDependency file="AAF.esm" state="Inactive"/>
        </dependencies>
      </visible>'''))
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<!-- GENERATED by tools/fomod_pack.py (nexus-tools/docs/FOMOD-STANDARD.md). Edit the tool, not this file. -->
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="{SCHEMA}">
  <moduleName>Silhouette</moduleName>
  <moduleImage path="fomod\\images\\window.jpg"/>
  <moduleDependencies operator="And">
    <gameDependency version="1.10.163.0"/>
    <fileDependency file="LooksMenu.esp" state="Active"/>
  </moduleDependencies>
  <requiredInstallFiles>
{chr(10).join(installs)}
  </requiredInstallFiles>
  <installSteps order="Explicit">
{chr(10).join(pages)}
  </installSteps>
</config>
'''


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    out, version = pathlib.Path(sys.argv[1]), sys.argv[2]
    entries = sorted((e for e in out.iterdir() if e.name.lower() != 'fomod'), key=lambda e: e.name.lower())
    if not entries:
        sys.exit(f'{out} holds nothing to install')
    fomod = out / 'fomod'
    (fomod / 'images').mkdir(parents=True, exist_ok=True)
    for _name, image, _d in FEATURES:
        src = IMG / image
        if not src.exists():
            sys.exit(f'no picture {src}')
        pic = Image.open(src).convert('RGB')
        pic.resize((1000, round(1000 * pic.height / pic.width)), Image.LANCZOS).save(fomod / 'images' / image, quality=86)
    Image.open(IMG / 'window.jpg').convert('RGB').resize((1000, 563), Image.LANCZOS).save(fomod / 'screenshot.png')
    config = module_config(entries)
    # UTF-8 with a BOM: both managers read it (Vortex detects it, MO2 retries encodings).
    (fomod / 'ModuleConfig.xml').write_text(config, encoding='utf-8-sig')
    (fomod / 'info.xml').write_text(f'''<?xml version="1.0" encoding="UTF-8"?>
<fomod>
  <Name>Silhouette</Name>
  <Author>Dudu'sButt</Author>
  <Version>{esc(version)}</Version>
  <Website>https://www.nexusmods.com/fallout4/mods/109439</Website>
  <Description>OBody NG's body distribution for Fallout 4.</Description>
</fomod>
''', encoding='utf-8-sig')

    from lxml import etree
    schema = etree.XMLSchema(etree.parse(str(XSD)))
    doc = etree.parse(str(fomod / 'ModuleConfig.xml'))
    if not schema.validate(doc):
        sys.exit('ModuleConfig.xml fails the 5.0 schema:\n' + '\n'.join(str(e) for e in schema.error_log))
    for el in doc.iter('image', 'moduleImage'):
        if not (out / el.get('path').replace('\\', '/')).exists():
            sys.exit(f'the installer shows {el.get("path")}, which is not in the release')
    for el in doc.iter('file', 'folder'):
        if not (out / el.get('source')).exists():
            sys.exit(f'the installer installs {el.get("source")}, which is not in the release')
    print(f'fomod: {len(entries)} entries installed as they are, the setup page, {len(FEATURES)} feature pages, AAF note; '
          f'valid against {XSD.name}')


if __name__ == '__main__':
    main()
