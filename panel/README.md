# Lunatic Global Menu

C plugin for **Xfce on X11**. Shows the active application's name, window title,
menus and maximized-window controls.

Dependencies: GTK 3/GIO, GModule, Xlib and libxfce4panel.

## Install

From the repository root:

```sh
make panel
sudo make install-panel
xfce4-panel -r
```

Add **Lunatic Global Menu** through **Panel → Add New Items**.
[Packaging and removal](../PACKAGING.md).

## Menu support

| Application stack | Menu support | Export requirement |
| --- | --- | --- |
| GTK 3 | Built in | Native GMenu/GAction exports or standard menubars via the included GTK module |
| GTK 2 | Conditional | External Canonical DBusMenu exporter |
| GTK 4 | Conditional | Application-provided GMenu/GAction or Canonical DBusMenu export; no bundled GTK 4 exporter |
| Qt 5 / Qt 6 Widgets | Conditional | Platform integration exporting Canonical DBusMenu |
| Electron | Conditional | Native X11 global-menu export |
| Custom-drawn menus | No automatic export | Application must publish one of the supported protocols |

The GTK 3 module is enabled automatically through Xfce settings. Fully restart
applications after installation or updates. Removing the last panel instance
disables the module.

Titles and window controls work independently of menu export. Wayland and other
panel hosts are not supported.

## Controls

The title overlays the controls and menus without reserving horizontal space.
Hover, keyboard focus, or holding Alt reveals the action row; open menus keep it
visible. Menu underlines appear while Alt is held, using XKB events without grabs.

Fully maximized windows also show **Close**, **Minimize** and **Restore**, using
xfwm4's current theme artwork, button order, and hover/pressed states. Theme
changes reload the cached artwork. Right-click → **Properties**:

- **Maximum title width:** 40–1200 logical pixels; default 200.
- **Hide maximized title bars:** enabled by default; changes xfwm4's session-wide
  setting. Disable before removing the plugin to restore title bars.
  Application-drawn header bars are unaffected.

## Development

```sh
make -C panel check
make -C panel demo
make check-install
```

GPL-3.0-only; see [COPYING](../COPYING).
