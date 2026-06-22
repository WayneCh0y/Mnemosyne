#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_resolve.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

#define UWP_PREFIX     "shell:AppsFolder\\"
#define UWP_PREFIX_LEN 17

int is_uwp_app(const char *app) {
    return app != NULL && strncmp(app, UWP_PREFIX, UWP_PREFIX_LEN) == 0;
}

#ifdef _WIN32

static int ascii_ieq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int ascii_iprefix(const char *prefix, const char *s) {
    while (*prefix && *s) {
        char cp = (*prefix >= 'A' && *prefix <= 'Z') ? (char)(*prefix + 32) : *prefix;
        char cs = (*s      >= 'A' && *s      <= 'Z') ? (char)(*s      + 32) : *s;
        if (cp != cs) return 0;
        prefix++; s++;
    }
    return *prefix == 0;
}

/* Copies `name` into `out` with a trailing .exe stripped (case-insensitive).
   Returns 1 on success, 0 if the result would be empty or too long. */
static int strip_exe(const char *name, char *out, size_t out_size) {
    size_t n = strlen(name);
    if (n >= 4 && ascii_ieq(name + n - 4, ".exe")) n -= 4;
    if (n == 0 || n >= out_size) return 0;
    memcpy(out, name, n);
    out[n] = '\0';
    return 1;
}

/* Recursively scans `dir` (max 8 levels) for *.lnk files whose stem matches
   `query` exactly or by case-insensitive prefix. Updates *best with the
   shortest matching stem; exact matches win. */
static void scan_lnks(const char *dir, const char *query, int depth,
                      char *best, size_t best_size,
                      size_t *best_stem_len, int *best_exact) {
    if (depth > 8) return;

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char full[MAX_PATH + 256];
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_lnks(full, query, depth + 1, best, best_size,
                      best_stem_len, best_exact);
            continue;
        }

        size_t fn_len = strlen(fd.cFileName);
        if (fn_len < 5) continue;
        if (!ascii_ieq(fd.cFileName + fn_len - 4, ".lnk")) continue;

        char stem[MAX_PATH];
        size_t stem_len = fn_len - 4;
        if (stem_len >= sizeof(stem)) continue;
        memcpy(stem, fd.cFileName, stem_len);
        stem[stem_len] = '\0';

        if (ascii_ieq(stem, query)) {
            strncpy(best, full, best_size - 1);
            best[best_size - 1] = '\0';
            *best_stem_len = stem_len;
            *best_exact    = 1;
        } else if (!*best_exact && ascii_iprefix(query, stem)) {
            if (best[0] == '\0' || stem_len < *best_stem_len) {
                strncpy(best, full, best_size - 1);
                best[best_size - 1] = '\0';
                *best_stem_len = stem_len;
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

static int find_shortcut(const char *name, char *out, size_t out_size) {
    char query[256];
    if (!strip_exe(name, query, sizeof(query))) return 0;

    char dirs[4][MAX_PATH];
    int nd = 0;
    const char *up       = getenv("USERPROFILE");
    const char *pub      = getenv("PUBLIC");
    const char *appdata  = getenv("APPDATA");
    const char *progdata = getenv("ProgramData");
    if (up)       snprintf(dirs[nd++], MAX_PATH, "%s\\Desktop", up);
    if (pub)      snprintf(dirs[nd++], MAX_PATH, "%s\\Desktop", pub);
    if (appdata)  snprintf(dirs[nd++], MAX_PATH,
                           "%s\\Microsoft\\Windows\\Start Menu\\Programs", appdata);
    if (progdata) snprintf(dirs[nd++], MAX_PATH,
                           "%s\\Microsoft\\Windows\\Start Menu\\Programs", progdata);

    char best[MAX_PATH] = {0};
    size_t best_stem_len = 0;
    int best_exact = 0;

    for (int i = 0; i < nd; i++) {
        scan_lnks(dirs[i], query, 0, best, sizeof(best),
                  &best_stem_len, &best_exact);
    }

    if (best[0] == '\0') return 0;
    strncpy(out, best, out_size - 1);
    out[out_size - 1] = '\0';
    return 1;
}

/* Finds a UWP / Store app by name via PowerShell's Get-StartApps. On match,
   writes "shell:AppsFolder\<AUMID>" — a value the launcher hands to
   explorer.exe. */
static int find_uwp(const char *name, char *out, size_t out_size) {
    char query[128];
    if (!strip_exe(name, query, sizeof(query))) return 0;

    /* Restrict to a safe character set since `query` is interpolated into
       the PowerShell command line. */
    for (const char *p = query; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == ' ' || *p == '-' || *p == '.')) {
            return 0;
        }
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "powershell.exe -NoProfile -NonInteractive -Command "
             "\"Get-StartApps | Where-Object Name -like '*%s*' | "
             "Select-Object -First 1 -ExpandProperty AppID\" 2>NUL",
             query);

    FILE *p = _popen(cmd, "r");
    if (!p) return 0;

    char appid[256] = {0};
    char *got = fgets(appid, sizeof(appid), p);
    _pclose(p);
    if (!got) return 0;

    size_t n = strlen(appid);
    while (n > 0 && (appid[n-1] == '\r' || appid[n-1] == '\n' ||
                     appid[n-1] == ' '  || appid[n-1] == '\t')) {
        appid[--n] = '\0';
    }
    if (n == 0) return 0;

    /* UWP AUMIDs contain '!'. A path-like result is a desktop shortcut
       already handled by find_shortcut — skip. */
    if (strchr(appid, '!') == NULL) return 0;

    int written = snprintf(out, out_size, "%s%s", UWP_PREFIX, appid);
    return (written > 0 && (size_t)written < out_size) ? 1 : 0;
}

