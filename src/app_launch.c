#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_launch.h"
#include "theme.h"
#include "app_resolve.h"
#include "workspace.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#endif

/* Case-insensitive compare of the first n bytes (ASCII only). */
static int ieq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ca = (a[i] >= 'A' && a[i] <= 'Z') ? (char)(a[i] + 32) : a[i];
        char cb = (b[i] >= 'A' && b[i] <= 'Z') ? (char)(b[i] + 32) : b[i];
        if (ca != cb || ca == '\0') return 0;
    }
    return 1;
}

const char *new_window_launcher(const char *app) {
    if (app == NULL) return NULL;
    size_t n = strlen(app);
    if (n >= 4 && ieq_n(app + n - 4, ".exe", 4)) n -= 4;
    if (n == 4 && ieq_n(app, "code", 4)) return "code";
    return NULL;
}

int is_new_window_app(const char *app) {
    return new_window_launcher(app) != NULL;
}

int is_url(const char *value) {
    if (value == NULL || value[0] == '\0') return 0;
    if (!((value[0] >= 'a' && value[0] <= 'z') ||
          (value[0] >= 'A' && value[0] <= 'Z'))) return 0;

    for (int i = 1; i < 32 && value[i]; i++) {
        char c = value[i];
        if (c == ':') return i >= 2;  /* min 2 chars excludes "C:\..." */
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') ||
                 c == '+' || c == '-' || c == '.';
        if (!ok) return 0;
    }
    return 0;
}

void layout_parse(const char *token, int *screen, char *part, size_t psize) {
    int s = 1;
    const char *p = (token != NULL) ? token : "";
    const char *colon = strchr(p, ':');
    if (colon != NULL) {
        s = atoi(p);            /* leading digits before ':' */
        if (s < 1) s = 1;
        p = colon + 1;
    }
    if (p[0] == '\0') p = "full";
    if (screen != NULL) *screen = s;
    if (part != NULL && psize > 0) {
        strncpy(part, p, psize - 1);
        part[psize - 1] = '\0';
    }
}

int partition_rect(const char *part, int X, int Y, int W, int H,
                   int *x, int *y, int *w, int *h) {
    int hw = W / 2, hh = H / 2;        /* right/bottom take the odd-pixel remainder */
    int rx = X, ry = Y, rw = W, rh = H;
    if      (strcmp(part, "left")   == 0) { rw = hw; }
    else if (strcmp(part, "right")  == 0) { rx = X + hw; rw = W - hw; }
    else if (strcmp(part, "top")    == 0) { rh = hh; }
    else if (strcmp(part, "bottom") == 0) { ry = Y + hh; rh = H - hh; }
    else if (strcmp(part, "tl")     == 0) { rw = hw; rh = hh; }
    else if (strcmp(part, "tr")     == 0) { rx = X + hw; rw = W - hw; rh = hh; }
    else if (strcmp(part, "bl")     == 0) { ry = Y + hh; rw = hw; rh = H - hh; }
    else if (strcmp(part, "br")     == 0) { rx = X + hw; ry = Y + hh; rw = W - hw; rh = H - hh; }
    else return 0;                     /* "full" / unknown → caller maximizes */
    *x = rx; *y = ry; *w = rw; *h = rh;
    return 1;
}

#if !defined(_WIN32) && !defined(__APPLE__)
int screen_count(void) { return 1; }
#endif

#ifdef _WIN32
/* ── Monitors ────────────────────────────────────────────────────────────── */

typedef struct { RECT work; int x, y, primary; } MonInfo;
typedef struct { MonInfo m[16]; int n; } MonList;

static BOOL CALLBACK mon_cb(HMONITOR hm, HDC dc, LPRECT r, LPARAM lp) {
    (void)dc; (void)r;
    MonList *ml = (MonList *)lp;
    if (ml->n >= 16) return TRUE;
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoA(hm, &mi)) {
        MonInfo *e = &ml->m[ml->n++];
        e->work    = mi.rcWork;
        e->x       = mi.rcMonitor.left;
        e->y       = mi.rcMonitor.top;
        e->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
    }
    return TRUE;
}

