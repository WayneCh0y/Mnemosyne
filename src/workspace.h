#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stddef.h>

#define WORKSPACE_NAME_MAX          128
#define WORKSPACE_APP_MAX           4096
#define WORKSPACE_TARGET_MAX        4096
#define WORKSPACE_ENTRIES_MAX       64

/* A folder path ("NUSY4S1/CS2030S"; "" = root). Deliberately the same row width
   as a target so the folder registry can be a targetlist_* list unchanged — see
   the growable-list rationale below. */
#define WORKSPACE_FOLDER_MAX        WORKSPACE_TARGET_MAX
#define WORKSPACE_FOLDER_SEP        '/'

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
    char folder[WORKSPACE_FOLDER_MAX];  /* containing folder path; "" = root */
    WorkspaceEntry entries[WORKSPACE_ENTRIES_MAX];
    int entry_count;
} Workspace;

/* ── Folder registry ────────────────────────────────────────────────────────
   Folders are stored explicitly rather than inferred from the workspaces' folder
   paths: a folder is created before anything is moved into it, so an empty one
   has to survive a save. Every ancestor is listed in its own right ("NUSY4S1"
   and "NUSY4S1/CS2030S" both appear), which makes a lookup a plain string
   compare with no path reconstruction. Backed by targetlist_*. */
typedef struct {
    char (*paths)[WORKSPACE_FOLDER_MAX];
    int count;
    int cap;
} FolderList;

/* Loads just the folder registry. Returns 0=ok (empty list if the file is v1 or
   absent), -1 on error. Caller must workspace_free_folders(). */
int  workspace_load_folders(FolderList *out);
void workspace_free_folders(FolderList *f);

/* Workspaces plus the folders they live in — the whole document, as the pickers
   need it. Owns both halves; free with workspace_store_free(). */
typedef struct {
    Workspace *ws;
    int ws_count;
    FolderList folders;
} WorkspaceStore;

int  workspace_store_load(WorkspaceStore *st);
int  workspace_store_save(const WorkspaceStore *st);
void workspace_store_free(WorkspaceStore *st);

/* Returns heap-allocated array of all workspaces; caller must workspace_free_all(). */
Workspace *workspace_load_all(int *count);

/* Frees every entry's targets, then the workspace array itself. */
void workspace_free_all(Workspace *ws, int count);

/* Serialises workspaces array to workspaces.json, preserving the folder registry
   already on disk. Returns 0 on success. */
int workspace_save_all(Workspace *ws, int count);

/* As workspace_save_all, but writes the given folder registry too — the one call
   that can create or remove a folder. Returns 0 on success. */
int workspace_save_all_with_folders(Workspace *ws, int count, const FolderList *f);

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
