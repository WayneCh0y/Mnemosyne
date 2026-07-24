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
#include <fcntl.h>
#endif

#include "command_handler.h"
#include "help.h"
#include "ingest.h"
#include "index.h"
#include "search.h"
#include "config.h"
#include "remove.h"
#include "reindex.h"
#include "relocate.h"
#include "picker.h"
#include "workspace.h"
#include "app_resolve.h"
#include "app_launch.h"
#include "tokenizer.h"
#include "theme.h"

static int is_valid_add(int argc) { return argc == 3; }

static void cmd_add(int argc, char *argv[]) {
    if (!is_valid_add(argc)) { print_help(); return; }
    ingest_path(argv[2]);
}

/* Updates the index if a file has been modified, moved, or deleted. */
static void update_files(void) {
    /* Missing files: try to relocate within their repository; drop otherwise. */
    relocate_scan_all();

    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (entries == NULL) return;

    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(entries[i].original_path, &st) == 0 &&
            (long)st.st_mtime > entries[i].last_modified)
            ingest_file(entries[i].original_path);
    }

    free(entries);
}

/* Parses `mn search`'s argv into a query string plus its flags:
     -c            → *is_case_sensitive = 1
     --top N       → *top_n = N (positive integer); *invalid_top = 1 on missing
                     or non-positive N so the caller can bail with an error.
   Everything else is joined into the query string. `top_n` is left at 0
   when the flag is absent, meaning "no cap". */
static char* build_query(int argc, char *argv[], char *raw_out, int raw_size,
                          int *is_case_sensitive, int *top_n, int *invalid_top) {
    static char query[256];
    query[0] = '\0';
    *is_case_sensitive = 0;
    *top_n   = 0;
    *invalid_top = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            *is_case_sensitive = 1;
            continue;
        }
        if (strcmp(argv[i], "--top") == 0) {
            if (i + 1 >= argc) { *invalid_top = 1; continue; }
            /* strtol (not atoi) so trailing garbage — `--top 5x`, `--top 5run` —
               is rejected rather than silently truncated to the leading digits. */
            char *end = NULL;
            long n = strtol(argv[i + 1], &end, 10);
            if (end == argv[i + 1] || *end != '\0' || n <= 0) *invalid_top = 1;
            else                                              *top_n = (int)n;
            i++;  /* consume the number arg regardless */
            continue;
        }
        strncat(query, argv[i], sizeof(query) - strlen(query) - 1);
        if (i < argc - 1) strncat(query, " ", sizeof(query) - strlen(query) - 1);
    }

    /* trim trailing space if the last arg was a flag */
    int qend = (int)strlen(query);
    if (qend > 0 && query[qend - 1] == ' ') query[qend - 1] = '\0';

    if (raw_out) {
        strncpy(raw_out, query, raw_size - 1);
        for (int i = 0; raw_out[i]; i++)
            if (raw_out[i] == '\\') raw_out[i] = '/';
    }

    if (!*is_case_sensitive) {
        for (int i = 0; query[i]; i++)
            query[i] = (char)tolower((unsigned char)query[i]);
    }

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
        /* Close the Terminal window hosting this tty via AppleScript, working
           regardless of the "close if shell exited cleanly" preference (which
           would otherwise keep the window open after a SIGKILL).

           Doing the close while mn is still running makes Terminal prompt
           "terminate running processes in this window?". So we hand it to a
           detached helper: setsid() + /dev/null fds drop the controlling tty so
           Terminal no longer counts it as a window process; it waits briefly for
           mn and the shell to exit, then closes the now-idle window with no
           prompt. */
        char script[512];
        snprintf(script, sizeof(script),
            "sleep 0.4; osascript"
            " -e 'tell application \"Terminal\"'"
            " -e 'repeat with w in windows'"
            " -e 'if tty of front tab of w is \"%s\" then close w'"
            " -e 'end repeat'"
            " -e 'end tell'"
            " 2>/dev/null",
            tty);
        pid_t helper = fork();
        if (helper == 0) {
            setsid();
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > STDERR_FILENO) close(devnull);
            }
            execl("/bin/sh", "sh", "-c", script, (char *)NULL);
            _exit(127);
        }
    }
    pid_t ppid = getppid();
    if (ppid > 1) kill(ppid, SIGKILL);
