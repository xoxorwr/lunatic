#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check theme payload staging and removal with default and custom prefixes."""
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import configparser

ROOT = Path(__file__).resolve().parents[1]
config = configparser.ConfigParser(interpolation=None)
config.read(ROOT / 'Ambiance/Kvantum/Ambiance.kvconfig')
assert config['%General']['author'] == 'Tsu Jan'
svg = ET.parse(ROOT / 'Ambiance/Kvantum/Ambiance.svg')
ids = {node.get('id') for node in svg.iter() if node.get('id')}
for node in svg.iter():
    link = node.get('{http://www.w3.org/1999/xlink}href')
    if link and link.startswith('#'):
        assert link[1:] in ids, link
# The engine intentionally falls back to its built-in SVG for missing optional
# elements. Core Ambiance controls must be provided by our own artwork.
for prefix in ['button', 'lineedit', 'tab', 'checkbox', 'radio', 'menuitem']:
    assert any(name.startswith(prefix + '-') for name in ids), prefix

for prefix in ['/usr/local', '/opt/lunatic']:
    with tempfile.TemporaryDirectory(prefix='lunatic-theme-') as directory:
        stage = Path(directory)
        variables = [f'DESTDIR={stage}', f'PREFIX={prefix}']
        subprocess.run(['make', '-s', '-C', str(ROOT), 'install', *variables], check=True)
        installed = stage / prefix.lstrip('/') / 'share/themes/Ambiance'
        for source in (ROOT / 'Ambiance').rglob('*'):
            if source.is_file():
                target = installed / source.relative_to(ROOT / 'Ambiance')
                assert source.read_bytes() == target.read_bytes(), target
        # Relative GTK 4 imports must resolve after staging, not just in-tree.
        assert (installed / 'gtk-4.0/../gtk-3.20/gtk-tabs.css').is_file()
        marker = installed.parent / 'other-theme'
        marker.mkdir()
        (marker / 'keep').write_text('keep')
        subprocess.run(['make', '-s', '-C', str(ROOT), 'uninstall', *variables], check=True)
        assert not installed.exists()
        assert (marker / 'keep').read_text() == 'keep'
print('Validated Qt SVG references, complete staged theme payloads and removal.')
