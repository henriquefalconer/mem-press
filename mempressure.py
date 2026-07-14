#!/usr/bin/env python3
"""
mempressure - a tiny btop-inspired TUI.

A single braille filled-area graph of macOS memory pressure inside a rounded
box that fills the whole terminal. Each column of the graph is one sample:
  * HEIGHT  <- kern.memorystatus_level        (free %, drawn as "used" pressure)
  * COLOR   <- kern.memorystatus_vm_pressure_level
                1 -> green   (normal)
                2 -> yellow  (warning)
                4 -> red     (critical)
These are exactly the metrics that drive Activity Monitor's green/yellow/red
Memory Pressure graph.

No menus, no config, no shortcuts. Ctrl-C or 'q' to quit.
"""
import os, sys, time, select, termios, tty, shutil, subprocess

# ---- btop's braille "up" fill (diagonal: both dot-columns equal) ----
FILL = [" ", "⣀", "⣤", "⣶", "⣿"]   #   ⣀ ⣤ ⣶ ⣿

# ---- btop rounded box glyphs ----
TL, TR, BL, BR = "╭", "╮", "╰", "╯"   # ╭ ╮ ╰ ╯
HL, VL = "─", "│"                               # ─ │
TITLE_L, TITLE_R = "┐", "┌"                     # ┐ ┌ (btop title brackets)

# ---- colors sampled from the live btop window (truecolor RGB) ----
GREEN  = (181, 230, 133)   # #b5e685
YELLOW = (255, 215, 122)   # #ffd77a
RED    = (217,  98, 109)   # #d9626d
BORDER = (108, 108,  75)   # #6c6c4b  (btop panel border)
TITLE  = (255, 255, 255)   # white, full opacity
GRAD_LOW = 0.45            # bottom-row opacity of the graph gradient (btop-like)
def fg(c):  return f"\033[38;2;{c[0]};{c[1]};{c[2]}m"
RESET = "\033[0m"

LEVEL_COLOR = {1: GREEN, 2: YELLOW, 4: RED}


def sysctl_int(key):
    try:
        return int(subprocess.check_output(["sysctl", "-n", key]).decode().strip())
    except Exception:
        return 0


def sample():
    """Return (used_fraction, color, level, free_pct)."""
    lvl = sysctl_int("kern.memorystatus_vm_pressure_level")
    free = sysctl_int("kern.memorystatus_level")
    used = max(0.0, min(1.0, (100 - free) / 100.0))
    return used, LEVEL_COLOR.get(lvl, GREEN), lvl, free


def render(cols, rows, history, level, free):
    inner_w = max(1, cols - 2)
    inner_h = max(1, rows - 2)
    graph_h = max(1, inner_h - 1)   # first interior row is the status line
    dot_h = graph_h * 4

    # right-align newest sample to the right edge
    window = history[-inner_w:]
    pad = inner_w - len(window)

    out = ["\033[H"]  # cursor home

    # --- top border with title ---
    title = "mem press"
    if len(title) + 6 > inner_w:
        title = ""
    rest = inner_w - 1 - len(title) - (2 if title else 0)
    if title:
        out.append(fg(BORDER) + TL + HL * 1 + TITLE_L
                   + fg(TITLE) + "\033[1m" + title + "\033[22m" + fg(BORDER)
                   + TITLE_R + HL * max(0, rest) + TR + RESET + "\n")
    else:
        out.append(fg(BORDER) + TL + HL * inner_w + TR + RESET + "\n")

    # --- status line (first interior row) ---
    label = "Free-Page Availability:"
    pct = max(0, min(100, free))       # kern.memorystatus_level (free-page %)
    value = f"{pct}%"
    gap = inner_w - 2 - len(label) - len(value)
    if gap >= 1:
        out.append(fg(BORDER) + VL + RESET
                   + fg(TITLE) + "\033[1m" + " " + label + "\033[22m" + RESET + " " * gap
                   + fg(TITLE) + value + RESET + " "
                   + fg(BORDER) + VL + RESET + "\n")
    else:
        txt = (" " + label + " " + value + " ")[:inner_w].ljust(inner_w)
        out.append(fg(BORDER) + VL + RESET + fg(TITLE) + txt + RESET
                   + fg(BORDER) + VL + RESET + "\n")

    # --- graph rows (top -> bottom) ---
    for r in range(graph_h):
        rows_below = graph_h - 1 - r
        line = [fg(BORDER) + VL + RESET]
        cur = None
        for c in range(inner_w):
            if c < pad:
                if cur is not None:
                    line.append(RESET); cur = None
                line.append(" ")
                continue
            used, color, *_ = window[c - pad]
            total = round(used * dot_h)             # this column's fill height in dots
            n = total - rows_below * 4
            n = 0 if n < 0 else (4 if n > 4 else n)
            if n == 0:
                if cur is not None:
                    line.append(RESET); cur = None
                line.append(" ")
            else:
                # btop-style gradient over the column's own fill: dim base -> bright tip,
                # so every stacked ⣿ layer has a distinct opacity
                cell_h = rows_below * 4 + n * 0.5
                frac = cell_h / total if total else 1.0
                factor = GRAD_LOW + (1.0 - GRAD_LOW) * (1.0 if frac > 1.0 else frac)
                dc = (int(color[0] * factor), int(color[1] * factor), int(color[2] * factor))
                if cur != dc:
                    line.append(fg(dc)); cur = dc
                line.append(FILL[n])
        if cur is not None:
            line.append(RESET)
        line.append(fg(BORDER) + VL + RESET)
        out.append("".join(line) + "\n")

    # --- bottom border ---
    out.append(fg(BORDER) + BL + HL * inner_w + BR + RESET)
    sys.stdout.write("".join(out))
    sys.stdout.flush()


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    history = []
    # alt screen, hide cursor, disable auto-wrap (so the full-width bottom
    # border can't trigger a scroll that leaves a blank last line), clear
    sys.stdout.write("\033[?1049h\033[?25l\033[?7l\033[2J")
    sys.stdout.flush()
    try:
        tty.setcbreak(fd)
        last_sample = 0.0
        last_size = None
        while True:
            now = time.time()
            if now - last_sample >= 1.0:
                history.append(sample())
                if len(history) > 4096:
                    history = history[-4096:]
                last_sample = now
            cols, rows = shutil.get_terminal_size((80, 24))
            if (cols, rows) != last_size:
                sys.stdout.write("\033[2J")
                last_size = (cols, rows)
            _, _, level, free = history[-1]
            render(cols, rows, history, level, free)
            # wait up to 250ms, quit on 'q'
            if select.select([sys.stdin], [], [], 0.25)[0]:
                if sys.stdin.read(1).lower() == "q":
                    break
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        sys.stdout.write("\033[?7h\033[?25h\033[?1049l")   # restore wrap+cursor, leave alt screen
        sys.stdout.flush()


if __name__ == "__main__":
    main()
