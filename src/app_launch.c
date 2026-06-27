#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_launch.h"
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

#ifdef _WIN32
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

/* Moves w to the partition named by `layout` on the primary monitor's work area.
   "" / "full" (or anything unrecognised) → maximize. */
static void win_move(HWND w, const char *layout) {
    const char *tok = (layout && layout[0]) ? layout : "full";
    if (strcmp(tok, "full") == 0) { ShowWindow(w, SW_MAXIMIZE); return; }

    RECT wa;
    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0)) return;
    int L = wa.left, T = wa.top, W = wa.right - wa.left, H = wa.bottom - wa.top;
    int hw = W / 2, hh = H / 2;
    int x = L, y = T, ww = W, hgt = H;

    if      (strcmp(tok, "left")   == 0) { ww = hw; }
    else if (strcmp(tok, "right")  == 0) { x = L + hw; ww = W - hw; }
    else if (strcmp(tok, "top")    == 0) { hgt = hh; }
    else if (strcmp(tok, "bottom") == 0) { y = T + hh; hgt = H - hh; }
    else if (strcmp(tok, "tl")     == 0) { ww = hw; hgt = hh; }
    else if (strcmp(tok, "tr")     == 0) { x = L + hw; ww = W - hw; hgt = hh; }
    else if (strcmp(tok, "bl")     == 0) { y = T + hh; ww = hw; hgt = H - hh; }
    else if (strcmp(tok, "br")     == 0) { x = L + hw; y = T + hh; ww = W - hw; hgt = H - hh; }
    else { ShowWindow(w, SW_MAXIMIZE); return; }

    ShowWindow(w, SW_RESTORE);   /* a maximized window can't be freely sized */
    /* Expand the target by the invisible border so the *visible* frame lands flush
       on the grid (query after restoring — margins differ when maximized). */
    RECT m;
    win_frame_margins(w, &m);
    SetWindowPos(w, NULL, x - m.left, y - m.top,
                 ww + m.left + m.right, hgt + m.top + m.bottom,
                 SWP_NOZORDER | SWP_NOACTIVATE);
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
    if (w) win_move(w, layout);
    free(before);
}

static void launch_url_win(const char *url) {
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "open";
    sei.lpFile = url;
    sei.nShow  = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei))
        fprintf(stderr, "error: failed to open '%s'\n", url);
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
            fprintf(stderr, "error: failed to launch '%s'\n", app);
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
            fprintf(stderr, "error: failed to launch '%s'\n", app);
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
            fprintf(stderr, "error: failed to launch '%s'\n", app);
    }

    win_place_new(before, layout);   /* move the new window into its partition */
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
        fprintf(stderr, "error: failed to open '%s'\n", path);
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