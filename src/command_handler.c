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
#include "remove.h"
#include "picker.h"

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

/* Updates the index if a file has been modified or deleted. */
static void update_files(void) {
    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (entries == NULL) return;

    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(entries[i].original_path, &st) != 0)
            remove_entry_by_abs_path(entries[i].original_path);
        else if ((long)st.st_mtime > entries[i].last_modified)
            ingest_file(entries[i].original_path);
    }

    free(entries);
}

static char* build_query(int argc, char *argv[], char *raw_out, int raw_size) {
    static char query[256];
    query[0] = '\0';

    for (int i = 2; i < argc; i++) {
        strncat(query, argv[i], sizeof(query) - strlen(query) - 1);
        if (i < argc - 1) strncat(query, " ", sizeof(query) - strlen(query) - 1);
    }

    if (raw_out) {
        strncpy(raw_out, query, raw_size - 1);
        for (int i = 0; raw_out[i]; i++)
            if (raw_out[i] == '\\') raw_out[i] = '/';
    }

    for (int i = 0; query[i]; i++)
        query[i] = (char)tolower((unsigned char)query[i]);

    return query;
}

/* Returns the line number of the first occurrence of the query in the
   original file. */
static int find_line_in_original(const char *path, const char *query) {
    FILE *f = fopen(path, "r");
    if (f == NULL) return 1;

    int qlen = (int)strlen(query);
    char line[4096];
    int line_num = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        line_num++;

        for (int i = 0; line[i] != '\0'; i++)
            line[i] = (char)tolower((unsigned char)line[i]);

        const char *p = line;
        while ((p = strstr(p, query)) != NULL) {
            if (is_word_match(line, p, query)) {
                fclose(f);
                return line_num;
            }
            p += qlen;
        }
    }

    fclose(f);
    return 1;
}

static void handle_enter(SearchResult *results, int selected, const char *query) {
    const char *file_path = results[selected].original_path;
    const char *repo_path = NULL;
    int entry_count;
    IndexEntry *entries = index_get_entries(&entry_count);
    if (entries) {
        for (int i = 0; i < entry_count; i++) {
            if (strcmp(entries[i].original_path, file_path) == 0) {
                if (strcmp(entries[i].repository, "none") != 0)
                    repo_path = entries[i].repository;
                break;
            }
        }
    }
    const char *ide_name = get_ide();
    int line = find_line_in_original(file_path, query);
    char launch[32 + 4096 + 4096 + 64];

    if (repo_path && (strcmp(ide_name, "code") == 0 || strcmp(ide_name, "cursor") == 0)) {
        snprintf(launch, sizeof(launch), "%s \"%s\" --goto \"%s:%d\"", ide_name, repo_path, file_path, line);
    } else if (strcmp(ide_name, "code") == 0 || strcmp(ide_name, "cursor") == 0) {
        snprintf(launch, sizeof(launch), "%s --goto \"%s:%d\"", ide_name, file_path, line);
    } else if (repo_path && strcmp(ide_name, "idea") == 0) {
        snprintf(launch, sizeof(launch), "%s --line %d \"%s\" \"%s\"", ide_name, line, repo_path, file_path);
    } else if (strcmp(ide_name, "idea") == 0) {
        snprintf(launch, sizeof(launch), "%s --line %d \"%s\"", ide_name, line, file_path);
    } else {
        snprintf(launch, sizeof(launch), "%s +%d \"%s\"", ide_name, line, file_path);
    }
    free(entries);
    system(launch);
}

static void cmd_search(int argc, char *argv[]) {
    /*
        We update files regardless, when a search command is triggered.
    */
    update_files();

    if (!is_valid_search(argc)) { print_help(); return; }

    char raw_query[256] = {0};
    char* query = build_query(argc, argv, raw_query, sizeof(raw_query));

    int count;
    SearchResult* results = search(query, raw_query, &count);

    /*
        Nothing found :/
    */
    if (count == 0) {
        printf("No results found.\n");
        free(results);
        return;
    }

    // We cap display lines to 5.
    int display = count < 5 ? count : 5;

    int chosen = run_search_picker(results, display);
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1)
        handle_enter(results, chosen, query);
    free(results);

    return;
}

static void handle_list_enter(IndexEntry *entries, int selected) {
    const char *file_path = entries[selected].original_path;
    const char *repo_path = (strcmp(entries[selected].repository, "none") != 0)
                            ? entries[selected].repository : NULL;
    const char *ide_name = get_ide();
    char launch[32 + 4096 + 4096 + 32];
    if (repo_path && (strcmp(ide_name, "code") == 0 || strcmp(ide_name, "cursor") == 0))
        snprintf(launch, sizeof(launch), "%s \"%s\" --goto \"%s\"", ide_name, repo_path, file_path);
    else if (repo_path && strcmp(ide_name, "idea") == 0)
        snprintf(launch, sizeof(launch), "%s \"%s\" \"%s\"", ide_name, repo_path, file_path);
    else
        snprintf(launch, sizeof(launch), "%s \"%s\"", ide_name, file_path);
    system(launch);
}

static void cmd_list(int argc, char *argv[]) {
    (void)argc; (void)argv;
    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (!entries || count == 0) {
        printf("No files indexed yet.\n");
        free(entries);
        return;
    }
    int chosen = run_list_picker(entries, count);
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1)
        handle_list_enter(entries, chosen);
    free(entries);
}

static int is_valid_remove(int argc) {
    if (argc == 3) { return 1; }
    return 0;
}

static void cmd_remove(int argc, char *argv[]) {
    if (!is_valid_remove(argc)) { print_help() ; return; }
    
    remove_file(argv[2]);
}

static int is_valid_config(int argc, char *argv[]) {
    if (argc == 4 && strcmp(argv[2], "ide") == 0) {
        return 1;
    }
    if (argc == 3 && strcmp(argv[2], "ide") == 0) {
        return 1;
    }
    return 0;
}

static void cmd_config(int argc, char *argv[]) {
    if (!is_valid_config(argc, argv)) { print_help(); return; }

    if (argc == 3) {
        size_t count;
        const char **ide_list = get_ide_list(&count);
        int chosen = run_ide_picker(ide_list, count);
        printf(ANSI_CLEAR ANSI_RESET);
        if (chosen != -1) {
            if (set_ide(ide_list[chosen]) == 0) {
                printf("Default IDE updated to: %s\n", ide_list[chosen]);
            }
        }
    } else {
        if (set_ide(argv[3]) == 0) {
            printf("Default IDE updated to: %s\n", argv[3]);
        }
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