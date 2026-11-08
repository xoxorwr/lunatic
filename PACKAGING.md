# Packaging

```sh
make launcher panel
make install PREFIX=/usr DESTDIR="$pkgdir"
make check-install
```

For split packages, use `install-launcher` and `install-panel`. Install/uninstall
only copy/remove files; no display, session bus, or package scripts are required.
Let the package manager remove its owned files. Upgrades must drop obsolete files;
for old source installs, use the old Makefiles to uninstall first.

The optional Ambiance theme is separate: `make install-theme PREFIX=/usr
DESTDIR="$pkgdir"` installs `$(DATADIR)/themes/Ambiance/` and
`$(BINDIR)/lunatic-xfce-settings`. Use `uninstall-theme` to remove them.
The theme tree includes GTK 2/3/4 and a Kvantum theme for Qt Widgets. Qt
activation requires the appropriate Kvantum style plugin and user selection;
installation never changes qt5ct/qt6ct or Kvantum settings.
The settings helper runs only when explicitly invoked by the desktop user;
package scripts must not run it. Restore its saved settings before removing
the theme if it was used.

## Paths

All paths accept overrides; `DESTDIR` is prepended for staging.

| Variable | Default |
| --- | --- |
| `PREFIX` | `/usr/local`; use `/usr` for distro packages |
| `BINDIR` | `$(PREFIX)/bin` |
| `DATADIR` | `$(PREFIX)/share` |
| `LIBDIR` | Xfce's lib/lib64/multiarch suffix under `PREFIX` |
| `GTK_MODULEDIR` | GTK's pkg-config libdir + `/gtk-3.0/modules` |

Keep Xfce's plugin library and descriptor prefixes paired. Custom paths must
match the target Xfce/XDG/GTK search paths. Use target pkg-config for cross builds.

| Package | Files |
| --- | --- |
| Launcher | `$(BINDIR)/lunatic-launcher` |
| Launcher | `$(DATADIR)/applications/lunatic-launcher.desktop` |
| Launcher | `$(DATADIR)/icons/hicolor/scalable/apps/lunatic-launcher.svg` |
| Panel | `$(LIBDIR)/xfce4/panel/plugins/liblunatic-global-menu.so` |
| Panel | `$(DATADIR)/xfce4/panel/plugins/lunatic-global-menu.desktop` |
| Panel | `$(GTK_MODULEDIR)/liblunatic-panel-gtk-module.so` |

## Removal

1. Uncheck **Hide title bars of maximized windows** in Properties, if enabled.
2. Remove Global Menu items from the panel. The last removal clears its GTK module
   setting; ordinary restarts preserve it.
3. Remove the package, or run `make uninstall` with the original path overrides.

Package removal preserves personal settings. Do not edit users' homes or contact
their sessions from package scripts. Loaded GTK modules remain until apps exit.

If files were removed first, reinstall and follow the steps above. Alternatively,
use `xfce4-settings-editor`: remove only `lunatic-panel-gtk-module` from
**xsettings → /Gtk/Modules**, preserving other colon-separated entries; set
**xfwm4 → /general/titleless_maximize** to false if needed. Remove stale panel
items and fully restart affected applications.

## Upgrade from the previous names

Remove the old **Unity Global Menu** panel items and uninstall the old version
before installing Lunatic. Add **Lunatic Global Menu** afterward and fully restart
GTK applications. The launcher imports old settings into `lunatic/launcher.ini`.
Keep `com.canonical.*` protocol names unchanged: they provide app compatibility.
