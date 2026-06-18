#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "remove.h"
#include "config.h"
#include "sha256.h"
#include "index.h"

int remove_entry_by_abs_path(const char *abs_path) {
    char hash[65];
    sha256_string(abs_path, hash);

    int rc = index_remove(abs_path);
    if (rc == 1) {
        char doc_path[4096];
        snprintf(doc_path, sizeof(doc_path), "%s/index/docs/%s.txt", get_data_path(), hash);
        remove(doc_path);
    }
    return rc;
}

void remove_file(const char *path) {
    char abs_path[4096];

    /* Step 1: Obtain absolute path */
#ifdef _WIN32
    if (_fullpath(abs_path, path, sizeof(abs_path)) == NULL) {
        fprintf(stderr, "error: could not resolve absolute path for: %s\n", path);
        return;
    }
#else
    if (realpath(path, abs_path) == NULL) {
        fprintf(stderr, "error: could not resolve absolute path for: %s\n", path);
        return;
    }
#endif

    /* Steps 2-4: Hash, remove from index, remove doc */
    int rc = remove_entry_by_abs_path(abs_path);
    if (rc == 0) {
        fprintf(stderr, "warning: %s was not indexed\n", abs_path);
    }
    /* rc == -1: index_remove already printed the error */
}