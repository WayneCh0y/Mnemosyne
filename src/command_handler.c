#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
/* windows.h defines FILE_TYPE_UNKNOWN as 0x0000, which conflicts with our enum */
#undef FILE_TYPE_UNKNOWN
#endif

#include "command_handler.h"
#include "help.h"
#include "ingest.h"
#include "index.h"
#include "search.h"
#include "config.h"
#include "remove.h"
#include "picker.h"
#include "workspace.h"

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

static int is_new_window_app(const char *app) {
    return strcmp(app, "code") == 0 || strcmp(app, "cursor") == 0;
}

#ifdef _WIN32
/* Looks up <app>.exe in the Windows App Paths registry (HKCU then HKLM).
   Writes the full executable path into out on success. Returns 1 if found. */
static int resolve_app_path_win(const char *app, char *out, size_t out_size) {
    char key[512];
    snprintf(key, sizeof(key),
             "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s.exe", app);
    HKEY roots[2] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (int i = 0; i < 2; i++) {
        HKEY hkey;
        if (RegOpenKeyExA(roots[i], key, 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
            DWORD size = (DWORD)out_size;
            DWORD type;
            LONG rc = RegQueryValueExA(hkey, NULL, NULL, &type, (LPBYTE)out, &size);
            RegCloseKey(hkey);
            if (rc == ERROR_SUCCESS && type == REG_SZ) {
                int len = (int)strlen(out);
                if (len >= 2 && out[0] == '"' && out[len - 1] == '"') {
                    memmove(out, out + 1, len - 2);
                    out[len - 2] = '\0';
                }
                return 1;
            }
        }
    }
    return 0;
}

/* Launches app+target via ShellExecuteEx so no cmd.exe window appears.
   If the app resolves to a full .exe path, it is called directly.
   Otherwise a hidden cmd.exe /c is used as a fallback (handles .cmd scripts). */
static void win_launch(const char *app, const char *target) {
    const char *new_win = is_new_window_app(app) ? "--new-window" : "";
    char params[WORKSPACE_TARGET_MAX + 32];
    params[0] = '\0';

    if (new_win[0] && target[0])
        snprintf(params, sizeof(params), "%s \"%s\"", new_win, target);
    else if (new_win[0])
        strncpy(params, new_win, sizeof(params) - 1);
    else if (target[0])
        snprintf(params, sizeof(params), "\"%s\"", target);

    char resolved[4096];
    if (resolve_app_path_win(app, resolved, sizeof(resolved))) {
        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = "open";
        sei.lpFile       = resolved;
        sei.lpParameters = params[0] ? params : NULL;
        sei.nShow        = SW_SHOWNORMAL;
        ShellExecuteExA(&sei);
    } else {
        /* Not in App Paths — wrap in a hidden cmd.exe (handles .cmd scripts) */
        char cmd_params[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 64];
        if (params[0])
            snprintf(cmd_params, sizeof(cmd_params), "/c %s %s", app, params);
        else
            snprintf(cmd_params, sizeof(cmd_params), "/c %s", app);

        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = "open";
        sei.lpFile       = "cmd.exe";
        sei.lpParameters = cmd_params;
        sei.nShow        = SW_HIDE;
        ShellExecuteExA(&sei);
    }
}
#endif

static void launch_workspace(const Workspace *ws) {
    printf("Opening '%s' (%d app%s)...\n",
           ws->name, ws->entry_count, ws->entry_count == 1 ? "" : "s");
    for (int i = 0; i < ws->entry_count; i++) {
        /* Skip duplicate (app, target) pairs — same combo already launched. */
        int duplicate = 0;
        for (int j = 0; j < i; j++) {
            if (strcmp(ws->entries[j].app,    ws->entries[i].app)    == 0 &&
                strcmp(ws->entries[j].target, ws->entries[i].target) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;

        const char *app    = ws->entries[i].app;
        const char *target = ws->entries[i].target;
#ifdef _WIN32
        win_launch(app, target);
#elif defined(__APPLE__)
        {
            const char *new_win = is_new_window_app(app) ? " --new-window" : "";
            char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 64];
            /* 'open -a <app>' resolves GUI apps by bundle name (case-insensitive).
               code/cursor are in PATH via their installer — use bare name. */
            if (!is_new_window_app(app)) {
                if (target[0] != '\0')
                    snprintf(cmd, sizeof(cmd), "open -a \"%s\" \"%s\"", app, target);
                else
                    snprintf(cmd, sizeof(cmd), "open -a \"%s\"", app);
            } else {
                if (target[0] != '\0')
                    snprintf(cmd, sizeof(cmd), "%s%s \"%s\"", app, new_win, target);
                else
                    snprintf(cmd, sizeof(cmd), "%s%s", app, new_win);
            }
            system(cmd);
        }
#else
        {
            const char *new_win = is_new_window_app(app) ? " --new-window" : "";
            char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 32];
            if (target[0] != '\0')
                snprintf(cmd, sizeof(cmd), "%s%s \"%s\"", app, new_win, target);
            else
                snprintf(cmd, sizeof(cmd), "%s%s", app, new_win);
            system(cmd);
        }
#endif
    }
}

static void cmd_open_run(void) {
    int count;
    Workspace *ws = workspace_load_all(&count);
    if (!ws || count == 0) {
        printf("No workspaces yet. Create one with: mn open create <name>\n");
        free(ws);
        return;
    }
    int chosen = run_workspace_picker(ws, count);
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1)
        launch_workspace(&ws[chosen]);
    free(ws);
}

