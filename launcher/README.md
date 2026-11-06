# Lunatic Launcher for X11

Part of Lunatic by **xoxorwr**, derived from Unity 7's source and artwork.
Runs on X11/EWMH desktops, including Xfce. Requires GTK 3; transparency needs
compositing. Native Wayland is unsupported.

## Build and install

Requires a C11 compiler, Make, pkg-config, GLib's resource compiler, and development
files for GTK 3/GIO, Xlib, XInput2 and XRender. No Unity services or OpenGL runtime required.
Commands below run from `launcher/`:

```sh
make
./lunatic-launcher
make install PREFIX="$HOME/.local"
./lunatic-launcher --autohide --icon-size 48 --monitor 0
./lunatic-launcher --pin firefox.desktop --pin thunar.desktop
./lunatic-launcher --help
```

Settings and favorites: `$XDG_CONFIG_HOME/lunatic/launcher.ini`
(default `~/.config/lunatic/launcher.ini`). Existing legacy launcher settings are
imported when this file is absent.

## Settings

Right-click → **Settings…**. Changes apply immediately and persist.

| Section | Options |
| --- | --- |
| Behavior | Auto hide; Slide/Fade/Fade and slide; reveal sensitivity; minimize single-window apps on click; all workspaces (default off) |
| Appearance | Transparency; wallpaper/custom color; all/primary monitor; left/right edge |
| Icons | Size 24–96 px; Show Desktop (default off); Trash (default on) |

System icons stay at the bottom: Show Desktop above Trash. Each dock fills the
monitor's available height around existing panels. All monitors share one process.
Interior monitor edges cannot reserve space through EWMH struts. Fullscreen
windows can cover the dock. Use `--no-autohide` to keep it visible otherwise.

## Controls

| Action | Input |
| --- | --- |
| Focus application / show active group's windows | Click icon |
| New instance | Middle-click or Shift-click |
| Window list, desktop actions, pin/unpin, close | Right-click icon |
| Pin / reorder | Drop a desktop file / drag an icon |
| Open Dash search | Top button or tap left Super |
| Search / launch / dismiss | Type / Enter / Escape; clicking outside also dismisses |
| Pin a Dash result | Hover and click its plus control |
| Activate application 1–10 | Super+1…9, Super+0; add Shift for a new instance |
| Keyboard navigation | Super+F1, then Up/Down, Enter, Menu, Escape |

Existing shortcut owners retain their bindings. Launched applications survive
launcher exit. Window-count markers are separate from application badges.

Transparent Dash backgrounds use a blurred desktop snapshot captured on opening,
with the dock's color over it. The blur is cached while open; text and icons stay
sharp. If capture is unavailable, a mostly opaque background preserves readability.

## Application signals

Supports X11 urgency and `com.canonical.Unity.LauncherEntry.Update` on the session
bus: signature `(sa{sv})`, application URI `application://YOUR-APP.desktop`.

| Properties | Effect |
| --- | --- |
| `count` (int64), `count-visible` (boolean) | Red badge, capped at `99+`; zero hides it |
| `progress` (double), `progress-visible` (boolean) | Progress indicator |
| `urgent` (boolean) | Repeating bounce and hidden-edge attention beacon |

Keep the D-Bus connection alive for urgency; disconnect clears its request.
Badge/progress updates survive one-shot publishers and clear with the app's last
window. Activating the requesting window acknowledges attention; badges remain.
GTK's animation preference controls bouncing.

## Development

```sh
make check                  # unit tests, CLI, desktop entry
make check-renderer         # headless CPU renderer; images in build/cpu/
make check-renderer-parity  # optional GL comparison; needs libepoxy and EGL
make check-upstream        # snapshot hashes and original/C state comparison
make bake-assets           # regenerate bundled PNGs; needs rsvg-convert
```

Upstream checks also need Python 3 and a C++ compiler. Ordinary builds use bundled
assets and download nothing. See the [source map](PORTING.md).

For urgency diagnostics, run
`LUNATIC_LAUNCHER_TRACE_URGENCY=/tmp/lunatic-urgency ./lunatic-launcher`.
This writes state logs and rendered frames, at most once per second.

## Memory

Frames are reused when unchanged; per-monitor frame memory scales with pixel area.
Dash caches up to 96 icons, runs at most four decodes concurrently, and cancels
unfinished requests on close. Its blurred backdrop uses about 80 KiB regardless
of display scale and is released on close.

Measure a running launcher's RSS, PSS and private memory on Linux:

```sh
python3 tools/memory-report.py PID
```

Not ported: remote DBusMenu quicklists, removable-device icons, HUD, Expo, Unity
lenses and touch gestures. Compiz spread uses a window list; Dash searches local
applications. Live session integration needs desktop testing.

GPL-3.0-only. Original copyright notices and [upstream source](upstream/README.md) retained.
