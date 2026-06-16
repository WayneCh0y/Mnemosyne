#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "index.h"
#include "config.h"
#include "cJSON.h"

void index_add(const char *original_path, const char *hash,
               long size_bytes, long last_modified, const char *file_type) {
    char manifest_path[4096];
    snprintf(manifest_path, sizeof(manifest_path), "%s/index/manifest.json", get_data_path());

    /* Read manifest into buffer */
    FILE *f = fopen(manifest_path, "r");
    if (f == NULL) {
        fprintf(stderr, "error: could not open manifest: %s\n", manifest_path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    if (buf == NULL) { fclose(f); return; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    /* Parse JSON array */
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (root == NULL) {
        fprintf(stderr, "error: manifest.json is malformed\n");
        return;
    }

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
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "original_path", original_path);
    cJSON_AddStringToObject(entry, "hash",          hash);
    cJSON_AddNumberToObject(entry, "size_bytes",    (double)size_bytes);
    cJSON_AddNumberToObject(entry, "last_modified", (double)last_modified);
    cJSON_AddStringToObject(entry, "file_type",     file_type);
    cJSON_AddItemToArray(root, entry);

    /* Write back */
    char *output = cJSON_Print(root);
    f = fopen(manifest_path, "w");
    if (f == NULL) {
        fprintf(stderr, "error: could not write manifest\n");
        free(output);
        cJSON_Delete(root);
        return;
    }
    fprintf(f, "%s\n", output);
    fclose(f);

    free(output);
    cJSON_Delete(root);
}

IndexEntry *index_get_entries(int *count) {
    char manifest_path[4096];
    snprintf(manifest_path, sizeof(manifest_path), "%s/index/manifest.json", get_data_path());

    FILE *f = fopen(manifest_path, "r");
    if (f == NULL) { fprintf(stderr, "error: could not open manifest: %s\n", manifest_path); *count = 0; return NULL; }
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    char *buf = malloc(size + 1);
    if (buf == NULL) { fclose(f); *count = 0; return NULL; }
    fread(buf, 1, size, f); buf[size] = '\0'; fclose(f);

    cJSON *root = cJSON_Parse(buf); free(buf);
    if (root == NULL) { fprintf(stderr, "error: manifest.json is malformed\n"); *count = 0; return NULL; }

    int n = cJSON_GetArraySize(root);
    IndexEntry *entries = malloc(n * sizeof(IndexEntry));
    if (entries == NULL) { cJSON_Delete(root); *count = 0; return NULL; }

    for (int i = 0; i < n; i++) {
        cJSON *entry       = cJSON_GetArrayItem(root, i);
        cJSON *op          = cJSON_GetObjectItem(entry, "original_path");
        cJSON *h           = cJSON_GetObjectItem(entry, "hash");
        cJSON *lm          = cJSON_GetObjectItem(entry, "last_modified");
        strncpy(entries[i].original_path, op->valuestring, sizeof(entries[i].original_path) - 1);
        entries[i].original_path[sizeof(entries[i].original_path) - 1] = '\0';
        strncpy(entries[i].hash, h->valuestring, sizeof(entries[i].hash) - 1);
        entries[i].hash[sizeof(entries[i].hash) - 1] = '\0';
        entries[i].last_modified = (long)lm->valuedouble;
    }

    cJSON_Delete(root);
    *count = n;
    return entries;
}