static void cmd_open_create(const char *name) {
    int rc = workspace_create(name);
    if (rc == 0)
        printf("Workspace '%s' created.\n", name);
    else if (rc == -1)
        fprintf(stderr, "error: workspace '%s' already exists\n", name);
    else
        fprintf(stderr, "error: failed to create workspace\n");
}

static void cmd_open_add(const char *ws_name) {
    Workspace tmp;
    if (workspace_get(ws_name, &tmp) != 0) {
        fprintf(stderr, "error: workspace '%s' not found\n", ws_name);
        return;
    }

    char app[WORKSPACE_APP_MAX];
    char target[WORKSPACE_TARGET_MAX];

    printf("App name (e.g. msedge, code, outlook): ");
    fflush(stdout);
    if (fgets(app, sizeof(app), stdin) == NULL) return;
    app[strcspn(app, "\n")] = '\0';
    if (app[0] == '\0') { printf("No app name entered.\n"); return; }

    if (is_new_window_app(app)) {
        int entry_count;
        IndexEntry *entries = index_get_entries(&entry_count);
        if (!entries || entry_count == 0) {
            free(entries);
            printf("No files indexed yet.\n");
            printf("Path: ");
            fflush(stdout);
            if (fgets(target, sizeof(target), stdin) == NULL) return;
            target[strcspn(target, "\n")] = '\0';
        } else {
            int ok = run_path_picker(entries, entry_count, target, sizeof(target));
            printf(ANSI_CLEAR ANSI_RESET);
            free(entries);
            if (!ok) { printf("Cancelled.\n"); return; }
        }
    } else {
        printf("URL or path (press Enter to skip): ");
        fflush(stdout);
        if (fgets(target, sizeof(target), stdin) == NULL) return;
        target[strcspn(target, "\n")] = '\0';
    }

    int rc = workspace_add_entry(ws_name, app, target);
    if (rc == 0) {
        if (target[0] != '\0')
            printf("Added to '%s': %s \xe2\x86\x92 %s\n", ws_name, app, target);
        else
            printf("Added to '%s': %s\n", ws_name, app);
    } else if (rc == -1) {
        fprintf(stderr, "error: workspace '%s' not found\n", ws_name);
    } else if (rc == -2) {
        fprintf(stderr, "error: workspace '%s' is full (max %d entries)\n",
                ws_name, WORKSPACE_ENTRIES_MAX);
    } else {
        fprintf(stderr, "error: failed to save workspace\n");
    }
}

