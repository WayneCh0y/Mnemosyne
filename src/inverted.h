#ifndef INVERTED_H
#define INVERTED_H

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

#endif
