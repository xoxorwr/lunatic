# Source and porting map

Source: [Ubuntu Unity](https://gitlab.com/ubuntu-unity/unity/unity), commit
`2fb9954c7c17736d3f68e0d3ae26683790f812bf`.
All 126 upstream launcher files, renderer references and original artwork are
retained unmodified in `upstream/`. Verify with `make check-upstream`.
The C++ snapshot is reference material; production builds compile only C.

| Upstream | C implementation | Adaptation |
| --- | --- | --- |
| `LauncherHideMachine.cpp`, `LauncherHoverMachine.cpp` | `src/launcher-core.c` | Flags, hide/hover decisions, reveal threshold, 400 ms hide delay |
| `Decaymulator.cpp` | `src/launcher-core.c` | Integer decay at 10 ms intervals |
| `Launcher.cpp` layout and animation functions | `src/launcher-core.c` | Folding, shelf placement, drag limits, pulse/wiggle curves |
| `LauncherIcon.cpp` color/count rendering | `src/launcher-core.c`, `src/renderer.c` | Original color weighting, HSV adjustments and count geometry |
| `unity-shared/IconRenderer.cpp` | `src/renderer.c` | Original layers, perspective, tint and markers; CPU raster replaces Nux |
| `LauncherOptions.cpp` | `src/main.c`, `src/launcher-core.c` | Defaults: 48 px icon, 54 px tile, 125 ms hide, 200 ms unfold |
| `WindowedLauncherIcon.cpp` | `src/main.c` | X11/EWMH replaces Compiz/BAMF; window list replaces spread |
| `FavoriteStoreGSettings.cpp` | `src/desktop.c`, `src/main.c` | XDG config replaces Unity schemas |
| Model, desktop and trash icons | `src/main.c` | Apps above fixed system group; Show Desktop above Trash, unlike upstream's movable favorite |
| `LauncherEntryRemote.cpp` | `src/main.c` | GDBus counts, progress and urgency; remote menus unported |

## Rendering and lifecycle

Each monitor uses one reusable ARGB32 dock surface. Perspective-correct bilinear
sampling, premultiplied blending and subpixel coverage share geometry with the GL
test backend. Production creates no GL context. Bundled PNG variants cover
1×/2×/3×/4×/8×; custom sizes are resampled once.

Dirty frames reuse geometry and converted images; GTK exposure reuses the finished
surface. Monitor removal/scale changes release affected caches. App source images
and matching indexes are shared across views. X11 events coalesce into idle
updates; the animation timer slows from 16 ms to 100 ms when idle.

## Validation

- Original/C comparisons: all 262,144 hide and 128 hover flag combinations.
- CPU rendering: scale, transparency, hide modes, positions and cache release.
- Optional GL parity: 14 scenes; measured maximum channel error 3/255, limit 4/255.
- Offscreen CPU timing on the test host: about 1.4 ms/frame at 1×, 5.4 ms at 2×.

Live window-manager integration, keyboard grabs, drag/drop and monitor changes
need desktop testing. See [features and limits](README.md) and [memory notes](README.md#memory).
