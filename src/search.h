#ifndef SEARCH_H
#define SEARCH_H

typedef struct {
    char original_path[4096];
    char context[256];
    char file_type[16];
    int  match_count;
    long last_modified;
} SearchResult;

int is_word_match(const char *buf, const char *mp, const char *query);

SearchResult *search(const char *query, const char *raw_query, int *count);

#endif