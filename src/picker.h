#ifndef PICKER_H
#define PICKER_H

#include "search.h"
#include "index.h"
#include "workspace.h"

#define CURSOR_TOKEN  "▌"

#define KEY_UP        1000
#define KEY_DOWN      1001
#define KEY_LEFT      1002
#define KEY_RIGHT     1003
#define KEY_ENTER     13
#define KEY_ESC       27
#define KEY_BACKSPACE 8
#define KEY_TAB       9

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
#define ANSI_LIFT_HL     "\033[1;37;46m"  /* bold white on cyan — lifted/reordering link */
#define ANSI_EDIT_HL     "\033[1;37;45m"  /* bold white on magenta — link being edited */
#define ANSI_REN_HL      "\033[30;43m"    /* black on yellow — rename workspace button */
#define ANSI_REVERSE     "\033[7m"        /* reverse video — highlight block cursor */
#define ANSI_REVERSE_OFF "\033[27m"
#define ANSI_ACCENT      "\033[96m"        /* bright cyan — selection bar */
#define ANSI_BRIGHT_RED    "\033[91m"
#define ANSI_BRIGHT_GREEN  "\033[92m"
#define ANSI_BRIGHT_YELLOW "\033[93m"

/* Path-completion dropdown — a silver frame, and violet on the one selected row
   ("> name"). Everything else stays dim: the list is an overlay to glance at,
   not another highlighted control competing with the picker underneath it.
   256-colour, not 24-bit: macOS Terminal.app ignores "38;2;r;g;b" outright and
   would draw the whole dropdown uncoloured. */
#define ANSI_SILVER    "\033[38;5;250m"           /* frame  — grey  #bcbcbc */
#define ANSI_VIOLET    "\033[38;5;141m"           /* selected suggestion #af87ff */

int read_key(void);
/* Generic numbered string-list picker. Returns the chosen index, or -1 (Esc). */
int run_menu_picker(const char *title, const char *subtitle,
                    const char **list, int display);
int run_ide_picker(const char **list, int display);
/* Per-app list of links/targets collected in the multiselect picker. Backed by a
   growable fixed-width string list (see targetlist_push/remove/free); items is
   NULL until the first push. */
typedef struct {
    char (*items)[WORKSPACE_TARGET_MAX];
    int  count;
    int  cap;
} AppLinks;

/* Multi-select checklist. selected[] (length count) is in/out: 1 = ticked.
   ↑/↓ move the cursor, which can land on an app or, for a selected app, one of
   its links. Backspace on an app toggles its tick; Backspace on a link removes
   that link immediately. Enter confirms, Esc cancels.
   If links is non-NULL (length count), pressing any printable key on a selected
   app opens an inline field to add a link (appended, no fixed cap); links are
   shown as "→ link" lines beneath the app. The caller owns links and must
   targetlist_free() each before freeing the array.
   Returns 1 if confirmed, 0 if cancelled. */
int run_multiselect_picker(const char *title, const char *subtitle,
                           const char **labels, int count, int *selected,
                           AppLinks *links);

/* One link inside the workspace editor. text is the current value (what gets
   saved); orig is the value loaded from disk ("" for a link added this session).
   orig_pos is the link's original index among the entry's saved links, or -1 for
   a link added this session. deleted stages the link for removal (shown red).
   All metadata lives in the struct so reordering swaps whole entries — there are
   no parallel arrays to keep aligned. Derived display state:
     new      = orig_pos < 0
     edited   = orig_pos >= 0 && text != orig
     moved    = orig_pos >= 0 && orig_pos != rank, where rank is the link's index
                among the entry's existing (orig_pos >= 0) links in current order. */
typedef struct {
    char text[WORKSPACE_TARGET_MAX];
    char orig[WORKSPACE_TARGET_MAX];
    int  orig_pos;
    int  deleted;
} EditLink;

/* Growable list of EditLink; items is NULL until the first push. */
typedef struct {
    EditLink *items;
    int count;
    int cap;
} EditLinkList;

/* Appends a link to the list (orig_pos = -1 for a link added this session). */
void editlink_push(EditLinkList *l, const char *text, const char *orig, int orig_pos);
/* Frees the list's backing array and resets it to empty. */
void editlink_free(EditLinkList *l);

/* Workspace editor entry: an existing stored app instance (is_new=0) or a new
   app being added this session (is_new=1). Each row in the editor represents
   exactly one workspace entry so instances of the same app stay separate.
   links holds every target for this entry in display/save order — links loaded
   from disk and links added this session intermixed — each carrying its own
   add/edit/move/delete state (see EditLink).
   Staged removals: marked_delete flags an existing app for deletion (shown red).
   New apps are un-staged by dropping them from the apps array.
   Placement changes are derived, not flagged: layout is the current token and
   orig_layout the one loaded from disk, so the badge shows added / edited /
   removed by comparing them — and re-picking the original partition clears the
   indication on its own. */
typedef struct {
    char app[WORKSPACE_APP_MAX];
    char display[256];
    int  is_new;
    int  marked_delete;
    EditLinkList links;
    char layout[16];        /* screen-partition token ("" = none) chosen via '\' */
    char orig_layout[16];   /* layout as loaded from disk ("" for a new app) */
} WsEditorApp;

/* Workspace editor picker for `mn open edit`. Shows existing apps and their links;
   lets the user add links to existing apps, edit a link's text in place ('e'),
   reorder links (←/→), rename the workspace, add new (validated) apps, and stage
   removals of apps/links (Backspace). apps[]/count are in/out: new apps are
   appended (is_new=1), the links list is filled (new entries have orig_pos=-1),
   and per-link deleted / marked_delete flags are set.
   ws_name is an in/out buffer of ws_name_size bytes holding the workspace name; a
   rename updates it in place (the caller persists it). taken_names (length
   taken_count) lists the other workspaces' names so a rename can reject duplicates.
   *delete_workspace is set to 1 if the whole workspace is staged for removal.
   Returns 1 if confirmed (Enter), 0 if cancelled (Esc). */
int run_workspace_edit_picker(char *ws_name, size_t ws_name_size,
                              const char **taken_names, int taken_count,
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
   When allow_empty is set, Enter on an empty value confirms (used as "skip").
   If prefill is non-NULL/non-empty, the box opens seeded with that text. */
int run_text_input(const char *title, const char *subtitle, const char *label,
                   char *out, size_t out_size, int allow_empty, const char *prefill);

#endif
