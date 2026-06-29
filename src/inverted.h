#ifndef INVERTED_H
#define INVERTED_H

#include <stdint.h>

typedef struct InvertedIndex InvertedIndex;

/* Load inverted.bin from disk into memory. If the file does not exist,
   returns an empty index ready to be added to. Caller owns; free with
   inverted_free. */
InvertedIndex *inverted_load(void);

/* Record every word in `text` against `hash` (the sha256 of the doc's path,
   matching the manifest entry). Safe to call repeatedly across docs. */
void inverted_add_doc(InvertedIndex *idx, const char *hash, const char *text);

/* Write the in-memory index back to inverted.bin, overwriting whatever
   was there. */
void inverted_save(InvertedIndex *idx);

void inverted_free(InvertedIndex *idx);

/* Returns 1 if inverted.bin exists on disk, 0 otherwise. */
int inverted_exists(void);

/* Number of docs currently indexed in `idx`. Used by callers (e.g. search)
   to detect a corrupt-but-present inverted.bin: if doc_count is 0 yet the
   manifest has entries, the on-disk file is unusable and should be rebuilt. */
uint32_t inverted_doc_count(const InvertedIndex *idx);

/* Rebuilds inverted.bin from scratch by walking every entry in manifest.json
   and re-tokenizing its stored doc. Used by `mn remove`, `mn reindex`, and
   on the next `mn search` when inverted.bin is missing. */
void inverted_rebuild(void);

/* One match returned by inverted_query. */
typedef struct {
    char     hash[65];
    uint32_t match_count;  /* number of times the query (or phrase) hits this doc */
} InvertedMatch;

/* Looks up the query in `idx`. For a single-word query, returns docs whose
   text contains that word. For a multi-word query, returns docs where the
   words appear consecutively (phrase match). `query` should already be
   lowercased by the caller. Sets *out_count and returns a malloc'd array
   (or NULL when no matches). Free with free(). */
InvertedMatch *inverted_query(InvertedIndex *idx, const char *query, int *out_count);

#endif
