#!/usr/bin/env python3
"""Bake upstream artwork for dependency-free runtime decoding.

Only regeneration needs rsvg-convert. Normal builds use the checked-in PNGs.
Default 1x, 2x, 3x and 4x sizes are rasterized directly from the original SVG.
The 8x copy covers larger icon-size settings without upsampling.
"""
import pathlib, shutil, subprocess, xml.etree.ElementTree as ET
root = pathlib.Path(__file__).resolve().parent.parent
source = root / 'upstream/resources'
out = root / 'data/assets'
out.mkdir(parents=True, exist_ok=True)
sizes = {
 'launcher_mark.svg': 48,
 'progress_bar_trough.svg': 54,
 'progress_bar_fill.svg': 42,
}
subprocess.run(['python3', str(root / 'tools/bake-attention.py')], check=True)
entries=[('attention-halo.png/1', 'attention-halo.png')]
for p in [*sorted(source.iterdir()), root / 'data/launcher_mark.svg']:
 if p.name == 'launcher_bfb.svg': continue  # Keep upstream branding in the reference snapshot only.
 if p.suffix not in {'.svg','.png'}: continue
 if p.suffix=='.png':
  dest=out/(p.name+'.png');shutil.copyfile(p,dest)
  entries.append((p.name+'/1',dest.name));continue
 size=sizes.get(p.name)
 if size is None:
  size=int(p.stem.rsplit('_',1)[1])
 for scale in [1,2,3,4,8]:
  dest=out/(p.name+f'@{scale}.png')
  subprocess.run(['rsvg-convert','--keep-aspect-ratio','--width',str(size*scale),
                  '--height',str(size*scale),'--output',str(dest),str(p)],check=True)
  entries.append((p.name+f'/{scale}',dest.name))
resource=ET.Element('gresources')
group=ET.SubElement(resource,'gresource',{'prefix':'/org/xoxorwr/lunatic/launcher'})
for alias,name in entries:
 ET.SubElement(group,'file',{'alias':alias}).text='assets/'+name
ET.indent(resource)
ET.ElementTree(resource).write(root/'data/lunatic.gresource.xml',encoding='UTF-8',xml_declaration=True)
