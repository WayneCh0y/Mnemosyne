#ifndef SEARCH_H
#define SEARCH_H

typedef struct {
    char original_path[4096];
    char context[256];
    char file_type[16];
    int  match_count;
    long last_modified;
    int  match_start;  /* byte offset in context[] where query begins; -1 if path-only match */
    int  match_len;    /* byte length of the matched query */
} SearchResult;

int is_word_match(const char *buf, const char *mp, const char *query);

SearchResult *search(const char *query, const char *raw_query, int *count, int is_case_sensitive);

/* Returns the 1-based line number of the first word-boundary match of
   query (lowercase) in the file at path. Returns 1 if not found or unreadable. */
int search_find_line(const char *path, const char *query, int is_case_sensitive);

#endif