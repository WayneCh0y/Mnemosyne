#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "workspace.h"
#include "theme.h"
#include "config.h"
#include "cJSON.h"

static void workspace_path(char *out, size_t n) {
    snprintf(out, n, "%s/workspaces.json", get_data_path());
}

/* ── Growable fixed-width string lists ─────────────────────────────────── */

int targetlist_push(char (**arr)[WORKSPACE_TARGET_MAX], int *cap, int *count,
                    const char *s) {
    if (*count >= *cap) {
        int ncap = *cap ? *cap * 2 : 4;
        char (*p)[WORKSPACE_TARGET_MAX] = realloc(*arr, (size_t)ncap * sizeof(*p));
        if (p == NULL) return -1;
        *arr = p;
        *cap = ncap;
    }
    strncpy((*arr)[*count], s, WORKSPACE_TARGET_MAX - 1);
    (*arr)[*count][WORKSPACE_TARGET_MAX - 1] = '\0';
    (*count)++;
    return 0;
}

void targetlist_remove(char (*arr)[WORKSPACE_TARGET_MAX], int *count, int idx) {
    if (idx < 0 || idx >= *count) return;
    for (int j = idx; j < *count - 1; j++)
        memcpy(arr[j], arr[j + 1], WORKSPACE_TARGET_MAX);
    (*count)--;
}

void targetlist_swap(char (*arr)[WORKSPACE_TARGET_MAX], int idx1, int idx2) {
    char tmp[WORKSPACE_TARGET_MAX];
    memcpy(tmp,        arr[idx1], WORKSPACE_TARGET_MAX);
    memcpy(arr[idx1],  arr[idx2], WORKSPACE_TARGET_MAX);
    memcpy(arr[idx2],  tmp,       WORKSPACE_TARGET_MAX);
}

void targetlist_free(char (**arr)[WORKSPACE_TARGET_MAX], int *cap, int *count) {
    free(*arr);
    *arr   = NULL;
    *cap   = 0;
    *count = 0;
}

static cJSON *load_ws_json(void) {
    char path[4096];
    workspace_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ui_err("could not open workspaces file: %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (root == NULL) {
        ui_err("workspaces.json is malformed");
        return NULL;
    }
    return root;
}

/* ── Document shape ─────────────────────────────────────────────────────────
   v1 was a bare array of workspaces. v2 is an object carrying the folder
   registry alongside them:

     { "version": 2, "folders": ["NUSY4S1", ...], "workspaces": [ ... ] }

   Reading tolerates both and writing always produces v2 — the same read-both,
   write-new migration the singular "target" → "targets" change used, so there is
   no upgrade step to run. */

#define WS_SCHEMA_VERSION 2

/* Loads the document normalised to the v2 shape, so no caller has to know which
   version is on disk: a legacy array is wrapped (folders empty), and a v2 object
   is guaranteed to have both members as arrays even if hand-edited. */
static cJSON *load_ws_doc(void) {
    cJSON *root = load_ws_json();
    if (root == NULL) return NULL;

    if (cJSON_IsArray(root)) {
        cJSON *doc = cJSON_CreateObject();
        if (doc == NULL) { cJSON_Delete(root); return NULL; }
        cJSON_AddNumberToObject(doc, "version", WS_SCHEMA_VERSION);
        cJSON_AddItemToObject(doc, "folders", cJSON_CreateArray());
        cJSON_AddItemToObject(doc, "workspaces", root);   /* doc owns it now */
        return doc;
    }

    if (!cJSON_IsArray(cJSON_GetObjectItem(root, "workspaces"))) {
        cJSON_DeleteItemFromObject(root, "workspaces");
        cJSON_AddItemToObject(root, "workspaces", cJSON_CreateArray());
    }
    if (!cJSON_IsArray(cJSON_GetObjectItem(root, "folders"))) {
        cJSON_DeleteItemFromObject(root, "folders");
        cJSON_AddItemToObject(root, "folders", cJSON_CreateArray());
    }
    return root;
}

static int save_ws_json(cJSON *root) {
    char path[4096];
    workspace_path(path, sizeof(path));
    char *output = cJSON_Print(root);
    if (output == NULL) return -1;
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        ui_err("could not write workspaces file");
        free(output);
        return -1;
    }
    fprintf(f, "%s\n", output);
    fclose(f);
    free(output);
    return 0;
}