/* a before b? primary first, then left-to-right, then top-to-bottom. */
static int mon_less(const MonInfo *a, const MonInfo *b) {
    if (a->primary != b->primary) return a->primary > b->primary;
    if (a->x != b->x)             return a->x < b->x;
    return a->y < b->y;
}

static int enum_monitors(MonList *ml) {
    ml->n = 0;
    EnumDisplayMonitors(NULL, NULL, mon_cb, (LPARAM)ml);
    for (int i = 0; i < ml->n; i++) {        /* selection sort, stable enough */
        int best = i;
        for (int k = i + 1; k < ml->n; k++)
            if (mon_less(&ml->m[k], &ml->m[best])) best = k;
        if (best != i) { MonInfo t = ml->m[i]; ml->m[i] = ml->m[best]; ml->m[best] = t; }
    }
    return ml->n;
}

int screen_count(void) {
    MonList ml;
    int n = enum_monitors(&ml);
    return n > 0 ? n : 1;
}

/* Work area (taskbar-excluded) of the 1-based screen `index`; out-of-range falls
   back to the primary (sorted first). */
static void screen_work_area(int index, int *x, int *y, int *w, int *h) {
    MonList ml;
    int n = enum_monitors(&ml);
    if (n > 0) {
        int i = index - 1;
        if (i < 0 || i >= n) i = 0;
        RECT a = ml.m[i].work;
        *x = a.left; *y = a.top; *w = a.right - a.left; *h = a.bottom - a.top;
        return;
    }
    RECT a;
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &a, 0)) {
        *x = a.left; *y = a.top; *w = a.right - a.left; *h = a.bottom - a.top;
    } else { *x = 0; *y = 0; *w = 1280; *h = 720; }
}

/* ── Window placement ───────────────────────────────────────────────────────
   We can't reliably map a launched process to its window (apps launch through
   cmd.exe/explorer.exe brokers, browsers reuse an existing process). Instead we
   snapshot the top-level windows just before launching, then watch for the first
   new "real" window to appear and move it. */

#define WIN_SNAP_MAX 1024
typedef struct { HWND items[WIN_SNAP_MAX]; int n; } HwndSet;

static BOOL CALLBACK snap_cb(HWND h, LPARAM lp) {
    HwndSet *s = (HwndSet *)lp;
    if (s->n < WIN_SNAP_MAX) s->items[s->n++] = h;
    return TRUE;
}

static int hwnd_in_set(const HwndSet *s, HWND h) {
    for (int i = 0; i < s->n; i++) if (s->items[i] == h) return 1;
    return 0;
}

typedef struct { const HwndSet *before; HWND found; HWND any; } NewWinCtx;

/* Scans for a window that is new since the launch, visible, top-level (no
   owner), not a tool window, and a sensible size. Of those:
     any   — the first one, whatever it is.
     found — the first that is also resizable (WS_THICKFRAME), and stops the scan.
   The resizable test is what separates a real app window from the splash or
   updater a slow app shows first (Discord's updater, installer progress
   windows): those are fixed-size, real windows practically never are. Without
   it we snap the splash — which then closes — and the window the user actually
   wanted lands wherever the app felt like putting it. */
