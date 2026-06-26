#ifndef PICKER_H
#define PICKER_H

#include "search.h"
#include "index.h"
#include "workspace.h"

#define CURSOR_TOKEN  "▌"

#define KEY_UP        1000
#define KEY_DOWN      1001
#define KEY_ENTER     13
#define KEY_ESC       27
#define KEY_BACKSPACE 8

#define ANSI_CLEAR       "\033[H\033[2J"
#define ANSI_RESET       "\033[0m"
#define ANSI_DIM         "\033[2m"
#define ANSI_BOLD        "\033[1m"
#define ANSI_SEL         "\033[1;38;5;196m"
#define ANSI_SEL_HL      "\033[30;42m"
#define ANSI_CURSOR_HIDE "\033[?25l"
#define ANSI_CURSOR_SHOW "\033[?25h"
#define ANSI_CYAN        "\033[36m"
#define ANSI_MAGENTA     "\033[35m"
#define ANSI_BLUE        "\033[34m"
#define ANSI_YELLOW      "\033[33m"
#define ANSI_DIM_YELLOW  "\033[2;33m"
#define ANSI_GREEN       "\033[32m"
#define ANSI_WHITE       "\033[37m"
#define ANSI_RED         "\033[31m"
#define ANSI_DEL_HL      "\033[30;41m"
#define ANSI_ADD_HL      "\033[30;42m"
#define ANSI_APP_HL      "\033[37;44m"

int read_key(void);
/* Generic numbered string-list picker. Returns the chosen index, or -1 (Esc). */
int run_menu_picker(const char *title, const char *subtitle,
                    const char **list, int display);
int run_ide_picker(const char **list, int display);
/* Per-app list of links/targets collected in the multiselect picker. */
#define SNAP_LINKS_MAX 8
typedef struct {
    char items[SNAP_LINKS_MAX][WORKSPACE_TARGET_MAX];
    int  count;
} AppLinks;

/* Multi-select checklist. selected[] (length count) is in/out: 1 = ticked.
   ↑/↓ move the cursor, which can land on an app or, for a selected app, one of
   its links. Backspace on an app toggles its tick; Backspace on a link removes
   that link immediately. Enter confirms, Esc cancels.
   If links is non-NULL (length count), pressing any printable key on a selected
   app opens an inline field to add a link (appended, up to SNAP_LINKS_MAX); links
   are shown as "→ link" lines beneath the app.
   Returns 1 if confirmed, 0 if cancelled. */
int run_multiselect_picker(const char *title, const char *subtitle,
                           const char **labels, int count, int *selected,
                           AppLinks *links);

/* Workspace editor entry: an existing stored app instance (is_new=0) or a new
   app being added this session (is_new=1). Each row in the editor represents
   exactly one workspace entry so instances of the same app stay separate.
   existing_links holds the targets already saved for this entry (shown dimmed);
   new_links holds additional links typed this session (shown green).
   Staged removals: marked_delete flags an existing app for deletion (shown red),
   and existing_del[k] flags the k-th existing link for deletion (shown red).
   New apps/links are un-staged by dropping them from the arrays. */
typedef struct {
    char app[WORKSPACE_APP_MAX];
    char display[256];
    int  is_new;
    int  marked_delete;
    AppLinks existing_links;
    int  existing_del[SNAP_LINKS_MAX];
    AppLinks new_links;
} WsEditorApp;

/* Workspace editor picker for `mn open edit`. Shows existing apps and their links;
   lets the user add links to existing apps, add new (validated) apps, and stage
   removals of apps/links (Backspace). apps[]/count are in/out: new apps are
   appended (is_new=1), new_links filled, and marked_delete/existing_del flags set.
   *delete_workspace is set to 1 if the whole workspace is staged for removal.
   Returns 1 if confirmed (Enter), 0 if cancelled (Esc). */
int run_workspace_edit_picker(const char *ws_name,
                              WsEditorApp *apps, int *count, int max,
                              int *delete_workspace);
int run_search_picker(SearchResult *results, int count);
int run_list_picker(IndexEntry *entries, int count,
                    const char *title, const char *subtitle);
/* Workspace picker: lists workspaces, expanding the selected one's apps in a
   left-rail frame. Returns the chosen index, or -1 (Esc). */
int run_workspace_picker(Workspace *ws, int count,
                         const char *title, const char *subtitle);
/* Strip directory prefix and trailing .exe from app path for a short display name. */
void ws_display_name(const char *app, char *out, size_t out_size);
/* Single styled text input box. Returns 1 with out filled, 0 if cancelled (Esc).
   When allow_empty is set, Enter on an empty value confirms (used as "skip"). */
int run_text_input(const char *title, const char *subtitle, const char *label,
                   char *out, size_t out_size, int allow_empty);

#endif
