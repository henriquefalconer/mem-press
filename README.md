# mem-press

A tiny [btop](https://github.com/aristocratos/btop)-inspired terminal UI that shows memory pressure on **macOS, Linux, WSL, and native Windows** as a single braille graph.

<img src="https://github.com/henriquefalconer/mem-press/blob/main/docs/screenshot.png?raw=true" width="68%" />

## What it shows

A TUI application, containing:

- A **`Free-Page Availability:`** readout — macOS's kernel free-page percentage, Linux/WSL's reclaim-aware `MemAvailable / MemTotal` percentage, or native Windows' `GetPerformanceInfo().PhysicalAvailable / PhysicalTotal` (the host-wide available physical-page count).
- A scrolling **braille filled-area graph** where each column is one 1-second sample:
  - **color** ← the same three categories on every platform: green (>20% available), yellow (11–20%), red (≤10%). Linux/WSL additionally escalates for memory PSI contention (`some` ≥ 5% / 20%, `full` ≥ 1%); macOS retains its kernel pressure signal. Native Windows uses its host-wide available physical pages because Windows has no public PSI equivalent.
  - **height** ← used-memory pressure (`100 − availability`), so the graph rises under pressure like Activity Monitor's.
  - a btop-style vertical opacity gradient per column (bright tip → dim base).

The green/yellow/red and border colors are sampled directly from btop's default theme; the box glyphs and braille fill match btop's.

## Run

`./mem-press` is a portable POSIX launcher: it compiles a native cached binary for macOS, Linux, or WSL on first run, then executes it. In Git Bash/MSYS it also normalizes Windows cache paths. From PowerShell or cmd, run `mem-press.cmd`; it builds a native Windows executable with MSVC (or gcc/clang) and measures the whole Windows host, not the WSL VM.

```sh
./mem-press
```

To build manually instead (native Windows builds also need `Psapi.lib` / `-lpsapi`):

```sh
cc -O2 -Wall -Wextra -o mem-press.bin mem-press.c -lm
./mem-press.bin
```

`q` or `Ctrl-C` to quit. No config, no menus, no shortcuts. Linux/WSL needs `/proc/meminfo`; PSI is used automatically when available.
