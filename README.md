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
