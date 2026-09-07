// mem-press - a tiny btop-inspired memory-pressure TUI for macOS, Linux, and Windows.
//
// A single braille filled-area graph in a rounded box that fills the terminal.
// Each column is one 1-second sample:
//   color  <- native pressure signal (macOS) or PSI + availability (Linux)
//   height <- used pressure (100 - availability)
// with a per-column btop-style opacity gradient, plus a Free-Page Availability
// readout.  q / Ctrl-C to quit.
//
// Build:  cc -O2 -Wall -Wextra -o mem-press mem-press.c -lm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#include <conio.h>
#include <io.h>
#else
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#endif
#include <signal.h>
#include <time.h>
#include <math.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif

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
    if (lvl >= 3) return RED;
    if (lvl == 2) return YELLOW;
    return GREEN;
}

#if defined(__APPLE__)
static int sysctl_int(const char *name) {
    int v = 0; size_t n = sizeof(v);
    if (sysctlbyname(name, &v, &n, NULL, 0) != 0) return -1;
    return v;
}
#elif defined(_WIN32)
/* PhysicalAvailable is Windows' reclaim-aware available-page count.  Unlike
 * WSL's /proc view, this is the host-wide physical memory view. */
static int read_windows_memory(int *available_pct) {
    PERFORMANCE_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    pi.cb = sizeof pi;
    if (!GetPerformanceInfo(&pi, sizeof pi)) return 0;
    unsigned long long total = (unsigned long long)pi.PhysicalTotal * pi.PageSize;
    unsigned long long available = (unsigned long long)pi.PhysicalAvailable * pi.PageSize;
    if (!total) return 0;
    *available_pct = (int)((available * 100 + total / 2) / total);
    if (*available_pct > 100) *available_pct = 100;
    return 1;
}
#elif defined(__linux__)
/* Linux PSI is workload pressure; MemAvailable is the kernel's reclaim-aware
 * estimate.  Combining them avoids calling a mostly-idle cache-heavy machine
 * "critical", while still showing pressure before RAM is completely full. */
static int read_linux_memory(int *available_pct, double *some, double *full) {
    unsigned long long total = 0, available = 0;
    char line[256];
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp) {
        while (fgets(line, sizeof line, fp)) {
            if (sscanf(line, "MemTotal: %llu kB", &total) == 1) continue;
            if (sscanf(line, "MemAvailable: %llu kB", &available) == 1) continue;
        }
        fclose(fp);
    }
    if (!total || !available) {
        struct sysinfo si;
        if (sysinfo(&si) != 0 || !si.totalram) return 0;
        total = (unsigned long long)si.totalram * si.mem_unit;
        available = (unsigned long long)(si.freeram + si.bufferram) * si.mem_unit;
    }
    *available_pct = (int)((available * 100 + total / 2) / total);
    if (*available_pct > 100) *available_pct = 100;
    *some = *full = 0.0;

    fp = fopen("/proc/pressure/memory", "r");
    if (!fp) return 1; /* PSI was added after MemAvailable; availability works alone. */
    while (fgets(line, sizeof line, fp)) {
        double avg10;
        if (sscanf(line, "some avg10=%lf", &avg10) == 1) *some = avg10;
        if (sscanf(line, "full avg10=%lf", &avg10) == 1) *full = avg10;
    }
    fclose(fp);
    return 1;
}
#endif

