# Gamepad Battery

Show your Xbox / XInput controller's battery level **directly in the Windows system tray** — the icon itself is the battery.

![screenshot](screenshot.png)

## Features

- **Battery at a glance** — the tray icon shows a number (3 = full, 2 = medium, 1 = low, 0 = empty, ? = not connected), colored accordingly
- **Zero dependencies** — uses only the built-in `xinput1_4.dll` and GDI+
- **No network** — no telemetry, no background connections
- **Tiny** — ~160 KB single executable

## Why only four levels?

That is a limit of the `XInput` API itself: `XINPUT_BATTERY_INFORMATION` only reports Empty / Low / Medium / Full. Exact percentages require vendor-specific software.

## Build

Requires Visual Studio (C++ desktop workload). In an *x64 Native Tools Command Prompt*:

```bat
build.bat
