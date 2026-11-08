#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare real GTK 4 / Qt controls with GTK 3 at normal and larger text sizes."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
HERE = Path(__file__).resolve().parent
for size in [10, 14]:
    output = args.output / str(size)
    available = []
    for toolkit in ['gtk3', 'gtk4', 'qt6']:
        script = 'qt-gallery.py' if toolkit == 'qt6' else 'widget-gallery.py'
        options = ['--font-size', str(size)] if toolkit == 'qt6' else ['--gtk', toolkit[-1], '--font', f'Ubuntu {size}']
        result = subprocess.run([sys.executable, str(HERE / script), '--output', str(output), *options], text=True, stdout=subprocess.PIPE)
        if result.returncode == 77:
            print(result.stdout.strip())
            continue
        if result.returncode:
            raise SystemExit(result.returncode)
        available.append(toolkit)
    if 'gtk3' not in available:
        print('SKIP: density comparison requires the GTK 3 reference gallery')
        continue
    reports = {toolkit: json.loads((output / (toolkit + '.json')).read_text())['widgets'] for toolkit in available}
    reference = reports['gtk3']
    for toolkit, widgets in reports.items():
        for name in ['button-normal', 'entry', 'spin', 'combo', 'check-checked', 'menubar', 'scale']:
            # Fonts have different rasterization and rounding in each toolkit.
            # Two logical pixels permit that, but reject touch-sized controls.
            assert widgets[name]['natural'] <= reference[name]['natural'] + 2, (size, toolkit, name, widgets[name], reference[name])
        # QProgressBar reserves a font line plus 8px in its own sizeHint,
        # even with text hidden. Respect that native floor instead of clipping.
        line_height = json.loads((output / (toolkit + '.json')).read_text()).get('font-height', 0)
        assert widgets['progress']['natural'] <= max(reference['progress']['natural'], line_height + 8 if line_height else 0) + 2, (size, toolkit, widgets['progress'])
        for name in ['tabs-text', 'tabs-close']:
            assert widgets[name]['header'] <= reference[name]['header'] + 2, (size, toolkit, name, widgets[name], reference[name])
    print(f'PASS: compact controls and unclipped allocations at Ubuntu {size}: {", ".join(available)}')
