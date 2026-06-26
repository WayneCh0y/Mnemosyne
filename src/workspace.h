#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stddef.h>

#define WORKSPACE_NAME_MAX          128
#define WORKSPACE_APP_MAX           4096
#define WORKSPACE_TARGET_MAX        4096
#define WORKSPACE_ENTRIES_MAX       64
#define WORKSPACE_ENTRY_TARGETS_MAX 8

typedef struct {
    char app[WORKSPACE_APP_MAX];
    char targets[WORKSPACE_ENTRY_TARGETS_MAX][WORKSPACE_TARGET_MAX];
    int  target_count;  /* 0 = launch app standalone with no args */
} WorkspaceEntry;

typedef struct {
    char name[WORKSPACE_NAME_MAX];
    WorkspaceEntry entries[WORKSPACE_ENTRIES_MAX];
    int entry_count;
} Workspace;

/* Returns heap-allocated array of all workspaces; caller must free(). */
Workspace *workspace_load_all(int *count);

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
