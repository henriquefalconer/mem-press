# mem-press

A tiny [btop](https://github.com/aristocratos/btop)-inspired terminal UI that shows memory pressure on **macOS, Linux, and WSL** as a single braille graph.

<img src="https://github.com/henriquefalconer/mem-press/blob/main/docs/screenshot.png?raw=true" width="68%" />

## What it shows

A TUI application, containing:

- A **`Free-Page Availability:`** readout — macOS's kernel free-page percentage, or Linux/WSL's reclaim-aware `MemAvailable / MemTotal` percentage (not just `MemFree`).
- A scrolling **braille filled-area graph** where each column is one 1-second sample:
  - **color** ← macOS's `kern.memorystatus_vm_pressure_level` (`1` green, `2` yellow, `4` red). On Linux/WSL it combines reclaim-aware availability with memory PSI (`/proc/pressure/memory`): green (over 20% available and low contention), yellow (20% or less available or PSI `some` ≥ 5%), red (10% or less available, PSI `full` ≥ 1%, or `some` ≥ 20%). PSI is optional.
  - **height** ← used-memory pressure (`100 − availability`), so the graph rises under pressure like Activity Monitor's.
  - a btop-style vertical opacity gradient per column (bright tip → dim base).

The green/yellow/red and border colors are sampled directly from btop's default theme; the box glyphs and braille fill match btop's.

## Run

`./mem-press` is a portable launcher: it compiles a native cached binary for macOS, Linux, or WSL on first run, then executes it.

```sh
./mem-press
```

To build manually instead:

```sh
cc -O2 -Wall -Wextra -o mem-press.bin mem-press.c -lm
./mem-press.bin
```

`q` or `Ctrl-C` to quit. No config, no menus, no shortcuts. Linux/WSL needs `/proc/meminfo`; PSI is used automatically when available.
