#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inverted.h"
#include "theme.h"
#include "config.h"
#include "index.h"

#define MAX_TOKEN_LEN 128

typedef void (*token_callback)(const char *word, uint32_t position, void *userdata);

/* Walks `text`, emitting each alphanumeric run as a lowercased, null-terminated
   token along with its 0-based position in the stream. Runs longer than
   MAX_TOKEN_LEN are truncated rather than split. */
static void tokenize(const char *text, token_callback cb, void *userdata) {
    char buf[MAX_TOKEN_LEN + 1];
    uint32_t position = 0;

    while (*text) {
        while (*text && !isalnum((unsigned char)*text)) text++;
        if (!*text) break;

        size_t len = 0;
        while (*text && isalnum((unsigned char)*text)) {
            if (len < MAX_TOKEN_LEN) {
                buf[len++] = (char)tolower((unsigned char)*text);
            }
            text++;
        }
        buf[len] = '\0';

        cb(buf, position++, userdata);
    }
}

#define BUCKET_COUNT 4096   /* fixed for now; can grow to a resizing map later */

typedef struct {
    uint32_t doc_id;
    uint32_t position;
} Hit;

typedef struct WordEntry {
    char    *word;          /* owned */
    Hit     *hits;          /* owned, dynamic */
    uint32_t hit_count;
    uint32_t hit_cap;
    struct WordEntry *next; /* chain for hash-bucket collisions */
} WordEntry;

struct InvertedIndex {
    WordEntry *buckets[BUCKET_COUNT];
    uint32_t   word_count;

    /* Doc table: doc_id is the index into this array. */
    char     (*doc_hashes)[65];
    uint32_t   doc_count;
    uint32_t   doc_cap;
};

static uint32_t hash_word(const char *word) {
    /* djb2 hash */
    uint32_t h = 5381;
    for (const unsigned char *p = (const unsigned char *)word; *p; p++)
        h = ((h << 5) + h) + *p;
    return h;
}

static uint32_t intern_doc(InvertedIndex *idx, const char *hash) {
    for (uint32_t i = 0; i < idx->doc_count; i++) {
        if (strcmp(idx->doc_hashes[i], hash) == 0) return i;
    }
    if (idx->doc_count == idx->doc_cap) {
        uint32_t new_cap = idx->doc_cap ? idx->doc_cap * 2 : 16;
        char (*resized)[65] = realloc(idx->doc_hashes, new_cap * 65);
        if (resized == NULL) return idx->doc_count; /* allocation failure: best-effort */
        idx->doc_hashes = resized;
        idx->doc_cap = new_cap;
    }
    strncpy(idx->doc_hashes[idx->doc_count], hash, 64);
    idx->doc_hashes[idx->doc_count][64] = '\0';
    return idx->doc_count++;
}

static WordEntry *find_or_create_word(InvertedIndex *idx, const char *word) {
    uint32_t b = hash_word(word) % BUCKET_COUNT;
    for (WordEntry *e = idx->buckets[b]; e != NULL; e = e->next) {
        if (strcmp(e->word, word) == 0) return e;
    }
    WordEntry *e = calloc(1, sizeof(WordEntry));
    if (e == NULL) return NULL;
    e->word = strdup(word);
    e->next = idx->buckets[b];
    idx->buckets[b] = e;
    idx->word_count++;
    return e;
}

static void word_add_hit(WordEntry *e, uint32_t doc_id, uint32_t position) {
    if (e->hit_count == e->hit_cap) {
        uint32_t new_cap = e->hit_cap ? e->hit_cap * 2 : 4;
        Hit *resized = realloc(e->hits, new_cap * sizeof(Hit));
        if (resized == NULL) return;
        e->hits = resized;
        e->hit_cap = new_cap;
    }
    e->hits[e->hit_count].doc_id   = doc_id;
    e->hits[e->hit_count].position = position;
    e->hit_count++;
}

typedef struct {
    InvertedIndex *idx;
    uint32_t       doc_id;
} AddCtx;

static void add_token_cb(const char *word, uint32_t position, void *userdata) {
    AddCtx *ctx = (AddCtx *)userdata;
    WordEntry *e = find_or_create_word(ctx->idx, word);
    if (e == NULL) return;
    word_add_hit(e, ctx->doc_id, position);
}

