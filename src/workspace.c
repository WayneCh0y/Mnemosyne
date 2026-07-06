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
    cJSON *root = load_ws_json();
    if (root == NULL) { *count = 0; return NULL; }

    int n = cJSON_GetArraySize(root);
    Workspace *ws = malloc(n * sizeof(Workspace));
    if (ws == NULL) { cJSON_Delete(root); *count = 0; return NULL; }

    for (int i = 0; i < n; i++) {
        cJSON *obj  = cJSON_GetArrayItem(root, i);
        cJSON *name = cJSON_GetObjectItem(obj, "name");
        cJSON *ents = cJSON_GetObjectItem(obj, "entries");

        strncpy(ws[i].name, name ? name->valuestring : "", WORKSPACE_NAME_MAX - 1);
        ws[i].name[WORKSPACE_NAME_MAX - 1] = '\0';

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

    cJSON_Delete(root);
    *count = n;
    return ws;
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

int workspace_save_all(Workspace *ws, int count) {
    cJSON *root = cJSON_CreateArray();
    if (root == NULL) return -3;

    for (int i = 0; i < count; i++) {
        cJSON *obj  = cJSON_CreateObject();
        cJSON *ents = cJSON_CreateArray();
        cJSON_AddStringToObject(obj, "name", ws[i].name);
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

    int rc = save_ws_json(root);
    cJSON_Delete(root);
    return rc;
}

int workspace_create(const char *name) {
    cJSON *root = load_ws_json();
    if (root == NULL) return -2;

    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n; i++) {
        cJSON *obj  = cJSON_GetArrayItem(root, i);
        cJSON *nm   = cJSON_GetObjectItem(obj, "name");
        if (nm && strcmp(nm->valuestring, name) == 0) {
            cJSON_Delete(root);
            return -1;
        }
    }

    cJSON *obj  = cJSON_CreateObject();
    cJSON *ents = cJSON_CreateArray();
    cJSON_AddStringToObject(obj, "name", name);
    cJSON_AddItemToObject(obj, "entries", ents);
    cJSON_AddItemToArray(root, obj);

    int rc = save_ws_json(root);
    cJSON_Delete(root);
    return rc;
}

int workspace_add_entry_with_targets(const char *name, const char *app,
                                     const char targets[][WORKSPACE_TARGET_MAX],
                                     int target_count) {
    int count;
    Workspace *ws = workspace_load_all(&count);
    if (ws == NULL) return -3;

    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(ws[i].name, name) == 0) { idx = i; break; }
    }
    if (idx == -1) { workspace_free_all(ws, count); return -1; }
    if (ws[idx].entry_count >= WORKSPACE_ENTRIES_MAX) { workspace_free_all(ws, count); return -2; }

    int j = ws[idx].entry_count;
    WorkspaceEntry *en = &ws[idx].entries[j];
    strncpy(en->app, app, WORKSPACE_APP_MAX - 1);
    en->app[WORKSPACE_APP_MAX - 1] = '\0';
    /* fresh slot beyond the loaded count → init before pushing. */
    en->targets      = NULL;
    en->target_count = 0;
    en->target_cap   = 0;
    en->layout[0]    = '\0';
    for (int k = 0; k < target_count; k++)
        targetlist_push(&en->targets, &en->target_cap, &en->target_count, targets[k]);
    ws[idx].entry_count++;

    int rc = workspace_save_all(ws, count);
    workspace_free_all(ws, count);
    return rc;
}

int workspace_add_entry(const char *name, const char *app, const char *target) {
    if (target == NULL || target[0] == '\0') {
        return workspace_add_entry_with_targets(name, app, NULL, 0);
    }
    char single[1][WORKSPACE_TARGET_MAX];
    strncpy(single[0], target, WORKSPACE_TARGET_MAX - 1);
    single[0][WORKSPACE_TARGET_MAX - 1] = '\0';
    return workspace_add_entry_with_targets(name, app, single, 1);
}

int workspace_remove(const char *name) {
    cJSON *root = load_ws_json();
    if (root == NULL) return -2;

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

    if (!found) { cJSON_Delete(root); return -1; }
    int rc = save_ws_json(root);
    cJSON_Delete(root);
    return rc;
}
