# Ambiance

Classic Unity 7 Ambiance for GTK 2/3/4 and Qt Widgets, with Xfce panel styling
and Xfwm4 decorations. GTK 2 requires Murrine (`gtk2-engines-murrine` on
Debian/Ubuntu). Qt requires Kvantum for each Qt major version in use.

GTK 3/4 tabs use warm-grey inactive tabs and raised ivory selected tabs, with a
bold selected label. The selection stays distinct in unfocused windows. This
applies to standard notebooks, including Thunar and Terminator; application
specific tab styles retain their overrides.

GTK 4 and Qt retain the compact desktop proportions of GTK 2/3 and Unity 7.
They inherit the desktop font and scale rather than forcing small text or a
fixed widget height. The existing GTK 2/3 and Xfwm4 styles are unchanged by
these ports. GTK 4 shares the GTK 3 tab stylesheet; Qt uses matching artwork.

## Install

From the repository root:

```sh
sudo make install-theme
sudo make uninstall-theme
```

Installs to `/usr/local/share/themes/Ambiance`. For a per-user install, use
`make install-theme PREFIX="$HOME/.local"`; use the same prefix to uninstall.
`DATADIR`, `THEMEDIR` and `DESTDIR` are supported for packaging.

The same install command includes `gtk-4.0/` and `Kvantum/`. Installation
does not change session settings or overwrite user configuration files.

## GTK 4

Choose Ambiance as the desktop GTK theme, then restart GTK 4 applications.
This port targets plain GTK 4 widgets, including client-side header bars.
It is a classic light theme.

Libadwaita applications supply their own stylesheet and do not necessarily
follow the desktop GTK theme. Full libadwaita restyling is not included. No
global `~/.config/gtk-4.0/gtk.css` override is installed. Sandboxed applications
also need access to the theme through their runtime's theme integration.

## Qt Widgets

Install Kvantum's Qt 5 and/or Qt 6 style plugin for the applications you use.
In **Kvantum Manager → Install/Update Theme**, choose the installed directory
`/usr/local/share/themes/Ambiance/Kvantum` (adjust for your installation prefix),
then select **Ambiance** in **Change/Delete Theme**. Importing creates a user
copy; repeat Install/Update Theme after upgrading the bundled theme.

On Xfce, select **kvantum** as the widget style in your distribution's
qt5ct/qt6ct configuration. For a single application, test with:

```sh
application -style kvantum
```

Qt 5 and Qt 6 use the same SVG/configuration files but require their respective
style plugins. The supplied gallery is tested with Qt 6. Qt Quick/QML controls
and applications with custom widget styles are outside this port's scope.
Window-manager title bars continue to use the existing Xfwm4 theme.

## Xfce

Select **Ambiance** in both **Appearance → Style** and **Window Manager → Style**.
For the panel, use the system/theme background, dark mode off, full opacity,
and a 24px horizontal layout with one row.

Optional Unity-style window settings, with backup and restore:

```sh
lunatic-xfce-settings preview
lunatic-xfce-settings apply
lunatic-xfce-settings restore
```

Run without sudo; requires Python 3 and PyGObject. Add `--panel ID` to `apply`
for panel size/background settings. Restore before uninstalling the theme.
Restart the panel with `xfce4-panel -r` after changes.

For maximized-window controls, add **Lunatic Global Menu** and enable
**Hide maximized title bars** in its Properties. Disable it before removing
that plugin. Application-drawn header bars remain application controlled.

## Development

```sh
make -C theme/gtk generate-xfwm
make -C theme/gtk check
make -C theme/gtk check-ui
```

`check` validates artwork and staged installation/removal without a display.
`check-ui` needs GTK 3/4 introspection, PyGObject, PySide6, the Qt 6 Kvantum
plugin, and a display for GTK. It renders comparable galleries at Ubuntu 10
and 14 pt, checks that allocated sizes fit their contents, and compares new
control/tab heights with GTK 3 (allowing two logical pixels for font rounding).
Unavailable displays/styles are reported as skipped. Previews and measurement
JSON files go to `/tmp/lunatic-theme-preview`; override with `PREVIEW_DIR`.
Qt runs offscreen with a temporary configuration; previews never select a
theme in the user's session.

## Source and licenses

Based on [Ubuntu's ubuntu-themes 24.04 source](https://archive.ubuntu.com/ubuntu/pool/main/u/ubuntu-themes/ubuntu-themes_24.04.orig.tar.gz).
See [COPYRIGHT](COPYRIGHT) and [COPYING](COPYING) for upstream attribution and
licenses. Local Xfce integration, generator and settings helper are GPL-3.0-only.
The Qt port adapts KvAmbiance from Kvantum 1.1.8 by Pedram Pourang (Tsu Jan),
under GPL-3.0-or-later. See [Kvantum/README.md](Ambiance/Kvantum/README.md) and
its accompanying license. GTK 4 widget styling and gallery tools are
GPL-3.0-only; the adapted upstream named-color file retains its original license.