#else
    if (!isatty(STDIN_FILENO)) return;
    pid_t ppid = getppid();
    if (ppid > 1) kill(ppid, SIGKILL);  /* interactive shells ignore SIGTERM */
#endif
}

static void handle_enter(SearchResult *results, int selected, const char *query, int is_case_sensitive) {
    const char *file_path = results[selected].original_path;

    /* Open PDFs with the OS's default PDF viewer instead of the IDE. Jump to
       the match's page when the viewer supports it; the page is printed either
       way so the user always has a manual fallback (⌥⌘G / Ctrl+G). */
    if (strcmp(results[selected].file_type, "pdf") == 0) {
        int page = results[selected].page > 0 ? results[selected].page : 1;
        ui_info("Opening '%s' at page %d", file_path, page);
        open_pdf_at_page(file_path, page);
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
    int line = search_find_line(file_path, query, is_case_sensitive);
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

    char raw_query[256] = {0};
    int is_case_sensitive = 0;
    int top_n = 0, invalid_top = 0;
    char *query = build_query(argc, argv, raw_query, sizeof(raw_query),
                              &is_case_sensitive, &top_n, &invalid_top);

    if (invalid_top) {
        ui_err("--top requires a positive integer, e.g. --top 5");
        return;
    }
    if (query[0]=='\0') { print_help(); return; }

    int count;
    SearchResult *results = search(query, raw_query, &count, is_case_sensitive);

    if (count == 0) {
        ui_info("No matches found.");
        ui_hint("Try fewer or different words, or add -c for case-sensitive search.");
        free(results);
        return;
    }

    /* --top N: results is already sorted by BM25 score (highest first) in
       search(), so the top-N slice is just the first N entries. */
    if (top_n > 0 && count > top_n) count = top_n;

    int chosen = run_search_picker(results, count);
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1)
        handle_enter(results, chosen, query, is_case_sensitive);
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
        ui_info("No files indexed yet.");
        ui_hint("Add one with: mn add <file>");
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
        ui_info("No files indexed yet.");
        ui_hint("Add one with: mn add <file>");
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
            ui_ok("Removed '%s' from the index.", entries[chosen].original_path);
        else if (rc == 0)
            ui_warn("'%s' was not indexed", entries[chosen].original_path);
        /* rc == -1: index_remove already printed the error */
    }
    free(entries);
}

static void cmd_reindex(int argc, char *argv[]) {
    (void)argv;
    if (argc != 2) { print_help(); return; }
    reindex_all();
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
                ui_ok("Default IDE updated to: %s", ide_list[chosen]);
            }
        }
    } else {
        if (set_ide(argv[3]) == 0) {
            ui_ok("Default IDE updated to: %s", argv[3]);
        }
    }
}