Workspace *workspace_load_all(int *count) {
    cJSON *doc = load_ws_doc();
    if (doc == NULL) { *count = 0; return NULL; }
    cJSON *root = cJSON_GetObjectItem(doc, "workspaces");

    int n = cJSON_GetArraySize(root);
    Workspace *ws = malloc(n * sizeof(Workspace));
    if (ws == NULL) { cJSON_Delete(doc); *count = 0; return NULL; }

    for (int i = 0; i < n; i++) {
        cJSON *obj  = cJSON_GetArrayItem(root, i);
        cJSON *name = cJSON_GetObjectItem(obj, "name");
        cJSON *ents = cJSON_GetObjectItem(obj, "entries");
        cJSON *fold = cJSON_GetObjectItem(obj, "folder");

        strncpy(ws[i].name, name ? name->valuestring : "", WORKSPACE_NAME_MAX - 1);
        ws[i].name[WORKSPACE_NAME_MAX - 1] = '\0';

        /* malloc leaves this garbage, and a v1 workspace has no folder at all. */
        strncpy(ws[i].folder, (fold && fold->valuestring) ? fold->valuestring : "",
                WORKSPACE_FOLDER_MAX - 1);
        ws[i].folder[WORKSPACE_FOLDER_MAX - 1] = '\0';

        ws[i].entry_count = 0;
        if (ents && cJSON_IsArray(ents)) {
            int ec = cJSON_GetArraySize(ents);
            if (ec > WORKSPACE_ENTRIES_MAX) ec = WORKSPACE_ENTRIES_MAX;
            for (int j = 0; j < ec; j++) {
                cJSON *e   = cJSON_GetArrayItem(ents, j);
                cJSON *app = cJSON_GetObjectItem(e, "app");
                strncpy(ws[i].entries[j].app, app ? app->valuestring : "",
                        WORKSPACE_APP_MAX - 1);
                ws[i].entries[j].app[WORKSPACE_APP_MAX - 1] = '\0';
                /* malloc leaves these garbage; init before any push. */
                ws[i].entries[j].targets      = NULL;
                ws[i].entries[j].target_count = 0;
                ws[i].entries[j].target_cap   = 0;
                ws[i].entries[j].layout[0]     = '\0';
                WorkspaceEntry *en = &ws[i].entries[j];

                cJSON *lay = cJSON_GetObjectItem(e, "layout");
                if (lay && lay->valuestring) {
                    strncpy(en->layout, lay->valuestring, sizeof(en->layout) - 1);
                    en->layout[sizeof(en->layout) - 1] = '\0';
                }

                cJSON *tgts = cJSON_GetObjectItem(e, "targets");
                if (tgts && cJSON_IsArray(tgts)) {
                    /* New format: "targets": ["url1", "url2", ...] */
                    int tc = cJSON_GetArraySize(tgts);
                    for (int k = 0; k < tc; k++) {
                        cJSON *t = cJSON_GetArrayItem(tgts, k);
                        if (t && t->valuestring)
                            targetlist_push(&en->targets, &en->target_cap,
                                            &en->target_count, t->valuestring);
                    }
                } else {
                    /* Backward compat: old format "target": "url" */
                    cJSON *tgt = cJSON_GetObjectItem(e, "target");
                    const char *tval = (tgt && tgt->valuestring) ? tgt->valuestring : "";
                    if (tval[0] != '\0')
                        targetlist_push(&en->targets, &en->target_cap,
                                        &en->target_count, tval);
                }
                ws[i].entry_count++;
            }
        }
    }

    cJSON_Delete(doc);
    *count = n;
    return ws;
}

int workspace_load_folders(FolderList *out) {
    memset(out, 0, sizeof(*out));
    cJSON *doc = load_ws_doc();
    if (doc == NULL) return -1;

    cJSON *arr = cJSON_GetObjectItem(doc, "folders");
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *p = cJSON_GetArrayItem(arr, i);
        if (p && p->valuestring && p->valuestring[0])
            targetlist_push(&out->paths, &out->cap, &out->count, p->valuestring);
    }
    cJSON_Delete(doc);
    return 0;
}

void workspace_free_folders(FolderList *f) {
    targetlist_free(&f->paths, &f->cap, &f->count);
}

void workspace_free_all(Workspace *ws, int count) {
    if (ws == NULL) return;
    for (int i = 0; i < count; i++)
        for (int j = 0; j < ws[i].entry_count; j++)
            targetlist_free(&ws[i].entries[j].targets,
                            &ws[i].entries[j].target_cap,
                            &ws[i].entries[j].target_count);
    free(ws);
}

int workspace_save_all_with_folders(Workspace *ws, int count, const FolderList *f) {
    cJSON *doc = cJSON_CreateObject();
    if (doc == NULL) return -3;
    cJSON_AddNumberToObject(doc, "version", WS_SCHEMA_VERSION);

    cJSON *folders = cJSON_CreateArray();
    if (f != NULL)
        for (int i = 0; i < f->count; i++)
            cJSON_AddItemToArray(folders, cJSON_CreateString(f->paths[i]));
    cJSON_AddItemToObject(doc, "folders", folders);

    cJSON *root = cJSON_CreateArray();
    if (root == NULL) { cJSON_Delete(doc); return -3; }
    cJSON_AddItemToObject(doc, "workspaces", root);

    for (int i = 0; i < count; i++) {
        cJSON *obj  = cJSON_CreateObject();
        cJSON *ents = cJSON_CreateArray();
        cJSON_AddStringToObject(obj, "name", ws[i].name);
        /* omitted at root, the way an empty layout is */
        if (ws[i].folder[0])
            cJSON_AddStringToObject(obj, "folder", ws[i].folder);
        for (int j = 0; j < ws[i].entry_count; j++) {
            cJSON *e    = cJSON_CreateObject();
            cJSON *tgts = cJSON_CreateArray();
            cJSON_AddStringToObject(e, "app", ws[i].entries[j].app);
            for (int k = 0; k < ws[i].entries[j].target_count; k++)
                cJSON_AddItemToArray(tgts, cJSON_CreateString(ws[i].entries[j].targets[k]));
            cJSON_AddItemToObject(e, "targets", tgts);
            if (ws[i].entries[j].layout[0])
                cJSON_AddStringToObject(e, "layout", ws[i].entries[j].layout);
            cJSON_AddItemToArray(ents, e);
        }
        cJSON_AddItemToObject(obj, "entries", ents);
        cJSON_AddItemToArray(root, obj);
    }

    int rc = save_ws_json(doc);
    cJSON_Delete(doc);
    return rc;
}

