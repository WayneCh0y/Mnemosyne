#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stddef.h>

#define WORKSPACE_NAME_MAX          128
#define WORKSPACE_APP_MAX           4096
#define WORKSPACE_TARGET_MAX        4096
#define WORKSPACE_ENTRIES_MAX       64

typedef struct {
    char app[WORKSPACE_APP_MAX];
    char (*targets)[WORKSPACE_TARGET_MAX];  /* heap-grown; NULL when target_cap == 0 */
    int  target_count;  /* 0 = launch app standalone with no args */
    int  target_cap;
    char layout[16];    /* screen-partition token ("" = none); see picker placement */
} WorkspaceEntry;

/* ── Growable fixed-width string lists ──────────────────────────────────────
   Backing for both WorkspaceEntry.targets and the pickers' AppLinks.items: a
   dynamically grown array of WORKSPACE_TARGET_MAX-wide rows. Keeping the row
   width fixed means `arr[k]`, strncpy/memcpy, and `char (*)[WORKSPACE_TARGET_MAX]`
   function parameters all keep working unchanged. */

/* Appends s to the list (doubling cap, starting at 4). Returns 0=ok, -1=oom. */
int  targetlist_push(char (**arr)[WORKSPACE_TARGET_MAX], int *cap, int *count, const char *s);
/* Removes index idx, shifting the tail down. */
void targetlist_remove(char (*arr)[WORKSPACE_TARGET_MAX], int *count, int idx);
/* Swaps entries at idx1 and idx2 in-place. */
void targetlist_swap(char (*arr)[WORKSPACE_TARGET_MAX], int idx1, int idx2);
/* Frees the backing store and resets *arr=NULL, *cap=*count=0. (free(NULL) safe.) */
void targetlist_free(char (**arr)[WORKSPACE_TARGET_MAX], int *cap, int *count);

typedef struct {
    char name[WORKSPACE_NAME_MAX];
    WorkspaceEntry entries[WORKSPACE_ENTRIES_MAX];
    int entry_count;
} Workspace;

/* Returns heap-allocated array of all workspaces; caller must workspace_free_all(). */
Workspace *workspace_load_all(int *count);

/* Frees every entry's targets, then the workspace array itself. */
void workspace_free_all(Workspace *ws, int count);

/* Serialises workspaces array to workspaces.json. Returns 0 on success. */
int workspace_save_all(Workspace *ws, int count);

/* Creates a new empty workspace. Returns 0=ok, -1=already exists, -2=error. */
int workspace_create(const char *name);

/* Adds an entry to an existing workspace with a single target (empty = standalone).
   Returns 0=ok, -1=not found, -2=full, -3=error. */
int workspace_add_entry(const char *name, const char *app, const char *target);

/* Adds an entry with multiple targets (one entry = one app window/instance).
   Returns 0=ok, -1=not found, -2=full, -3=error. */
int workspace_add_entry_with_targets(const char *name, const char *app,
                                     const char targets[][WORKSPACE_TARGET_MAX],
                                     int target_count);

/* Removes an entire workspace. Returns 0=ok, -1=not found, -2=error. */
int workspace_remove(const char *name);

#endif