static void launch_workspace(const Workspace *ws) {
    ui_info("Opening '%s' (%d app%s)...",
            ws->name, ws->entry_count, ws->entry_count == 1 ? "" : "s");
#ifdef _WIN32
    /* Each entry represents one app instance (one window). For UWP/IDEs, launch
       once per target. For browsers, all targets open as tabs in one window. */
    for (int i = 0; i < ws->entry_count; i++) {
        const char *app = ws->entries[i].app;
        int tc = ws->entries[i].target_count;
        const char *layout = ws->entries[i].layout;

        if (is_uwp_app(app) || is_new_window_app(app)) {
            /* UWP and IDE apps: launch once per target (or once standalone). */
            if (tc == 0) {
                app_launch(app, "", layout);
            } else {
                for (int k = 0; k < tc; k++)
                    app_launch(app, ws->entries[i].targets[k], layout);
            }
        } else {
            /* Regular executable / .lnk (e.g. browsers): each entry is one window.
               A single non-web target opens directly in the app. Web URLs (one or
               many) go through `app --new-window <urls>` so each entry gets its OWN
               window we can position — otherwise a URL opened via the OS handler
               becomes a tab in an existing window, ignoring app + layout, and two
               URL entries can't occupy two partitions. */
            const char *t0 = (tc >= 1) ? ws->entries[i].targets[0] : "";
            int web = (strncmp(t0, "http://", 7) == 0 || strncmp(t0, "https://", 8) == 0);
            if (tc == 0) {
                app_launch(app, "", layout);
            } else if (tc == 1 && !web) {
                app_launch(app, ws->entries[i].targets[0], layout);
            } else {
                size_t cap = (size_t)tc * (WORKSPACE_TARGET_MAX + 4) + 16;
                char *params = malloc(cap);
                if (params == NULL) { ui_err("out of memory"); continue; }
                int pos = snprintf(params, cap, "--new-window");
                for (int k = 0; k < tc && pos < (int)cap - 1; k++)
                    pos += snprintf(params + pos, cap - pos,
                                    " \"%s\"", ws->entries[i].targets[k]);
                int   running = win_app_running(app);   /* before our launch */
                void *before  = win_capture_before();
                SHELLEXECUTEINFOA sei = {0};
                sei.cbSize       = sizeof(sei);
                sei.lpVerb       = "open";
                sei.lpFile       = app;
                sei.lpParameters = params;
                sei.nShow        = SW_SHOWNORMAL;
                if (!ShellExecuteExA(&sei))
                    ui_err("failed to launch '%s'", app);
                win_place_new(before, layout, running);
                free(params);
            }
        }
    }
#else
    /* Each entry is one app instance. For IDEs, launch once per target.
       For other apps, pass all targets in one invocation. */
    for (int i = 0; i < ws->entry_count; i++) {
        /* These branches shell out directly rather than through app_launch, so
           they canonicalise the app name themselves: a workspace saved before
           that was enforced can hold "Code" where PATH has "code". */
        const char *app = ws->entries[i].app;
        const char *launcher = new_window_launcher(app);
        if (launcher != NULL) app = launcher;
        int tc = ws->entries[i].target_count;

#if defined(__APPLE__)
        const char *layout = ws->entries[i].layout;
        if (is_new_window_app(app)) {
            /* IDEs: one launch per target (or once standalone). Every launch
               opens its own window, so each is placed individually — sampling
               the window count first so placement waits for the window this
               launch adds rather than one the IDE already had open. This mirrors
               the per-launch placement the Windows branch gets from app_launch. */
            if (tc == 0) {
                int prior = layout[0] ? mac_window_count(app) : 0;
                char cmd[WORKSPACE_APP_MAX + 16];
                snprintf(cmd, sizeof(cmd), "%s --new-window", app);
                system(cmd);
                if (layout[0]) mac_place_window(app, layout, prior);
            } else {
                for (int k = 0; k < tc; k++) {
                    int prior = layout[0] ? mac_window_count(app) : 0;
                    char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 16];
                    snprintf(cmd, sizeof(cmd), "%s --new-window \"%s\"",
                             app, ws->entries[i].targets[k]);
                    system(cmd);
                    if (layout[0]) mac_place_window(app, layout, prior);
                }
            }
        } else {
            /* GUI apps via 'open -a': pass all targets together (one window). */
            size_t cap = WORKSPACE_APP_MAX + (size_t)tc * (WORKSPACE_TARGET_MAX + 4) + 16;
            char *cmd = malloc(cap);
            if (cmd == NULL) { ui_err("out of memory"); continue; }
            int pos = snprintf(cmd, cap, "open -a \"%s\"", app);
            for (int k = 0; k < tc && pos < (int)cap - 1; k++)
                pos += snprintf(cmd + pos, cap - pos,
                                " \"%s\"", ws->entries[i].targets[k]);
            system(cmd);
            free(cmd);
            /* Snap into the assigned partition. `open -a` reuses a running app's
               window rather than adding one, so there is no new window to wait
               for: place the front one. */
            if (layout[0]) mac_place_window(app, layout, 0);
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
            if (cmd == NULL) { ui_err("out of memory"); continue; }
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
    WorkspaceStore st;
    workspace_store_load(&st);
    if (st.ws_count == 0) {
        ui_info("No workspaces yet.");
        ui_hint("Make one with: mn open edit, then /create or /snap");
        workspace_store_free(&st);
        return;
    }
    /* Browse only: folders are organised in `mn open edit`, so nothing here can
       write to the store. */
    char cwd[WORKSPACE_FOLDER_MAX] = "";
    int chosen = run_workspace_browser(&st, "Open a workspace", 0, NULL,
                                       cwd, sizeof(cwd));
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1)
        launch_workspace(&st.ws[chosen]);
    workspace_store_free(&st);
}