static double now_sec(void) {
#if defined(_WIN32)
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

// ---- history ----
typedef struct { double used; int lvl; int freep; } Sample;
static Sample *hist = NULL;
static int hist_n = 0, hist_cap = 0;

static void push_sample(void) {
    int lvl = 1, freep = -1;
#if defined(__APPLE__)
    /* These are the same kernel signals used by Activity Monitor. */
    lvl = sysctl_int("kern.memorystatus_vm_pressure_level");
    freep = sysctl_int("kern.memorystatus_level");
#elif defined(_WIN32)
    if (read_windows_memory(&freep)) {
        /* Keep the same three availability bands as Linux. Windows has no
         * public PSI equivalent, so availability is the portable signal. */
        if (freep <= 10) lvl = 3;
        else if (freep <= 20) lvl = 2;
    }
#elif defined(__linux__)
    double some = 0.0, full = 0.0;
    if (read_linux_memory(&freep, &some, &full)) {
        /* PSI thresholds are percentages of the last ten seconds.  `full` is
         * severe system-wide thrashing; availability provides an early and a
         * final guard when PSI is disabled (common on older WSL kernels). */
        if (freep <= 10 || full >= 1.0 || some >= 20.0) lvl = 3;
        else if (freep <= 20 || some >= 5.0) lvl = 2;
    }
#endif
    double used = freep < 0 ? 0.0 : (100 - freep) / 100.0;
    if (used < 0) used = 0;
    if (used > 1) used = 1;
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
    int freep = hist_n ? hist[hist_n - 1].freep : -1;

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
    char val[16];
    int vl = freep < 0 ? snprintf(val, sizeof val, "--")
                       : snprintf(val, sizeof val, "%d%%", freep > 100 ? 100 : freep);
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
            if (n < 0) n = 0;
            if (n > 4) n = 4;
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

#if defined(_WIN32)
    fwrite(ob, 1, ob_len, stdout); fflush(stdout);
#else
    ssize_t w = write(STDOUT_FILENO, ob, ob_len); (void)w;
#endif
}

// ---- terminal setup / teardown ----
#if defined(_WIN32)
static DWORD g_console_mode = 0;
static int g_console_saved = 0;
static UINT g_input_cp = 0, g_output_cp = 0;
static int g_cp_saved = 0;
#else
static struct termios g_orig;
static int g_raw = 0;
#endif
static int g_alt = 0;
static volatile sig_atomic_t g_stop = 0;

static void cleanup(void) {
    if (g_alt) { fputs("\033[?7h\033[?25h\033[?1049l", stdout); fflush(stdout); g_alt = 0; }
#if defined(_WIN32)
    if (g_console_saved) SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), g_console_mode);
    if (g_cp_saved) {
        SetConsoleCP(g_input_cp);
        SetConsoleOutputCP(g_output_cp);
        g_cp_saved = 0;
    }
#else
    if (g_raw) { tcsetattr(STDIN_FILENO, TCSADRAIN, &g_orig); g_raw = 0; }
#endif
}
static void on_sig(int s) { (void)s; g_stop = 1; }

static void get_size(int *cols, int *rows) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else { *cols = 80; *rows = 24; }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
        *cols = ws.ws_col; *rows = ws.ws_row;
    } else { *cols = 80; *rows = 24; }
#endif
}

int main(void) {
#if defined(_WIN32)
    /* The UI is emitted as UTF-8. Native Windows consoles otherwise commonly
     * decode the box and braille bytes as CP437/CP1252 (Γò¡/Γú╢ mojibake). */
    g_input_cp = GetConsoleCP();
    g_output_cp = GetConsoleOutputCP();
    if (g_input_cp && g_output_cp) {
        g_cp_saved = 1;
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
    }
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(in, &mode)) {
        g_console_mode = mode; g_console_saved = 1;
        SetConsoleMode(in, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
    }
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD out_mode;
    if (GetConsoleMode(out, &out_mode)) SetConsoleMode(out, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    if (tcgetattr(STDIN_FILENO, &g_orig) == 0) {
        struct termios raw = g_orig;
        raw.c_lflag &= ~(ICANON | ECHO);        // cbreak; keep ISIG for Ctrl-C
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_raw = 1;
    }
#endif
#if defined(_WIN32)
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
#else
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_sig;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);
#endif
    atexit(cleanup);

    // alt screen, hide cursor, disable auto-wrap (so the full-width bottom
    // border can't scroll and leave a blank last line), clear
    fputs("\033[?1049h\033[?25l\033[?7l\033[2J", stdout); fflush(stdout);
    g_alt = 1;

    push_sample();
    double last_sample = now_sec();
    int last_cols = -1, last_rows = -1;

    while (!g_stop) {
        double t = now_sec();
        if (t - last_sample >= 1.0) { push_sample(); last_sample = t; }

        int cols, rows; get_size(&cols, &rows);
        if (cols != last_cols || rows != last_rows) {
            fputs("\033[2J", stdout); fflush(stdout);
            last_cols = cols; last_rows = rows;
        }
        render(cols, rows);

#if defined(_WIN32)
        Sleep(250);
        if (_kbhit()) { int ch = _getch(); if (ch == 'q' || ch == 'Q' || ch == 3) break; }
#else
        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = {0, 250000};
        int rv = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (rv > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char ch;
            ssize_t nr = read(STDIN_FILENO, &ch, 1);
            if (nr == 1 && (ch == 'q' || ch == 'Q')) break;
            if (nr == 0 || (nr < 0 && errno != EINTR)) break;
        }
#endif
    }
    cleanup();
    return 0;
}
