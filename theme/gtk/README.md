# Ambiance

Classic Unity 7 GTK 2/3 theme with Xfce panel styling and Xfwm4 decorations.
GTK 2 requires Murrine (`gtk2-engines-murrine` on Debian/Ubuntu).

GTK 3 tabs use warm-grey inactive tabs and raised ivory selected tabs, with a
bold selected label. The selection stays distinct in unfocused windows. This
applies to standard notebooks, including Thunar and Terminator; application
specific tab styles retain their overrides.

## Install

From the repository root:

```sh
sudo make install-theme
sudo make uninstall-theme
```

Installs to `/usr/local/share/themes/Ambiance`. For a per-user install, use
`make install-theme PREFIX="$HOME/.local"`; use the same prefix to uninstall.
`DATADIR`, `THEMEDIR` and `DESTDIR` are supported for packaging.

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
```

## Source and licenses

Based on [Ubuntu's ubuntu-themes 24.04 source](https://archive.ubuntu.com/ubuntu/pool/main/u/ubuntu-themes/ubuntu-themes_24.04.orig.tar.gz).
See [COPYRIGHT](COPYRIGHT) and [COPYING](COPYING) for upstream attribution and
licenses. Local Xfce integration, generator and settings helper are GPL-3.0-only.
