#ifndef SEARCH_H
#define SEARCH_H

typedef struct {
    char original_path[4096];
    char context[64];
    char file_type[16];
    int  match_count;
    long last_modified;
} SearchResult;

SearchResult *search(const char *query, int *count);

#endif