#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "reindex.h"
#include "index.h"
#include "ingest.h"
#include "remove.h"

void reindex_all(void) {
    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (entries == NULL || count == 0) {
        printf("No files indexed yet.\n");
        free(entries);
        return;
    }

    /* Iterate over our own snapshot — ingest_file and remove_entry_by_abs_path
       both rewrite manifest.json under us, but the snapshot is stable. */
    int reindexed = 0, removed = 0;
    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(entries[i].original_path, &st) != 0) {
            if (remove_entry_by_abs_path(entries[i].original_path) == 1)
                removed++;
        } else {
            ingest_file(entries[i].original_path);
            reindexed++;
        }
    }

    free(entries);

    printf("Reindexed %d file%s", reindexed, reindexed == 1 ? "" : "s");
    if (removed > 0)
        printf(" (%d missing file%s dropped)", removed, removed == 1 ? "" : "s");
    printf(".\n");
}
