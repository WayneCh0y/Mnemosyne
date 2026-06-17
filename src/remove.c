#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "remove.h"
#include "config.h"
#include "sha256.h"
#include "index.h"

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

    /* Step 2: Compute hash of the absolute path */
    char hash[65];
    sha256_string(abs_path, hash);

    /* Step 3: Remove from index */
    int rc = index_remove(abs_path);
    if (rc == 1)      {         
        /* Step 4: Remove the corresponding file in docs/ */
        char doc_path[4096];
        snprintf(doc_path, sizeof(doc_path), "%s/index/docs/%s.txt", get_data_path(), hash);
        remove(doc_path);
    }
    else if (rc == 0) { 
        fprintf(stderr, "warning: %s was not indexed\n", abs_path); 
    }
    /* rc == -1: index_remove already printed the error */
}