InvertedIndex *inverted_load(void) {
    InvertedIndex *idx = calloc(1, sizeof(InvertedIndex));
    if (idx == NULL) return NULL;

    char path[4096];
    snprintf(path, sizeof(path), "%s/index/inverted.bin", get_data_path());

    FILE *f = fopen(path, "rb");
    if (f == NULL) return idx;  /* no file yet — first-ever ingest */

    char     magic[4];
    uint32_t version, doc_count, word_count;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "MNIV", 4) != 0
        || fread(&version,    sizeof(uint32_t), 1, f) != 1 || version != 1
        || fread(&doc_count,  sizeof(uint32_t), 1, f) != 1
        || fread(&word_count, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        ui_warn("inverted.bin is corrupt or unrecognized; starting empty");
        return idx;
    }

    if (doc_count > 0) {
        idx->doc_hashes = malloc(doc_count * 65);
        if (idx->doc_hashes == NULL) { fclose(f); return idx; }
        idx->doc_cap = doc_count;
        for (uint32_t i = 0; i < doc_count; i++) {
            uint32_t doc_id;
            if (fread(&doc_id, sizeof(uint32_t), 1, f) != 1
                || fread(idx->doc_hashes[i], 1, 65, f) != 65) {
                fclose(f);
                return idx;
            }
        }
        idx->doc_count = doc_count;
    }

    char word_buf[MAX_TOKEN_LEN + 1];
    for (uint32_t i = 0; i < word_count; i++) {
        uint16_t word_len;
        uint32_t hit_count;
        if (fread(&word_len, sizeof(uint16_t), 1, f) != 1) break;
        if (word_len > MAX_TOKEN_LEN) break;
        if (fread(word_buf, 1, word_len, f) != word_len) break;
        word_buf[word_len] = '\0';
        if (fread(&hit_count, sizeof(uint32_t), 1, f) != 1) break;

        WordEntry *e = find_or_create_word(idx, word_buf);
        if (e == NULL) break;
        if (hit_count > 0) {
            e->hits = malloc(hit_count * sizeof(Hit));
            if (e->hits == NULL) break;
            e->hit_cap = hit_count;
            if (fread(e->hits, sizeof(Hit), hit_count, f) != hit_count) break;
            e->hit_count = hit_count;
        }
    }

    fclose(f);
    return idx;
}

void inverted_add_doc(InvertedIndex *idx, const char *hash, const char *text) {
    if (idx == NULL || hash == NULL || text == NULL) return;
    AddCtx ctx = { idx, intern_doc(idx, hash) };
    tokenize(text, add_token_cb, &ctx);
}

void inverted_save(InvertedIndex *idx) {
    if (idx == NULL) return;

    char path[4096];
    snprintf(path, sizeof(path), "%s/index/inverted.bin", get_data_path());

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ui_err("could not write inverted index: %s", path);
        return;
    }

    const char magic[4]   = { 'M', 'N', 'I', 'V' };
    const uint32_t version = 1;
    fwrite(magic,             1, 4, f);
    fwrite(&version,          sizeof(uint32_t), 1, f);
    fwrite(&idx->doc_count,   sizeof(uint32_t), 1, f);
    fwrite(&idx->word_count,  sizeof(uint32_t), 1, f);

    for (uint32_t i = 0; i < idx->doc_count; i++) {
        uint32_t doc_id = i;
        fwrite(&doc_id,            sizeof(uint32_t), 1, f);
        fwrite(idx->doc_hashes[i], 1,                65, f);
    }

    for (uint32_t b = 0; b < BUCKET_COUNT; b++) {
        for (WordEntry *e = idx->buckets[b]; e != NULL; e = e->next) {
            uint16_t word_len = (uint16_t)strlen(e->word);
            fwrite(&word_len,     sizeof(uint16_t),  1,             f);
            fwrite(e->word,       1,                 word_len,      f);
            fwrite(&e->hit_count, sizeof(uint32_t),  1,             f);
            fwrite(e->hits,       sizeof(Hit),       e->hit_count,  f);
        }
    }

    fclose(f);
}

void inverted_free(InvertedIndex *idx) {
    if (idx == NULL) return;
    for (uint32_t b = 0; b < BUCKET_COUNT; b++) {
        WordEntry *e = idx->buckets[b];
        while (e != NULL) {
            WordEntry *next = e->next;
            free(e->word);
            free(e->hits);
            free(e);
            e = next;
        }
    }
    free(idx->doc_hashes);
    free(idx);
}

int inverted_exists(void) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/index/inverted.bin", get_data_path());
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

