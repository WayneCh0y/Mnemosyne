#ifndef INDEX_H
#define INDEX_H

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

IndexEntry *index_get_entries(int *count);

#endif