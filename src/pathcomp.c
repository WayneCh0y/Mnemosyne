#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "pathcomp.h"

#ifdef _WIN32
#define PC_NATIVE_SEP '\\'
#else
#define PC_NATIVE_SEP '/'
#endif

static int is_sep(char c) {
#ifdef _WIN32
    return c == '\\' || c == '/';
#else
    return c == '/';
#endif
}

/* Case-insensitive compare, used for both matching and ordering so completion
   behaves the same on a case-insensitive filesystem (Windows) and a sensitive
   one (Linux): typing "prog" still finds "Program Files". */
static int ci_cmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int ci_starts_with(const char *s, const char *prefix, int prefix_len) {
    for (int i = 0; i < prefix_len; i++) {
        if (s[i] == '\0') return 0;
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) return 0;
    }
    return 1;
}

/* A link can be a URL as well as a path, and "https://…" is full of separators.
   Anything with a scheme, or a bare "www.", is not a path we can complete. */
static int looks_like_url(const char *s) {
    if (strstr(s, "://") != NULL) return 1;
    return strncmp(s, "www.", 4) == 0;
}

static const char *home_dir(void) {
#ifdef _WIN32
    const char *h = getenv("USERPROFILE");
#else
    const char *h = getenv("HOME");
#endif
    return (h && h[0]) ? h : NULL;
}

/* Inserts one child into pc->items, keeping the list sorted (directories first,
   then case-insensitively by name) and capped at PATHCOMP_MAX_ITEMS. Entries
   that sort past the cap are counted in pc->total but dropped. */
static void pathcomp_insert(PathComp *pc, const char *name, int is_dir) {
    pc->total++;

    int at = pc->count;
    for (int i = 0; i < pc->count; i++) {
        const PathCompItem *it = &pc->items[i];
        if (is_dir != it->is_dir) {
            if (is_dir) { at = i; break; }   /* directories sort above files */
            continue;
        }
        if (ci_cmp(name, it->name) < 0) { at = i; break; }
    }
    if (at >= PATHCOMP_MAX_ITEMS) return;   /* sorts past the cap — counted only */

    int end = pc->count < PATHCOMP_MAX_ITEMS ? pc->count : PATHCOMP_MAX_ITEMS - 1;
    for (int i = end; i > at; i--)
        pc->items[i] = pc->items[i - 1];

    strncpy(pc->items[at].name, name, PATHCOMP_NAME_MAX - 1);
    pc->items[at].name[PATHCOMP_NAME_MAX - 1] = '\0';
    pc->items[at].is_dir = is_dir;
    if (pc->count < PATHCOMP_MAX_ITEMS) pc->count++;
}

/* Lists `dir` (which ends in a separator) and feeds every child whose name
   starts with `frag` into pc. Hidden files stay hidden until the fragment
   itself starts with a dot, so a leading "." is how you ask for them. */
static void scan_dir(PathComp *pc, const char *dir, const char *frag, int frag_len) {
    int want_hidden = (frag_len > 0 && frag[0] == '.');

#ifdef _WIN32
    char pattern[PATHCOMP_PATH_MAX + 2];
    snprintf(pattern, sizeof(pattern), "%s*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        /* Dot-prefixed names carry no hidden attribute on Windows, but they are
           still clutter (~ is full of .aws, .cache, …), so treat them as hidden
           on both platforms. */
        if (!want_hidden && (fd.cFileName[0] == '.' ||
                             (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))) continue;
        if (!ci_starts_with(fd.cFileName, frag, frag_len)) continue;
        pathcomp_insert(pc, fd.cFileName,
                        (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (d == NULL) return;

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (!want_hidden && e->d_name[0] == '.') continue;
        if (!ci_starts_with(e->d_name, frag, frag_len)) continue;

        char full[PATHCOMP_PATH_MAX];
        snprintf(full, sizeof(full), "%s%s", dir, e->d_name);
        struct stat st;
        int is_dir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
        pathcomp_insert(pc, e->d_name, is_dir);
    }

    closedir(d);
#endif
}

int pathcomp_update(PathComp *pc, const char *buf) {
    memset(pc, 0, sizeof(*pc));
    pc->sep = PC_NATIVE_SEP;

    if (buf == NULL || buf[0] == '\0') return 0;
    if (looks_like_url(buf)) return 0;

    /* The final segment (everything after the last separator) is the prefix
       being completed; everything before it is the directory to list. With no
       separator there is no directory to scan — bare app names stay untouched. */
    int len  = (int)strlen(buf);
    int last = -1;
    for (int i = 0; i < len; i++)
        if (is_sep(buf[i])) last = i;
    if (last < 0) return 0;

    pc->sep        = buf[last];
    pc->frag_start = last + 1;
    pc->frag_len   = len - pc->frag_start;
    if (pc->frag_len >= PATHCOMP_NAME_MAX) return 0;   /* not a plausible name */

    int dir_len = last + 1;                            /* keep the separator */
    if (dir_len >= PATHCOMP_PATH_MAX) return 0;
    memcpy(pc->dir, buf, (size_t)dir_len);
    pc->dir[dir_len] = '\0';

    /* "~/…" is the one prefix a shell would have expanded for us. */
    char scan[PATHCOMP_PATH_MAX];
    if (pc->dir[0] == '~' && is_sep(pc->dir[1])) {
        const char *home = home_dir();
        if (home == NULL) return 0;
        if (snprintf(scan, sizeof(scan), "%s%s", home, pc->dir + 1) >= (int)sizeof(scan))
            return 0;
    } else {
        memcpy(scan, pc->dir, (size_t)dir_len + 1);
    }

    scan_dir(pc, scan, buf + pc->frag_start, pc->frag_len);
    return pc->count;
}

int pathcomp_apply(const PathComp *pc, int sel, char *buf, int *len, int *pos,
                   size_t size) {
    if (sel < 0 || sel >= pc->count) return 0;

    const PathCompItem *it = &pc->items[sel];
    size_t name_len = strlen(it->name);
    size_t need = (size_t)pc->frag_start + name_len + (it->is_dir ? 1u : 0u);
    if (need >= size) return 0;

    memcpy(buf + pc->frag_start, it->name, name_len);
    size_t end = (size_t)pc->frag_start + name_len;
    if (it->is_dir) buf[end++] = pc->sep;   /* trailing separator drills in */
    buf[end] = '\0';

    /* The whole tail was the fragment we just replaced, so the cursor belongs
       at the end of what is now the longest known-good path. */
    *len = (int)end;
    *pos = (int)end;
    return 1;
}
