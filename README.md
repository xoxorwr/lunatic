# Lunatic

Unity7-style launcher/dock and global-menu panel plugins, written in C without dependencies.

<p align="center">
  <img src="docs/img_0.png" alt="Lunatic launcher and global menu on Xfce">
</p>

## Launcher

Distro-independent X11/EWMH dock with application search, pinned apps, window switching, badges and urgency animations. Requires GTK 3; does not require Xfce.

```sh
make launcher
./launcher/lunatic-launcher
sudo make install-launcher
```

[Launcher requirements, settings and shortcuts](launcher/README.md).

## Panel plugins

Currently supports Xfce: global application menus, window titles and maximized-window controls. Includes a GTK 3 menu exporter.

```sh
make panel
sudo make install-panel
xfce4-panel -r
```

Add **Lunatic Global Menu** through **Panel → Add New Items**.

[Panel requirements, installation and configuration](panel/README.md).

## Both components

```sh
sudo make install       # build and install both
make check              # check both
make check-install      # verify staged installation
sudo make uninstall     # remove both
```

Use `uninstall-launcher` or `uninstall-panel` to remove one component.

Before uninstalling the panel, follow [removal steps](PACKAGING.md#removal).

## GTK theme

Classic Unity 7 Ambiance is included in [theme/gtk](theme/gtk/README.md), with
GTK 2/3/4 styles, a Kvantum theme for Qt Widgets, Xfce panel styling, and native
Xfwm4 title bars and controls. The ports retain compact Unity 7 proportions.
Install with `sudo make install-theme`; select **Ambiance** for both the GTK
and window manager themes. An optional settings helper matches Unity's button
layout and window behavior, with backup and restore support.
