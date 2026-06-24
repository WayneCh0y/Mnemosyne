#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_enum.h"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#endif

/* ── shared helpers ────────────────────────────────────────────────────── */

/* Basename after the last '/' or '\\'. Returns a pointer within path. */
static const char *base_name(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

#ifdef _WIN32
static int path_ieq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}
#else
static int path_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
#endif

/* If `display` (the captured basename / process name, with an optional trailing
   .exe) is a known launcher, return the canonical name `mn open add` would store
   so snapshots launch identically. Otherwise NULL. */
static const char *canonical_launcher(const char *display) {
    const char *b = base_name(display);
    size_t n = strlen(b);
    if (n >= 4) {                      /* strip a trailing ".exe", case-insensitive */
        const char *ext = b + n - 4;
        if (ext[0] == '.' &&
            (ext[1] == 'e' || ext[1] == 'E') &&
            (ext[2] == 'x' || ext[2] == 'X') &&
            (ext[3] == 'e' || ext[3] == 'E'))
            n -= 4;
    }
    if (n == 4 &&
        (b[0] == 'c' || b[0] == 'C') && (b[1] == 'o' || b[1] == 'O') &&
        (b[2] == 'd' || b[2] == 'D') && (b[3] == 'e' || b[3] == 'E'))
        return "code";
    return NULL;
}

/* Append app/display to out unless app is already present. */
static void add_unique(RunningApp *out, int *count, int max,
                       const char *app, const char *display) {
    if (*count >= max) return;
    const char *launch = canonical_launcher(display);
    if (launch) app = launch;
    for (int i = 0; i < *count; i++) {
#ifdef _WIN32
        if (path_ieq(out[i].app, app)) return;
#else
        if (path_eq(out[i].app, app)) return;
#endif
    }
    snprintf(out[*count].app, sizeof(out[*count].app), "%s", app);
    snprintf(out[*count].display, sizeof(out[*count].display), "%s", display);
    (*count)++;
}

/* ── Windows ───────────────────────────────────────────────────────────── */
#ifdef _WIN32

static DWORD parent_pid_of(DWORD pid) {
    DWORD ppid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = { .dwSize = sizeof(pe) };
        if (Process32First(snap, &pe)) {
            do {
                if (pe.th32ProcessID == pid) { ppid = pe.th32ParentProcessID; break; }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }
    return ppid;
}

typedef struct {
    RunningApp *out;
    int        *count;
    int         max;
    DWORD       self_pid;
    DWORD       parent_pid;
} EnumCtx;

static BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lparam) {
    EnumCtx *ctx = (EnumCtx *)lparam;

    /* Keep only top-level, user-facing windows (the "alt-tab" heuristic). */
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;
    if (GetWindowTextLengthA(hwnd) == 0) return TRUE;
    if (GetWindowLongPtrA(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;

    /* Skip DWM-cloaked windows: suspended UWP apps, other virtual desktops, and
       host windows like ApplicationFrameHost/TextInputHost. */
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
        && cloaked)
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == ctx->self_pid || pid == ctx->parent_pid) return TRUE;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return TRUE;

    char path[WORKSPACE_APP_MAX];
    DWORD sz = sizeof(path);
    if (QueryFullProcessImageNameA(h, 0, path, &sz))
        add_unique(ctx->out, ctx->count, ctx->max, path, base_name(path));
    CloseHandle(h);

    return (*ctx->count < ctx->max) ? TRUE : FALSE;
}

int app_enum_running(RunningApp *out, int max) {
    int count = 0;
    DWORD self = GetCurrentProcessId();
    EnumCtx ctx = { out, &count, max, self, parent_pid_of(self) };
    EnumWindows(enum_proc, (LPARAM)&ctx);
    return count;
}

/* ── macOS ─────────────────────────────────────────────────────────────── */
#elif defined(__APPLE__)

int app_enum_running(RunningApp *out, int max) {
    /* System Events lists every GUI (non-background-only) process by app name,
       which is exactly the value `open -a <name>` (see app_launch.c) expects. */
    FILE *fp = popen(
        "osascript -e 'tell application \"System Events\" to get name of "
        "every process whose background only is false' 2>/dev/null", "r");
    if (!fp) return -1;

    char buf[8192] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    pclose(fp);

    int count = 0;
    /* AppleScript prints a comma+space separated list, e.g. "Safari, Code". */
    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ' || *tok == '\t' || *tok == '\n') tok++;
        char *end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n'))
            *--end = '\0';
        if (*tok) add_unique(out, &count, max, tok, tok);
    }
    return count;
}

/* ── Linux ─────────────────────────────────────────────────────────────── */
#else
#include <unistd.h>

int app_enum_running(RunningApp *out, int max) {
    /* wmctrl -lp lists managed windows; column 3 is the owning PID. */
    FILE *fp = popen("wmctrl -lp 2>/dev/null", "r");
    if (!fp) return -1;

    char line[4096];
    int count = 0;
    int any = 0;
    while (fgets(line, sizeof(line), fp)) {
        any = 1;
        /* Fields: <win-id> <desktop> <pid> <host> <title...> */
        unsigned long winid; int desktop; long pid;
        if (sscanf(line, "%lx %d %ld", &winid, &desktop, &pid) != 3) continue;
        if (pid <= 0) continue;

        char link[64], path[WORKSPACE_APP_MAX];
        snprintf(link, sizeof(link), "/proc/%ld/exe", pid);
        ssize_t len = readlink(link, path, sizeof(path) - 1);
        if (len <= 0) continue;
        path[len] = '\0';
        add_unique(out, &count, max, path, base_name(path));
        if (count >= max) break;
    }
    int rc = pclose(fp);

    /* No output and a non-zero exit usually means wmctrl isn't installed. */
    if (!any && rc != 0) return -1;
    return count;
}

#endif