/* Frees the heap list each WsEditorApp owns, then the array (over full capacity;
   the no-op on NULL/zeroed slots makes this safe regardless of editor_count). */
static void free_editor_apps(WsEditorApp *apps, int cap) {
    if (apps == NULL) return;
    for (int i = 0; i < cap; i++)
        editlink_free(&apps[i].links);
    free(apps);
}

/* Builds workspace editor state from a workspace's stored entries: one editor row
   per entry so instances of the same app stay separate. apps must point at a
   zeroed WORKSPACE_ENTRIES_MAX array (unused link lists start as {NULL,0,0}); the
   row count loaded is returned in *count. */
static void load_editor_apps(const Workspace *ws, WsEditorApp *apps, int *count) {
    int n = 0;
    for (int i = 0; i < ws->entry_count && n < WORKSPACE_ENTRIES_MAX; i++) {
        WsEditorApp *e = &apps[n++];
        const WorkspaceEntry *src = &ws->entries[i];
        strncpy(e->app, src->app, WORKSPACE_APP_MAX - 1);
        ws_display_name(src->app, e->display, sizeof(e->display));
        strncpy(e->layout, src->layout, sizeof(e->layout) - 1);
        e->layout[sizeof(e->layout) - 1] = '\0';
        /* remember the stored placement so the editor can show it as changed */
        memcpy(e->orig_layout, e->layout, sizeof(e->orig_layout));
        for (int k = 0; k < src->target_count; k++)
            editlink_push(&e->links, src->targets[k], src->targets[k], k);
    }
    *count = n;
}

/* Rebuilds workspace ws_idx's stored entries in place from the editor state, then
   writes the whole store to disk. Applies any rename to the workspace name and
   drops apps/links staged for removal. A full rebuild is index-safe and covers
   adding/removing apps and adding/removing individual links. Returns the
   workspace_store_save result (0 on success). */
static int commit_editor_apps(WorkspaceStore *st, int ws_idx, const char *ws_name,
                              const WsEditorApp *apps, int count) {
    Workspace *w = &st->ws[ws_idx];
    strncpy(w->name, ws_name, WORKSPACE_NAME_MAX - 1);
    w->name[WORKSPACE_NAME_MAX - 1] = '\0';
    /* The loaded entries own heap targets — free them before overwriting. */
    for (int j = 0; j < w->entry_count; j++)
        targetlist_free(&w->entries[j].targets, &w->entries[j].target_cap,
                        &w->entries[j].target_count);
    int ec = 0;
    for (int i = 0; i < count; i++) {
        const WsEditorApp *a = &apps[i];
        if (a->marked_delete) continue;                 /* app staged for removal */
        WorkspaceEntry *e = &w->entries[ec++];
        strncpy(e->app, a->app, WORKSPACE_APP_MAX - 1);
        e->app[WORKSPACE_APP_MAX - 1] = '\0';
        strncpy(e->layout, a->layout, sizeof(e->layout) - 1);
        e->layout[sizeof(e->layout) - 1] = '\0';
        /* dest slot may be beyond the old count (garbage) → init before pushing. */
        e->targets = NULL; e->target_count = 0; e->target_cap = 0;
        for (int k = 0; k < a->links.count; k++)
            if (!a->links.items[k].deleted)
                targetlist_push(&e->targets, &e->target_cap, &e->target_count,
                                a->links.items[k].text);
    }
    w->entry_count = ec;
    return workspace_store_save(st);
}

