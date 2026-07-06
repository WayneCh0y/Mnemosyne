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

int is_new_window_app(const char *app) {
    return strcmp(app, "code") == 0;
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

#ifndef _WIN32
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

typedef struct { const HwndSet *before; HWND found; } NewWinCtx;

/* A "real" app window: visible, top-level (no owner), not a tool window, and a
   sensible size — and not one that already existed before the launch. */
static BOOL CALLBACK newwin_cb(HWND h, LPARAM lp) {
    NewWinCtx *c = (NewWinCtx *)lp;
    if (!IsWindowVisible(h))            return TRUE;
    if (GetWindow(h, GW_OWNER) != NULL) return TRUE;
    if (hwnd_in_set(c->before, h))      return TRUE;
    if (GetWindowLongA(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    RECT r;
    if (!GetWindowRect(h, &r)) return TRUE;
    if (r.right - r.left < 200 || r.bottom - r.top < 150) return TRUE;
    c->found = h;
    return FALSE;   /* stop at the first match */
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

void win_place_new(void *before_v, const char *layout) {
    HwndSet *before = (HwndSet *)before_v;
    if (before == NULL) return;
    HWND w = NULL;
    for (int tries = 0; tries < 40 && w == NULL; tries++) {   /* ~3s max */
        NewWinCtx c = { before, NULL };
        EnumWindows(newwin_cb, (LPARAM)&c);
        w = c.found;
        if (w == NULL) Sleep(75);
    }
    if (w) win_snap(w, layout);
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
/* Visible frame of the main display — the usable area with the menu bar and Dock
   excluded — in System Events' top-left coordinates: *X,*Y is the top-left corner
   and *W,*H the size. Cached so a multi-app workspace resolves it only once.

   Primary source is AppKit's NSScreen.visibleFrame via JXA (converted from Cocoa's
   bottom-left origin to top-left). If that's unavailable we fall back to Finder's
   full desktop bounds (origin 0,0) — placement then ignores the menu bar, the old
   behaviour, but at least still works. Returns 1 on success. */
static int mac_visible_frame(int *X, int *Y, int *W, int *H) {
    static int rx = 0, ry = 0, rw = 0, rh = 0, done = 0;
    if (!done) {
        FILE *fp = popen(
            "osascript -l JavaScript -e 'ObjC.import(\"AppKit\");"
            "var s=$.NSScreen.mainScreen,v=s.visibleFrame,f=s.frame;"
            "function R(n){return Math.round(n)}"
            "[R(v.origin.x),R(f.size.height-(v.origin.y+v.size.height)),"
            "R(v.size.width),R(v.size.height)].join(\" \")' 2>/dev/null", "r");
        if (fp != NULL) {
            char out[160] = {0};
            char *got = fgets(out, sizeof(out), fp);
            pclose(fp);
            if (got != NULL && sscanf(out, "%d %d %d %d", &rx, &ry, &rw, &rh) == 4
                && rw > 0 && rh > 0)
                done = 1;
        }
        if (!done) {   /* fallback: full desktop bounds "0, 0, W, H" */
            FILE *fp2 = popen("osascript -e 'tell application \"Finder\" to get "
                              "bounds of window of desktop' 2>/dev/null", "r");
            if (fp2 == NULL) return 0;
            char out[128] = {0};
            char *got = fgets(out, sizeof(out), fp2);
            pclose(fp2);
            if (got == NULL) return 0;
            rx = 0; ry = 0;
            if (sscanf(out, "%*d, %*d, %d, %d", &rw, &rh) != 2 || rw <= 0 || rh <= 0)
                return 0;
            done = 1;
        }
    }
    *X = rx; *Y = ry; *W = rw; *H = rh;
    return 1;
}

/* Positions the front window of `app` into the partition `layout` via System
   Events. Main display only (screen prefix > 1 is ignored).

   The window is matched by *case-insensitive* process name (AppleScript `whose
   name is` ignores case, so "mail" matches "Mail"), and we poll until that
   process exists AND owns a window before moving/sizing it — so a slow-launching
   app (e.g. Discord) is awaited rather than missed. The script reports back:
   "ok" placed, "perm" assistive access denied, "timeout" never appeared. The
   Accessibility hint is printed once; after a denial the rest of the run skips
   (retrying would only nag). */
void mac_place_window(const char *app, const char *layout) {
    static int perm_denied = 0;
    if (perm_denied) return;

    int screen = 1;
    char part[16];
    layout_parse(layout, &screen, part, sizeof(part));
    if (screen != 1) return;   /* main display only for now */

    int X, Y, W, H;
    if (!mac_visible_frame(&X, &Y, &W, &H)) return;

    int x, y, w, h;
    if (!partition_rect(part, X, Y, W, H, &x, &y, &w, &h)) {
        x = X; y = Y; w = W; h = H;   /* "full" / unknown → whole visible area */
    }

    char cmd[WORKSPACE_APP_MAX + 1024];
    snprintf(cmd, sizeof(cmd),
        "osascript"
        " -e 'set appName to \"%s\"'"
        " -e 'tell application \"System Events\"'"
        " -e 'repeat 50 times'"               /* ~10s; returns as soon as ready */
        " -e 'try'"
        " -e 'set ps to (every process whose name is appName)'"
        " -e 'if (count of ps) > 0 then'"
        " -e 'set p to item 1 of ps'"
        " -e 'if (count of windows of p) > 0 then'"
        " -e 'set frontmost of p to true'"
        " -e 'set position of front window of p to {%d, %d}'"
        " -e 'try'"
        " -e 'set size of front window of p to {%d, %d}'"
        " -e 'end try'"
        " -e 'return \"ok\"'"
        " -e 'end if'"
        " -e 'end if'"
        " -e 'on error number errNum'"
        " -e 'if errNum is -1719 then return \"perm\"'"
        " -e 'end try'"
        " -e 'delay 0.2'"
        " -e 'end repeat'"
        " -e 'return \"timeout\"'"
        " -e 'end tell' 2>/dev/null",
        app, x, y, w, h);

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