#ifndef SEARCH_H
#define SEARCH_H

typedef struct {
    char  original_path[4096];
    char  context[256];
    char  file_type[16];
    int   match_count;
    long  last_modified;
    int   match_start;  /* byte offset in context[] where query begins; -1 if path-only match */
    int   match_len;    /* byte length of the matched query */
    int   page;         /* 1-based page of the first match for PDFs; 0 for non-PDFs */
    float score;        /* BM25 score copied from InvertedMatch; 0 for path-only hits */
} SearchResult;

int is_word_match(const char *buf, const char *mp, const char *query);

/* Searches indexed docs for `query` (whole-word content match) and `raw_query`
   (path-segment match). Both matches honour `is_case_sensitive`: when 0, callers
   must pass `query` already lowercased; `raw_query` is lowercased internally. */
SearchResult *search(const char *query, const char *raw_query, int *count, int is_case_sensitive);

/* Returns the 1-based line number of the first word-boundary match of `query`
   in the file at `path`. When `is_case_sensitive` is 0, `query` must already be
   lowercased (each line is lowercased before matching). Returns 1 if not found
   or unreadable. */
int search_find_line(const char *path, const char *query, int is_case_sensitive);

#endif