/* Severity of a deferred workspace-session message. The browse/edit session runs
   on the alternate screen buffer, so its closing message can't be printed until
   the buffer is torn down (or it would be wiped with it); each exit path stages
   the text and one of these, and cmd_open_edit prints it once, back on the real
   terminal. */
enum { PEND_NONE = 0, PEND_OK, PEND_ERR, PEND_INFO };

/* Edits one workspace: builds the editor state, runs the editor once, and returns
   how it ended. /save commits the staged changes and returns to the browser
   (WSEDIT_SAVE); /back discards and returns to the browser (WSEDIT_BACK); /exit
   discards and returns to the terminal (WSEDIT_EXIT). Only a save write-failure
   stages a message here (into msg / *msg_kind), downgrading the outcome to
   WSEDIT_EXIT; the caller composes the ordinary exit message. Renaming and
   deleting the workspace itself are the browser's job now, so this touches only the
   apps and links inside it. */
static int edit_one_workspace(WorkspaceStore *st, int ws_idx,
                              char *msg, size_t msg_sz, int *msg_kind) {
    /* Build workspace editor state. calloc zeroes every slot so unused link lists
       start as {NULL,0,0}. */
    WsEditorApp *editor_apps = calloc(WORKSPACE_ENTRIES_MAX, sizeof(WsEditorApp));
    if (!editor_apps) {
        snprintf(msg, msg_sz, "out of memory");
        *msg_kind = PEND_ERR;
        return WSEDIT_EXIT;
    }
    int editor_count = 0;
    load_editor_apps(&st->ws[ws_idx], editor_apps, &editor_count);

    /* A private copy of the name for the title and the commit: commit_editor_apps
       writes it back into the same workspace, so it must not alias that buffer. */
    char ws_name[WORKSPACE_NAME_MAX];
    snprintf(ws_name, sizeof(ws_name), "%s", st->ws[ws_idx].name);

    int outcome = run_workspace_edit_picker(ws_name, editor_apps, &editor_count,
                                            WORKSPACE_ENTRIES_MAX);

    if (outcome == WSEDIT_SAVE &&
        commit_editor_apps(st, ws_idx, ws_name, editor_apps, editor_count) != 0) {
        snprintf(msg, msg_sz, "failed to save workspace '%s'", ws_name);
        *msg_kind = PEND_ERR;
        outcome = WSEDIT_EXIT;   /* bail to the terminal on a write failure */
    }

    free_editor_apps(editor_apps, WORKSPACE_ENTRIES_MAX);
    return outcome;
}

