#!/usr/bin/env python3
"""Apply or restore the optional Ambiance settings in the current Xfce session."""
# SPDX-License-Identifier: GPL-3.0-only

import argparse
import json
import os
from pathlib import Path
import sys


def settings(panel=None):
    result = [
        ("xsettings", "/Net/ThemeName", "s", "Ambiance"),
        ("xsettings", "/Gtk/DecorationLayout", "s", "close,minimize,maximize:"),
        ("xfwm4", "/general/theme", "s", "Ambiance"),
        ("xfwm4", "/general/button_layout", "s", "CHM|"),
        ("xfwm4", "/general/title_alignment", "s", "left"),
        ("xfwm4", "/general/double_click_action", "s", "maximize"),
        ("xfwm4", "/general/mousewheel_rollup", "b", False),
        ("xfwm4", "/general/borderless_maximize", "b", True),
        ("xfwm4", "/general/restore_on_move", "b", True),
        ("xfwm4", "/general/show_dock_shadow", "b", False),
    ]
    if panel is not None:
        prefix = f"/panels/panel-{panel}/"
        result += [("xfce4-panel", prefix + name, kind, value) for name, kind, value in [
            ("size", "u", 24), ("nrows", "u", 1),
            ("background-style", "u", 0), ("dark-mode", "b", False),
            ("enter-opacity", "u", 100), ("leave-opacity", "u", 100),
        ]]
    return result


class Xfconf:
    def __init__(self):
        from gi.repository import Gio, GLib
        self.Gio, self.GLib = Gio, GLib
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def call(self, method, signature, values):
        return self.bus.call_sync(
            "org.xfce.Xfconf", "/org/xfce/Xfconf", "org.xfce.Xfconf",
            method, self.GLib.Variant(signature, values), None,
            self.Gio.DBusCallFlags.NONE, 3000, None)

    def read(self, channel, prop):
        try:
            return self.call("GetProperty", "(ss)", (channel, prop)).get_child_value(0).get_variant()
        except self.GLib.Error as error:
            if self.Gio.DBusError.get_remote_error(error) in {
                    "org.xfce.Xfconf.Error.PropertyNotFound", "org.xfce.Xfconf.Error.ChannelNotFound"}:
                return None
            raise

    def write(self, channel, prop, value):
        if value is None:
            if self.read(channel, prop) is not None:
                self.call("ResetProperty", "(ssb)", (channel, prop, False))
        else:
            self.call("SetProperty", "(ssv)", (channel, prop, value))

    def restore(self, entries):
        # Restore the GTK theme last, after the panel and window manager.
        for entry in reversed(entries):
            value = None if entry["value"] is None else self.GLib.Variant.parse(
                self.GLib.VariantType.new(entry["type"]), entry["value"], None, None)
            self.write(entry["channel"], entry["property"], value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["preview", "apply", "restore"])
    parser.add_argument("--panel", type=int, help="also style this existing panel ID (24px, theme background)")
    state = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state"))
    parser.add_argument("--backup", type=Path, default=state / "lunatic/ambiance-xfce.json")
    args = parser.parse_args()
    if args.action == "preview":
        for channel, prop, kind, value in settings(args.panel):
            print(f"{channel} {prop} = {value}")
        return
    if os.geteuid() == 0:
        parser.error("run as your desktop user, without sudo")
    if args.action == "apply" and args.backup.exists():
        parser.error(f"a backup already exists at {args.backup}; restore it before applying again")
    xfconf = Xfconf()
    if args.action == "restore":
        entries = json.loads(args.backup.read_text())["settings"]
        xfconf.restore(entries)
        args.backup.unlink()
        print("Previous settings restored. Restart the panel with: xfce4-panel -r")
        return
    if args.panel is not None:
        panels = xfconf.read("xfce4-panel", "/panels")
        if panels is None or args.panel not in panels.unpack():
            parser.error(f"panel {args.panel} does not exist; inspect: xfconf-query -c xfce4-panel -p /panels")
    entries, updates = [], []
    for channel, prop, kind, value in settings(args.panel):
        old = xfconf.read(channel, prop)
        if old is not None:
            actual = old.get_type_string()
            if actual != kind and not (actual in {"i", "u"} and kind in {"i", "u"}):
                parser.error(f"unexpected type {actual} for {channel} {prop}")
            kind = actual
        entries.append({"channel": channel, "property": prop,
                        "type": old.get_type_string() if old is not None else None,
                        "value": old.print_(True) if old is not None else None})
        updates.append((channel, prop, xfconf.GLib.Variant(kind, value)))
    args.backup.parent.mkdir(parents=True, exist_ok=True)
    with args.backup.open("x") as backup:
        json.dump({"settings": entries}, backup, indent=2)
        backup.flush()
        os.fsync(backup.fileno())
    try:
        for channel, prop, value in updates:
            xfconf.write(channel, prop, value)
    except Exception:
        xfconf.restore(entries)
        args.backup.unlink()
        raise
    print(f"Ambiance settings applied. Backup: {args.backup}")
    print("Restart the panel with: xfce4-panel -r")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        sys.exit(str(error))
