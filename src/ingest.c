#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ingest.h"
#include "config.h"
#include "sha256.h"
#include "index.h"
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

void ingest_file(const char *path) {
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

    free(text);
}
