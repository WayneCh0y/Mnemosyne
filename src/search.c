#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "search.h"
#include "index.h"
#include "init.h"

static int cmp(const void *a, const void *b) {
    const SearchResult *ra = a, *rb = b;
    if (rb->last_modified != ra->last_modified)
        return (rb->last_modified > ra->last_modified) ? 1 : -1;
    return rb->match_count - ra->match_count;
}

static void build_context(const char *line, const char *query, char *context, int ctx_size) {
    char *mp      = strstr(line, query);
    int   qlen    = (int)strlen(query);
    int   moff    = (int)(mp - line);
    int   linelen = (int)strlen(line);
    int   after   = linelen - moff - qlen;

    int pre_dots = (moff  > 0) ? 3 : 0;
    int suf_dots = (after > 0) ? 3 : 0;
    int avail    = ctx_size - 1 - qlen - pre_dots - suf_dots;
    if (avail < 0) avail = 0;

    int show_before = avail / 2;
    int show_after  = avail - show_before;
    if (show_before >= moff)  { show_before = moff;  pre_dots = 0; }
    if (show_after  >= after) { show_after  = after; suf_dots = 0; }

    int pos = 0;
    if (pre_dots) { memcpy(context, "...", 3); pos = 3; }
    memcpy(context + pos, mp - show_before, show_before); pos += show_before;
    memcpy(context + pos, mp, qlen);                      pos += qlen;
    memcpy(context + pos, mp + qlen, show_after);         pos += show_after;
    if (suf_dots) { memcpy(context + pos, "...", 3); pos += 3; }
    context[pos] = '\0';
}

static int scan_file(const char *path, const char *query, char *context, int ctx_size) {
    FILE *f = fopen(path, "r");
    if (f == NULL) return 0;

    int  match_count = 0;
    char line[1024];

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, query)) {
            match_count++;
            if (match_count == 1) {
                line[strcspn(line, "\n")] = '\0';
                build_context(line, query, context, ctx_size);
            }
        }
    }
    fclose(f);
    return match_count;
}

SearchResult *search(const char *query, int *count) {
    int total;
    IndexEntry *entries = index_get_entries(&total);
    if (entries == NULL) { *count = 0; return NULL; }

    SearchResult *results = malloc(total * sizeof(SearchResult));
    if (results == NULL) { free(entries); *count = 0; return NULL; }

    int found = 0;

    for (int i = 0; i < total; i++) {
        char doc_path[4096];
        snprintf(doc_path, sizeof(doc_path), "%s/index/docs/%s.txt",
                 get_data_path(), entries[i].hash);

        char context[48] = {0};
        int  match_count = scan_file(doc_path, query, context, sizeof(context));

        if (match_count > 0) {
            strncpy(results[found].original_path, entries[i].original_path,
                    sizeof(results[found].original_path) - 1);
            results[found].original_path[sizeof(results[found].original_path) - 1] = '\0';
            strncpy(results[found].context, context, sizeof(results[found].context) - 1);
            results[found].context[sizeof(results[found].context) - 1] = '\0';
            results[found].match_count   = match_count;
            results[found].last_modified = entries[i].last_modified;
            found++;
        }
    }

    free(entries);
    qsort(results, found, sizeof(SearchResult), cmp);
    *count = found;
    return results;
}
