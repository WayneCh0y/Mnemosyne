#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "index.h"
#include "config.h"
#include "cJSON.h"

void find_outermost_git_root(const char *start_dir, char *out, size_t out_size) {
    char dir[4096];
    strncpy(dir, start_dir, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    char best[4096] = {0};

    while (dir[0] != '\0') {
        char git_path[4096 + 6];
#ifdef _WIN32
        snprintf(git_path, sizeof(git_path), "%s\\.git", dir);
#else
        snprintf(git_path, sizeof(git_path), "%s/.git", dir);
#endif
        struct stat st;
        if (stat(git_path, &st) == 0) {
            strncpy(best, dir, sizeof(best) - 1);
            best[sizeof(best) - 1] = '\0';
        }

#ifdef _WIN32
        char *last = strrchr(dir, '\\');
#else
        char *last = strrchr(dir, '/');
#endif
        if (last == NULL) break;
        *last = '\0';
    }

    const char *result = best[0] ? best : "none";
    strncpy(out, result, out_size - 1);
    out[out_size - 1] = '\0';
}

static void find_git_root(const char *original_path, char *out, size_t out_size) {
    char dir[4096];
    strncpy(dir, original_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

#ifdef _WIN32
    char *sep = strrchr(dir, '\\');
#else
    char *sep = strrchr(dir, '/');
#endif
    if (sep) *sep = '\0';

    find_outermost_git_root(dir, out, out_size);
}

/* Returns a cJSON object representing the manifest */
static cJSON *load_manifest(void) {
    char manifest_path[4096];
    snprintf(manifest_path, sizeof(manifest_path), "%s/index/manifest.json", get_data_path());

    FILE *f = fopen(manifest_path, "r");
    if (f == NULL) {
        fprintf(stderr, "error: could not open manifest: %s\n", manifest_path);
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
        fprintf(stderr, "error: manifest.json is malformed\n");
        return NULL;
    }
    return root;
}

/* Overwrites manifest.json with root's JSON */
static int save_manifest(cJSON *root) {
    char manifest_path[4096];
    snprintf(manifest_path, sizeof(manifest_path), "%s/index/manifest.json", get_data_path());

    char *output = cJSON_Print(root);
    if (output == NULL) return -1;

    FILE *f = fopen(manifest_path, "w");
    if (f == NULL) {
        fprintf(stderr, "error: could not write manifest\n");
        free(output);
        return -1;
    }
    fprintf(f, "%s\n", output);
    fclose(f);
    free(output);
    return 0;
}

void index_add(const char *original_path, const char *hash,
               long size_bytes, long last_modified, const char *file_type) {
    cJSON *root = load_manifest();
    if (root == NULL) return;

    /* Remove existing entry for this path (re-index case) */
    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n; i++) {
        cJSON *entry = cJSON_GetArrayItem(root, i);
        cJSON *op = cJSON_GetObjectItem(entry, "original_path");
        if (op && strcmp(op->valuestring, original_path) == 0) {
            cJSON_DeleteItemFromArray(root, i);
            break;
        }
    }

    /* Build and append new entry */
    char repository[4096];
    find_git_root(original_path, repository, sizeof(repository));

    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "original_path", original_path);
    cJSON_AddStringToObject(entry, "hash",          hash);
    cJSON_AddNumberToObject(entry, "size_bytes",    (double)size_bytes);
    cJSON_AddNumberToObject(entry, "last_modified", (double)last_modified);
    cJSON_AddStringToObject(entry, "file_type",     file_type);
    cJSON_AddStringToObject(entry, "repository",    repository);
    cJSON_AddItemToArray(root, entry);

    save_manifest(root);
    cJSON_Delete(root);
}

int index_remove(const char *original_path) {
    cJSON *root = load_manifest();
    if (root == NULL) return -1;

    int has_removed = 0;
    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n; i++) {
        cJSON *entry = cJSON_GetArrayItem(root, i);
        cJSON *op = cJSON_GetObjectItem(entry, "original_path");
        if (op && strcmp(op->valuestring, original_path) == 0) {
            cJSON_DeleteItemFromArray(root, i);
            has_removed = 1;
            break;
        }
    }

    int rc = has_removed;
    /* Save manifest only if anything was removed */
    if (has_removed && save_manifest(root) != 0) rc = -1;
    cJSON_Delete(root);
    return rc;
}

IndexEntry *index_get_entries(int *count) {
    cJSON *root = load_manifest();
    if (root == NULL) { *count = 0; return NULL; }

    int n = cJSON_GetArraySize(root);
    IndexEntry *entries = malloc(n * sizeof(IndexEntry));
    if (entries == NULL) { cJSON_Delete(root); *count = 0; return NULL; }

    for (int i = 0; i < n; i++) {
        cJSON *entry       = cJSON_GetArrayItem(root, i);
        cJSON *op          = cJSON_GetObjectItem(entry, "original_path");
        cJSON *h           = cJSON_GetObjectItem(entry, "hash");
        cJSON *lm          = cJSON_GetObjectItem(entry, "last_modified");
        cJSON *repo        = cJSON_GetObjectItem(entry, "repository");
        cJSON *ft          = cJSON_GetObjectItem(entry, "file_type");
        strncpy(entries[i].original_path, op->valuestring, sizeof(entries[i].original_path) - 1);
        entries[i].original_path[sizeof(entries[i].original_path) - 1] = '\0';
        strncpy(entries[i].hash, h->valuestring, sizeof(entries[i].hash) - 1);
        entries[i].hash[sizeof(entries[i].hash) - 1] = '\0';
        entries[i].last_modified = (long)lm->valuedouble;
        if (repo && repo->valuestring) {
            strncpy(entries[i].repository, repo->valuestring, sizeof(entries[i].repository) - 1);
            entries[i].repository[sizeof(entries[i].repository) - 1] = '\0';
        } else {
            strncpy(entries[i].repository, "none", sizeof(entries[i].repository) - 1);
        }
        if (ft && ft->valuestring) {
            strncpy(entries[i].file_type, ft->valuestring, sizeof(entries[i].file_type) - 1);
            entries[i].file_type[sizeof(entries[i].file_type) - 1] = '\0';
        } else {
            strncpy(entries[i].file_type, "txt", sizeof(entries[i].file_type) - 1);
        }
    }

    cJSON_Delete(root);
    *count = n;
    return entries;
}
