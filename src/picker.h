#ifndef PICKER_H
#define PICKER_H

#include "search.h"
#include "index.h"
#include "workspace.h"
#include "wstree.h"

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
/* Alternate screen buffer — the surface every full-screen TUI (vim, less, htop)
   draws on. It has no scrollback, so redraws don't stack scrolled-off copies
   into history behind them; on exit the terminal is restored exactly as it was,
   undoing every frame in one step. But it still scrolls: a frame taller than
   the terminal loses its top irrecoverably (no scrollback to recover it from),
   so every renderer must size its window against term_size — see
   picker_window_size in picker.c. Entered/left through picker_alt_*
   (depth-counted) so nested pickers switch buffers once. */
#define ANSI_ALT_ENTER   "\033[?1049h"
#define ANSI_ALT_LEAVE   "\033[?1049l"
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

/* Workspace folders. The colours above split along a line worth keeping: the
   8-colour set names actions on a thing (blue app, green add, red delete,
   magenta edit, yellow rename) while the 256-colour set is structure and chrome
   (silver frames, violet highlight). A folder is structure, so it takes a
   256-colour gold — the colour a folder is everywhere else, and one that reads
   as a container rather than as something staged for a change.
   It does not compete with the dim yellow of a link: that yellow only ever
   appears indented inside the expanded app frame, carries a "→", and is dimmed;
   a folder row is bright, top-level, and carries a "▸". */
#define ANSI_FOLDER    "\033[38;5;179m"           /* folder row   #d7af5f */
#define ANSI_FOLDER_HL "\033[38;5;235;48;5;179m"  /* selected folder — dark on gold */
#define ANSI_CRUMB     "\033[38;5;245m"           /* breadcrumb ancestors #8a8a8a */

int read_key(void);
/* Alternate-screen bracket. Every picker enters on the way in and leaves on the
   way out, but the switch is depth-counted: only the outermost pair actually
   moves between buffers. Wrap a whole session of composed pickers (the workspace
   browser and editor alternating) in one enter/leave so the buffer is held for
   the duration and the screen never flashes back to the shell between them. */
void picker_alt_enter(void);
void picker_alt_leave(void);
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
    char layout[16];        /* screen-partition token ("" = none) chosen via /place */
    char orig_layout[16];   /* layout as loaded from disk ("" for a new app) */
} WsEditorApp;

/* How run_workspace_edit_picker ended. SAVE commits the staged changes and returns
   to the workspace browser (one level up); BACK discards them and returns to the
   browser; EXIT discards them and returns to the terminal. Both /save and /back
   land back in the browser — the difference is whether the work is kept. */
#define WSEDIT_SAVE   1
#define WSEDIT_EXIT   0
#define WSEDIT_BACK (-1)

/* Workspace editor picker for `mn open edit`. Shows existing apps and their links.
   Driven like the browser: arrows move and every action is a "/" command — /add
   adds an app, /append gives the selected app a file, folder or URL, /edit opens
   the selected link's text, /remove and /restore stage and unstage a row, /place
   assigns a screen partition, /reorder lifts a link for ←/→, and /save (commit and
   return to the browser), /back and /exit are the ways out. Renaming and deleting
   the workspace itself live in the browser now, not here. The list has no Esc; only
   the palette does, to close itself without running anything.

   A typed link is checked before it is accepted: a URI is trusted, a path must
   exist. So must an app named to /add.

   apps[]/count are in/out: new apps are appended (is_new=1), the links list is
   filled (new entries have orig_pos=-1), and per-link deleted / marked_delete
   flags are set. Everything is staged — /save (WSEDIT_SAVE) is what commits.
   ws_name is the workspace name, shown in the title (not modified here).
   Returns WSEDIT_SAVE / WSEDIT_BACK / WSEDIT_EXIT. */
int run_workspace_edit_picker(const char *ws_name,
                              WsEditorApp *apps, int *count, int max);
int run_search_picker(SearchResult *results, int count);
int run_list_picker(IndexEntry *entries, int count,
                    const char *title, const char *subtitle);
/* Workspace browser for `mn open` and step 1 of `mn open edit`. Drills through
   the folder tree — Enter descends into a folder or chooses a workspace,
   Backspace goes back up — expanding the selected row in a left-rail frame: a
   workspace shows its apps, a folder a preview of what it holds.

   The list has no Esc and no Backspace: "/" opens the command palette, which is
   both how you act on a row and the only way out (/back, /exit). Esc inside the
   palette closes it again without running anything. `mn open` is read-only and
   gets just those two ways out; with edit_mode set, /create and /snap (which make
   a workspace in the folder being shown), /new-folder, /move, /reorder, /rename
   and /delete join them, /select ticks rows so one /move can carry several, and st
   is mutated in place. /rename and /delete act on the cursor row whichever it is —
   this is where a workspace is named and removed, not the editor — and /reorder
   lifts it for ↑/↓ among its same-kind siblings, swapping the store entries so the
   arrangement is what the next save writes.
   *dirty is set to 1 if anything changed, so the caller can persist even when the
   browse itself ends in /exit. dirty may be NULL when edit_mode is 0.

   cwd is an in/out buffer of cwd_size bytes holding the folder being shown ("" =
   top level): passing back what the last call left means `mn open edit` reopens
   where the editor's /back came from rather than at the root.

   Returns the chosen workspace's index into st->ws, or -1 (/exit). */
int run_workspace_browser(WorkspaceStore *st, const char *title,
                          int edit_mode, int *dirty,
                          char *cwd, size_t cwd_size);
/* Strip directory prefix and trailing .exe from app path for a short display name. */
void ws_display_name(const char *app, char *out, size_t out_size);

#endif