static BOOL CALLBACK newwin_cb(HWND h, LPARAM lp) {
    NewWinCtx *c = (NewWinCtx *)lp;
    if (!IsWindowVisible(h))            return TRUE;
    if (GetWindow(h, GW_OWNER) != NULL) return TRUE;
    if (hwnd_in_set(c->before, h))      return TRUE;
    if (GetWindowLongA(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    RECT r;
    if (!GetWindowRect(h, &r)) return TRUE;
    if (r.right - r.left < 200 || r.bottom - r.top < 150) return TRUE;
    if (c->any == NULL) c->any = h;
    if (!(GetWindowLongA(h, GWL_STYLE) & WS_THICKFRAME)) return TRUE;
    c->found = h;
    return FALSE;   /* stop at the first real window */
}

/* Thickness of a window's invisible DWM border (the gap between the window
   rectangle and the visible frame) on each side; zeroed if unavailable. Snapping
   to a rect without this correction leaves the visible edges inset from the grid. */
static void win_frame_margins(HWND w, RECT *m) {
    m->left = m->top = m->right = m->bottom = 0;
    RECT wr, fr;
    if (!GetWindowRect(w, &wr)) return;
    if (DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof(fr)) != S_OK)
        return;
    m->left   = fr.left   - wr.left;
    m->top    = fr.top    - wr.top;
    m->right  = wr.right  - fr.right;
    m->bottom = wr.bottom - fr.bottom;
}

/* Sends one Win+<vk> chord (press LWIN+vk, release vk+LWIN) to the focused
   window — i.e. the OS snap shortcuts the user already uses by hand. */
static void send_win_chord(WORD vk) {
    INPUT in[4];
    memset(in, 0, sizeof(in));
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_LWIN;
    in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = vk;
    in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = vk;      in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_LWIN; in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

/* Brings w to the foreground so the chords land on it. SetForegroundWindow is
   restricted to the current foreground thread, so we briefly attach input to it
   (the standard workaround). */
static void focus_window(HWND w) {
    DWORD me  = GetCurrentThreadId();
    DWORD fgt = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    if (fgt != me) AttachThreadInput(me, fgt, TRUE);
    ShowWindow(w, SW_RESTORE);
    BringWindowToTop(w);
    SetForegroundWindow(w);
    SetFocus(w);
    if (fgt != me) AttachThreadInput(me, fgt, FALSE);
}

/* Sizes w to an explicit rect, expanding by the invisible DWM border so the
   *visible* frame lands flush on the grid. Used only for partitions the snap
   shortcuts can't express (top/bottom halves). */
static void win_pixel_place(HWND w, int x, int y, int ww, int hgt) {
    ShowWindow(w, SW_RESTORE);   /* a maximized window can't be freely sized */
    RECT m;
    win_frame_margins(w, &m);
    SetWindowPos(w, NULL, x - m.left, y - m.top,
                 ww + m.left + m.right, hgt + m.top + m.bottom,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Places w into the partition encoded by `layout` (screen:partition) by driving
   the OS snap keyboard shortcuts, so the window manager owns the exact geometry
   instead of us hard-sizing it:
     left/right     → Win+←/→
     tl/tr/bl/br    → Win+←/→ then Win+↑/↓
     full / unknown → maximize on the target screen
     top/bottom     → pixel fallback (Windows has no horizontal-half shortcut)
   For screen N>1 the window is relocated (move only, no resize) onto that monitor
   first, so the OS snaps within it. Snapping briefly focuses the window and may
   surface Snap Assist for single-half snaps. */
static void win_snap(HWND w, const char *layout) {
    int screen = 1;
    char part[16];
    layout_parse(layout, &screen, part, sizeof(part));

    int L, T, W, H;
    screen_work_area(screen, &L, &T, &W, &H);

    /* Park the window on the target monitor first (move only, no resize) so a
       maximize or OS snap acts on the right screen — the app may have opened on a
       different monitor. The subsequent Win+Arrow snaps within this monitor. */
    ShowWindow(w, SW_RESTORE);
    SetWindowPos(w, NULL, L, T, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (strcmp(part, "full") == 0) { ShowWindow(w, SW_MAXIMIZE); return; }

    if (strcmp(part, "top") == 0 || strcmp(part, "bottom") == 0) {
        int x, y, ww, hgt;
        if (partition_rect(part, L, T, W, H, &x, &y, &ww, &hgt))
            win_pixel_place(w, x, y, ww, hgt);
        return;
    }

    focus_window(w);
    if      (strcmp(part, "left")  == 0) { send_win_chord(VK_LEFT); }
    else if (strcmp(part, "right") == 0) { send_win_chord(VK_RIGHT); }
    else if (strcmp(part, "tl")    == 0) { send_win_chord(VK_LEFT);  Sleep(120); send_win_chord(VK_UP); }
    else if (strcmp(part, "tr")    == 0) { send_win_chord(VK_RIGHT); Sleep(120); send_win_chord(VK_UP); }
    else if (strcmp(part, "bl")    == 0) { send_win_chord(VK_LEFT);  Sleep(120); send_win_chord(VK_DOWN); }
    else if (strcmp(part, "br")    == 0) { send_win_chord(VK_RIGHT); Sleep(120); send_win_chord(VK_DOWN); }
    else { ShowWindow(w, SW_MAXIMIZE); }   /* unknown → maximize on the target screen */
}

void *win_capture_before(void) {
    HwndSet *s = calloc(1, sizeof(HwndSet));
    if (s) EnumWindows(snap_cb, (LPARAM)s);
    return s;
}

/* ~15s of polling. A cold-starting app (Discord, Slack) can sit on a splash for
   ten seconds before its real window exists, and the old 3s budget simply gave
   up on those. The wait ends the moment that window appears, so a normally
   quick app pays nothing for the larger budget — only a launch that was going
   to miss its window anyway now takes longer to admit it. */
#define WIN_WAIT_TRIES 200
#define WIN_WAIT_MS    75

void win_place_new(void *before_v, const char *layout) {
    HwndSet *before = (HwndSet *)before_v;
    if (before == NULL) return;

    HWND w = NULL, fallback = NULL;
    for (int tries = 0; tries < WIN_WAIT_TRIES; tries++) {
        NewWinCtx c = { before, NULL, NULL };
        EnumWindows(newwin_cb, (LPARAM)&c);
        if (c.found != NULL) { w = c.found; break; }
        if (fallback == NULL) fallback = c.any;
        Sleep(WIN_WAIT_MS);
    }
    /* Nothing resizable ever appeared, so this app's only window really is
       fixed-size. Place that rather than nothing — but it may have been a splash
       that has since closed, hence the liveness check. */
    if (w == NULL) w = fallback;
    if (w != NULL && IsWindow(w)) win_snap(w, layout);
    free(before);
}

static void launch_url_win(const char *url) {
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "open";
    sei.lpFile = url;
    sei.nShow  = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei))
        ui_err("failed to open '%s'", url);
}

/* Launches an app + optional target on Windows.
   code/cursor are .cmd launchers on PATH, so they go through a hidden cmd.exe.
   UWP / Microsoft Store apps (shell:AppsFolder\<AUMID>) are launched via
   explorer.exe. Every other app is a full executable path supplied by the
   user, launched directly via ShellExecuteEx. */
static void launch_app_win(const char *app, const char *target, const char *layout) {
    void *before = win_capture_before();   /* snapshot windows before launch */

    if (is_uwp_app(app)) {
        char params[WORKSPACE_APP_MAX + 4];
        snprintf(params, sizeof(params), "\"%s\"", app);
        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = "open";
        sei.lpFile       = "explorer.exe";
        sei.lpParameters = params;
        sei.nShow        = SW_SHOWNORMAL;
        if (!ShellExecuteExA(&sei))
            ui_err("failed to launch '%s'", app);
        (void)target;
    } else if (is_new_window_app(app)) {
        char cmd_params[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 64];
        if (target[0])
            snprintf(cmd_params, sizeof(cmd_params),
                     "/c %s --new-window \"%s\"", app, target);
        else
            snprintf(cmd_params, sizeof(cmd_params), "/c %s --new-window", app);

        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = "open";
        sei.lpFile       = "cmd.exe";
        sei.lpParameters = cmd_params;
        sei.nShow        = SW_HIDE;
        if (!ShellExecuteExA(&sei))
            ui_err("failed to launch '%s'", app);
    } else {
        char params[WORKSPACE_TARGET_MAX + 4];
        params[0] = '\0';
        if (target[0])
            snprintf(params, sizeof(params), "\"%s\"", target);

        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = "open";
        sei.lpFile       = app;
        sei.lpParameters = params[0] ? params : NULL;
        sei.nShow        = SW_SHOWNORMAL;
        if (!ShellExecuteExA(&sei))
            ui_err("failed to launch '%s'", app);
    }

    win_place_new(before, layout);   /* move the new window into its partition */
}
#endif

#if defined(__APPLE__)
/* ── Screens ─────────────────────────────────────────────────────────────────
   System Events places windows in one global coordinate space anchored at the
   *primary* screen's top-left, y growing downward. AppKit reports screens with
   a bottom-left origin instead, so every frame has to be flipped through the
   primary screen's height — the primary's, not the screen's own. Flipping a
   secondary display through its own height (what this code used to do) lands
   the window at the wrong y whenever the two displays differ in height, which
   is most of the time.

   The primary is identified as the screen whose frame origin is exactly (0,0) —
   the anchor of the global space, and the one thing here that is stable.
   NSScreen.mainScreen is not: it means "the screen holding the key window",
   which for a background osascript is wherever the user's focus happens to be.

   Screens are then ordered exactly as Windows orders monitors — primary first,
   then left-to-right, then top-to-bottom — so "screen N" in the placement
   picker means the same thing on both platforms. Enumerated once per run. */

#define MAC_SCREEN_MAX 16

typedef struct { int x, y, w, h, primary; } MacScreen;

/* a before b? primary first, then left-to-right, then top-to-bottom. */
static int mac_screen_less(const MacScreen *a, const MacScreen *b) {
    if (a->primary != b->primary) return a->primary > b->primary;
    if (a->x != b->x)             return a->x < b->x;
    return a->y < b->y;
}

/* Fills `out` with the visible frame (menu bar and Dock excluded) of every
   screen, in System Events' top-left coordinates, sorted. Returns the count. */
static int mac_screens(MacScreen *out, int max) {
    static MacScreen cache[MAC_SCREEN_MAX];
    static int cached = -1;

    if (cached < 0) {
        cached = 0;
        FILE *fp = popen(
            "osascript -l JavaScript -e 'ObjC.import(\"AppKit\");"
            "function R(n){return Math.round(n)}"
            "var ss=$.NSScreen.screens,H=0,i,f;"
            "for(i=0;i<ss.count;i++){f=ss.objectAtIndex(i).frame;"
            "if(R(f.origin.x)===0&&R(f.origin.y)===0)H=f.size.height}"
            "if(H===0)H=ss.objectAtIndex(0).frame.size.height;"
            "var o=[];"
            "for(i=0;i<ss.count;i++){var s=ss.objectAtIndex(i),v=s.visibleFrame;"
            "f=s.frame;"
            "o.push([R(v.origin.x),R(H-(v.origin.y+v.size.height)),"
            "R(v.size.width),R(v.size.height),"
            "(R(f.origin.x)===0&&R(f.origin.y)===0)?1:0].join(\" \"))}"
            "o.join(\"\\n\")' 2>/dev/null", "r");
        if (fp != NULL) {
            char line[160];
            while (cached < MAC_SCREEN_MAX && fgets(line, sizeof(line), fp) != NULL) {
                MacScreen s;
                if (sscanf(line, "%d %d %d %d %d",
                           &s.x, &s.y, &s.w, &s.h, &s.primary) == 5 &&
                    s.w > 0 && s.h > 0)
                    cache[cached++] = s;
            }
            pclose(fp);
        }

        /* AppKit unreachable (JXA disabled, or a stripped-down system): fall
           back to Finder's desktop bounds. One screen, and the menu bar / Dock
           are not excluded — the old behaviour, but placement still works. */
        if (cached == 0) {
            FILE *fp2 = popen("osascript -e 'tell application \"Finder\" to get "
                              "bounds of window of desktop' 2>/dev/null", "r");
            if (fp2 != NULL) {
                char buf[128] = {0};
                char *got = fgets(buf, sizeof(buf), fp2);
                pclose(fp2);
                MacScreen s = { 0, 0, 0, 0, 1 };
                if (got != NULL &&
                    sscanf(buf, "%*d, %*d, %d, %d", &s.w, &s.h) == 2 &&
                    s.w > 0 && s.h > 0)
                    cache[cached++] = s;
            }
        }

        for (int i = 0; i < cached; i++) {      /* selection sort, as on Windows */
            int best = i;
            for (int k = i + 1; k < cached; k++)
                if (mac_screen_less(&cache[k], &cache[best])) best = k;
            if (best != i) {
                MacScreen t = cache[i]; cache[i] = cache[best]; cache[best] = t;
            }
        }
    }

    int n = (cached < max) ? cached : max;
    for (int i = 0; i < n; i++) out[i] = cache[i];
    return n;
}

int screen_count(void) {
    MacScreen s[MAC_SCREEN_MAX];
    int n = mac_screens(s, MAC_SCREEN_MAX);
    return n > 0 ? n : 1;
}

/* Visible frame of the 1-based screen `index`; out-of-range falls back to the
   primary (sorted first), matching screen_work_area on Windows. */
static int mac_screen_area(int index, int *X, int *Y, int *W, int *H) {
    MacScreen s[MAC_SCREEN_MAX];
    int n = mac_screens(s, MAC_SCREEN_MAX);
    if (n <= 0) return 0;
    int i = index - 1;
    if (i < 0 || i >= n) i = 0;
    *X = s[i].x; *Y = s[i].y; *W = s[i].w; *H = s[i].h;
    return 1;
}

/* ── Finding the process that owns an app's windows ──────────────────────────
   A workspace stores the name `open -a` accepts — a bundle's *display* name
   ("Visual Studio Code"), or one of the IDE launcher names ("code"). System
   Events, however, reports a process under its CFBundleName. For most apps the
   two agree (Safari is "Safari" either way), but for VS Code they do not: it is
   "Visual Studio Code" on disk and "Code" as a process. Matching on the name
   therefore silently found no process for exactly those apps, and placement sat
   out its full timeout doing nothing.

   LaunchServices resolves either spelling to the same bundle identifier without
   launching anything, so match on that instead and keep the name match as the
   fallback for an app it can't resolve. */

/* Resolves appName to a bundle identifier in `theID`, or "" on failure. `id of
   application` is a Standard Additions command, so this has to sit outside the
   System Events tell block. */
#define MAC_RESOLVE_APP_ID \
    " -e 'set theID to \"\"'" \
    " -e 'try'" \
    " -e 'set theID to id of application appName'" \
    " -e 'end try'"

/* Sets procList to the app's running processes. Used inside the tell block. */
#define MAC_SET_PROCLIST \
    " -e 'if theID is not \"\" then'" \
    " -e 'set procList to (every process whose bundle identifier is theID)'" \
    " -e 'else'" \
    " -e 'set procList to (every process whose name is appName)'" \
    " -e 'end if'"

/* Standard windows `app` owns right now; 0 if it isn't running, or if the
   window query is refused (no Accessibility — placement can't work then either,
   and mac_place_window reports that itself). Sampled just before a launch that
   is known to add a window, so the placement can wait for *that* window instead
   of grabbing one the app already had open. */
int mac_window_count(const char *app) {
    char cmd[WORKSPACE_APP_MAX + 512];
    snprintf(cmd, sizeof(cmd),
        "osascript"
        " -e 'set appName to \"%s\"'"
        MAC_RESOLVE_APP_ID
        " -e 'tell application \"System Events\"'"
        MAC_SET_PROCLIST
        " -e 'if (count of procList) is 0 then return 0'"
        " -e 'return (count of (every window of item 1 of procList"
        " whose subrole is \"AXStandardWindow\"))'"
        " -e 'end tell' 2>/dev/null",
        app);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return 0;
    char buf[32] = {0};
    char *got = fgets(buf, sizeof(buf), fp);
    pclose(fp);

    int n = (got != NULL) ? atoi(buf) : 0;
    return n > 0 ? n : 0;
}

/* Positions a window of `app` into the partition `layout` via System Events, on
   whichever screen the layout names.

   The process is found by bundle identifier (see MAC_SET_PROCLIST above), and we
   poll until it owns a window that is worth placing — so a slow-launching app is
   awaited rather than missed.

   "Worth placing" means subrole AXStandardWindow. A cold-starting app shows a
   splash or updater first (Discord is the usual offender), and that window is
   what `front window` returns: we would place the splash, the splash would then
   close, and the real window would open wherever it liked. Only a proper
   titled window carries the standard subrole. If none ever appears we settle for
   the front window at the end rather than placing nothing at all.

   `prior_windows` is how many standard windows the app owned before the launch:
   we wait for one *beyond* that count, which is what stops an already-running
   app (a second `code --new-window`) from having its existing front window
   snapped while the newly opened one keeps the geometry it restored. Pass 0 when
   the launch reuses an existing window (`open -a` on a running app) so the first
   window found is placed immediately rather than waited on.

   Position is re-applied after the resize because moving a window across screens
   can make the window server clamp it back to the display it came from.

   The script reports back: "ok" placed, "perm" assistive access denied,
   "timeout" nothing to place. The Accessibility hint is printed once; after a
   denial the rest of the run skips (retrying would only nag). */
void mac_place_window(const char *app, const char *layout, int prior_windows) {
    static int perm_denied = 0;
    if (perm_denied) return;
    if (prior_windows < 0) prior_windows = 0;

    int screen = 1;
    char part[16];
    layout_parse(layout, &screen, part, sizeof(part));

    int X, Y, W, H;
    if (!mac_screen_area(screen, &X, &Y, &W, &H)) return;

    int x, y, w, h;
    if (!partition_rect(part, X, Y, W, H, &x, &y, &w, &h)) {
        x = X; y = Y; w = W; h = H;   /* "full" / unknown → whole visible area */
    }

    char cmd[WORKSPACE_APP_MAX + 2048];
    snprintf(cmd, sizeof(cmd),
        "osascript"
        /* theWin / theProc rather than window / target: bare nouns risk
           colliding with System Events' own dictionary terms. */
        " -e 'set appName to \"%s\"'"
        MAC_RESOLVE_APP_ID
        " -e 'tell application \"System Events\"'"
        " -e 'set theWin to missing value'"
        " -e 'set theProc to missing value'"
        " -e 'repeat 75 times'"              /* ~15s; exits as soon as ready */
        " -e 'try'"
        MAC_SET_PROCLIST
        " -e 'if (count of procList) > 0 then'"
        " -e 'set theProc to item 1 of procList'"
        /* No inner try around this: a transient error (the process is still
           mid-launch) is caught by the outer handler and simply retried, while
           an Accessibility refusal has to reach that handler to be reported.
           Swallowing it here is what used to turn a denial into a silent 15s
           wait and no hint. */
        " -e 'set winList to (every window of theProc whose subrole is \"AXStandardWindow\")'"
        " -e 'if (count of winList) > %d then'"
        " -e 'set theWin to item 1 of winList'"
        " -e 'exit repeat'"
        " -e 'end if'"
        " -e 'end if'"
        " -e 'on error number errNum'"
        /* -25211 is the one System Events actually raises for a denied
           terminal; -1719/-1743 cover the other refusal shapes. */
        " -e 'if errNum is -25211 or errNum is -1719 or errNum is -1743 then return \"perm\"'"
        " -e 'end try'"
        " -e 'delay 0.2'"
        " -e 'end repeat'"
        /* no standard window ever showed up — settle for whatever is in front */
        " -e 'if theWin is missing value and theProc is not missing value then'"
        " -e 'try'"
        " -e 'if (count of windows of theProc) > 0 then set theWin to front window of theProc'"
        " -e 'end try'"
        " -e 'end if'"
        " -e 'if theWin is missing value then return \"timeout\"'"
        " -e 'try'"
        " -e 'set frontmost of theProc to true'"
        " -e 'end try'"
        " -e 'set position of theWin to {%d, %d}'"
        " -e 'try'"
        " -e 'set size of theWin to {%d, %d}'"
        " -e 'end try'"
        " -e 'try'"
        " -e 'set position of theWin to {%d, %d}'"
        " -e 'end try'"
        " -e 'return \"ok\"'"
        " -e 'end tell' 2>/dev/null",
        app, prior_windows, x, y, w, h, x, y);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return;
    char res[32] = {0};
    char *got = fgets(res, sizeof(res), fp);
    pclose(fp);

    if (got != NULL && strncmp(res, "perm", 4) == 0) {
        perm_denied = 1;   /* skip the remaining apps this run */
        fprintf(stderr,
            "note: Mnemosyne can't position windows until you grant Accessibility "
            "access to your terminal (System Settings > Privacy & Security > "
            "Accessibility), then reopen the workspace.\n");
    }
}
#endif

void app_launch(const char *app, const char *target, const char *layout) {
    /* A workspace saved before app names were canonicalised can hold "Code";
       invoke the launcher by the name it actually has on PATH. */
    const char *launcher = new_window_launcher(app);
    if (launcher != NULL) app = launcher;

    /* If the target is a protocol URI (spotify:..., https://..., etc.), let
       the OS route it to the right handler. The chosen app is informational
       — Windows / macOS / Linux already know which app owns each scheme. The OS
       handler decides the window, so layout placement does not apply here. */
    if (is_url(target)) {
        (void)layout;
#ifdef _WIN32
        launch_url_win(target);
#elif defined(__APPLE__)
        char cmd[WORKSPACE_TARGET_MAX + 16];
        snprintf(cmd, sizeof(cmd), "open \"%s\"", target);
        system(cmd);
#else
        char cmd[WORKSPACE_TARGET_MAX + 32];
        snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" &", target);
        system(cmd);
#endif
        (void)app;
        return;
    }

#ifdef _WIN32
    launch_app_win(app, target, layout);
#elif defined(__APPLE__)
    (void)layout;
    {
        const char *new_win = is_new_window_app(app) ? " --new-window" : "";
        char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 64];
        /* 'open -a <app>' resolves GUI apps by bundle name (case-insensitive).
           code/cursor are in PATH via their installer — use bare name. */
        if (!is_new_window_app(app)) {
            if (target[0] != '\0')
                snprintf(cmd, sizeof(cmd), "open -a \"%s\" \"%s\"", app, target);
            else
                snprintf(cmd, sizeof(cmd), "open -a \"%s\"", app);
        } else {
            if (target[0] != '\0')
                snprintf(cmd, sizeof(cmd), "%s%s \"%s\"", app, new_win, target);
            else
                snprintf(cmd, sizeof(cmd), "%s%s", app, new_win);
        }
        system(cmd);
    }
#else
    (void)layout;
    {
        /* code/cursor return immediately; a user-supplied executable path is a
           GUI app that would otherwise block, so background it with '&'. */
        const char *new_win = is_new_window_app(app) ? " --new-window" : "";
        const char *bg      = is_new_window_app(app) ? "" : " &";
        char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 32];
        if (target[0] != '\0')
            snprintf(cmd, sizeof(cmd), "\"%s\"%s \"%s\"%s", app, new_win, target, bg);
        else
            snprintf(cmd, sizeof(cmd), "\"%s\"%s%s", app, new_win, bg);
        system(cmd);
    }
#endif
}

void open_with_default_app(const char *path) {
#ifdef _WIN32
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "open";
    sei.lpFile = path;
    sei.nShow  = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei))
        ui_err("failed to open '%s'", path);
#elif defined(__APPLE__)
    char cmd[WORKSPACE_TARGET_MAX + 16];
    snprintf(cmd, sizeof(cmd), "open \"%s\"", path);
    system(cmd);
#else
    char cmd[WORKSPACE_TARGET_MAX + 32];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" &", path);
    system(cmd);
#endif
}