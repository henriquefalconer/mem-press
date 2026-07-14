// mem-press - a tiny btop-inspired macOS memory-pressure TUI.
//
// A single braille filled-area graph in a rounded box that fills the terminal.
// Each column is one 1-second sample:
//   color  <- kern.memorystatus_vm_pressure_level  (1 green, 2 yellow, 4 red)
//   height <- used pressure (100 - kern.memorystatus_level)
// with a per-column btop-style opacity gradient, plus a Free-Page Availability
// readout (the raw kern.memorystatus_level).  q / Ctrl-C to quit.
//
// Build:  clang -O2 -o mem-press mem-press.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/select.h>

typedef struct { int r, g, b; } Color;

static const Color GREEN  = {181, 230, 133};   // #b5e685  (from btop)
static const Color YELLOW = {255, 215, 122};   // #ffd77a
static const Color RED    = {217,  98, 109};   // #d9626d
static const Color BORDER = {108, 108,  75};   // #6c6c4b  (btop panel border)
static const Color TITLE  = {255, 255, 255};   // white
#define GRAD_LOW 0.45

// braille fill (diagonal of btop's braille_up table): " " ⣀ ⣤ ⣶ ⣿
static const char *FILL[5] = { " ", "\xe2\xa3\x80", "\xe2\xa3\xa4", "\xe2\xa3\xb6", "\xe2\xa3\xbf" };
// btop rounded box glyphs
#define TL "\xe2\x95\xad"  // ╭
#define TR "\xe2\x95\xae"  // ╮
#define BL "\xe2\x95\xb0"  // ╰
#define BR "\xe2\x95\xaf"  // ╯
#define HL "\xe2\x94\x80"  // ─
#define VL "\xe2\x94\x82"  // │
#define TITLE_L "\xe2\x94\x90"  // ┐
#define TITLE_R "\xe2\x94\x8c"  // ┌
#define RESET "\033[0m"

static Color level_color(int lvl) {
    if (lvl == 2) return YELLOW;
    if (lvl == 4) return RED;
    return GREEN;
}

