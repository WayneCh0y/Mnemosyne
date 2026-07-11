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

/* Batched manifest mutation. Use this from callers that touch many files in a
   row (e.g. directory ingest) to avoid re-parsing/rewriting manifest.json on
   every entry. Sequence: begin -> add (N times) -> end. */
typedef struct IndexManifest IndexManifest;

IndexManifest *index_manifest_begin(void);
void           index_manifest_add(IndexManifest *m,
                                  const char *original_path,
                                  const char *hash,
                                  long size_bytes,
                                  long last_modified,
                                  const char *file_type);
void           index_manifest_end(IndexManifest *m);

int index_remove(const char* original_path);

IndexEntry *index_get_entries(int *count);

/* Walks up from `start_dir` looking for `.git`. Returns the OUTERMOST git
   root found (keeps walking past inner repos so nested repos / submodules
   resolve to the outer project). Writes "none" if no ancestor has .git. */
void find_outermost_git_root(const char *start_dir, char *out, size_t out_size);

#endif