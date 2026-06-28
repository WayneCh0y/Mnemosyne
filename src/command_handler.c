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
#include "app_launch.h"
#include "app_enum.h"
#include "tokenizer.h"

static int is_valid_add(int argc) { return argc == 3; }

static void cmd_add(int argc, char *argv[]) {
    if (!is_valid_add(argc)) { print_help(); return; }
    ingest_path(argv[2]);
}

static int is_valid_search(int argc) { return argc >= 3; }

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
#elif defined(__APPLE__)
    if (!isatty(STDIN_FILENO)) return;
    const char *tty = ttyname(STDIN_FILENO);
    if (tty) {
        /* Ask Terminal.app to close the window hosting this tty via AppleScript.
           This works regardless of the "close if shell exited cleanly" preference,
           which would otherwise keep the window open after a SIGKILL. */
        char script[512];
        snprintf(script, sizeof(script),
            "osascript"
            " -e 'tell application \"Terminal\"'"
            " -e 'repeat with w in windows'"
            " -e 'if tty of front tab of w is \"%s\" then close w'"
            " -e 'end repeat'"
            " -e 'end tell'"
            " 2>/dev/null",
            tty);
        system(script);
    }
    pid_t ppid = getppid();
    if (ppid > 1) kill(ppid, SIGKILL);
#else
    if (!isatty(STDIN_FILENO)) return;
    pid_t ppid = getppid();
    if (ppid > 1) kill(ppid, SIGKILL);  /* interactive shells ignore SIGTERM */
#endif
}

static void handle_enter(SearchResult *results, int selected, const char *query) {
    const char *file_path = results[selected].original_path;

    /* Open PDFs with the OS's default PDF viewer instead of the IDE. */
    if (strcmp(results[selected].file_type, "pdf") == 0) {
        open_with_default_app(file_path);
        close_terminal();
        return;
    }

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
    int line = search_find_line(file_path, query);
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
    update_files();

    if (!is_valid_search(argc)) { print_help(); return; }

    char raw_query[256] = {0};
    char *query = build_query(argc, argv, raw_query, sizeof(raw_query));

    int count;
    SearchResult *results = search(query, raw_query, &count);

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
}

static void handle_list_enter(IndexEntry *entries, int selected) {
    const char *file_path = entries[selected].original_path;

    /* Open PDFs with the OS's default PDF viewer instead of the IDE. */
    if (strcmp(entries[selected].file_type, "pdf") == 0) {
        open_with_default_app(file_path);
        close_terminal();
        return;
    }

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

static int compare_by_path(const void *a, const void *b) {
    const IndexEntry *ea = (const IndexEntry *)a;
    const IndexEntry *eb = (const IndexEntry *)b;
    return strcmp(ea->original_path, eb->original_path);
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

    /* List folders first. */
    qsort(entries, count, sizeof(IndexEntry), compare_by_path);
    
    int chosen = run_list_picker(entries, count, "Browse indexed files", NULL);
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1)
        handle_list_enter(entries, chosen);
    free(entries);
}

