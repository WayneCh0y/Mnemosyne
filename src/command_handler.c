#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

#include "command_handler.h"
#include "help.h"
#include "ingest.h"
#include "index.h"
#include "search.h"
#include "config.h"

static int is_valid_add(int argc) {
    if (argc == 3) {
        return 1;
    }
    return 0;
}

static void cmd_add(int argc, char *argv[]) {
    if (!is_valid_add(argc)) { print_help(); return; }

    ingest_file(argv[2]);

    return;
}

static int is_valid_search(int argc) {
    if (argc >= 3) { return 1; }
    return 0;
}

static void update_files(void) {
    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (entries == NULL) return;

    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(entries[i].original_path, &st) != 0) continue;
        if ((long)st.st_mtime > entries[i].last_modified)
            ingest_file(entries[i].original_path);
    }

    free(entries);
}

static char* build_query(int argc, char *argv[]) {
    static char query[256];
    query[0] = '\0';

    for (int i = 2; i < argc; i++) {
        strcat(query, argv[i]);
        if (i < argc - 1) { strcat(query, " "); }
    }

    for (int i = 0; query[i]; i++)
        query[i] = (char)tolower((unsigned char)query[i]);

    return query;
}

static void cmd_search(int argc, char *argv[]) {
    /*
        We update files regardless, when a search command is triggered.
    */
    update_files();

    if (!is_valid_search(argc)) { print_help(); return; }

    char* query = build_query(argc, argv);
    
    int count;
    SearchResult* results = search(query, &count);

    /*
        Nothing found :/
    */
    if (count == 0) {
        printf("No results!\n");
        free(results);
        return;
    }

    // We cap display lines to 5.
    int display = count < 5 ? count : 5;
    
    for (int i = 0; i < display; i++) {
        printf("[%d] %s\n", i + 1, results[i].original_path);
        printf("    %s\n", results[i].context);
    }

    free(results);

    return;
}

static void cmd_list(int argc, char *argv[]) { return; }
static void cmd_remove(int argc, char *argv[]) { return; }

static int is_valid_config(int argc, char *argv[]) {
    if (argc == 4 && strcmp(argv[2], "ide") == 0) {
        return 1;
    }
    return 0;
}

static void cmd_config(int argc, char *argv[]) { 
    if (!is_valid_config(argc, argv)) { print_help(); return; }

    if (set_ide(argv[3]) == 0) {
        printf("Default IDE updated to: %s\n", argv[3]);
    }
}

void handle_command(int argc, char *argv[]) {
    const char *cmd = argv[1];

    if (strcmp(cmd, "add") == 0)         { cmd_add(argc, argv);    return; }
    if (strcmp(cmd, "search") == 0)      { cmd_search(argc, argv); return; }
    if (strcmp(cmd, "list") == 0)        { cmd_list(argc, argv);   return; }
    if (strcmp(cmd, "remove") == 0)      { cmd_remove(argc, argv); return; }
    if (strcmp(cmd, "config") == 0)      { cmd_config(argc, argv); return; }
    if (strcmp(cmd, "help") == 0)        { print_help();           return; }

    fprintf(stderr, "Unknown command: %s\n", cmd);
}