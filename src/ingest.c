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

#include "ingest.h"
#include "config.h"
#include "sha256.h"
#include "index.h"
#include "inverted.h"
#include "parser/parser.h"
#include "parser/normalise.h"
#include "theme.h"

static FileType get_file_type(const char *path) {
    static const struct { const char *ext; FileType type; } map[] = {
        { ".txt", FILE_TYPE_TXT },
        { ".md",  FILE_TYPE_MD  },
        { ".tex", FILE_TYPE_TEX },
        { ".pdf", FILE_TYPE_PDF },
        { NULL,   FILE_TYPE_UNKNOWN }
    };

    const char *ext = strrchr(path, '.');
    if (ext == NULL) { return FILE_TYPE_UNKNOWN; }

    for (int i = 0; map[i].ext != NULL; i++) {
        if (strcmp(ext, map[i].ext) == 0) { return map[i].type; }
    }

    return FILE_TYPE_UNKNOWN;
}

static char* file_type_to_string(FileType filetype) {
    switch (filetype)
    {
    case FILE_TYPE_TXT:         return "txt";
    case FILE_TYPE_MD:          return "md";
    case FILE_TYPE_TEX:         return "tex";
    case FILE_TYPE_PDF:         return "pdf";
    default:                    return "unk";
    }
}

static int write_to_docs(const char *hash, const char *abs_path, const char *text) {
    char doc_path[4096];
    snprintf(doc_path, sizeof(doc_path), "%s/index/docs/%s.txt", get_data_path(), hash);

    FILE *f = fopen(doc_path, "wb");
    if (f == NULL) {
        ui_err("could not write to index: %s", doc_path);
        return 0;
    }

    char norm_path[4096];
    strncpy(norm_path, abs_path, sizeof(norm_path) - 1);
    norm_path[sizeof(norm_path) - 1] = '\0';
    for (int i = 0; norm_path[i]; i++)
        if (norm_path[i] == '\\') norm_path[i] = '/';
    fprintf(f, "[PATH]%s[/PATH]\n", norm_path);

    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return 1;
}

/* Inner ingest: caller owns load/save of `idx` and `manifest`. If `idx` is
   NULL the inverted index isn't updated. If `manifest` is non-NULL the entry
   is appended to the batched manifest; otherwise index_add is used (single
   load/save). Returns 1 on success, 0 on failure. */
static int ingest_file_impl(const char *path, InvertedIndex *idx,
                            IndexManifest *manifest) {
    char abs_path[4096];

    /* Step 1: Obtain absolute path */
#ifdef _WIN32
    if (_fullpath(abs_path, path, sizeof(abs_path)) == NULL) {
        ui_err("could not resolve path: %s", path);
        return 0;
    }
#else
    if (realpath(path, abs_path) == NULL) {
        ui_err("could not resolve path: %s", path);
        return 0;
    }
#endif

    /* Step 2: Check if file exists */
    struct stat st;
    if (stat(abs_path, &st) != 0) {
        ui_err("file not found: %s", abs_path);
        return 0;
    }

    /* Step 3: Check if file type is supported */
    FileType filetype = get_file_type(abs_path);
    if (filetype == FILE_TYPE_UNKNOWN) {
        ui_err("unsupported file type: %s", abs_path);
        return 0;
    }

    /* Step 4: Parse file */
    char *text = parse_file(abs_path, filetype);
    if (text == NULL) {
        ui_err("failed to parse file: %s", abs_path);
        return 0;
    }
    if (text[0] == '\0') {
        ui_err("file is empty: %s", abs_path);
        free(text);
        return 0;
    }

    /* Step 5: Replace smart punctuation with ASCII so search queries match */
    normalise_punctuation(text);

    /* Step 6: Hash the absolute path */
    char hash[65];
    sha256_string(abs_path, hash);

    /* Step 7: Write to docs/ */
    if (!write_to_docs(hash, abs_path, text)) {
        free(text);
        return 0;
    }

    /* Step 8: Update manifest */
    const char *file_type = file_type_to_string(filetype);
    if (manifest != NULL)
        index_manifest_add(manifest, abs_path, hash, (long)st.st_size, (long)st.st_mtime, file_type);
    else
        index_add(abs_path, hash, (long)st.st_size, (long)st.st_mtime, file_type);

    /* Step 9: Update the inverted index */
    if (idx != NULL) inverted_add_doc(idx, hash, text);

    free(text);
    return 1;
}

void ingest_file(const char *path) {
    InvertedIndex *idx = inverted_load();
    ingest_file_impl(path, idx, NULL);
    inverted_save(idx);
    inverted_free(idx);
}

/* Recursively ingests every supported file under `dir`; returns the count added. */
static int ingest_directory(const char *dir, int depth, InvertedIndex *idx,
                            IndexManifest *manifest) {
    if (depth > 8) return 0;
    int added = 0;
#ifdef _WIN32
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return added;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char child_path[4096];
        snprintf(child_path, sizeof(child_path), "%s\\%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            added += ingest_directory(child_path, depth + 1, idx, manifest);
        } else {
            if (get_file_type(child_path) != FILE_TYPE_UNKNOWN) {
                added += ingest_file_impl(child_path, idx, manifest);
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (d == NULL) return added;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char child_path[4096];
        snprintf(child_path, sizeof(child_path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(child_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            added += ingest_directory(child_path, depth + 1, idx, manifest);
        } else if (S_ISREG(st.st_mode)) {
            if (get_file_type(child_path) != FILE_TYPE_UNKNOWN) {
                added += ingest_file_impl(child_path, idx, manifest);
            }
        }
    }

    closedir(d);
#endif
    return added;
}

void ingest_path(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        ui_err("path not found: %s", path);
        return;
    }

    InvertedIndex *idx = inverted_load();

    if (S_ISDIR(st.st_mode)) {
        IndexManifest *m = index_manifest_begin();
        int added = ingest_directory(path, 0, idx, m);
        index_manifest_end(m);
        if (added > 0)
            ui_ok("Indexed %d file%s from '%s'.", added, added == 1 ? "" : "s", path);
        else
            ui_info("No supported files found in '%s'.", path);
    } else if (S_ISREG(st.st_mode)) {
        if (ingest_file_impl(path, idx, NULL))
            ui_ok("Added '%s' to the index.", path);
    } else {
        ui_err("unsupported path type: %s", path);
    }

    inverted_save(idx);
    inverted_free(idx);
}