static void cmd_remove(int argc, char *argv[]) {
    /* Direct form: remove by path (scriptable, backward compatible). */
    if (argc == 3) { remove_path(argv[2]); return; }
    if (argc != 2) { print_help(); return; }

    /* Interactive form: pick a file from the index to remove. */
    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (!entries || count == 0) {
        printf("No files indexed yet.\n");
        free(entries);
        return;
    }

    qsort(entries, count, sizeof(IndexEntry), compare_by_path);

    int chosen = run_list_picker(entries, count, "Remove a file", NULL);
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
    return (argc == 3 || argc == 4) && strcmp(argv[2], "ide") == 0;
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

static void launch_workspace(const Workspace *ws) {
    printf("Opening '%s' (%d app%s)...\n",
           ws->name, ws->entry_count, ws->entry_count == 1 ? "" : "s");
#ifdef _WIN32
    /* Each entry represents one app instance (one window). For UWP/IDEs, launch
       once per target. For browsers, all targets open as tabs in one window. */
    for (int i = 0; i < ws->entry_count; i++) {
        const char *app = ws->entries[i].app;
        int tc = ws->entries[i].target_count;

        if (is_uwp_app(app) || is_new_window_app(app)) {
            /* UWP and IDE apps: launch once per target (or once standalone). */
            if (tc == 0) {
                app_launch(app, "");
            } else {
                for (int k = 0; k < tc; k++)
                    app_launch(app, ws->entries[i].targets[k]);
            }
        } else {
            /* Regular executable / .lnk (e.g. browsers): each entry is one window.
               Single target: plain launch. Multiple targets: --new-window url1 url2 ...
               so the browser opens them all as tabs in one new window. */
            if (tc == 0) {
                app_launch(app, "");
            } else if (tc == 1) {
                app_launch(app, ws->entries[i].targets[0]);
            } else {
                size_t cap = (size_t)tc * (WORKSPACE_TARGET_MAX + 4) + 16;
                char *params = malloc(cap);
                if (params == NULL) { fprintf(stderr, "error: out of memory\n"); continue; }
                int pos = snprintf(params, cap, "--new-window");
                for (int k = 0; k < tc && pos < (int)cap - 1; k++)
                    pos += snprintf(params + pos, cap - pos,
                                    " \"%s\"", ws->entries[i].targets[k]);
                SHELLEXECUTEINFOA sei = {0};
                sei.cbSize       = sizeof(sei);
                sei.lpVerb       = "open";
                sei.lpFile       = app;
                sei.lpParameters = params;
                sei.nShow        = SW_SHOWNORMAL;
                if (!ShellExecuteExA(&sei))
                    fprintf(stderr, "error: failed to launch '%s'\n", app);
                free(params);
            }
        }
    }
#else
    /* Each entry is one app instance. For IDEs, launch once per target.
       For other apps, pass all targets in one invocation. */
    for (int i = 0; i < ws->entry_count; i++) {
        const char *app = ws->entries[i].app;
        int tc = ws->entries[i].target_count;

#if defined(__APPLE__)
        if (is_new_window_app(app)) {
            /* IDEs: one launch per target (or once standalone). */
            if (tc == 0) {
                char cmd[WORKSPACE_APP_MAX + 16];
                snprintf(cmd, sizeof(cmd), "%s --new-window", app);
                system(cmd);
            } else {
                for (int k = 0; k < tc; k++) {
                    char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 16];
                    snprintf(cmd, sizeof(cmd), "%s --new-window \"%s\"",
                             app, ws->entries[i].targets[k]);
                    system(cmd);
                }
            }
        } else {
            /* GUI apps via 'open -a': pass all targets together (one window). */
            size_t cap = WORKSPACE_APP_MAX + (size_t)tc * (WORKSPACE_TARGET_MAX + 4) + 16;
            char *cmd = malloc(cap);
            if (cmd == NULL) { fprintf(stderr, "error: out of memory\n"); continue; }
            int pos = snprintf(cmd, cap, "open -a \"%s\"", app);
            for (int k = 0; k < tc && pos < (int)cap - 1; k++)
                pos += snprintf(cmd + pos, cap - pos,
                                " \"%s\"", ws->entries[i].targets[k]);
            system(cmd);
            free(cmd);
        }
#else
        if (is_new_window_app(app)) {
            /* IDEs: one launch per target (or once standalone). */
            if (tc == 0) {
                char cmd[WORKSPACE_APP_MAX + 16];
                snprintf(cmd, sizeof(cmd), "%s --new-window", app);
                system(cmd);
            } else {
                for (int k = 0; k < tc; k++) {
                    char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 16];
                    snprintf(cmd, sizeof(cmd), "%s --new-window \"%s\"",
                             app, ws->entries[i].targets[k]);
                    system(cmd);
                }
            }
        } else {
            /* GUI app: pass all targets in one invocation, backgrounded. */
            size_t cap = WORKSPACE_APP_MAX + (size_t)tc * (WORKSPACE_TARGET_MAX + 4) + 16;
            char *cmd = malloc(cap);
            if (cmd == NULL) { fprintf(stderr, "error: out of memory\n"); continue; }
            int pos = snprintf(cmd, cap, "\"%s\"", app);
            for (int k = 0; k < tc && pos < (int)cap - 1; k++)
                pos += snprintf(cmd + pos, cap - pos,
                                " \"%s\"", ws->entries[i].targets[k]);
            pos += snprintf(cmd + pos, cap - pos, " &");
            system(cmd);
            free(cmd);
        }
#endif
    }
#endif
    close_terminal();
}

static void cmd_open_run(void) {
    int count;
    Workspace *ws = workspace_load_all(&count);
    if (!ws || count == 0) {
        printf("No workspaces yet. Create one with: mn open create <name>\n");
        workspace_free_all(ws, count);
        return;
    }
    int chosen = run_workspace_picker(ws, count, "Open a workspace", NULL);
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1)
        launch_workspace(&ws[chosen]);
    workspace_free_all(ws, count);
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

/* Frees the heap lists each WsEditorApp owns, then the array (over full capacity;
   the no-op on NULL/zeroed slots makes this safe regardless of editor_count). */
static void free_editor_apps(WsEditorApp *apps, int cap) {
    if (apps == NULL) return;
    for (int i = 0; i < cap; i++) {
        targetlist_free(&apps[i].existing_links.items, &apps[i].existing_links.cap,
                        &apps[i].existing_links.count);
        targetlist_free(&apps[i].new_links.items, &apps[i].new_links.cap,
                        &apps[i].new_links.count);
        free(apps[i].existing_del);
        free(apps[i].existing_pos);
    }
    free(apps);
}

