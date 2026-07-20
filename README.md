# mem-press

A tiny [btop](https://github.com/aristocratos/btop)-inspired terminal UI that shows **macOS memory pressure** as a single braille graph.

![mem press](https://github.com/henriquefalconer/mem-press/blob/main/docs/screenshot.png?raw=true)

## What it shows

A rounded box that fills the whole terminal, containing:

- A **`Free-Page Availability:`** readout — the raw `kern.memorystatus_level` value (percentage of free pages).
- A scrolling **braille filled-area graph** where each column is one 1-second sample:
  - **color** ← `kern.memorystatus_vm_pressure_level` — `1` green, `2` yellow, `4` red (the same signal that drives Activity Monitor's green/yellow/red Memory Pressure graph, and the `DISPATCH_MEMORYPRESSURE` NORMAL/WARN/CRITICAL states).
  - **height** ← used-memory pressure (`100 − kern.memorystatus_level`), so the graph rises under pressure like Activity Monitor's.
  - a btop-style vertical opacity gradient per column (bright tip → dim base).

The green/yellow/red and border colors are sampled directly from btop's default theme; the box glyphs and braille fill match btop's.

## Run

It's a single native binary (Mach-O, Apple silicon) — like btop:

```sh
./mem-press
```

`q` or `Ctrl-C` to quit. No config, no menus, no shortcuts. macOS only (reads `sysctl` memory metrics).

Rebuild from source:

```sh
clang -O2 -o mem-press mem-press.c
```
