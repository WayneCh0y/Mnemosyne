#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_enum.h"
#include "cJSON.h"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#endif

/* ── shared helpers ────────────────────────────────────────────────────── */

static const char *base_name(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

static const char *canonical_launcher(const char *display) {
    const char *b = base_name(display);
    size_t n = strlen(b);
    if (n >= 4) {
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

/* Append a RunningApp entry.  Display is the process basename only.
   On Windows every top-level window is a distinct entry (two Edge windows →
   two rows).  On macOS/Linux true duplicates are skipped. */
static void add_unique(RunningApp *out, int *count, int max,
                       const char *app, const char *display, const char *target) {
    if (*count >= max) return;
    const char *launch = canonical_launcher(display);
    if (launch) app = launch;
#ifndef _WIN32
    for (int i = 0; i < *count; i++)
        if (strcmp(out[i].app, app) == 0 && strcmp(out[i].display, display) == 0)
            return;
#endif
    snprintf(out[*count].app,     sizeof(out[*count].app),     "%s", app);
    snprintf(out[*count].display, sizeof(out[*count].display), "%s", display);
    snprintf(out[*count].target,  sizeof(out[*count].target),  "%s", target ? target : "");
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

static int str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

static const char *ide_storage_launcher_for_process(const char *bn) {
    const char *launcher = canonical_launcher(bn);
    if (launcher) return launcher;
    if (str_ieq(bn, "Cursor.exe") || str_ieq(bn, "Cursor")) return "cursor";
    return NULL;
}

static int ascii_i_contains(const char *haystack, const char *needle) {
    if (needle == NULL || needle[0] == '\0') return 0;
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < n && p[i]) {
            char ca = (p[i] >= 'A' && p[i] <= 'Z') ? (char)(p[i] + 32) : p[i];
            char cb = (needle[i] >= 'A' && needle[i] <= 'Z') ? (char)(needle[i] + 32) : needle[i];
            if (ca != cb) break;
            i++;
        }
        if (i == n) return 1;
    }
    return 0;
}

static int is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static void percent_decode(const char *src, char *out, size_t out_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < out_size; i++) {
        if (src[i] == '%' && is_hex(src[i + 1]) && is_hex(src[i + 2])) {
            out[j++] = (char)((hex_val(src[i + 1]) << 4) | hex_val(src[i + 2]));
            i += 2;
        } else {
            out[j++] = src[i];
        }
    }
    out[j] = '\0';
}

static int storage_value_to_path(const char *value, char *out, size_t out_size) {
    if (value == NULL || value[0] == '\0') return 0;

    char tmp[WORKSPACE_TARGET_MAX];
    if (strncmp(value, "file://", 7) == 0) {
        percent_decode(value + 7, tmp, sizeof(tmp));
        if (tmp[0] == '/' &&
            ((tmp[1] >= 'A' && tmp[1] <= 'Z') || (tmp[1] >= 'a' && tmp[1] <= 'z')) &&
            tmp[2] == ':') {
            memmove(tmp, tmp + 1, strlen(tmp));
        } else if (tmp[0] != '/' && tmp[0] != '\0' && tmp[1] != ':') {
            char unc[WORKSPACE_TARGET_MAX];
            unc[0] = '\\';
            unc[1] = '\\';
            strncpy(unc + 2, tmp, sizeof(unc) - 3);
            unc[sizeof(unc) - 1] = '\0';
            memcpy(tmp, unc, sizeof(tmp));
        }
    } else {
        strncpy(tmp, value, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
    }

    for (char *p = tmp; *p; p++)
        if (*p == '/') *p = '\\';

    if (tmp[0] == '\0') return 0;
    strncpy(out, tmp, out_size - 1);
    out[out_size - 1] = '\0';
    return 1;
}

static int is_ide_target_key(const char *key) {
    if (key == NULL) return 0;
    return str_ieq(key, "folder") || str_ieq(key, "workspace") ||
           str_ieq(key, "file") || str_ieq(key, "folderUri") ||
           str_ieq(key, "workspaceUri") || str_ieq(key, "fileUri");
}

static void path_label(const char *path, char *out, size_t out_size) {
    const char *b = base_name(path);
    size_t n = strlen(b);
    if (n > 15 && ascii_i_contains(b, ".code-workspace"))
        n -= 15;
    if (n >= out_size) n = out_size - 1;
    memcpy(out, b, n);
    out[n] = '\0';
}

typedef struct {
    const char *title;
    char first[WORKSPACE_TARGET_MAX];
    int candidates;
    char best[WORKSPACE_TARGET_MAX];
    int best_score;
} IdeTargetMatch;

static void consider_ide_target(IdeTargetMatch *m, const char *value) {
    char path[WORKSPACE_TARGET_MAX];
    if (!storage_value_to_path(value, path, sizeof(path))) return;

    m->candidates++;
    if (m->candidates == 1) {
        strncpy(m->first, path, sizeof(m->first) - 1);
        m->first[sizeof(m->first) - 1] = '\0';
    }

    char label[256];
    path_label(path, label, sizeof(label));
    if (label[0] == '\0' || !ascii_i_contains(m->title, label)) return;

    int score = (int)strlen(label);
    if (score > m->best_score) {
        strncpy(m->best, path, sizeof(m->best) - 1);
        m->best[sizeof(m->best) - 1] = '\0';
        m->best_score = score;
    }
}

static void walk_ide_storage(const cJSON *node, IdeTargetMatch *match) {
    if (node == NULL) return;
    if (cJSON_IsObject(node)) {
        for (const cJSON *child = node->child; child; child = child->next) {
            if (cJSON_IsString(child) && is_ide_target_key(child->string))
                consider_ide_target(match, child->valuestring);
            walk_ide_storage(child, match);
        }
    } else if (cJSON_IsArray(node)) {
        for (const cJSON *child = node->child; child; child = child->next)
            walk_ide_storage(child, match);
    }
}

static int ide_storage_path(const char *launcher, char *out, size_t out_size) {
    const char *appdata = getenv("APPDATA");
    if (appdata == NULL || appdata[0] == '\0') return 0;

    const char *dir = NULL;
    if (strcmp(launcher, "code") == 0) dir = "Code";
    else if (strcmp(launcher, "cursor") == 0) dir = "Cursor";
    else return 0;

    int written = snprintf(out, out_size, "%s\\%s\\User\\globalStorage\\storage.json",
                           appdata, dir);
    return written > 0 && (size_t)written < out_size;
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);

    char *buf = malloc((size_t)size + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

static int find_ide_window_target(const char *launcher, const char *title,
                                  char *out, size_t out_size) {
    char path[MAX_PATH + 128];
    if (!ide_storage_path(launcher, path, sizeof(path))) return 0;

    char *buf = read_text_file(path);
    if (buf == NULL) return 0;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (root == NULL) return 0;

    IdeTargetMatch match;
    memset(&match, 0, sizeof(match));
    match.title = title ? title : "";
    walk_ide_storage(root, &match);
    cJSON_Delete(root);

    const char *chosen = match.best[0] ? match.best :
                         (match.candidates == 1 ? match.first : NULL);
    if (chosen == NULL || chosen[0] == '\0') return 0;

    strncpy(out, chosen, out_size - 1);
    out[out_size - 1] = '\0';
    return 1;
}

/* UWP infrastructure processes that host other apps — never user-facing. */
static int is_system_process(const char *bn) {
    return str_ieq(bn, "ApplicationFrameHost.exe")
        || str_ieq(bn, "TextInputHost.exe");
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

    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;
    char title[512];
    if (GetWindowTextA(hwnd, title, sizeof(title)) <= 0) return TRUE;
    if (GetWindowLongPtrA(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;

    /* UWP app content windows use this class — they're already represented by
       their ApplicationFrameHost frame (which we filter by process name), so
       skip the raw content window to avoid duplicates or orphan entries. */
    char cls[64];
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strcmp(cls, "Windows.UI.Core.CoreWindow") == 0) return TRUE;

    /* Skip app-cloaked windows (suspended UWP content).
       Shell-cloaked windows (other virtual desktops) still appear. */
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
            && (cloaked & DWM_CLOAKED_APP))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == ctx->self_pid || pid == ctx->parent_pid) return TRUE;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return TRUE;

    char path[WORKSPACE_APP_MAX];
    DWORD sz = sizeof(path);
    if (QueryFullProcessImageNameA(h, 0, path, &sz)) {
        const char *bn = base_name(path);
        if (!is_system_process(bn)) {
            char target[WORKSPACE_TARGET_MAX] = {0};
            const char *launcher = ide_storage_launcher_for_process(bn);
            if (launcher)
                find_ide_window_target(launcher, title, target, sizeof(target));
            add_unique(ctx->out, ctx->count, ctx->max, path, bn, target);
        }
    }
    CloseHandle(h);

    return (*ctx->count < ctx->max) ? TRUE : FALSE;
}

int app_enum_running(RunningApp *out, int max) {
    int count = 0;
    DWORD self = GetCurrentProcessId();
    EnumCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.out        = out;
    ctx.count      = &count;
    ctx.max        = max;
    ctx.self_pid   = self;
    ctx.parent_pid = parent_pid_of(self);
    EnumWindows(enum_proc, (LPARAM)&ctx);
    return count;
}

/* ── macOS ─────────────────────────────────────────────────────────────── */
#elif defined(__APPLE__)

int app_enum_running(RunningApp *out, int max) {
    FILE *fp = popen(
        "osascript -e 'tell application \"System Events\" to get name of "
        "every process whose background only is false' 2>/dev/null", "r");
    if (!fp) return -1;

    char buf[8192] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';
    pclose(fp);

    int count = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ' || *tok == '\t' || *tok == '\n') tok++;
        char *end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n'))
            *--end = '\0';
        if (*tok) add_unique(out, &count, max, tok, tok, "");
    }
    return count;
}

/* ── Linux ─────────────────────────────────────────────────────────────── */
#else
#include <unistd.h>

int app_enum_running(RunningApp *out, int max) {
    FILE *fp = popen("wmctrl -lp 2>/dev/null", "r");
    if (!fp) return -1;

    char line[4096];
    int count = 0;
    int any = 0;
    while (fgets(line, sizeof(line), fp)) {
        any = 1;
        unsigned long winid; int desktop; long pid;
        if (sscanf(line, "%lx %d %ld", &winid, &desktop, &pid) != 3) continue;
        if (pid <= 0) continue;

        char link[64], path[WORKSPACE_APP_MAX];
        snprintf(link, sizeof(link), "/proc/%ld/exe", pid);
        ssize_t len = readlink(link, path, sizeof(path) - 1);
        if (len <= 0) continue;
        path[len] = '\0';
        add_unique(out, &count, max, path, base_name(path), "");
        if (count >= max) break;
    }
    int rc = pclose(fp);

    if (!any && rc != 0) return -1;
    return count;
}

#endif