/* Frees each AppLinks' backing list, then the array. */
static void free_app_links(AppLinks *links, int n) {
    if (links == NULL) return;
    for (int i = 0; i < n; i++)
        targetlist_free(&links[i].items, &links[i].cap, &links[i].count);
    free(links);
}

static void cmd_open_edit(void) {
    int count;
    Workspace *ws = workspace_load_all(&count);
    if (!ws || count == 0) {
        printf("No workspaces yet. Create one with: mn open create <name>\n");
        workspace_free_all(ws, count);
        return;
    }

    /* Step 1: pick workspace */
    int ws_idx = run_workspace_picker(ws, count, "Edit which workspace?", NULL);
    if (ws_idx == -1) {
        printf(ANSI_CLEAR ANSI_RESET "Cancelled.\n");
        workspace_free_all(ws, count);
        return;
    }

    /* Step 2: build workspace editor state from existing entries, one row per entry.
       calloc zeroes every slot, so unused link lists start as {NULL,0,0}. */
    WsEditorApp *editor_apps = calloc(WORKSPACE_ENTRIES_MAX, sizeof(WsEditorApp));
    if (!editor_apps) {
        fprintf(stderr, "error: out of memory\n");
        workspace_free_all(ws, count);
        return;
    }
    int editor_count = 0;

    /* One editor row per stored entry so instances of the same app stay separate. */
    const Workspace *chosen_ws = &ws[ws_idx];
    for (int i = 0; i < chosen_ws->entry_count && editor_count < WORKSPACE_ENTRIES_MAX; i++) {
        WsEditorApp *e = &editor_apps[editor_count++];
        const WorkspaceEntry *src = &chosen_ws->entries[i];
        strncpy(e->app, src->app, WORKSPACE_APP_MAX - 1);
        ws_display_name(src->app, e->display, sizeof(e->display));
        for (int k = 0; k < src->target_count; k++)
            targetlist_push(&e->existing_links.items, &e->existing_links.cap,
                            &e->existing_links.count, src->targets[k]);
        if (e->existing_links.count > 0) {
            e->existing_del = calloc(e->existing_links.count, sizeof(int));
            e->existing_pos = malloc(e->existing_links.count * sizeof(int));
            if (e->existing_pos)
                for (int k = 0; k < e->existing_links.count; k++)
                    e->existing_pos[k] = k;
        }
    }

    /* Step 3: run the editor — Esc cancels, Enter confirms. */
    int delete_ws = 0;
    if (!run_workspace_edit_picker(chosen_ws->name, editor_apps, &editor_count,
                                   WORKSPACE_ENTRIES_MAX, &delete_ws)) {
        printf(ANSI_CLEAR ANSI_RESET "Cancelled.\n");
        free_editor_apps(editor_apps, WORKSPACE_ENTRIES_MAX);
        workspace_free_all(ws, count);
        return;
    }

    /* Step 4: commit staged changes. */
    printf(ANSI_CLEAR ANSI_RESET);
    char ws_name[WORKSPACE_NAME_MAX];
    strncpy(ws_name, chosen_ws->name, sizeof(ws_name) - 1);
    ws_name[sizeof(ws_name) - 1] = '\0';

    if (delete_ws) {
        int rc = workspace_remove(ws_name);
        if (rc == 0)
            printf("Workspace '%s' removed.\n", ws_name);
        else
            fprintf(stderr, "error: failed to remove workspace '%s'\n", ws_name);
        free_editor_apps(editor_apps, WORKSPACE_ENTRIES_MAX);
        workspace_free_all(ws, count);
        return;
    }

    /* Rebuild the chosen workspace's entries in place from the editor state, then
       save the whole file. A full rebuild is index-safe and covers all four
       operations: adding/removing apps and adding/removing individual links. */
    Workspace *w = &ws[ws_idx];
    /* The loaded entries own heap targets — free them before overwriting. */
    for (int j = 0; j < w->entry_count; j++)
        targetlist_free(&w->entries[j].targets, &w->entries[j].target_cap,
                        &w->entries[j].target_count);
    int ec = 0;
    for (int i = 0; i < editor_count; i++) {
        const WsEditorApp *a = &editor_apps[i];
        if (a->marked_delete) continue;                 /* app staged for removal */
        WorkspaceEntry *e = &w->entries[ec++];
        strncpy(e->app, a->app, WORKSPACE_APP_MAX - 1);
        e->app[WORKSPACE_APP_MAX - 1] = '\0';
        /* dest slot may be beyond the old count (garbage) → init before pushing. */
        e->targets = NULL; e->target_count = 0; e->target_cap = 0;
        for (int k = 0; k < a->existing_links.count; k++)
            if (!a->existing_del[k])
                targetlist_push(&e->targets, &e->target_cap, &e->target_count,
                                a->existing_links.items[k]);
        for (int k = 0; k < a->new_links.count; k++)
            targetlist_push(&e->targets, &e->target_cap, &e->target_count,
                            a->new_links.items[k]);
    }
    w->entry_count = ec;

    if (workspace_save_all(ws, count) == 0)
        printf("Saved workspace '%s'.\n", ws_name);
    else
        fprintf(stderr, "error: failed to save workspace '%s'\n", ws_name);

    free_editor_apps(editor_apps, WORKSPACE_ENTRIES_MAX);
    workspace_free_all(ws, count);
}

