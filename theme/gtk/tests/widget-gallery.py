#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render and measure the same desktop widgets under GTK 3 or GTK 4.

No session settings are changed. Theme providers are process-local.
"""
import argparse
import json
from pathlib import Path
import time
import gi

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gtk', choices=['3', '4'], required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--font', default='Ubuntu 10')
args = parser.parse_args()
gi.require_version('Gtk', args.gtk + '.0')
from gi.repository import Gtk, Gdk, Gio, GLib  # noqa: E402

V4 = args.gtk == '4'
ROOT = Path(__file__).resolve().parents[1]
if V4:
    if not Gtk.init_check() or Gdk.Display.get_default() is None:
        print('SKIP: GTK 4 gallery needs a display')
        raise SystemExit(77)
else:
    if not Gtk.init_check()[0]:
        print('SKIP: GTK 3 gallery needs a display')
        raise SystemExit(77)
Gtk.Settings.get_default().set_property('gtk-font-name', args.font)
Gtk.Settings.get_default().set_property('gtk-enable-animations', False)
provider = Gtk.CssProvider()
errors = []
provider.connect('parsing-error', lambda _p, _s, error: errors.append(str(error)))
provider.load_from_path(str(ROOT / 'Ambiance' / ('gtk-4.0' if V4 else 'gtk-3.20') / 'gtk.css'))
if errors:
    raise RuntimeError('\n'.join(errors))
if V4:
    Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), provider, 600)
else:
    Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), provider, 600)


def append(box, widget, expand=False):
    widget.set_hexpand(expand)
    if V4:
        box.append(widget)
    else:
        box.pack_start(widget, expand, expand, 0)


def child(parent, widget):
    parent.set_child(widget) if V4 else parent.add(widget)


def label(text):
    w = Gtk.Label(label=text)
    w.set_xalign(0)
    return w


metrics = {}
box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
for edge in ['top', 'bottom', 'start', 'end']:
    getattr(box, 'set_margin_' + edge)(12)
append(box, label('Ambiance / GTK ' + args.gtk + ' / compact desktop widgets'))
if V4:
    menu = Gio.Menu()
    submenu = Gio.Menu()
    submenu.append('Open', 'win.open')
    submenu.append('Save', 'win.save')
    menu.append_submenu('File', submenu)
    menubar = Gtk.PopoverMenuBar.new_from_model(menu)
else:
    menubar = Gtk.MenuBar()
    file_item = Gtk.MenuItem(label='File')
    submenu = Gtk.Menu()
    for title in ['Open', 'Save']:
        submenu.append(Gtk.MenuItem(label=title))
    file_item.set_submenu(submenu)
    menubar.append(file_item)
append(box, menubar)
metrics['menubar'] = menubar
row = Gtk.Box(spacing=6)
for title in ['Normal', 'Disabled', 'Suggested']:
    w = Gtk.Button(label=title)
    if title == 'Disabled':
        w.set_sensitive(False)
    if title == 'Suggested':
        w.add_css_class('suggested-action') if V4 else w.get_style_context().add_class('suggested-action')
    append(row, w)
    metrics['button-' + title.lower()] = w
append(box, row)
row = Gtk.Box(spacing=6)
entry = Gtk.Entry()
entry.set_text('Compact entries, full-size text')
append(row, entry, True)
metrics['entry'] = entry
spin = Gtk.SpinButton.new_with_range(0, 100, 1)
spin.set_value(42)
append(row, spin)
metrics['spin'] = spin
if V4:
    combo = Gtk.DropDown.new_from_strings(['Ambiance', 'Another theme'])
else:
    combo = Gtk.ComboBoxText()
    combo.append_text('Ambiance')
    combo.append_text('Another theme')
    combo.set_active(0)
append(row, combo)
metrics['combo'] = combo
append(box, row)
row = Gtk.Box(spacing=12)
for title, enabled in [('Checked', True), ('Unchecked', False)]:
    w = Gtk.CheckButton(label=title)
    w.set_active(enabled)
    append(row, w)
    metrics['check-' + title.lower()] = w
append(box, row)

notebooks = []
for close_buttons in [False, True]:
    n = Gtk.Notebook()
    for title in ['Style', 'Icons', 'Fonts', 'Settings']:
        tab = Gtk.Box(spacing=4)
        text = label(title)
        append(tab, text)
        if close_buttons:
            b = Gtk.Button()
            image = Gtk.Image.new_from_icon_name('window-close-symbolic') if V4 else Gtk.Image.new_from_icon_name('window-close-symbolic', Gtk.IconSize.MENU)
            child(b, image)
            if not V4:
                b.set_relief(Gtk.ReliefStyle.NONE)
            append(tab, b)
            metrics['tab-close'] = b
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        append(page, label('Theme previews and settings' if not close_buttons else 'Tab labels with real close buttons'))
        if not V4:
            tab.show_all()
        n.append_page(page, tab)
    append(box, n)
    notebooks.append(n)
    metrics['tabs-close' if close_buttons else 'tabs-text'] = n
progress = Gtk.ProgressBar()
progress.set_fraction(0.6)
append(box, progress)
metrics["progress"] = progress
scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
scale.set_value(45)
scale.set_draw_value(False)
append(box, scale)
metrics["scale"] = scale
listing = Gtk.ListBox()
for text in ['Selected item', 'Another item', 'A third item']:
    listing.append(label(text)) if V4 else listing.add(label(text))
listing.select_row(listing.get_row_at_index(0))
append(box, listing)
window = Gtk.Window() if V4 else Gtk.OffscreenWindow()
window.set_default_size(640, -1)
child(window, box)
if V4:
    window.present()
else:
    window.show_all()
for n in notebooks:
    n.set_current_page(1)
for _ in range(35):
    while GLib.MainContext.default().pending():
        GLib.MainContext.default().iteration(False)
    time.sleep(0.01)

measurements = {}
for name, widget in metrics.items():
    minimum, natural = widget.measure(Gtk.Orientation.VERTICAL, -1)[:2] if V4 else widget.get_preferred_height()
    allocated = widget.get_allocated_height()
    assert allocated >= minimum, (args.gtk, name, allocated, minimum)
    measurements[name] = {'minimum': minimum, 'natural': natural, 'allocated': allocated}
    if isinstance(widget, Gtk.Notebook):
        page = widget.get_nth_page(widget.get_current_page())
        page_height = page.measure(Gtk.Orientation.VERTICAL, -1)[1] if V4 else page.get_preferred_height()[1]
        measurements[name]['header'] = natural - page_height
for n in notebooks:
    for i in range(n.get_n_pages()):
        tab = n.get_tab_label(n.get_nth_page(i))
        minimum = tab.measure(Gtk.Orientation.VERTICAL, -1)[0] if V4 else tab.get_preferred_height()[0]
        allocated = tab.get_allocated_height()
        assert allocated >= minimum, ('clipped tab', i, allocated, minimum)
args.output.mkdir(parents=True, exist_ok=True)
png = args.output / ('gtk' + args.gtk + '.png')
if V4:
    paintable = Gtk.WidgetPaintable.new(window)
    snapshot = Gtk.Snapshot.new()
    paintable.snapshot(snapshot, window.get_width(), window.get_height())
    node = snapshot.to_node()
    assert node is not None
    texture = window.get_native().get_renderer().render_texture(node, None)
    texture.save_to_png(str(png))
else:
    window.get_pixbuf().savev(str(png), 'png', [], [])
report = {'toolkit': 'gtk' + args.gtk, 'font': args.font, 'widgets': measurements}
(args.output / ('gtk' + args.gtk + '.json')).write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
window.destroy()
