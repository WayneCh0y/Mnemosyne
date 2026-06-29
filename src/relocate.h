#ifndef RELOCATE_H
#define RELOCATE_H

/* For each indexed entry whose original_path no longer exists on disk,
   scan its recorded repository for a file with the same basename and
   extension. If exactly one candidate is found, re-ingest at the new
   path and drop the stale entry. Otherwise (zero, ambiguous, or no
   repository recorded), drop the entry from the index. */
void relocate_scan_all(void);

#endif
