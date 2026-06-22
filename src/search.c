#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "search.h"
#include "index.h"
#include "config.h"
#include "types.h"

static FileType file_type_from_string(const char *s) {
    if (strcmp(s, "txt") == 0) return FILE_TYPE_TXT;
    if (strcmp(s, "md")  == 0) return FILE_TYPE_MD;
    if (strcmp(s, "tex") == 0) return FILE_TYPE_TEX;
    if (strcmp(s, "pdf") == 0) return FILE_TYPE_PDF;
    return FILE_TYPE_UNKNOWN;
}

static int cmp(const void *a, const void *b) {
    const SearchResult *ra = a, *rb = b;
    if (rb->last_modified != ra->last_modified)
        return (rb->last_modified > ra->last_modified) ? 1 : -1;
    return rb->match_count - ra->match_count;
}

int is_word_match(const char *buf, const char *mp, const char *query) {
    int qlen      = (int)strlen(query);
    int before_ok = (mp == buf) || !isalnum((unsigned char)*(mp - 1));
    int after_ok  = !isalnum((unsigned char)*(mp + qlen));
    return before_ok && after_ok;
}

static int is_path_match(const char *path, const char *mp, int qlen) {
    int before_ok = (mp == path) || *(mp - 1) == '/';
    int after_ok  = *(mp + qlen) == '\0'
                 || *(mp + qlen) == '/'
                 || *(mp + qlen - 1) == '/';
    return before_ok && after_ok;
}

static void build_context_start(const char *content, char *context, int ctx_size) {
    int len  = (int)strlen(content);
    int show = len < ctx_size - 1 ? len : ctx_size - 4;
    if (show < 0) show = 0;
    memcpy(context, content, show);
    if (show < len) { memcpy(context + show, "...", 3); show += 3; }
    context[show] = '\0';
}

static const char *extract_path_line(const char *buf, char *path_out, int path_out_size) {
    path_out[0] = '\0';
    if (strncmp(buf, "[PATH]", 6) != 0) return buf;
    const char *end = strstr(buf + 6, "[/PATH]");
    if (end == NULL) return buf;
    int plen = (int)(end - (buf + 6));
    if (plen <= 0 || plen >= path_out_size) return buf;
    memcpy(path_out, buf + 6, plen);
    path_out[plen] = '\0';
    const char *after = end + 7;
    if (*after == '\n') after++;
    return after;
}

static void build_context_txt(const char *buf, const char *mp, const char *query, char *context, int ctx_size) {
    int   qlen    = (int)strlen(query);
    int   moff    = (int)(mp - buf);
    int   linelen = (int)strlen(buf);
    int   after   = linelen - moff - qlen;

    int pre_dots = (moff  > 0) ? 3 : 0;
    int suf_dots = (after > 0) ? 3 : 0;
    int avail    = ctx_size - 1 - qlen - pre_dots - suf_dots;
    if (avail < 0) avail = 0;

    int show_before = avail / 4;
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

static char *read_file_buf(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    char *buf = malloc(size + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    long nread = (long)fread(buf, 1, size, f);
    buf[nread] = '\0';
    fclose(f);
    long j = 0;
    for (long i = 0; i < nread; i++)
        if (buf[i] != '\r') buf[j++] = buf[i];
    buf[j] = '\0';
    return buf;
}

static int scan_file_txt(const char *path, const char *query, const char *raw_query, char *context, int ctx_size) {
    char *buf = read_file_buf(path);
    if (buf == NULL) return 0;

    char stored_path[4096] = {0};
    const char *content_start = extract_path_line(buf, stored_path, sizeof(stored_path));

    int match_count = 0;
    int qlen = (int)strlen(query);
    const char *p = content_start;
    while ((p = strstr(p, query)) != NULL) {
        if (is_word_match(content_start, p, query)) {
            match_count++;
            if (match_count == 1) build_context_txt(buf, p, query, context, ctx_size);
        }
        p += qlen;
    }

    if (stored_path[0] != '\0' && raw_query[0] != '\0') {
        int rqlen = (int)strlen(raw_query);
        const char *rp = stored_path;
        while ((rp = strstr(rp, raw_query)) != NULL) {
            if (is_path_match(stored_path, rp, rqlen)) {
                if (match_count == 0) build_context_start(content_start, context, ctx_size);
                match_count++;
                break;
            }
            rp += rqlen;
        }
    }

    free(buf);
    return match_count;
}

static int scan_file_md(const char *path, const char *query, const char *raw_query, char *context, int ctx_size) {
    char *buf = read_file_buf(path);
    if (buf == NULL) return 0;

    char stored_path[4096] = {0};
    const char *content_start = extract_path_line(buf, stored_path, sizeof(stored_path));

    int match_count = 0;
    int qlen = (int)strlen(query);
    const char *p = content_start;
    while ((p = strstr(p, query)) != NULL) {
        if (is_word_match(content_start, p, query)) {
            match_count++;
            if (match_count == 1) build_context_txt(buf, p, query, context, ctx_size);
        }
        p += qlen;
    }

    if (stored_path[0] != '\0' && raw_query[0] != '\0') {
        int rqlen = (int)strlen(raw_query);
        const char *rp = stored_path;
        while ((rp = strstr(rp, raw_query)) != NULL) {
            if (is_path_match(stored_path, rp, rqlen)) {
                if (match_count == 0) build_context_start(content_start, context, ctx_size);
                match_count++;
                break;
            }
            rp += rqlen;
        }
    }

    free(buf);
    return match_count;
}

static int scan_file_tex(const char *path, const char *query, char *context, int ctx_size) {
    (void)path; (void)query; (void)context; (void)ctx_size;
    return 0;
}

static int scan_file_pdf(const char *path, const char *query, char *context, int ctx_size) {
    (void)path; (void)query; (void)context; (void)ctx_size;
    return 0;
}

SearchResult *search(const char *query, const char *raw_query, int *count) {
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

        char context[256] = {0};
        int  match_count;
        switch (file_type_from_string(entries[i].file_type)) {
            case FILE_TYPE_MD:  match_count = scan_file_md(doc_path, query, raw_query, context, sizeof(context));  break;
            case FILE_TYPE_TEX: match_count = scan_file_tex(doc_path, query, context, sizeof(context));            break;
            case FILE_TYPE_PDF: match_count = scan_file_pdf(doc_path, query, context, sizeof(context));            break;
            default:            match_count = scan_file_txt(doc_path, query, raw_query, context, sizeof(context)); break;
        }

        if (match_count > 0) {
            strncpy(results[found].original_path, entries[i].original_path,
                    sizeof(results[found].original_path) - 1);
            results[found].original_path[sizeof(results[found].original_path) - 1] = '\0';
            strncpy(results[found].context, context, sizeof(results[found].context) - 1);
            results[found].context[sizeof(results[found].context) - 1] = '\0';
            strncpy(results[found].file_type, entries[i].file_type,
                    sizeof(results[found].file_type) - 1);
            results[found].file_type[sizeof(results[found].file_type) - 1] = '\0';
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
