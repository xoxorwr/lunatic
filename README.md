# Lunatic

Unity's launcher and global menu, reimplemented in C with minimal dependencies and support for multiple desktop environments.

- [launcher/](launcher/README.md): Unity launcher port, including upstream snapshot, artwork, tests and documentation.
- [panel/](panel/README.md): global-menu implementation, with shared C code in `panel/common/` and a dedicated plugin for each desktop environment. Currently includes an Xfce adapter in `panel/xfce-plugin/`.

## Build

```sh
make                         # launcher only; no panel dependencies
./launcher/unity-launcher
make panel                   # optional Xfce global-menu plugin
make check                   # both components' checks
```

Launcher configuration and executable names remain unchanged. Panel installation is explicit: `make -C panel install`.
