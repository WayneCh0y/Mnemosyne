#ifndef INDEX_H
#define INDEX_H

#include <stddef.h>

typedef struct {
    char original_path[4096];
    char hash[65];
    long last_modified;
    char repository[4096];
    char file_type[16];
} IndexEntry;

void index_add(
    const char* original_path,
    const char* hash,
    long size_bytes,
    long last_modified,
    const char* file_type
);

int index_remove(const char* original_path);

IndexEntry *index_get_entries(int *count);

/* Walks up from `start_dir` looking for `.git`. Returns the OUTERMOST git
   root found (keeps walking past inner repos so nested repos / submodules
   resolve to the outer project). Writes "none" if no ancestor has .git. */
void find_outermost_git_root(const char *start_dir, char *out, size_t out_size);

#endif