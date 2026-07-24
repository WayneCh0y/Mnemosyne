#ifndef WSTREE_H
#define WSTREE_H

#include <stddef.h>
#include "workspace.h"

/* ── Workspace folder tree ──────────────────────────────────────────────────
   Pure path arithmetic over a FolderList plus the workspaces filed into it: no
   terminal, no I/O. The picker draws whatever wstree_build_rows() returns and
   calls the mutators; persisting is the caller's job (workspace_store_save).

   A folder is identified by its full '/'-separated path ("NUSY4S1/CS2030S"),
   with "" meaning the root. Every ancestor exists in the FolderList in its own
   right, so "does this folder exist" is a string compare and moving a subtree is
   a prefix rewrite. */

/* Writes path's parent to out ("" when path is a top-level folder). */
void wstree_parent(const char *path, char *out, size_t n);

/* Points at path's last component (the folder's own name). Never NULL. */
const char *wstree_leaf(const char *path);

/* Joins parent and name into out; a "" parent yields just name. */
void wstree_join(const char *parent, const char *name, char *out, size_t n);

/* 1 if name is usable as a single folder component: non-empty, no '/', no
   leading/trailing blanks, and not longer than the path buffer. */
int wstree_name_ok(const char *name);

/* 1 if a folder with exactly this path is registered. */
int wstree_exists(const FolderList *f, const char *path);

/* 1 if path sits directly inside parent (one level down, not deeper). */
int wstree_is_child_of(const char *path, const char *parent);

/* 1 if path is ancestor itself or anywhere beneath it — the guard that stops a
   folder being moved into its own subtree. */
int wstree_is_descendant(const char *path, const char *ancestor);

/* Direct folders + workspaces inside path. */
int wstree_count_children(const FolderList *f, const char *path,
                          const Workspace *ws, int ws_count);

/* Registers a new folder. 0=ok, -1=already exists, -2=out of memory. */
int wstree_add(FolderList *f, const char *path);

/* Removes the folder, reparenting its direct children (folders and workspaces)
   to its parent — a folder delete never destroys a workspace.
   0=ok, -1=a reparented child would collide with a name already in the parent
   (nothing is changed), -2=no such folder. */
int wstree_remove(FolderList *f, const char *path, Workspace *ws, int ws_count);

/* Renames the folder in place, rewriting its whole subtree's paths.
   0=ok, -1=a sibling already has that name, -2=no such folder. */
int wstree_rename(FolderList *f, const char *path, const char *newname,
                  Workspace *ws, int ws_count);

/* Moves the folder (and its subtree) into dest ("" = root).
   0=ok, -1=dest already holds that name, -2=no such folder,
   -3=dest is inside the folder being moved. */
int wstree_move(FolderList *f, const char *path, const char *dest,
                Workspace *ws, int ws_count);

/* ── Rows ───────────────────────────────────────────────────────────────────
   One level's contents, flattened for the picker: folders first, then
   workspaces, each in the stored array order (the browser's /reorder swaps the
   underlying entries, so that order is what shows). Rebuilt on every keystroke
   rather than cached, matching build_edit_rows() in picker.c. */

enum { WSROW_FOLDER, WSROW_WS };

typedef struct {
    int kind;   /* WSROW_FOLDER | WSROW_WS */
    int idx;    /* index into FolderList.paths, or into the Workspace array */
} WsRow;

/* Fills *rows with the direct children of cwd, growing the buffer as needed
   (rows and cap are in/out). Returns the row count. */
int wstree_build_rows(const FolderList *f, const Workspace *ws, int ws_count,
                      const char *cwd, WsRow **rows, int *cap);

#endif
