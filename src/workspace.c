#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "workspace.h"
#include "config.h"
#include "cJSON.h"

static void workspace_path(char *out, size_t n) {
    snprintf(out, n, "%s/workspaces.json", get_data_path());
}

void workspace_ensure_file(void) {
    char path[4096];
    workspace_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f != NULL) { fclose(f); return; }
    f = fopen(path, "w");
    if (f != NULL) { fprintf(f, "[]\n"); fclose(f); }
}

static cJSON *load_ws_json(void) {
    char path[4096];
    workspace_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "error: could not open workspaces file: %s\n", path);
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
        fprintf(stderr, "error: workspaces.json is malformed\n");
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
        fprintf(stderr, "error: could not write workspaces file\n");
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
                cJSON *tgt = cJSON_GetObjectItem(e, "target");
                strncpy(ws[i].entries[j].app, app ? app->valuestring : "",
                        WORKSPACE_APP_MAX - 1);
                ws[i].entries[j].app[WORKSPACE_APP_MAX - 1] = '\0';
                strncpy(ws[i].entries[j].target, tgt ? tgt->valuestring : "",
                        WORKSPACE_TARGET_MAX - 1);
                ws[i].entries[j].target[WORKSPACE_TARGET_MAX - 1] = '\0';
                ws[i].entry_count++;
            }
        }
    }

    cJSON_Delete(root);
    *count = n;
    return ws;
}

int workspace_save_all(Workspace *ws, int count) {
    cJSON *root = cJSON_CreateArray();
    if (root == NULL) return -3;

    for (int i = 0; i < count; i++) {
        cJSON *obj  = cJSON_CreateObject();
        cJSON *ents = cJSON_CreateArray();
        cJSON_AddStringToObject(obj, "name", ws[i].name);
        for (int j = 0; j < ws[i].entry_count; j++) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "app",    ws[i].entries[j].app);
            cJSON_AddStringToObject(e, "target", ws[i].entries[j].target);
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

int workspace_add_entry(const char *name, const char *app, const char *target) {
    int count;
    Workspace *ws = workspace_load_all(&count);
    if (ws == NULL) return -3;

    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(ws[i].name, name) == 0) { idx = i; break; }
    }
    if (idx == -1) { free(ws); return -1; }
    if (ws[idx].entry_count >= WORKSPACE_ENTRIES_MAX) { free(ws); return -2; }

    int j = ws[idx].entry_count;
    strncpy(ws[idx].entries[j].app, app, WORKSPACE_APP_MAX - 1);
    ws[idx].entries[j].app[WORKSPACE_APP_MAX - 1] = '\0';
    strncpy(ws[idx].entries[j].target, target, WORKSPACE_TARGET_MAX - 1);
    ws[idx].entries[j].target[WORKSPACE_TARGET_MAX - 1] = '\0';
    ws[idx].entry_count++;

    int rc = workspace_save_all(ws, count);
    free(ws);
    return rc;
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

int workspace_remove_entry(const char *name, int index) {
    int count;
    Workspace *ws = workspace_load_all(&count);
    if (ws == NULL) return -3;

    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(ws[i].name, name) == 0) { idx = i; break; }
    }
    if (idx == -1) { free(ws); return -1; }
    if (index < 0 || index >= ws[idx].entry_count) { free(ws); return -2; }

    for (int j = index; j < ws[idx].entry_count - 1; j++)
        ws[idx].entries[j] = ws[idx].entries[j + 1];
    ws[idx].entry_count--;

    int rc = workspace_save_all(ws, count);
    free(ws);
    return rc;
}

int workspace_get(const char *name, Workspace *out) {
    int count;
    Workspace *ws = workspace_load_all(&count);
    if (ws == NULL) return -1;

    for (int i = 0; i < count; i++) {
        if (strcmp(ws[i].name, name) == 0) {
            *out = ws[i];
            free(ws);
            return 0;
        }
    }
    free(ws);
    return -1;
}