uint32_t inverted_doc_count(const InvertedIndex *idx) {
    return idx ? idx->doc_count : 0;
}

/* Read the entire stored doc (docs/<hash>.txt) into a heap buffer. The doc
   already starts with the [PATH]…[/PATH] header that ingest writes — we
   tokenize that too, which is harmless (the path bytes become words in the
   index, and the inverted lookup will find them just like content words). */
static char *read_doc_text(const char *hash) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/index/docs/%s.txt", get_data_path(), hash);
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

void inverted_rebuild(void) {
    InvertedIndex *idx = calloc(1, sizeof(InvertedIndex));
    if (idx == NULL) return;

    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (entries != NULL) {
        for (int i = 0; i < count; i++) {
            char *text = read_doc_text(entries[i].hash);
            if (text == NULL) continue;
            inverted_add_doc(idx, entries[i].hash, text);
            free(text);
        }
        free(entries);
    }

    inverted_save(idx);
    inverted_free(idx);
}

/* Look up the WordEntry for an already-lowercased word. Returns NULL if
   absent. Read-only counterpart of find_or_create_word. */
static WordEntry *find_word(InvertedIndex *idx, const char *word) {
    uint32_t b = hash_word(word) % BUCKET_COUNT;
    for (WordEntry *e = idx->buckets[b]; e != NULL; e = e->next) {
        if (strcmp(e->word, word) == 0) return e;
    }
    return NULL;
}

/* Context for collecting query tokens during the tokenize() callback. */
typedef struct {
    char     tokens[16][MAX_TOKEN_LEN + 1];
    int      count;
} QueryTokens;

static void collect_token_cb(const char *word, uint32_t position, void *userdata) {
    (void)position;
    QueryTokens *qt = (QueryTokens *)userdata;
    if (qt->count >= 16) return;  /* cap query length at 16 tokens */
    strncpy(qt->tokens[qt->count], word, MAX_TOKEN_LEN);
    qt->tokens[qt->count][MAX_TOKEN_LEN] = '\0';
    qt->count++;
}

/* Does `e` have a hit at (doc_id, position)? Linear scan as postings lists
   are short per doc. */
static int has_hit_at(const WordEntry *e, uint32_t doc_id, uint32_t position) {
    for (uint32_t i = 0; i < e->hit_count; i++) {
        if (e->hits[i].doc_id == doc_id && e->hits[i].position == position) return 1;
    }
    return 0;
}

InvertedMatch *inverted_query(InvertedIndex *idx, const char *query, int *out_count) {
    *out_count = 0;
    if (idx == NULL || query == NULL) return NULL;

    QueryTokens qt = {0};
    tokenize(query, collect_token_cb, &qt);
    if (qt.count == 0) return NULL;

    /* Look up every token. If any is missing, no doc can match. */
    WordEntry *entries[16];
    for (int i = 0; i < qt.count; i++) {
        entries[i] = find_word(idx, qt.tokens[i]);
        if (entries[i] == NULL) return NULL;
    }

    /* Walk the first token's hits doc-by-doc. For each starting position,
       verify token[k] also has a hit at (doc_id, pos+k). If yes, it's a
       phrase occurrence. */
    InvertedMatch *results = NULL;
    int results_cap = 0;

    WordEntry *first = entries[0];
    for (uint32_t i = 0; i < first->hit_count; i++) {
        uint32_t doc_id = first->hits[i].doc_id;
        uint32_t pos    = first->hits[i].position;

        int phrase_ok = 1;
        for (int k = 1; k < qt.count; k++) {
            if (!has_hit_at(entries[k], doc_id, pos + (uint32_t)k)) {
                phrase_ok = 0;
                break;
            }
        }
        if (!phrase_ok) continue;

        /* Find existing result for this doc, or append a new one. */
        int found = -1;
        for (int j = 0; j < *out_count; j++) {
            if (strcmp(results[j].hash, idx->doc_hashes[doc_id]) == 0) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            results[found].match_count++;
        } else {
            if (*out_count == results_cap) {
                int new_cap = results_cap ? results_cap * 2 : 8;
                InvertedMatch *resized = realloc(results, (size_t)new_cap * sizeof(InvertedMatch));
                if (resized == NULL) break;
                results = resized;
                results_cap = new_cap;
            }
            strncpy(results[*out_count].hash, idx->doc_hashes[doc_id], 64);
            results[*out_count].hash[64] = '\0';
            results[*out_count].match_count = 1;
            (*out_count)++;
        }
    }

    return results;
}