static void cmd_open_edit(void) {
    WorkspaceStore st;
    workspace_store_load(&st);
    /* No early return on an empty store: /create and /snap live inside the
       browser, so an empty store is exactly the state you enter this screen to
       fix. Bailing out here would leave no way to make the first workspace. */

    /* Browse and edit alternate until something ends the session: the editor's
       /back returns here rather than to the terminal, so filing a workspace away
       and then editing it is one trip through `mn open edit`. cwd lives out here
       so /back reopens the folder you drilled into, not the top level. */
    char cwd[WORKSPACE_FOLDER_MAX] = "";

    /* Hold the alternate screen for the whole session: the browser and editor
       alternate, and without an outer bracket each hand-off would leave and
       re-enter the alt buffer, flashing the shell in between. One enter/leave
       here keeps the buffer up throughout; the inner pickers' own (depth-counted)
       brackets become no-ops. The closing message is deferred to a single slot
       and printed after the buffer is torn down, so it lands on the real
       terminal rather than being wiped with the alt screen. */
    picker_alt_enter();
    int  pend_kind = PEND_NONE;
    char pend_msg[256] = "";
    int  changed = 0;   /* any store change this session — folder, workspace, or edit */

    for (;;) {
        int dirty = 0;
        int ws_idx = run_workspace_browser(&st, "Edit which workspace?", 1, &dirty,
                                           cwd, sizeof(cwd));

        /* Browser edits (new folders, moves, reorders, workspace renames/deletes)
           are committed on the way out however the browse ended: a session's work
           is not a cancellation of it. */
        if (dirty) {
            changed = 1;
            if (workspace_store_save(&st) != 0) {
                snprintf(pend_msg, sizeof(pend_msg), "failed to save changes");
                pend_kind = PEND_ERR;
                break;
            }
        }

        if (ws_idx == -1) {   /* /exit from the browser */
            if (changed) { snprintf(pend_msg, sizeof(pend_msg), "Changes saved."); pend_kind = PEND_OK; }
            else         { snprintf(pend_msg, sizeof(pend_msg), "Cancelled.");      pend_kind = PEND_INFO; }
            break;
        }

        /* /save and /back both return to the browser; /save committed first. Only
           /exit (or a save failure) leaves for the terminal. */
        int oc = edit_one_workspace(&st, ws_idx, pend_msg, sizeof(pend_msg), &pend_kind);
        if (oc == WSEDIT_SAVE) { changed = 1; continue; }
        if (oc == WSEDIT_BACK) continue;

        if (pend_kind == PEND_NONE) {   /* WSEDIT_EXIT with no error staged */
            if (changed) { snprintf(pend_msg, sizeof(pend_msg), "Changes saved."); pend_kind = PEND_OK; }
            else         { snprintf(pend_msg, sizeof(pend_msg), "Cancelled.");      pend_kind = PEND_INFO; }
        }
        break;
    }

    picker_alt_leave();
    switch (pend_kind) {
    case PEND_OK:   ui_ok("%s", pend_msg);   break;
    case PEND_ERR:  ui_err("%s", pend_msg);  break;
    case PEND_INFO: ui_info("%s", pend_msg); break;
    default: break;
    }
    workspace_store_free(&st);
}

/* `mn open` runs a workspace, `mn open edit` organises them, and that is the
   whole surface. Creating and snapshotting used to be `mn open create <name>` and
   `mn open snap`, but both make a workspace *somewhere*, and argv has no way to
   say where: they landed at the root and had to be filed afterwards. They are
   /create and /snap inside the browser now, where the folder you are looking at
   is the answer. */
static void cmd_open(int argc, char *argv[]) {
    if (argc == 2)                                   { cmd_open_run();  return; }
    if (argc == 3 && strcmp(argv[2], "edit") == 0)   { cmd_open_edit(); return; }
    print_help();
}

void handle_command(int argc, char *argv[]) {
    const char *cmd = argv[1];

    if (strcmp(cmd, "add") == 0)         { cmd_add(argc, argv);    return; }
    if (strcmp(cmd, "search") == 0)      { cmd_search(argc, argv); return; }
    if (strcmp(cmd, "list") == 0)        { cmd_list(argc, argv);   return; }
    if (strcmp(cmd, "remove") == 0)      { cmd_remove(argc, argv); return; }
    if (strcmp(cmd, "reindex") == 0)     { cmd_reindex(argc, argv); return; }
    if (strcmp(cmd, "open") == 0)        { cmd_open(argc, argv);   return; }
    if (strcmp(cmd, "config") == 0)      { cmd_config(argc, argv); return; }
    if (strcmp(cmd, "help") == 0)        { print_help();           return; }

    fprintf(stderr, "Unknown command: %s\n", cmd);
}