int workspace_remove(const char *name) {
    cJSON *doc = load_ws_doc();
    if (doc == NULL) return -2;
    cJSON *root = cJSON_GetObjectItem(doc, "workspaces");

    int n = cJSON_GetArraySize(root);
    int found = 0;
    for (int i = 0; i < n; i++) {
        cJSON *obj = cJSON_GetArrayItem(root, i);
        cJSON *nm  = cJSON_GetObjectItem(obj, "name");
        if (nm && strcmp(nm->valuestring, name) == 0) {
            cJSON_DeleteItemFromArray(root, i);
            found = 1;
            break;
        }
    }

    if (!found) { cJSON_Delete(doc); return -1; }
    int rc = save_ws_json(doc);
    cJSON_Delete(doc);
    return rc;
}

/* ── Store ─────────────────────────────────────────────────────────────────
   The two halves are loaded and saved together so a folder edit and a workspace
   edit made in the same `mn open edit` session commit as one write. */

int workspace_store_load(WorkspaceStore *st) {
    memset(st, 0, sizeof(*st));
    st->ws = workspace_load_all(&st->ws_count);
    if (st->ws == NULL && st->ws_count == 0) {
        /* Missing/malformed file: load_all already reported it. An empty store
           is still usable — folders just have nothing to hold yet. */
        return -1;
    }
    workspace_load_folders(&st->folders);
    return 0;
}

int workspace_store_save(const WorkspaceStore *st) {
    return workspace_save_all_with_folders(st->ws, st->ws_count, &st->folders);
}

/* The array is grown one slot at a time rather than doubled: unlike the target
   lists, an add here is a person typing a workspace name, so the realloc is never
   on a hot path and an exact-sized array keeps workspace_store_free simple. */
int workspace_store_add(WorkspaceStore *st, const char *name, const char *folder) {
    for (int i = 0; i < st->ws_count; i++)
        if (strcmp(st->ws[i].name, name) == 0) return -1;

    Workspace *p = realloc(st->ws, (size_t)(st->ws_count + 1) * sizeof(Workspace));
    if (p == NULL) return -2;
    st->ws = p;

    /* Zeroed, so entry_count is 0 and every entry's targets pointer is NULL —
       workspace_free_all walks entries up to entry_count only, but a later
       targetlist_push into a fresh slot needs the NULL/0/0 start. */
    Workspace *w = &st->ws[st->ws_count];
    memset(w, 0, sizeof(*w));
    snprintf(w->name,   sizeof(w->name),   "%s", name);
    snprintf(w->folder, sizeof(w->folder), "%s", folder);
    return st->ws_count++;
}

int workspace_store_remove(WorkspaceStore *st, int idx) {
    if (idx < 0 || idx >= st->ws_count) return -1;
    Workspace *w = &st->ws[idx];
    for (int j = 0; j < w->entry_count; j++)
        targetlist_free(&w->entries[j].targets, &w->entries[j].target_cap,
                        &w->entries[j].target_count);
    /* Slide the tail down over the freed slot; the array block is freed whole by
       workspace_store_free, so there is no need to shrink the allocation. */
    memmove(&st->ws[idx], &st->ws[idx + 1],
            (size_t)(st->ws_count - idx - 1) * sizeof(Workspace));
    st->ws_count--;
    return 0;
}

int workspace_store_swap(WorkspaceStore *st, int i, int j) {
    if (i < 0 || j < 0 || i >= st->ws_count || j >= st->ws_count) return -1;
    if (i == j) return 0;
    /* A Workspace is large (its entries are fixed-width), so the temporary goes on
       the heap rather than the stack. Copying whole structs carries each entry's
       targets pointer with it, so ownership moves without a per-entry deep copy. */
    Workspace *tmp = malloc(sizeof(Workspace));
    if (tmp == NULL) return -2;
    memcpy(tmp,          &st->ws[i], sizeof(Workspace));
    memcpy(&st->ws[i],   &st->ws[j], sizeof(Workspace));
    memcpy(&st->ws[j],   tmp,        sizeof(Workspace));
    free(tmp);
    return 0;
}

void workspace_store_free(WorkspaceStore *st) {
    workspace_free_all(st->ws, st->ws_count);
    workspace_free_folders(&st->folders);
    st->ws = NULL;
    st->ws_count = 0;
}
