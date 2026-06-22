#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <io.h>
/* windows.h defines FILE_TYPE_UNKNOWN as 0x0000, which conflicts with our enum */
#undef FILE_TYPE_UNKNOWN
#else
#include <unistd.h>
#include <signal.h>
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
#include "app_resolve.h"

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

/* Closes the terminal mn is launched from by terminating its parent shell.
   Called after a successful open/launch so the launcher window disappears,
   leaving only the opened apps (which are detached and unaffected).
   Skipped when stdin isn't a TTY (pipes, scripts, CI) to avoid killing a
   parent in non-interactive runs. */
static void close_terminal(void) {
#ifdef _WIN32
    if (!_isatty(_fileno(stdin))) return;

    DWORD pid  = GetCurrentProcessId();
    DWORD ppid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = { .dwSize = sizeof(pe) };
        if (Process32First(snap, &pe)) {
            do {
                if (pe.th32ProcessID == pid) { ppid = pe.th32ParentProcessID; break; }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }
    if (ppid != 0) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, ppid);
        if (h) { TerminateProcess(h, 0); CloseHandle(h); }
    }
#else
    if (!isatty(STDIN_FILENO)) return;
    pid_t ppid = getppid();
    if (ppid > 1) kill(ppid, SIGKILL);  /* interactive shells ignore SIGTERM */
#endif
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
    close_terminal();
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

    int chosen = run_search_picker(results, count);
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
    close_terminal();
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
    int chosen = run_list_picker(entries, count, "Browse indexed files",
                                 "Use the arrow keys to move, Enter to open, Esc to cancel.");
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1)
        handle_list_enter(entries, chosen);
    free(entries);
}

static void cmd_remove(int argc, char *argv[]) {
    /* Direct form: remove by path (scriptable, backward compatible). */
    if (argc == 3) { remove_file(argv[2]); return; }
    if (argc != 2) { print_help(); return; }

    /* Interactive form: pick a file from the index to remove. */
    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (!entries || count == 0) {
        printf("No files indexed yet.\n");
        free(entries);
        return;
    }

    int chosen = run_list_picker(entries, count, "Remove a file",
                                 "Use the arrow keys to move, Enter to remove, Esc to cancel.");
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1) {
        /* Remove by stored absolute path (no realpath), so it works even if the
           underlying file has been deleted from disk. */
        int rc = remove_entry_by_abs_path(entries[chosen].original_path);
        if (rc == 1)
            printf("Removed '%s' from the index.\n", entries[chosen].original_path);
        else if (rc == 0)
            fprintf(stderr, "warning: '%s' was not indexed\n", entries[chosen].original_path);
        /* rc == -1: index_remove already printed the error */
    }
    free(entries);
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
/* Launches an app + optional target.
   code/cursor are .cmd launchers on PATH, so they go through a hidden cmd.exe.
   UWP / Microsoft Store apps (shell:AppsFolder\<AUMID>) are launched via
   explorer.exe. Every other app is a full executable path supplied by the
   user, launched directly via ShellExecuteEx. */
static void win_launch(const char *app, const char *target) {
    if (is_uwp_app(app)) {
        char params[WORKSPACE_APP_MAX + 4];
        snprintf(params, sizeof(params), "\"%s\"", app);
        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = "open";
        sei.lpFile       = "explorer.exe";
        sei.lpParameters = params;
        sei.nShow        = SW_SHOWNORMAL;
        if (!ShellExecuteExA(&sei))
            fprintf(stderr, "error: failed to launch '%s'\n", app);
        (void)target;
        return;
    }

    if (is_new_window_app(app)) {
        /* IDE launcher (.cmd) — run via hidden cmd.exe so no console flashes. */
        char cmd_params[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 64];
        if (target[0])
            snprintf(cmd_params, sizeof(cmd_params),
                     "/c %s --new-window \"%s\"", app, target);
        else
            snprintf(cmd_params, sizeof(cmd_params), "/c %s --new-window", app);

        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = "open";
        sei.lpFile       = "cmd.exe";
        sei.lpParameters = cmd_params;
        sei.nShow        = SW_HIDE;
        if (!ShellExecuteExA(&sei))
            fprintf(stderr, "error: failed to launch '%s'\n", app);
        return;
    }

    /* Full executable path — launch directly. */
    char params[WORKSPACE_TARGET_MAX + 4];
    params[0] = '\0';
    if (target[0])
        snprintf(params, sizeof(params), "\"%s\"", target);

    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize       = sizeof(sei);
    sei.lpVerb       = "open";
    sei.lpFile       = app;
    sei.lpParameters = params[0] ? params : NULL;
    sei.nShow        = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei))
        fprintf(stderr, "error: failed to launch '%s'\n", app);
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
            /* code/cursor return immediately; a user-supplied executable path is a
               GUI app that would otherwise block, so background it with '&'. */
            const char *new_win = is_new_window_app(app) ? " --new-window" : "";
            const char *bg      = is_new_window_app(app) ? "" : " &";
            char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 32];
            if (target[0] != '\0')
                snprintf(cmd, sizeof(cmd), "\"%s\"%s \"%s\"%s", app, new_win, target, bg);
            else
                snprintf(cmd, sizeof(cmd), "\"%s\"%s%s", app, new_win, bg);
            system(cmd);
        }
#endif
    }
    close_terminal();
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
    char target[WORKSPACE_TARGET_MAX] = {0};

    if (!run_app_picker(app, sizeof(app))) {
        printf(ANSI_CLEAR ANSI_RESET "Cancelled.\n");
        return;
    }

    if (!is_new_window_app(app)) {
        char resolved[WORKSPACE_APP_MAX];
        if (app_resolve(app, resolved, sizeof(resolved))) {
            strncpy(app, resolved, sizeof(app) - 1);
            app[sizeof(app) - 1] = '\0';
        }
    }

    if (is_new_window_app(app)) {
        int entry_count;
        IndexEntry *entries = index_get_entries(&entry_count);
        if (!entries || entry_count == 0) {
            free(entries);
            int ok = run_text_input("Add an app",
                                    "Path to open in the IDE (Enter to skip).",
                                    "Path", target, sizeof(target), 1);
            printf(ANSI_CLEAR ANSI_RESET);
            if (!ok) { printf("Cancelled.\n"); return; }
        } else {
            int ok = run_path_picker(entries, entry_count, target, sizeof(target));
            printf(ANSI_CLEAR ANSI_RESET);
            free(entries);
            if (!ok) { printf("Cancelled.\n"); return; }
        }
    } else {
        /* Non-IDE app: warn (don't block) if the value won't launch on this
           machine — the user may be on a different machine when they run it.
           UWP markers and macOS bundle names are trusted without a fs check. */
        int exists = app_value_exists(app);
        const char *subtitle = exists
            ? "Opened as an argument to the app (a URL or file). Enter to skip."
            : "\033[33mwarning: not found on this machine — it will still be saved.\033[0m";

        int ok = run_text_input("Add an app", subtitle, "URL or path",
                                target, sizeof(target), 1);
        printf(ANSI_CLEAR ANSI_RESET);
        if (!ok) { printf("Cancelled.\n"); return; }
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
    if (argc == 3 && strcmp(argv[2], "list")   == 0)                  { cmd_open_run();             return; }
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