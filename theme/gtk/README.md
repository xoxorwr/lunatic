# Ambiance

Classic Unity 7 Ambiance, imported from Ubuntu's `ubuntu-themes`
source release `24.04` (the `light-themes` package). This release retains the
original GTK 2/3 theme; the version number is not a GTK version.

`Ambiance/` includes GTK 2, GTK 3 before 3.20, GTK 3.20 and later, and the
matching Metacity, Unity and Xfwm4 window decorations. The `gtk-3.20/assets` symlink
shares the GTK 3 assets and must be preserved when copying the theme.
There is no GTK 4 theme.

## Install

From the repository root:

```sh
sudo make install-theme
sudo make uninstall-theme
```

These targets also work as `make -C theme/gtk install` and
`make -C theme/gtk uninstall`. The default destination is
`/usr/local/share/themes/Ambiance`. For a per-user installation, use
`make install-theme PREFIX="$HOME/.local"`, and use the same override when
uninstalling. `DATADIR` and `THEMEDIR` can override the theme location;
`DESTDIR` is prepended for package staging. Installation preserves the
asset symlink and includes the upstream license notices.

The root `install` and `uninstall` targets continue to manage the launcher
and panel; use the theme targets separately.

Select **Ambiance** in your desktop's appearance settings. On Xfce, choose it
under **Settings → Appearance → Style**. To preview a single GTK 3 application:

```sh
GTK_THEME=Ambiance your-gtk3-application
```

GTK 2 applications require the Murrine theme engine (`gtk2-engines-murrine`
on Ubuntu/Debian). GTK 3 uses CSS without an external engine. The upstream
metatheme recommends `ubuntu-mono-dark` icons and `DMZ-White` cursors; these
are installed separately.

## Xfce

Choose **Ambiance** in both **Appearance → Style** and **Window Manager → Style**.
`Ambiance/xfwm4/` sits alongside `unity/` and `metacity-1/`; it reuses the
original PNG buttons, with native XPM frames, hover/pressed states and
separate maximize/restore artwork. Frames have a 28px title bar and 3px resize
handles, since Xfwm4 does not use Unity's invisible resize extents. Auxiliary
shade/stick controls are drawn to match. Regenerate the XPM files and artwork
links with `make -C theme/gtk generate-xfwm`.
`make -C theme/gtk check` validates the XPM data, frame geometry and artwork
links. Installation replaces the managed `xfwm4/` directory so upgrades remove
obsolete generated assets.

GTK 3.20+ automatically loads `gtk-3.20/apps/xfce.css`. It gives the panel
Unity's charcoal gradient, light text and compact buttons, keeps it lit when
applications have focus, and preserves readable popup menus. Lunatic's panel
window controls use the same Xfwm4 artwork. Rebuild/reinstall the panel plugin
to enable the bold application-title styling.

In **Panel Preferences → Appearance**, select the system/theme background,
disable dark mode, and use full opacity. Set a horizontal top panel to 24px
with one row. Existing custom backgrounds or user GTK CSS can override the
theme; this does not require replacing your panel layout.

The optional settings helper requires Python 3 and PyGObject (`python3-gi`
on Ubuntu/Debian). Run it as your desktop user after installing the theme:

```sh
lunatic-xfce-settings preview
lunatic-xfce-settings apply
# Optionally include an existing panel's size/background settings:
# lunatic-xfce-settings apply --panel 1
xfce4-panel -r
```

It matches GTK header-bar and Xfwm button order (close, minimize, maximize on
the left), aligns window titles left, disables scroll-to-shade, enables
double-click maximize and drag-to-restore, removes maximized borders, and
disables dock shadows. It backs up the affected values before applying them;
`lunatic-xfce-settings restore` restores those values and removes properties
that did not previously exist. Restore before uninstalling the theme. Installing
or uninstalling files alone does not change your session settings.

For Unity-style maximized title bars, add **Lunatic Global Menu** to the panel
and enable **Hide maximized title bars** in its Properties. The plugin manages
Xfwm4's `titleless_maximize` setting; disable that option before removing it.
GTK client-side header bars remain application controlled. GTK's decoration
layout affects compatible header bars, but does not move them into the panel.
Xfwm4's compositor and Alt+Tab behavior remain distinct from Unity's Compiz.

The integration follows the [Xfce panel theming selectors](https://docs.xfce.org/xfce/xfce4-panel/theming)
and [Xfwm4 theme format](https://wiki.xfce.org/howto/xfwm4_theme).

## Source and licenses

- Upstream: <https://launchpad.net/ubuntu-themes>
- Archive: <https://archive.ubuntu.com/ubuntu/pool/main/u/ubuntu-themes/ubuntu-themes_24.04.orig.tar.gz>
- SHA-256: `fd2330c0369251ef3a49735eed4f5287b4b3ce50b8c8e5d503ebab008c5e0f19`
- Ubuntu source manifest: <https://archive.ubuntu.com/ubuntu/pool/main/u/ubuntu-themes/ubuntu-themes_24.04-0ubuntu1.dsc>

[COPYRIGHT](COPYRIGHT) preserves Ubuntu's attribution and artwork license
notice from `ubuntu-themes_24.04-0ubuntu1.diff.gz` (`debian/copyright`).
[COPYING](COPYING) preserves the upstream GPL text. Existing license notices
inside the theme files are retained.

Local additions are `xfwm4/`, `gtk-3.20/apps/xfce.css` and its import in
`gtk-main.css`, the XPM generator and the settings helper. New code is
GPL-3.0-only; upstream button artwork retains its original license.

The import also retains upstream's missing image references for toolbar
combobox borders and, in GTK 3.20+, two LibreOffice button images. GTK 3.24
loads the stylesheet successfully; those specific images are absent from
the source archive.