static int sysctl_int(const char *name) {
    int v = 0; size_t n = sizeof(v);
    if (sysctlbyname(name, &v, &n, NULL, 0) != 0) return 0;
    return v;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ---- history ----
typedef struct { double used; int lvl; int freep; } Sample;
static Sample *hist = NULL;
static int hist_n = 0, hist_cap = 0;

static void push_sample(void) {
    int lvl = sysctl_int("kern.memorystatus_vm_pressure_level");
    int freep = sysctl_int("kern.memorystatus_level");
    double used = (100 - freep) / 100.0;
    if (used < 0) used = 0; if (used > 1) used = 1;
    if (hist_n == hist_cap) {
        if (hist_cap >= 4096) {                 // keep the most recent 4096
            memmove(hist, hist + 1, (hist_cap - 1) * sizeof(Sample));
            hist_n = hist_cap - 1;
        } else {
            hist_cap = hist_cap ? hist_cap * 2 : 256;
            hist = realloc(hist, hist_cap * sizeof(Sample));
        }
    }
    hist[hist_n].used = used; hist[hist_n].lvl = lvl; hist[hist_n].freep = freep;
    hist_n++;
}

// ---- growable output buffer ----
static char *ob = NULL; static size_t ob_len = 0, ob_cap = 0;
static void bput(const char *s, size_t n) {
    if (ob_len + n + 1 > ob_cap) { ob_cap = (ob_len + n + 1) * 2; ob = realloc(ob, ob_cap); }
    memcpy(ob + ob_len, s, n); ob_len += n;
}
static void bputs(const char *s) { bput(s, strlen(s)); }
static void bfg(Color c) { char t[32]; int n = snprintf(t, sizeof t, "\033[38;2;%d;%d;%dm", c.r, c.g, c.b); bput(t, n); }

static void render(int cols, int rows) {
    int inner_w = cols - 2; if (inner_w < 1) inner_w = 1;
    int inner_h = rows - 2; if (inner_h < 1) inner_h = 1;
    int graph_h = inner_h - 1; if (graph_h < 1) graph_h = 1;   // one row for the status line
    int dot_h = graph_h * 4;

    int wlen = hist_n < inner_w ? hist_n : inner_w;
    int wstart = hist_n - wlen;
    int pad = inner_w - wlen;
    int freep = hist_n ? hist[hist_n - 1].freep : 0;

    ob_len = 0;
    bputs("\033[H");

    // --- top border with title ---
    const char *title = "mem press";
    int tl = (int)strlen(title);
    if (tl + 6 > inner_w) tl = 0;   // no room -> drop title
    if (tl) {
        int rest = inner_w - 3 - tl;
        if (rest < 0) rest = 0;
        bfg(BORDER); bputs(TL); bputs(HL); bputs(TITLE_L);
        bfg(TITLE); bputs("\033[1m"); bputs(title); bputs("\033[22m");
        bfg(BORDER); bputs(TITLE_R);
        for (int i = 0; i < rest; i++) bputs(HL);
        bputs(TR); bputs(RESET); bputs("\n");
    } else {
        bfg(BORDER); bputs(TL);
        for (int i = 0; i < inner_w; i++) bputs(HL);
        bputs(TR); bputs(RESET); bputs("\n");
    }

    // --- status line (first interior row) ---
    const char *label = "Free-Page Availability:";
    int ll = (int)strlen(label);
    int pct = freep < 0 ? 0 : (freep > 100 ? 100 : freep);
    char val[16]; int vl = snprintf(val, sizeof val, "%d%%", pct);
    int gap = inner_w - 2 - ll - vl;
    if (gap >= 1) {
        bfg(BORDER); bputs(VL); bputs(RESET);
        bfg(TITLE); bputs("\033[1m"); bputs(" "); bputs(label); bputs("\033[22m"); bputs(RESET);
        for (int i = 0; i < gap; i++) bputs(" ");
        bfg(TITLE); bputs(val); bputs(RESET); bputs(" ");
        bfg(BORDER); bputs(VL); bputs(RESET); bputs("\n");
    } else {
        char tmp[256]; int L = snprintf(tmp, sizeof tmp, " %s %s ", label, val);
        bfg(BORDER); bputs(VL); bputs(RESET); bfg(TITLE);
        for (int i = 0; i < inner_w; i++) { if (i < L) bput(&tmp[i], 1); else bputs(" "); }
        bputs(RESET); bfg(BORDER); bputs(VL); bputs(RESET); bputs("\n");
    }

    // --- graph rows (top -> bottom) ---
    for (int r = 0; r < graph_h; r++) {
        int rows_below = graph_h - 1 - r;
        bfg(BORDER); bputs(VL); bputs(RESET);
        int cr = -1, cg = -1, cb = -1;   // last emitted color; -1 => reset/none
        for (int c = 0; c < inner_w; c++) {
            if (c < pad) { if (cr >= 0) { bputs(RESET); cr = cg = cb = -1; } bputs(" "); continue; }
            Sample s = hist[wstart + (c - pad)];
            Color base = level_color(s.lvl);
            int total = (int)lround(s.used * dot_h);
            int n = total - rows_below * 4;
            if (n < 0) n = 0; if (n > 4) n = 4;
            if (n == 0) { if (cr >= 0) { bputs(RESET); cr = cg = cb = -1; } bputs(" "); }
            else {
                // gradient over the column's own fill: dim base -> bright tip
                double cell_h = rows_below * 4 + n * 0.5;
                double frac = total ? cell_h / total : 1.0;
                if (frac > 1.0) frac = 1.0;
                double f = GRAD_LOW + (1.0 - GRAD_LOW) * frac;
                int dr = (int)(base.r * f), dg = (int)(base.g * f), db = (int)(base.b * f);
                if (dr != cr || dg != cg || db != cb) { Color d = {dr, dg, db}; bfg(d); cr = dr; cg = dg; cb = db; }
                bputs(FILL[n]);
            }
        }
        if (cr >= 0) bputs(RESET);
        bfg(BORDER); bputs(VL); bputs(RESET); bputs("\n");
    }

    // --- bottom border (no trailing newline) ---
    bfg(BORDER); bputs(BL);
    for (int i = 0; i < inner_w; i++) bputs(HL);
    bputs(BR); bputs(RESET);

    ssize_t w = write(STDOUT_FILENO, ob, ob_len); (void)w;
}

// ---- terminal setup / teardown ----
static struct termios g_orig;
static int g_raw = 0, g_alt = 0;
static volatile sig_atomic_t g_stop = 0;

static void cleanup(void) {
    if (g_alt) { ssize_t w = write(STDOUT_FILENO, "\033[?7h\033[?25h\033[?1049l", 18); (void)w; g_alt = 0; }
    if (g_raw) { tcsetattr(STDIN_FILENO, TCSADRAIN, &g_orig); g_raw = 0; }
}
static void on_sig(int s) { (void)s; g_stop = 1; }

static void get_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
        *cols = ws.ws_col; *rows = ws.ws_row;
    } else { *cols = 80; *rows = 24; }
}

int main(void) {
    if (tcgetattr(STDIN_FILENO, &g_orig) == 0) {
        struct termios raw = g_orig;
        raw.c_lflag &= ~(ICANON | ECHO);        // cbreak; keep ISIG for Ctrl-C
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_raw = 1;
    }
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_sig;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);
    atexit(cleanup);

    // alt screen, hide cursor, disable auto-wrap (so the full-width bottom
    // border can't scroll and leave a blank last line), clear
    ssize_t w = write(STDOUT_FILENO, "\033[?1049h\033[?25l\033[?7l\033[2J", 22); (void)w;
    g_alt = 1;

    push_sample();
    double last_sample = now_sec();
    int last_cols = -1, last_rows = -1;

    while (!g_stop) {
        double t = now_sec();
        if (t - last_sample >= 1.0) { push_sample(); last_sample = t; }

        int cols, rows; get_size(&cols, &rows);
        if (cols != last_cols || rows != last_rows) {
            ssize_t z = write(STDOUT_FILENO, "\033[2J", 4); (void)z;
            last_cols = cols; last_rows = rows;
        }
        render(cols, rows);

        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = {0, 250000};
        int rv = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (rv > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) == 1 && (ch == 'q' || ch == 'Q')) break;
        }
    }
    cleanup();
    return 0;
}
