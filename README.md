# Vitals

A compact, adaptive stats dock for OBS Studio. Vitals replaces the built-in Stats dock with one that fits whatever space you give it, stays neutral while everything is fine, and turns amber or red the moment something needs your attention.

**Platform:** Windows 10 and 11 (x64) · **Requires:** OBS Studio 32 or later

## What it shows

| Group | Metrics |
|-------|---------|
| System | CPU, GPU, memory, free disk space (with recording time left), session time |
| Rendering | FPS, frame time against the frame budget, missed frames, skipped frames |
| Stream | Bitrate with trend line, dropped frames, data sent |
| Recording | Bitrate with trend line, file size |
| Other outputs | One row per active output: multistream targets, replay buffer, virtual camera, and outputs created by other plugins |

Dropped, missed and skipped frames show a percentage first and the raw count underneath. The header names the worst current problem, for example "Dropped frames 2.6%", next to the LIVE and REC timers.

## Layout

The layout follows the dock's own size, so it works docked, tabbed or floating:

- **130 px tall or less:** a single line with the metrics you pin. When space runs out, healthy metrics are hidden before alerts.
- **230 px wide or less:** a compact list with one row per metric.
- **Anything larger:** a grouped tile grid whose columns fill the width.

## Alerts

Every metric with a threshold turns amber at its warning level and red at its critical level. The dock's title bar takes the color of the worst alert so you notice it even when the dock is small. Thresholds, the title bar tint, which metrics appear, their order and the update rate are all set in **Tools → Vitals Settings**, or from the gear in the dock.

## Install

1. Download the latest Windows build from the [Releases](https://github.com/voidscape-development/stats-panel/releases) page.
2. Run the installer, or extract the zip into your OBS installation folder.
3. Start OBS and enable **Docks → Vitals**. You can close the built-in Stats dock; plugins cannot remove it.

## How it stays light

- One timer per dock, running only while the dock is visible. It stops when the dock is hidden, tabbed away or OBS is minimized.
- Cheap libobs getters run on the UI thread. GPU counters (PDH or NVML) and free disk space are sampled on a worker thread, once a second and every five seconds.
- The dock body is a single custom-painted widget. An update repaints only the tiles whose text changed, and never triggers a layout pass.
- Trend lines use fixed ring buffers, so updates don't allocate memory.
- Output start and stop come from OBS frontend events. Outputs are held through weak references, so Vitals never keeps an output alive.

## Building

Vitals uses the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate) build system. With Visual Studio 2022 and CMake 3.28 or later:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The first configure downloads OBS 32.0.4 sources, obs-deps and Qt 6 as pinned in `buildspec.json`. CI builds every pull request on Windows; see `.github/workflows`.

## Source layout

| File | Responsibility |
|------|----------------|
| `src/plugin-main.cpp` | Module entry points |
| `src/vitals-controller.*` | Registers the dock and Tools menu entry, runs the timer, handles OBS events, tints the title bar |
| `src/vitals-sampler.*` | Reads libobs statistics and tracks active outputs |
| `src/vitals-worker.*` | Background thread for GPU usage and free disk space |
| `src/vitals-gpu-win.cpp` | GPU usage from Windows performance counters or NVML |
| `src/vitals-display.*` | Turns a sample into formatted text, alert levels and the health summary |
| `src/vitals-view.*` | The custom-painted dock body |
| `src/vitals-settings*.*` | Settings storage and the settings dialog |
| `data/locale/en-US.ini` | All user-facing text |

## Ideas for later

- Sound alert when a metric goes critical, with a cooldown
- A line in the OBS log when a threshold is crossed
- Tinting the dock's tab when it is tabbed behind another dock
- Windows notification or taskbar flash when OBS isn't focused
- Separate alert levels per output

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
