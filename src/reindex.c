#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "reindex.h"
#include "index.h"
#include "ingest.h"
#include "remove.h"
#include "relocate.h"

void reindex_all(void) {
    int initial_count;
    IndexEntry *initial = index_get_entries(&initial_count);
    if (initial == NULL || initial_count == 0) {
        printf("No files indexed yet.\n");
        free(initial);
        return;
    }
    free(initial);

    /* Missing files: try to relocate within their repository; drop otherwise. */
    relocate_scan_all();

    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (entries == NULL) return;

    /* Iterate over our own snapshot — ingest_file rewrites manifest.json under
       us, but the snapshot is stable. */
    int reindexed = 0;
    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(entries[i].original_path, &st) == 0) {
            ingest_file(entries[i].original_path);
            reindexed++;
        }
    }

    int removed = initial_count - count;
    free(entries);

    printf("Reindexed %d file%s", reindexed, reindexed == 1 ? "" : "s");
    if (removed > 0)
        printf(" (%d missing file%s dropped)", removed, removed == 1 ? "" : "s");
    printf(".\n");
}
