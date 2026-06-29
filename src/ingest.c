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
        fprintf(stderr, "error: could not write to index: %s\n", doc_path);
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

/* Inner ingest: caller owns load/save of `idx`. If `idx` is NULL the inverted
   index isn't updated (used by callers that don't want to touch it here). */
static void ingest_file_impl(const char *path, InvertedIndex *idx) {
    char abs_path[4096];

    /* Step 1: Obtain absolute path */
#ifdef _WIN32
    if (_fullpath(abs_path, path, sizeof(abs_path)) == NULL) {
        fprintf(stderr, "error: could not resolve path: %s\n", path);
        return;
    }
#else
    if (realpath(path, abs_path) == NULL) {
        fprintf(stderr, "error: could not resolve path: %s\n", path);
        return;
    }
#endif

    /* Step 2: Check if file exists */
    struct stat st;
    if (stat(abs_path, &st) != 0) {
        fprintf(stderr, "error: file not found: %s\n", abs_path);
        return;
    }

    /* Step 3: Check if file type is supported */
    FileType filetype = get_file_type(abs_path);
    if (filetype == FILE_TYPE_UNKNOWN) {
        fprintf(stderr, "error: unsupported file type");
        return;
    }

    /* Step 4: Parse file */
    char *text = parse_file(abs_path, filetype);
    if (text == NULL) {
        fprintf(stderr, "error: failed to parse file: %s\n", abs_path);
        return;
    }
    if (text[0] == '\0') {
        fprintf(stderr, "error: file is empty: %s\n", abs_path);
        free(text);
        return;
    }

    /* Step 5: Hash the absolute path */
    char hash[65];
    sha256_string(abs_path, hash);

    /* Step 6: Write to docs/ */
    if (!write_to_docs(hash, abs_path, text)) {
        free(text);
        return;
    }

    /* Step 7: Update manifest */
    const char *file_type = file_type_to_string(filetype);
    index_add(abs_path, hash, (long)st.st_size, (long)st.st_mtime, file_type);

    /* Step 8: Update the inverted index */
    if (idx != NULL) inverted_add_doc(idx, hash, text);

    free(text);
}

void ingest_file(const char *path) {
    InvertedIndex *idx = inverted_load();
    ingest_file_impl(path, idx);
    inverted_save(idx);
    inverted_free(idx);
}

static void ingest_directory(const char *dir, int depth, InvertedIndex *idx) {
    if (depth > 8) return;
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
            ingest_directory(child_path, depth + 1, idx);
        } else {
            if (get_file_type(child_path) != FILE_TYPE_UNKNOWN) {
                ingest_file_impl(child_path, idx);
            }
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
            ingest_directory(child_path, depth + 1, idx);
        } else if (S_ISREG(st.st_mode)) {
            if (get_file_type(child_path) != FILE_TYPE_UNKNOWN) {
                ingest_file_impl(child_path, idx);
            }
        }
    }

    closedir(d);
#endif
}

void ingest_path(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "error: path not found: %s\n", path);
        return;
    }

    InvertedIndex *idx = inverted_load();

    if (S_ISDIR(st.st_mode)) {
        ingest_directory(path, 0, idx);
    } else if (S_ISREG(st.st_mode)) {
        ingest_file_impl(path, idx);
    } else {
        fprintf(stderr, "error: unsupported path type: %s\n", path);
    }

    inverted_save(idx);
    inverted_free(idx);
}