static void cmd_open_snap(void) {
    RunningApp apps[WORKSPACE_ENTRIES_MAX];
    int n = app_enum_running(apps, WORKSPACE_ENTRIES_MAX);
    if (n < 0) {
#ifdef __linux__
        fprintf(stderr, "error: snapshot needs 'wmctrl' — install it via your package manager\n");
#else
        fprintf(stderr, "error: failed to read running apps on this platform\n");
#endif
        return;
    }
    if (n == 0) {
        printf("No running apps detected.\n");
        return;
    }

    const char *labels[WORKSPACE_ENTRIES_MAX];
    char label_bufs[WORKSPACE_ENTRIES_MAX][256];
    int selected[WORKSPACE_ENTRIES_MAX];
    AppLinks *links = calloc(n, sizeof(AppLinks));
    if (links == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        app_display_token(apps[i].app, label_bufs[i], sizeof(label_bufs[i]));
        labels[i] = label_bufs[i];
        selected[i] = 1;
        if (apps[i].target[0] != '\0')
            targetlist_push(&links[i].items, &links[i].cap,
                            &links[i].count, apps[i].target);
    }

    char name[WORKSPACE_NAME_MAX] = {0};
    int name_taken = 0;

    /* Two-step flow; Esc steps back (and cancels at the review step). */
    enum { SNAP_REVIEW, SNAP_NAME } state = SNAP_REVIEW;
    for (;;) {
        if (state == SNAP_REVIEW) {
            if (!run_multiselect_picker("Snapshot running apps",
                                        "Pick apps; press any key on a green app to add a link.",
                                        labels, n, selected, links)) {
                printf(ANSI_CLEAR ANSI_RESET "Cancelled.\n");
                free_app_links(links, n);
                return;
            }
            int any = 0;
            for (int i = 0; i < n; i++) any += selected[i];
            if (!any) {
                printf(ANSI_CLEAR ANSI_RESET "Nothing selected.\n");
                free_app_links(links, n);
                return;
            }
            state = SNAP_NAME;
        } else { /* SNAP_NAME */
            const char *subtitle = name_taken
                ? "\033[33mthat name already exists — pick another.\033[0m"
                : "Name for the new workspace.";
            if (!run_text_input("Snapshot workspace", subtitle, "Name",
                                name, sizeof(name), 0)) {
                state = SNAP_REVIEW;   /* Esc → back to the checklist */
                name_taken = 0;
                continue;
            }
            int rc = workspace_create(name);
            if (rc == -1) { name_taken = 1; continue; }   /* exists → re-prompt */
            if (rc != 0) {
                printf(ANSI_CLEAR ANSI_RESET);
                fprintf(stderr, "error: failed to create workspace\n");
                free_app_links(links, n);
                return;
            }
            break;
        }
    }

    int added = 0;
    for (int i = 0; i < n; i++) {
        if (!selected[i]) continue;
        int rc;
        if (links[i].count == 0) {
            rc = workspace_add_entry(name, apps[i].app, "");
        } else {
            /* All links for this app instance go into one grouped entry. */
            rc = workspace_add_entry_with_targets(name, apps[i].app,
                                                  links[i].items, links[i].count);
        }
        if (rc == 0) added++;
    }
    free_app_links(links, n);
    printf(ANSI_CLEAR ANSI_RESET);
    printf("Created workspace '%s' with %d app%s.\n",
           name, added, added == 1 ? "" : "s");
}

static void cmd_open(int argc, char *argv[]) {
    if (argc == 2)                                       { cmd_open_run();           return; }
    if (argc == 4 && strcmp(argv[2], "create") == 0)     { cmd_open_create(argv[3]); return; }
    if (argc == 3 && strcmp(argv[2], "edit")   == 0)     { cmd_open_edit();          return; }
    if (argc == 3 && strcmp(argv[2], "snap")   == 0)     { cmd_open_snap();          return; }
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
