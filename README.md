# Unity desktop components for Xfce

- [launcher/](launcher/README.md): the C port of Unity's launcher, including its
  upstream snapshot, artwork, tests and documentation.
- [panel/](panel/README.md): initial global-menu implementation. Shared C code is
  in `panel/common/`; the Xfce panel adapter is in `panel/xfce-plugin/`.

```sh
make                         # launcher only; no panel dependencies
./launcher/unity-launcher
make panel                   # optional Xfce global-menu plugin
make check                   # both components' checks
```

Components can also be built independently with `make -C launcher` and
`make -C panel`. Launcher configuration and installed executable names are
unchanged. Panel installation is explicit: `make -C panel install`.
# lunatic
