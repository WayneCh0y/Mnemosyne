#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "wstree.h"

/* ── Path arithmetic ───────────────────────────────────────────────────── */

void wstree_parent(const char *path, char *out, size_t n) {
    const char *slash = strrchr(path, WORKSPACE_FOLDER_SEP);
    if (slash == NULL) { out[0] = '\0'; return; }
    size_t len = (size_t)(slash - path);
    if (len >= n) len = n - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

const char *wstree_leaf(const char *path) {
    const char *slash = strrchr(path, WORKSPACE_FOLDER_SEP);
    return slash ? slash + 1 : path;
}

void wstree_join(const char *parent, const char *name, char *out, size_t n) {
    if (parent == NULL || parent[0] == '\0')
        snprintf(out, n, "%s", name);
    else
        snprintf(out, n, "%s%c%s", parent, WORKSPACE_FOLDER_SEP, name);
}

int wstree_name_ok(const char *name) {
    if (name == NULL || name[0] == '\0')                return 0;
    if (strchr(name, WORKSPACE_FOLDER_SEP) != NULL)     return 0;
    if (isspace((unsigned char)name[0]))                return 0;
    size_t len = strlen(name);
    if (isspace((unsigned char)name[len - 1]))          return 0;
    if (len >= WORKSPACE_FOLDER_MAX)                    return 0;
    return 1;
}

int wstree_exists(const FolderList *f, const char *path) {
    for (int i = 0; i < f->count; i++)
        if (strcmp(f->paths[i], path) == 0) return 1;
    return 0;
}

int wstree_is_descendant(const char *path, const char *ancestor) {
    if (ancestor[0] == '\0')             return 1;   /* everything is under root */
    if (strcmp(path, ancestor) == 0)     return 1;
    size_t len = strlen(ancestor);
    return strncmp(path, ancestor, len) == 0 && path[len] == WORKSPACE_FOLDER_SEP;
}

int wstree_is_child_of(const char *path, const char *parent) {
    if (path[0] == '\0') return 0;                    /* root is nobody's child */
    if (parent[0] == '\0')
        return strchr(path, WORKSPACE_FOLDER_SEP) == NULL;
    size_t len = strlen(parent);
    if (strncmp(path, parent, len) != 0 || path[len] != WORKSPACE_FOLDER_SEP)
        return 0;
    return strchr(path + len + 1, WORKSPACE_FOLDER_SEP) == NULL;
}

int wstree_count_children(const FolderList *f, const char *path,
                          const Workspace *ws, int ws_count) {
    int n = 0;
    for (int i = 0; i < f->count; i++)
        if (wstree_is_child_of(f->paths[i], path)) n++;
    for (int i = 0; i < ws_count; i++)
        if (strcmp(ws[i].folder, path) == 0) n++;
    return n;
}

/* Rewrites buf in place when it names oldp or something beneath it, swapping the
   oldp prefix for newp and keeping the remainder ("a/b/c" under "a" → "z/b/c"). */
static void retarget_one(char *buf, size_t size, const char *oldp, const char *newp) {
    if (!wstree_is_descendant(buf, oldp)) return;
    char tail[WORKSPACE_FOLDER_MAX];
    snprintf(tail, sizeof(tail), "%s", buf + strlen(oldp));   /* "" or "/b/c" */
    snprintf(buf, size, "%s%s", newp, tail);
}

/* Moves a whole subtree from oldp to newp: every folder path and every
   workspace's folder that lies at or beneath oldp is rewritten. */
static void wstree_retarget(FolderList *f, Workspace *ws, int ws_count,
                            const char *oldp, const char *newp) {
    if (oldp[0] == '\0') return;   /* the root cannot be moved */
    for (int i = 0; i < f->count; i++)
        retarget_one(f->paths[i], WORKSPACE_FOLDER_MAX, oldp, newp);
    for (int i = 0; i < ws_count; i++)
        retarget_one(ws[i].folder, WORKSPACE_FOLDER_MAX, oldp, newp);
}

static int folder_index(const FolderList *f, const char *path) {
    for (int i = 0; i < f->count; i++)
        if (strcmp(f->paths[i], path) == 0) return i;
    return -1;
}

/* ── Mutators ──────────────────────────────────────────────────────────── */

int wstree_add(FolderList *f, const char *path) {
    if (wstree_exists(f, path)) return -1;
    if (targetlist_push(&f->paths, &f->cap, &f->count, path) != 0) return -2;
    return 0;
}

int wstree_remove(FolderList *f, const char *path, Workspace *ws, int ws_count) {
    int idx = folder_index(f, path);
    if (idx < 0) return -2;

    char parent[WORKSPACE_FOLDER_MAX];
    wstree_parent(path, parent, sizeof(parent));

    /* Check every reparent before doing any of them, so a refusal leaves the
       tree exactly as it was. Workspaces can't collide — their names are
       globally unique — so only folder leaves need testing. */
    for (int i = 0; i < f->count; i++) {
        if (!wstree_is_child_of(f->paths[i], path)) continue;
        char dest[WORKSPACE_FOLDER_MAX];
        wstree_join(parent, wstree_leaf(f->paths[i]), dest, sizeof(dest));
        if (wstree_exists(f, dest)) return -1;
    }

    /* Lift each direct child folder (with its own subtree) up to the parent.
       Retargeting only rewrites strings in place, so indices stay valid, and a
       child that has just moved no longer matches is_child_of(path). */
    for (int i = 0; i < f->count; i++) {
        if (!wstree_is_child_of(f->paths[i], path)) continue;
        char from[WORKSPACE_FOLDER_MAX], dest[WORKSPACE_FOLDER_MAX];
        snprintf(from, sizeof(from), "%s", f->paths[i]);
        wstree_join(parent, wstree_leaf(from), dest, sizeof(dest));
        wstree_retarget(f, ws, ws_count, from, dest);
    }

    /* Workspaces sitting directly in the folder move up too. */
    for (int i = 0; i < ws_count; i++)
        if (strcmp(ws[i].folder, path) == 0)
            snprintf(ws[i].folder, WORKSPACE_FOLDER_MAX, "%s", parent);

    targetlist_remove(f->paths, &f->count, folder_index(f, path));
    return 0;
}

int wstree_rename(FolderList *f, const char *path, const char *newname,
                  Workspace *ws, int ws_count) {
    if (folder_index(f, path) < 0) return -2;

    char parent[WORKSPACE_FOLDER_MAX], dest[WORKSPACE_FOLDER_MAX];
    wstree_parent(path, parent, sizeof(parent));
    wstree_join(parent, newname, dest, sizeof(dest));

    if (strcmp(dest, path) == 0)  return 0;      /* renamed to what it already is */
    if (wstree_exists(f, dest))   return -1;

    wstree_retarget(f, ws, ws_count, path, dest);
    return 0;
}

int wstree_move(FolderList *f, const char *path, const char *dest,
                Workspace *ws, int ws_count) {
    if (folder_index(f, path) < 0)         return -2;
    if (wstree_is_descendant(dest, path))  return -3;   /* into itself */

    char newp[WORKSPACE_FOLDER_MAX];
    wstree_join(dest, wstree_leaf(path), newp, sizeof(newp));

    if (strcmp(newp, path) == 0)  return 0;      /* already there */
    if (wstree_exists(f, newp))   return -1;

    wstree_retarget(f, ws, ws_count, path, newp);
    return 0;
}

/* ── Rows ──────────────────────────────────────────────────────────────── */

/* Sort key for a row: its folder path or workspace name. */
static const char *row_key(const FolderList *f, const Workspace *ws, const WsRow *r) {
    return r->kind == WSROW_FOLDER ? wstree_leaf(f->paths[r->idx]) : ws[r->idx].name;
}

int wstree_build_rows(const FolderList *f, const Workspace *ws, int ws_count,
                      const char *cwd, WsRow **rows, int *cap) {
    int need = f->count + ws_count;
    if (need > *cap) {
        int ncap = *cap ? *cap : 16;
        while (ncap < need) ncap *= 2;
        WsRow *p = realloc(*rows, (size_t)ncap * sizeof(WsRow));
        if (p == NULL) return 0;
        *rows = p; *cap = ncap;
    }

    /* Folders first, then workspaces: containers above contents reads as a
       hierarchy, and it keeps the folders in one predictable block no matter how
       many workspaces sit alongside them. Each block is sorted by name — an
       insertion sort because a level holds a handful of rows, not thousands. */
    WsRow *rw = *rows;
    int n = 0;
    for (int i = 0; i < f->count; i++)
        if (wstree_is_child_of(f->paths[i], cwd)) {
            WsRow r = { WSROW_FOLDER, i };
            int j = n++;
            while (j > 0 && strcmp(row_key(f, ws, &rw[j - 1]), wstree_leaf(f->paths[i])) > 0) {
                rw[j] = rw[j - 1]; j--;
            }
            rw[j] = r;
        }

    int folder_rows = n;
    for (int i = 0; i < ws_count; i++)
        if (strcmp(ws[i].folder, cwd) == 0) {
            WsRow r = { WSROW_WS, i };
            int j = n++;
            while (j > folder_rows && strcmp(row_key(f, ws, &rw[j - 1]), ws[i].name) > 0) {
                rw[j] = rw[j - 1]; j--;
            }
            rw[j] = r;
        }

    return n;
}