int app_resolve(const char *name, char *out, size_t out_size) {
    if (name == NULL || name[0] == '\0') return 0;
    if (is_uwp_app(name)) return 0;
    for (const char *p = name; *p; p++) {
        if (*p == '\\' || *p == '/' || *p == ':') return 0;
    }

    /* App Paths uses "<name>.exe" as the subkey. */
    char qname[256];
    size_t len = strlen(name);
    if (len >= sizeof(qname) - 4) return 0;
    int has_exe = (len >= 4 && ascii_ieq(name + len - 4, ".exe"));
    snprintf(qname, sizeof(qname), has_exe ? "%s" : "%s.exe", name);

    char subkey[512];
    snprintf(subkey, sizeof(subkey),
             "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s", qname);
    HKEY roots[2] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (int i = 0; i < 2; i++) {
        DWORD sz = (DWORD)out_size;
        if (RegGetValueA(roots[i], subkey, NULL, RRF_RT_REG_SZ,
                         NULL, out, &sz) == ERROR_SUCCESS && out[0] != '\0') {
            size_t n = strlen(out);
            if (n >= 2 && out[0] == '"' && out[n-1] == '"') {
                memmove(out, out + 1, n - 2);
                out[n - 2] = '\0';
            }
            return 1;
        }
    }

    DWORD found = SearchPathA(NULL, qname, NULL, (DWORD)out_size, out, NULL);
    if (found > 0 && found < out_size) return 1;

    if (find_shortcut(name, out, out_size)) return 1;
    if (find_uwp(name, out, out_size))      return 1;

    return 0;
}

int app_value_exists(const char *app) {
    if (is_uwp_app(app)) return 1;
    return GetFileAttributesA(app) != INVALID_FILE_ATTRIBUTES;
}

#else /* !_WIN32 */

int app_resolve(const char *name, char *out, size_t out_size) {
    (void)name; (void)out; (void)out_size;
    return 0;
}

int app_value_exists(const char *app) {
    /* No path separator → treat as a macOS bundle name (or unresolved name
       on Linux) and trust it rather than blocking the user. */
    if (strchr(app, '/') == NULL) return 1;
    return access(app, F_OK) == 0;
}

#endif
