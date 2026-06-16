#ifndef INDEX_H
#define INDEX_H

typedef struct {
    char original_path[4096];
    long last_modified;
} IndexEntry;

void index_add(
    const char* original_path,
    const char* hash,
    long size_bytes,
    long last_modified,
    const char* file_type
);

IndexEntry *index_get_entries(int *count);

#endif