static void cmd_open_list(void) {
    int count;
    Workspace *ws = workspace_load_all(&count);
    if (!ws || count == 0) {
        printf("No workspaces yet. Create one with: mn open create <name>\n");
        free(ws);
        return;
    }
    for (int i = 0; i < count; i++) {
        printf("[%d] %s (%d app%s)\n",
               i + 1, ws[i].name, ws[i].entry_count,
               ws[i].entry_count == 1 ? "" : "s");
        for (int j = 0; j < ws[i].entry_count; j++) {
            if (ws[i].entries[j].target[0] != '\0')
                printf("    [%d] %s \xe2\x86\x92 %s\n",
                       j + 1, ws[i].entries[j].app, ws[i].entries[j].target);
            else
                printf("    [%d] %s\n", j + 1, ws[i].entries[j].app);
        }
    }
    free(ws);
}

static void cmd_open_remove(int argc, char *argv[]) {
    const char *name = argv[3];

    if (argc == 4) {
        int rc = workspace_remove(name);
        if (rc == 0)
            printf("Workspace '%s' removed.\n", name);
        else if (rc == -1)
            fprintf(stderr, "error: workspace '%s' not found\n", name);
        else
            fprintf(stderr, "error: failed to remove workspace\n");
        return;
    }

    /* argc == 5: remove entry by 1-based index */
    int n = atoi(argv[4]);
    if (n <= 0) {
        fprintf(stderr, "error: entry index must be a positive number\n");
        return;
    }
    int rc = workspace_remove_entry(name, n - 1);
    if (rc == 0)
        printf("Entry %d removed from workspace '%s'.\n", n, name);
    else if (rc == -1)
        fprintf(stderr, "error: workspace '%s' not found\n", name);
    else if (rc == -2)
        fprintf(stderr, "error: no entry at index %d (run 'mn open list' to see indices)\n", n);
    else
        fprintf(stderr, "error: failed to save workspace\n");
}

static void cmd_open(int argc, char *argv[]) {
    if (argc == 2)                                                    { cmd_open_run();             return; }
    if (argc == 3 && strcmp(argv[2], "list")   == 0)                  { cmd_open_list();            return; }
    if (argc == 4 && strcmp(argv[2], "create") == 0)                  { cmd_open_create(argv[3]);   return; }
    if (argc == 4 && strcmp(argv[2], "add")    == 0)                  { cmd_open_add(argv[3]);      return; }
    if ((argc == 4 || argc == 5) && strcmp(argv[2], "remove") == 0)   { cmd_open_remove(argc, argv); return; }
    if (argc == 3 && strcmp(argv[2], "add")    == 0) {
        fprintf(stderr, "usage: mn open add <workspace-name>\n");
        return;
    }
    if (argc == 3 && strcmp(argv[2], "remove") == 0) {
        fprintf(stderr, "usage: mn open remove <workspace-name> [entry-index]\n");
        return;
    }
    print_help();
}

void handle_command(int argc, char *argv[]) {
    const char *cmd = argv[1];

    if (strcmp(cmd, "add") == 0)         { cmd_add(argc, argv);    return; }
    if (strcmp(cmd, "search") == 0)      { cmd_search(argc, argv); return; }
    if (strcmp(cmd, "list") == 0)        { cmd_list(argc, argv);   return; }
    if (strcmp(cmd, "remove") == 0)      { cmd_remove(argc, argv); return; }
    if (strcmp(cmd, "open") == 0)        { cmd_open(argc, argv);   return; }
    if (strcmp(cmd, "config") == 0)      { cmd_config(argc, argv); return; }
    if (strcmp(cmd, "help") == 0)        { print_help();           return; }

    fprintf(stderr, "Unknown command: %s\n", cmd);
}