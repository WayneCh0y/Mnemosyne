#ifndef PATHCOMP_H
#define PATHCOMP_H

#include <stddef.h>

/* ── Filesystem path completion ─────────────────────────────────────────────
   Suggests the children of the directory currently being typed, so a workspace
   app path or target can be picked instead of typed out in full.

   The engine is stateless between calls: pathcomp_update() re-scans on every
   keystroke and rewrites the PathComp, which the picker then renders as a
   dropdown under the field. pathcomp_apply() writes the chosen suggestion back
   into the caller's edit buffer. */

#define PATHCOMP_MAX_ITEMS 32
#define PATHCOMP_NAME_MAX  260
#define PATHCOMP_PATH_MAX  4096

typedef struct {
    char name[PATHCOMP_NAME_MAX];   /* the child's basename, no separator */
    int  is_dir;
} PathCompItem;

typedef struct {
    PathCompItem items[PATHCOMP_MAX_ITEMS];
    int  count;       /* suggestions held (capped at PATHCOMP_MAX_ITEMS) */
    int  total;       /* suggestions that matched — may exceed count */
    int  frag_start;  /* offset in the buffer where the final path segment starts */
    int  frag_len;    /* length of that segment (the prefix already typed) */
    char sep;         /* separator the user is typing with ('\\' or '/') */
    char dir[PATHCOMP_PATH_MAX];   /* directory being listed, as typed */
} PathComp;

/* Re-scans the directory the buffer is pointing into and fills pc with the
   children whose names start with the final segment. Yields nothing (count = 0)
   unless the buffer holds a separator and doesn't look like a URL, so bare app
   names ("code") and links ("https://…") are left alone. Returns pc->count. */
int pathcomp_update(PathComp *pc, const char *buf);

/* Replaces the buffer's final segment with suggestion `sel`, appending the
   separator for a directory so the next update lists its children. buf/len/pos
   are the caller's edit buffer of `size` bytes; the cursor lands at the end.
   Returns 1 if the buffer changed, 0 otherwise. */
int pathcomp_apply(const PathComp *pc, int sel, char *buf, int *len, int *pos,
                   size_t size);

#endif
