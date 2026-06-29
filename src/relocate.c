#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#undef FILE_TYPE_UNKNOWN
#else
#include <dirent.h>
#endif

#include "relocate.h"
#include "index.h"
#include "ingest.h"
#include "remove.h"

static const char *basename_of(const char *path) {
    const char *slash  = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *sep    = slash > bslash ? slash : bslash;
    return sep ? sep + 1 : path;
}

static int names_equal(const char *a, const char *b) {
#ifdef _WIN32
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
#else
    return strcmp(a, b) == 0;
#endif
}

/* Recursively walks `dir` for files whose basename matches `target_basename`.
   On the first match, copies the absolute path into `out` (size `out_size`)
   and increments `*match_count`. Stops walking once `*match_count` reaches 2,
   since the caller only needs to distinguish 0 / 1 / >1 matches. */
static void find_by_basename(const char *dir, const char *target_basename,
                             char *out, int out_size, int *match_count, int depth) {
    if (depth > 16 || *match_count > 1) return;

#ifdef _WIN32
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char child_path[4096];
        snprintf(child_path, sizeof(child_path), "%s\\%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            find_by_basename(child_path, target_basename, out, out_size, match_count, depth + 1);
            if (*match_count > 1) break;
        } else if (names_equal(fd.cFileName, target_basename)) {
            if (*match_count == 0) {
                strncpy(out, child_path, out_size - 1);
                out[out_size - 1] = '\0';
            }
            (*match_count)++;
            if (*match_count > 1) break;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (d == NULL) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char child_path[4096];
        snprintf(child_path, sizeof(child_path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(child_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            find_by_basename(child_path, target_basename, out, out_size, match_count, depth + 1);
            if (*match_count > 1) break;
        } else if (S_ISREG(st.st_mode) && names_equal(entry->d_name, target_basename)) {
            if (*match_count == 0) {
                strncpy(out, child_path, out_size - 1);
                out[out_size - 1] = '\0';
            }
            (*match_count)++;
            if (*match_count > 1) break;
        }
    }

    closedir(d);
#endif
}

void relocate_scan_all(void) {
    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (entries == NULL || count == 0) {
        free(entries);
        return;
    }

    /* Iterate over our own snapshot — ingest_file and remove_entry_by_abs_path
       rewrite manifest.json under us. */
    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(entries[i].original_path, &st) == 0) continue;

        if (strcmp(entries[i].repository, "none") == 0) {
            remove_entry_by_abs_path(entries[i].original_path);
            continue;
        }

        /* Widen to the outermost git root, so files moved out of a nested
           repo into its parent (submodule case) are still discoverable.
           Defensive for entries written before find_git_root returned the
           outermost root. */
        char scan_root[4096];
        find_outermost_git_root(entries[i].repository, scan_root, sizeof(scan_root));
        if (strcmp(scan_root, "none") == 0) {
            strncpy(scan_root, entries[i].repository, sizeof(scan_root) - 1);
            scan_root[sizeof(scan_root) - 1] = '\0';
        }

        const char *target = basename_of(entries[i].original_path);
        char found_path[4096] = {0};
        int  matches = 0;
        find_by_basename(scan_root, target,
                         found_path, sizeof(found_path), &matches, 0);

        if (matches == 1) {
            ingest_file(found_path);
            remove_entry_by_abs_path(entries[i].original_path);
        } else {
            remove_entry_by_abs_path(entries[i].original_path);
        }
    }

    free(entries);
}
