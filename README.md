# mem-press

A tiny [btop](https://github.com/aristocratos/btop)-inspired terminal UI that shows memory pressure on **macOS, Linux, WSL, and native Windows** as a single braille graph.

<img src="https://github.com/henriquefalconer/mem-press/blob/main/docs/screenshot.png?raw=true" width="68%" />

## What it shows

A TUI application, containing:

- A **`Free-Page Availability:`** readout — macOS's kernel free-page percentage, Linux/WSL's reclaim-aware `MemAvailable / MemTotal` percentage, or native Windows' `GetPerformanceInfo().PhysicalAvailable / PhysicalTotal` (the host-wide available physical-page count).
- A scrolling **braille filled-area graph** where each column is one 1-second sample:
  - **color** uses platform-specific signals (not equivalent measurements):
    - **macOS:** `kern.memorystatus_vm_pressure_level`: normal → green, warning → yellow, critical → red.
    - **Linux/WSL:** availability bands (>20% green, 11–20% yellow, ≤10% red), escalated by PSI `some avg10` ≥5% / ≥20% or `full avg10` ≥1%. These thresholds are application policy, not kernel-defined severity levels.
    - **Windows:** system-wide memory resource notifications: **low → red**, **high → green**, **neither → yellow**. Windows selects the physical-memory thresholds, rather than the app guessing from RAM percentages. Low wins if both queries report signaled during a transition. The intermediate range means “keep memory use constant,” not necessarily thrashing. Only if notification creation/querying fails do colors fall back to the availability bands above. Availability and graph height still use `PhysicalAvailable / PhysicalTotal`.
  - **height** ← used-memory pressure (`100 − availability`), so the graph rises under pressure like Activity Monitor's.
  - a btop-style vertical opacity gradient per column (bright tip → dim base).

The green/yellow/red and border colors are sampled directly from btop's default theme; the box glyphs and braille fill match btop's.

Windows uses the documented [memory resource notifications](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-creatememoryresourcenotification), queried without waiting. These describe physical-memory conditions, not Linux PSI-style stall time, commit exhaustion, or a guaranteed prediction of allocation failure. There is no universal three-color metric across these operating systems.

Native Windows regression test (Git Bash with MinGW GCC):

```sh
sh tests/windows-pressure.sh
```

## Run

`./mem-press` is a portable launcher. macOS and Linux build a native cached binary on first run. Git Bash and WSL use the checked-in native Windows executable immediately, so they require no C compiler or other build dependency; when a compiler is available, the launcher may rebuild from source. The Windows executable measures the whole Windows host, not the WSL VM. From PowerShell or cmd, run `mem-press.cmd`.

```sh
./mem-press
```

To build manually instead (native Windows builds also need `Psapi.lib` / `-lpsapi`):

```sh
cc -O2 -Wall -Wextra -o mem-press.bin mem-press.c -lm
./mem-press.bin
```

`q` or `Ctrl-C` to quit. No config, no menus, no shortcuts. Linux/WSL needs `/proc/meminfo`; PSI is used automatically when available.
