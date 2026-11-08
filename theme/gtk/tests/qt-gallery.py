#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render the bundled Kvantum theme with an isolated configuration directory."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--font-size', type=int, default=10)
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[1]
# Set the process environment before importing Qt. Never select a theme in the
# user's real Kvantum/qt5ct/qt6ct configuration just to run a preview.
isolated = tempfile.TemporaryDirectory(prefix='lunatic-qt-gallery-')
config = Path(isolated.name)
os.environ['XDG_CONFIG_HOME'] = str(config)
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
os.environ.pop('QT_QPA_PLATFORMTHEME', None)
shutil.copytree(ROOT / 'Ambiance/Kvantum', config / 'Kvantum/Ambiance')
(config / 'Kvantum/kvantum.kvconfig').write_text('[General]\ntheme=Ambiance\n')
from PySide6 import QtCore, QtGui, QtWidgets  # noqa: E402

app = QtWidgets.QApplication([])
style = QtWidgets.QStyleFactory.create('kvantum')
if style is None:
    print('SKIP: install the Qt 6 Kvantum style plugin')
    raise SystemExit(77)
app.setStyle(style)
app.setFont(QtGui.QFont('Ubuntu', args.font_size))
assert app.palette().color(QtGui.QPalette.Window).name() == '#f2f1f0', 'Bundled theme not loaded'
window = QtWidgets.QWidget()
layout = QtWidgets.QVBoxLayout(window)
layout.setContentsMargins(12, 12, 12, 12)
layout.setSpacing(8)
layout.addWidget(QtWidgets.QLabel('Ambiance / Qt Widgets / compact desktop widgets'))
metrics = {}
menubar = QtWidgets.QMenuBar()
menu = menubar.addMenu('File')
menu.addAction('Open')
menu.addAction('Save')
layout.addWidget(menubar)
metrics['menubar'] = menubar
row = QtWidgets.QHBoxLayout()
for title in ['Normal', 'Disabled', 'Default']:
    button = QtWidgets.QPushButton(title)
    button.setEnabled(title != 'Disabled')
    button.setDefault(title == 'Default')
    row.addWidget(button)
    metrics['button-' + title.lower()] = button
row.addStretch()
layout.addLayout(row)
row = QtWidgets.QHBoxLayout()
entry = QtWidgets.QLineEdit('Compact entries, full-size text')
row.addWidget(entry, 1)
metrics['entry'] = entry
spin = QtWidgets.QSpinBox()
spin.setValue(42)
row.addWidget(spin)
metrics['spin'] = spin
combo = QtWidgets.QComboBox()
combo.addItems(['Ambiance', 'Another theme'])
row.addWidget(combo)
metrics['combo'] = combo
layout.addLayout(row)
row = QtWidgets.QHBoxLayout()
for title, checked in [('Checked', True), ('Unchecked', False)]:
    w = QtWidgets.QCheckBox(title)
    w.setChecked(checked)
    row.addWidget(w)
    metrics['check-' + title.lower()] = w
row.addStretch()
layout.addLayout(row)
for closable in [False, True]:
    tabs = QtWidgets.QTabWidget()
    tabs.setTabsClosable(closable)
    for title in ['Style', 'Icons', 'Fonts', 'Settings']:
        tabs.addTab(QtWidgets.QLabel('Tab labels with real close buttons' if closable else 'Theme previews and settings'), title)
    tabs.setCurrentIndex(1)
    layout.addWidget(tabs)
    metrics['tabs-close' if closable else 'tabs-text'] = tabs.tabBar()
progress = QtWidgets.QProgressBar()
progress.setRange(0, 100)
progress.setValue(60)
progress.setTextVisible(False)
layout.addWidget(progress)
metrics["progress"] = progress
scale = QtWidgets.QSlider(QtCore.Qt.Horizontal)
scale.setValue(45)
layout.addWidget(scale)
metrics["scale"] = scale
listing = QtWidgets.QListWidget()
listing.addItems(['Selected item', 'Another item', 'A third item'])
listing.setCurrentRow(0)
listing.setFixedHeight(listing.sizeHintForRow(0) * 3 + 4)
layout.addWidget(listing)
window.resize(640, window.sizeHint().height())
window.show()
app.processEvents()
measurements = {}
for name, widget in metrics.items():
    natural = widget.sizeHint().height()
    allocated = widget.height()
    assert allocated >= natural, (name, allocated, natural)
    measurements[name] = {'natural': natural, 'allocated': allocated}
    if name.startswith('tabs-'):
        measurements[name]['header'] = natural
args.output.mkdir(parents=True, exist_ok=True)
window.grab().save(str(args.output / 'qt6.png'))
report = {'font-height': QtGui.QFontMetrics(app.font()).height(), 'toolkit': 'qt6', 'font': 'Ubuntu ' + str(args.font_size), 'widgets': measurements}
(args.output / 'qt6.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
window.close()
app.quit()
isolated.cleanup()
