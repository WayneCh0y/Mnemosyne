#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#include "picker.h"
#include "app_resolve.h"
#include "app_launch.h"
#include "pathcomp.h"
#include "tokenizer.h"

#ifdef _WIN32
int read_key(void) {
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        c = _getch();
        if (c == 72) return KEY_UP;
        if (c == 80) return KEY_DOWN;
        if (c == 75) return KEY_LEFT;
        if (c == 77) return KEY_RIGHT;
        return 0;
    }
    return c;
}
#else
int read_key(void) {
    struct termios orig, raw;
    tcgetattr(STDIN_FILENO, &orig);
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~ICRNL;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    /* TCSANOW, never TCSAFLUSH: a paste arrives as one burst of bytes, and
       flushing here would drop every byte after the one we return. */
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        /* EOF or error — treat as Esc so the picker unwinds instead of spinning. */
        tcsetattr(STDIN_FILENO, TCSANOW, &orig);
        return KEY_ESC;
    }

    int result = (int)(unsigned char)c;
    if (c == '\033') {
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        char seq[2] = {0, 0};
        if (read(STDIN_FILENO, &seq[0], 1) == 1 &&
            read(STDIN_FILENO, &seq[1], 1) == 1 && seq[0] == '[') {
            if (seq[1] == 'A') result = KEY_UP;
            if (seq[1] == 'B') result = KEY_DOWN;
            if (seq[1] == 'C') result = KEY_RIGHT;
            if (seq[1] == 'D') result = KEY_LEFT;
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    return result;
}
#endif

/* ── Sliding window (show at most PICKER_WINDOW rows, keep selection visible) ── */

#define PICKER_WINDOW 5

/* Top index of a 5-row window that keeps `selected` visible: centered, clamped to
   the ends. e.g. count=10 → selected 0,1,2 ⇒ rows 1-5; 3 ⇒ 2-6; 4 ⇒ 3-7; 9 ⇒ 6-10. */
static int picker_window_start(int selected, int count) {
    if (count <= PICKER_WINDOW) return 0;
    int start = selected - PICKER_WINDOW / 2;   /* center the selection (offset 2) */
    if (start < 0) start = 0;
    if (start > count - PICKER_WINDOW) start = count - PICKER_WINDOW;
    return start;
}

static void print_more_above(int start) {
    if (start > 0)
        printf(ANSI_DIM "    ⋯ %d more above" ANSI_RESET "\n", start);
}

static void print_more_below(int end, int count) {
    if (end < count)
        printf(ANSI_DIM "    ⋯ %d more below" ANSI_RESET "\n", count - end);
}

/* ── Shared numeric-jump input handler ─────────────────────────────────── */

/* Processes one keypress while num_input >= 0 (the digit-accumulation mode).
   Updates num_input, selected, show_error, and done in-place. */
static void handle_num_input(int key, int list_len,
                             int *num_input, int *selected,
                             int *prev_selected, int *show_error,
                             int *done) {
    if (key >= '1' && key <= '9') {
        *num_input = *num_input * 10 + (key - '0');
    } else if (key == '0') {
        if (*num_input > 0) *num_input = *num_input * 10;
    } else if (key == KEY_BACKSPACE || key == 127) {
        if (*num_input > 0) *num_input /= 10;
    } else if (key == KEY_ENTER) {
        if (*num_input >= 1 && *num_input <= list_len) {
            *selected = *num_input - 1;
            *done = 1;
        } else {
            *show_error = 1;
            *selected   = *prev_selected;
            *num_input  = -1;
        }
    } else if (key == KEY_ESC) {
        *selected  = *prev_selected;
        *num_input = -1;
    } else if (key == KEY_UP) {
        *num_input = -1; *selected = *prev_selected;
        if (*selected > 0) (*selected)--;
    } else if (key == KEY_DOWN) {
        *num_input = -1; *selected = *prev_selected;
        if (*selected < list_len - 1) (*selected)++;
    }
}

/* Visible column count of a UTF-8 string (counts code points, not bytes) so lines
   with arrows/dashes centre correctly. */
static int vis_cols(const char *s) {
    int n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

/* ── In-place single-line text editing ──────────────────────────────────────
   Applies one keypress to a text buffer with a cursor at *pos: ←/→ move the
   cursor, Backspace deletes the char before it, and a printable key inserts at
   it. *len tracks the length and buf stays NUL-terminated (size is its capacity).
   Returns 1 if the key was consumed, 0 otherwise (so the caller still handles
   Enter, Esc and the like). Shared by every workspace text-entry field. */
static int edit_buffer_key(int key, char *buf, int *len, int *pos, size_t size) {
    if (key == KEY_LEFT) {
        if (*pos > 0) (*pos)--;
    } else if (key == KEY_RIGHT) {
        if (*pos < *len) (*pos)++;
    } else if (key == KEY_BACKSPACE || key == 127) {
        if (*pos > 0) {
            memmove(&buf[*pos - 1], &buf[*pos],
                    (size_t)(*len - *pos) + 1);   /* include NUL */
            (*len)--; (*pos)--;
        }
    } else if (key >= 32 && key < 127) {
        if (*len < (int)size - 1) {
            memmove(&buf[*pos + 1], &buf[*pos],
                    (size_t)(*len - *pos) + 1);    /* include NUL */
            buf[*pos] = (char)key;
            (*len)++; (*pos)++;
        }
    } else {
        return 0;
    }
    return 1;
}

/* ── Inline path completion ─────────────────────────────────────────────────
   Every field that takes a filesystem path (a new app, a link, a link being
   edited) carries one of these. pathcomp.c re-scans the directory being typed
   on each keystroke; this layer owns the highlight and the dropdown drawn under
   the field, so a path can be walked into instead of typed out.

   Keys, while the list is open: ↑/↓ move the highlight, Tab accepts it (a
   directory gains a trailing separator, so Tab again lists its children), and
   Esc closes the list without leaving the field.

   Enter is never the dropdown's: it always submits the field. Completing and
   confirming are separate keys because they are separate intentions — you walk
   a path with Tab as many times as it takes, then press Enter once to commit
   what the field actually shows. An Enter that sometimes completed and
   sometimes submitted, depending on whether the highlight had been moved, meant
   the same keystroke did two different things on two consecutive presses. */

#define SUGGEST_WINDOW 5

typedef struct {
    PathComp pc;
    int sel;         /* highlighted suggestion */
    int dismissed;   /* Esc closed the list; the next edit brings it back */
} PathSuggest;

/* Opens the completion state on a field seeded with `buf` (empty, one typed
   character, or an existing link being edited). */
static void suggest_reset(PathSuggest *s, const char *buf) {
    memset(s, 0, sizeof(*s));
    pathcomp_update(&s->pc, buf);
}

/* Re-scans after an edit. The highlight returns to the top because the previous
   pick may no longer match, and a dismissed list reopens: typing means the user
   is still working on the path. */
static void suggest_refresh(PathSuggest *s, const char *buf) {
    pathcomp_update(&s->pc, buf);
    s->sel = 0;
    s->dismissed = 0;
}

static int suggest_open(const PathSuggest *s) {
    return !s->dismissed && s->pc.count > 0;
}

/* Applies one keypress to the dropdown. Returns 1 if the key belonged to it; 0
   leaves the key for the field's own Enter / Esc / text handling. */
static int suggest_key(PathSuggest *s, int key, char *buf, int *len, int *pos,
                       size_t size) {
    if (!suggest_open(s)) return 0;

    switch (key) {
    case KEY_UP:
        s->sel = (s->sel > 0) ? s->sel - 1 : s->pc.count - 1;
        return 1;
    case KEY_DOWN:
        s->sel = (s->sel + 1) % s->pc.count;
        return 1;
    case KEY_ESC:
        s->dismissed = 1;
        return 1;
    case KEY_TAB:
        if (pathcomp_apply(&s->pc, s->sel, buf, len, pos, size))
            suggest_refresh(s, buf);
        return 1;
    default:
        return 0;   /* Enter included: the field owns it */
    }
}

/* Right-anchored copy of the directory being listed: a deep path keeps the tail
   that identifies it ("…\Users\Wayne\Projects\") rather than the drive letter. */
static void dir_brief(const char *dir, char *out, size_t n, int max_cols) {
    int len = (int)strlen(dir);
    if (len <= max_cols) {
        snprintf(out, n, "%s", dir);
        return;
    }
    snprintf(out, n, "\xe2\x80\xa6%s", dir + len - (max_cols - 1));
}

/* Draws the dropdown beneath the field, carrying its own key hints (the field's
   usual hint line is suppressed while it's open). Nothing is drawn when the
   buffer isn't a path, has no matches, or the list has been dismissed. */
static void render_suggestions(const PathSuggest *s) {
    if (!suggest_open(s)) return;
    const PathComp *pc = &s->pc;

    char where[64];
    dir_brief(pc->dir, where, sizeof(where), 44);
    printf("\n" ANSI_SILVER "  \xe2\x94\x8c " ANSI_RESET ANSI_DIM "%d match%s in %s" ANSI_RESET "\n",
           pc->total, pc->total == 1 ? "" : "es", where);

    int start = picker_window_start(s->sel, pc->count);
    int end   = start + (pc->count < SUGGEST_WINDOW ? pc->count : SUGGEST_WINDOW);
    for (int i = start; i < end; i++) {
        const PathCompItem *it = &pc->items[i];
        char tail[2] = { it->is_dir ? pc->sep : '\0', '\0' };   /* dirs show their separator */

        /* Only the selected row carries colour — a violet "> " marker and its
           name. The rest stay dim, so the eye lands on one row, not on a wall of
           highlighted text. */
        printf(ANSI_SILVER "  \xe2\x94\x82 " ANSI_RESET);
        if (i == s->sel)
            printf(ANSI_VIOLET "> %s%s" ANSI_RESET "\n", it->name, tail);
        else
            printf(ANSI_DIM "  %s%s" ANSI_RESET "\n", it->name, tail);
    }

    /* Matches past the item cap can't be scrolled to, so say what does reach
       them rather than dangling a count the highlight can never land on. */
    int hidden = pc->total - end;
    if (hidden > 0)
        printf(ANSI_SILVER "  \xe2\x94\x82 " ANSI_RESET ANSI_DIM "\xe2\x8b\xaf %d more%s" ANSI_RESET "\n",
               hidden, pc->total > pc->count ? " \xe2\x80\x94 keep typing to narrow" : "");

    printf(ANSI_SILVER "  \xe2\x94\x94 " ANSI_RESET ANSI_DIM "Tab complete  \xe2\x80\xa2  \xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  Enter confirm  \xe2\x80\xa2  Esc dismiss" ANSI_RESET);
}

/* ── Generic indexed-list navigation loop ───────────────────────────────────
   The menu, search, list and workspace pickers share one control flow: ↑/↓ move,
   Enter selects, Esc cancels (returns -1), and 1-9 begins a numeric jump handled
   by handle_num_input. Only the drawing differs, so each picker passes a render
   callback (called with the live selection / jump state) plus its row count; this
   loop owns cursor hide/show and key handling. Returns the chosen index, or -1. */
typedef void (*PickerRender)(void *ctx, int selected, int num_input, int show_error);

static int run_indexed_picker(int count, PickerRender render, void *ctx) {
    int selected = 0, prev_selected = 0, num_input = -1, show_error = 0, done = 0;

    printf(ANSI_CURSOR_HIDE);
    render(ctx, selected, num_input, show_error);

    while (!done) {
        int key = read_key();
        show_error = 0;

        if (num_input >= 0) {
            handle_num_input(key, count, &num_input, &selected,
                             &prev_selected, &show_error, &done);
        } else {
            switch (key) {
            case KEY_UP:    if (selected > 0)         selected--; break;
            case KEY_DOWN:  if (selected < count - 1) selected++; break;
            case KEY_ENTER: done = 1;                             break;
            case KEY_ESC:   selected = -1; done = 1;              break;
            default:
                if (key >= '1' && key <= '9') {
                    prev_selected = selected;
                    num_input = key - '0';
                }
                break;
            }
        }

        if (!done) render(ctx, selected, num_input, show_error);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
}

/* ── Generic menu picker ───────────────────────────────────────────────── */

static void print_picker_header(const char *title, const char *subtitle) {
    printf(ANSI_CLEAR ANSI_RESET);
    printf(ANSI_BOLD ANSI_CYAN "▶ %s" ANSI_RESET "\n", title);
    if (subtitle && subtitle[0])
        printf(ANSI_DIM "%s" ANSI_RESET "\n", subtitle);
    printf("\n");
}

/* extra_hint, if non-NULL, is injected between "Enter select" and "Esc cancel"
   (e.g. "Backspace back" for the drill-in file picker). */
static void print_picker_footer(int num_input, int show_error, const char *extra_hint) {
    if (show_error) {
        printf("\n" ANSI_YELLOW "That index doesn't exist." ANSI_RESET);
    } else if (num_input >= 0) {
        if (num_input == 0)
            printf("\n" ANSI_YELLOW "Jump target: -" ANSI_RESET);
        else
            printf("\n" ANSI_YELLOW "Jump target: %d" ANSI_RESET, num_input);
    }
    if (extra_hint && extra_hint[0])
        printf("\n" ANSI_DIM "↑/↓ move  •  Enter select  •  %s  •  Esc cancel  •  1-9 jump" ANSI_RESET,
               extra_hint);
    else
        printf("\n" ANSI_DIM "↑/↓ move  •  Enter select  •  Esc cancel  •  1-9 jump" ANSI_RESET);
    fflush(stdout);
}

static void render_menu_list(const char *title, const char *subtitle,
                             const char **list, int display, int selected,
                             int num_input, int show_error) {
    print_picker_header(title, subtitle);
    for (int i = 0; i < display; i++) {
        if (num_input < 0 && i == selected) {
            printf("  " ANSI_ACCENT CURSOR_TOKEN ANSI_RESET " " ANSI_BOLD "[%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        } else {
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        }
    }
    print_picker_footer(num_input, show_error, NULL);
}

typedef struct {
    const char *title, *subtitle;
    const char **list;
    int display;
} MenuPickerCtx;

static void render_menu_cb(void *ctx, int selected, int num_input, int show_error) {
    const MenuPickerCtx *c = ctx;
    render_menu_list(c->title, c->subtitle, c->list, c->display,
                     selected, num_input, show_error);
}

int run_menu_picker(const char *title, const char *subtitle,
                    const char **list, int display) {
    MenuPickerCtx ctx = { title, subtitle, list, display };
    return run_indexed_picker(display, render_menu_cb, &ctx);
}

int run_ide_picker(const char **list, int display) {
    return run_menu_picker("Choose a default IDE", NULL, list, display);
}

/* ── Multi-select checklist picker ─────────────────────────────────────── */

#define MS_MODE_LIST 0
#define MS_MODE_TYPE 1

/* Navigated as a flat list of rows: one per app, plus one per link beneath each
   selected app (so the cursor can land on a single link to remove it). */
enum { MS_ROW_APP, MS_ROW_LINK };
typedef struct {
    int kind;
    int app;   /* index into labels[]/selected[]/links[] */
    int link;  /* link index within links[app] for MS_ROW_LINK, else -1 */
} MsRow;

/* Builds the flat row list into a heap buffer, growing it as needed.
   rows and cap are in/out; returns the row count. */
static int build_ms_rows(int count, const int *selected, const AppLinks *links,
                         MsRow **rows, int *cap) {
    int need = 0;
    for (int i = 0; i < count; i++) {
        need++;
        if (links && selected[i]) need += links[i].count;
    }
    if (need > *cap) {
        int ncap = *cap ? *cap : 16;
        while (ncap < need) ncap *= 2;
        MsRow *p = realloc(*rows, (size_t)ncap * sizeof(MsRow));
        if (p != NULL) { *rows = p; *cap = ncap; }
    }
    int n = 0;
    MsRow *r = *rows;
    for (int i = 0; i < count && n < *cap; i++) {
        r[n].kind = MS_ROW_APP; r[n].app = i; r[n].link = -1; n++;
        if (links && selected[i])
            for (int k = 0; k < links[i].count && n < *cap; k++) {
                r[n].kind = MS_ROW_LINK; r[n].app = i; r[n].link = k; n++;
            }
    }
    return n;
}

static void render_multiselect(const char *title, const char *subtitle,
                               const char **labels,
                               const int *selected, const AppLinks *links,
                               const MsRow *rows, int nrows,
                               int cursor, int mode, const char *typed, int type_app,
                               int edit_pos, const PathSuggest *sg) {
    print_picker_header(title, subtitle);
    int start = picker_window_start(cursor, nrows);
    int end   = start + (nrows < PICKER_WINDOW ? nrows : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        const MsRow *r = &rows[i];
        const char *cur = (i == cursor) ? ANSI_ACCENT CURSOR_TOKEN ANSI_RESET : " ";
        if (r->kind == MS_ROW_APP) {
            if (selected[r->app])
                printf("  %s " ANSI_APP_HL " %s " ANSI_RESET "\n", cur, labels[r->app]);
            else
                printf("  %s " ANSI_DIM "%s" ANSI_RESET "\n", cur, labels[r->app]);
        } else { /* MS_ROW_LINK */
            printf("  %s     " ANSI_DIM_YELLOW "\xe2\x86\x92 %s" ANSI_RESET "\n",
                   cur, links[r->app].items[r->link]);
        }
    }
    print_more_below(end, nrows);
    if (mode == MS_MODE_TYPE) {
        /* highlight block cursor at edit_pos (a space when parked past the end) */
        char cur = typed[edit_pos] ? typed[edit_pos] : ' ';
        const char *rest = typed[edit_pos] ? typed + edit_pos + 1 : "";
        printf("\n" ANSI_CYAN "  Add link for %s: " ANSI_RESET
               "%.*s" ANSI_REVERSE "%c" ANSI_REVERSE_OFF "%s",
               labels[type_app], edit_pos, typed, cur, rest);
        if (suggest_open(sg)) render_suggestions(sg);
        else printf("\n" ANSI_DIM "  \xe2\x86\x90/\xe2\x86\x92 move  \xe2\x80\xa2  Enter add  \xe2\x80\xa2  Esc cancel  \xe2\x80\xa2  Backspace delete" ANSI_RESET);
    } else {
        const MsRow *r = &rows[cursor];
        if (r->kind == MS_ROW_LINK)
            printf("\n" ANSI_DIM "↑/↓ move  •  Backspace remove link  •  Enter confirm  •  Esc cancel" ANSI_RESET);
        else if (links && selected[r->app])
            printf("\n" ANSI_DIM "↑/↓ move  •  Backspace deselect  •  type to add link  •  Enter confirm  •  Esc cancel" ANSI_RESET);
        else
            printf("\n" ANSI_DIM "↑/↓ move  •  Backspace toggle  •  Enter confirm  •  Esc cancel" ANSI_RESET);
    }
    fflush(stdout);
}

int run_multiselect_picker(const char *title, const char *subtitle,
                           const char **labels, int count, int *selected,
                           AppLinks *links) {
    MsRow *rows    = NULL;
    int  rows_cap  = 0;
    int  cursor    = 0;
    int  done      = 0;
    int  cancelled = 0;
    int  mode      = MS_MODE_LIST;
    int  type_app  = 0;   /* app being given a new link in MS_MODE_TYPE */
    char typed[WORKSPACE_TARGET_MAX] = {0};
    int  typed_len = 0;
    int  edit_pos  = 0;   /* cursor within typed[] */
    PathSuggest sg;       /* path completion for the link field */

    suggest_reset(&sg, typed);
    int nrows = build_ms_rows(count, selected, links, &rows, &rows_cap);
    printf(ANSI_CURSOR_HIDE);
    render_multiselect(title, subtitle, labels, selected, links,
                       rows, nrows, cursor, mode, typed, type_app, edit_pos, &sg);

    while (!done) {
        int key = read_key();

        if (mode == MS_MODE_TYPE) {
            /* Path completion sees the key first, so ↑/↓/Tab drive the dropdown
               while it's open; otherwise this is the same in-place editing as
               the workspace editor's link field. */
            if (suggest_key(&sg, key, typed, &typed_len, &edit_pos, sizeof(typed))) {
                /* consumed by the dropdown */
            } else if (key == KEY_ENTER) {
                if (typed_len > 0)
                    targetlist_push(&links[type_app].items, &links[type_app].cap,
                                    &links[type_app].count, typed);
                mode = MS_MODE_LIST; typed[0] = '\0'; typed_len = 0; edit_pos = 0;
            } else if (key == KEY_ESC) {
                mode = MS_MODE_LIST; typed[0] = '\0'; typed_len = 0; edit_pos = 0;
            } else if (edit_buffer_key(key, typed, &typed_len, &edit_pos, sizeof(typed))) {
                suggest_refresh(&sg, typed);
            }
        } else {
            const MsRow *r = &rows[cursor];
            switch (key) {
            case KEY_UP:    if (cursor > 0)         cursor--; break;
            case KEY_DOWN:  if (cursor < nrows - 1) cursor++; break;
            case KEY_BACKSPACE:
            case 127:
                if (r->kind == MS_ROW_APP) {
                    selected[r->app] = !selected[r->app];
                } else { /* MS_ROW_LINK → remove the link immediately */
                    targetlist_remove(links[r->app].items, &links[r->app].count, r->link);
                }
                break;
            case KEY_ENTER: done = 1; break;
            case KEY_ESC:   cancelled = 1; done = 1; break;
            default:
                /* Any printable key on a selected app row → start adding a link
                   inline, seeded with that first character. */
                if (links && r->kind == MS_ROW_APP && selected[r->app]
                    && key >= 33 && key < 127) {
                    mode      = MS_MODE_TYPE;
                    type_app  = r->app;
                    typed[0]  = (char)key;
                    typed[1]  = '\0';
                    typed_len = 1;
                    edit_pos  = 1;
                    suggest_reset(&sg, typed);
                }
                break;
            }
        }

        if (!done) {
            nrows = build_ms_rows(count, selected, links, &rows, &rows_cap);
            if (cursor >= nrows) cursor = nrows - 1;
            if (cursor < 0)      cursor = 0;
            render_multiselect(title, subtitle, labels, selected, links,
                               rows, nrows, cursor, mode, typed, type_app, edit_pos, &sg);
        }
    }

    free(rows);
    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return cancelled ? 0 : 1;
}

/* ── Search results picker ─────────────────────────────────────────────── */

static void print_context(const SearchResult *r, int dimmed) {
    const char *reset  = dimmed ? "\033[0;2m"  : ANSI_RESET;
    const char *color1 = dimmed ? "\033[2;35m" : ANSI_MAGENTA;
    const char *color2 = dimmed ? "\033[2;34m" : ANSI_BLUE;
    const char *hl     = dimmed ? ANSI_DIM_YELLOW : "\033[30;43m";
    int hl_start = r->match_start;
    int hl_end   = (hl_start >= 0) ? hl_start + r->match_len : -1;
    int in_hl    = 0;
    int is_md = (strcmp(r->file_type, "md") == 0);
    int is_pdf = (strcmp(r->file_type, "pdf") == 0);
    int at_line_start = 1;
    if (dimmed) printf(ANSI_DIM);
    printf("    ");
    /* PDF matches carry a 1-based page number; show it so the user knows where
       the match sits even when the viewer can't be told to jump there. */
    if (is_pdf && r->page > 0)
        printf("%s[p.%d]%s ", color2, r->page, reset);
    const char *p = r->context;
    while (*p != '\0') {
        int idx = (int)(p - r->context);
        if (!in_hl && idx == hl_start) { printf("%s", hl); in_hl = 1; }
        if ( in_hl && idx == hl_end)   { printf("%s", reset); if (dimmed) printf(ANSI_DIM); in_hl = 0; }

        if (is_md && at_line_start && *p == '#') {
            while (*p == '#') p++;
            if (*p == ' ') p++;
            if (!in_hl) printf("%s", color1);
            while (*p != '\0' && *p != '\n') {
                int cidx = (int)(p - r->context);
                if (!in_hl && cidx == hl_start) { printf("%s", hl); in_hl = 1; }
                if ( in_hl && cidx == hl_end)   { printf("%s%s", reset, color1); in_hl = 0; }
                putchar(*p++);
            }
            if (in_hl) { printf("%s", reset); in_hl = 0; }
            printf("%s", reset);
            at_line_start = 0;
        } else if (is_md && strncmp(p, "[LIST] ", 7) == 0) {
            printf("- "); p += 7; at_line_start = 0;
        } else if (is_md && strncmp(p, " | ", 3) == 0) {
            printf("\n    - "); p += 3; at_line_start = 0;
        } else if (is_md && strncmp(p, "[/LIST]", 7) == 0) {
            p += 7;
        } else if (is_md && strncmp(p, "[LINK]", 6) == 0) {
            printf("%slink%s", color2, reset); p += 6; at_line_start = 0;
        } else if (*p == '\n') {
            printf("\n    "); p++; at_line_start = 1;
        } else {
            putchar(*p++); at_line_start = 0;
        }
    }
    if (in_hl) printf(ANSI_RESET);
    printf(ANSI_RESET "\n");
}

static void print_result_divider(int dimmed) {
    printf(dimmed ? ANSI_DIM : ANSI_RESET);
    printf("    ─────────────────────────────────────────\n");
    printf(ANSI_RESET);
}

static void render_results(SearchResult *results, int count, int selected,
                           int num_input, int show_error) {
    print_picker_header("Search results", NULL);
    int start = picker_window_start(selected, count);
    int end   = start + (count < PICKER_WINDOW ? count : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        if (i > start) print_result_divider(i != selected || num_input >= 0);
        if (num_input < 0 && i == selected) {
            printf("  " ANSI_ACCENT CURSOR_TOKEN ANSI_RESET " " ANSI_BOLD "[%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 0);
        } else {
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 1);
        }
        printf("\n");
    }
    print_more_below(end, count);
    print_picker_footer(num_input, show_error, NULL);
}

typedef struct { SearchResult *results; int count; } SearchPickerCtx;

static void render_search_cb(void *ctx, int selected, int num_input, int show_error) {
    const SearchPickerCtx *c = ctx;
    render_results(c->results, c->count, selected, num_input, show_error);
}

int run_search_picker(SearchResult *results, int count) {
    SearchPickerCtx ctx = { results, count };
    return run_indexed_picker(count, render_search_cb, &ctx);
}

static void find_parent_dir(const char *path, char *out, size_t out_size) {
    strncpy(out, path, out_size - 1);
    out[out_size - 1] = '\0';
#ifdef _WIN32
    char *last = strrchr(out, '\\');
#else
    char *last = strrchr(out, '/');
#endif

    if (last) *last = '\0';
}

/* ── Index list picker ─────────────────────────────────────────────────── */

/* Max files to preview under the selected folder before collapsing the rest. */
#define FOLDER_FRAME_MAX_ENTRIES 8

/* Contiguous run of entries[] that share the same parent directory. Built from
   a path-sorted entries[] so each group's files are stored back-to-back. */
typedef struct {
    char dir[4096];
    int  start;   /* index into entries[] of the first file in this folder */
    int  count;   /* number of files in this folder */
} FolderGroup;

/* Groups a path-sorted entries[] by parent directory. Returns a malloc'd array
   sized to the resulting group count. Caller frees. */
static FolderGroup *group_by_folder(IndexEntry *entries, int count, int *group_count) {
    FolderGroup *groups = malloc(sizeof(FolderGroup) * (size_t)count);
    if (!groups) { *group_count = 0; return NULL; }
    int g = 0;
    for (int i = 0; i < count; i++) {
        char dir[4096];
        find_parent_dir(entries[i].original_path, dir, sizeof(dir));
        if (g > 0 && strcmp(groups[g - 1].dir, dir) == 0) {
            groups[g - 1].count++;
        } else {
            strncpy(groups[g].dir, dir, sizeof(groups[g].dir) - 1);
            groups[g].dir[sizeof(groups[g].dir) - 1] = '\0';
            groups[g].start = i;
            groups[g].count = 1;
            g++;
        }
    }
    *group_count = g;
    return groups;
}

static const char *path_basename(const char *path) {
#ifdef _WIN32
    const char *base = strrchr(path, '\\');
#else
    const char *base = strrchr(path, '/');
#endif
    return base ? base + 1 : path;
}

/* Dim-yellow rail listing the files of the selected folder, capped so a large
   folder can't overflow. Mirrors render_workspace_frame. */
static void render_folder_frame(IndexEntry *entries, const FolderGroup *g) {
    if (g->count == 0) return;
    int lines = 0;
    for (int k = 0; k < g->count; k++) {
        if (lines >= FOLDER_FRAME_MAX_ENTRIES) {
            printf(ANSI_DIM ANSI_BRIGHT_YELLOW "    \xe2\x94\x82 \xe2\x8b\xaf \xe2\x80\xa6" ANSI_RESET "\n");
            break;
        }
        const char *base = path_basename(entries[g->start + k].original_path);
        printf(ANSI_DIM ANSI_BRIGHT_YELLOW "    \xe2\x94\x82 %s" ANSI_RESET "\n", base);
        lines++;
    }
}

static void render_folder_list(IndexEntry *entries,
                               FolderGroup *groups, int count, int selected,
                               int num_input, int show_error,
                               const char *title, const char *subtitle) {
    print_picker_header(title, subtitle);
    int start = picker_window_start(selected, count);
    int end   = start + (count < PICKER_WINDOW ? count : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
#ifdef _WIN32
        const char *sep = "\\";
#else
        const char *sep = "/";
#endif
        if (num_input < 0 && i == selected) {
            printf("  " ANSI_ACCENT CURSOR_TOKEN ANSI_RESET " " ANSI_BOLD "[%d] %s%s" ANSI_RESET "  " ANSI_DIM "(%d file%s)" ANSI_RESET "\n",
                   i + 1, groups[i].dir, sep,
                   groups[i].count, groups[i].count == 1 ? "" : "s");
            render_folder_frame(entries, &groups[i]);
        } else {
            printf(ANSI_DIM "    [%d] %s%s  (%d file%s)" ANSI_RESET "\n",
                   i + 1, groups[i].dir, sep,
                   groups[i].count, groups[i].count == 1 ? "" : "s");
        }
    }
    print_more_below(end, count);
    print_picker_footer(num_input, show_error, NULL);
}

/* Folder-level picker. Returns the chosen group index, or -1 (Esc). */
static int run_folder_select(IndexEntry *entries, FolderGroup *groups, int count,
                             const char *title, const char *subtitle) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    render_folder_list(entries, groups, count, selected, num_input, show_error,
                       title, subtitle);

    while (!done) {
        int key = read_key();
        show_error = 0;

        if (num_input >= 0) {
            handle_num_input(key, count, &num_input, &selected,
                             &prev_selected, &show_error, &done);
        } else {
            switch (key) {
            case KEY_UP:    if (selected > 0)         selected--; break;
            case KEY_DOWN:  if (selected < count - 1) selected++; break;
            case KEY_ENTER: done = 1;                             break;
            case KEY_ESC:   selected = -1; done = 1;             break;
            default:
                if (key >= '1' && key <= '9') {
                    prev_selected = selected;
                    num_input = key - '0';
                }
                break;
            }
        }

        if (!done)
            render_folder_list(entries, groups, count, selected, num_input, show_error,
                               title, subtitle);
    }
    return selected;
}

static void render_file_list(IndexEntry *entries, const FolderGroup *g, int selected,
                             int num_input, int show_error, const char *title) {
    print_picker_header(title, g->dir);
    int start = picker_window_start(selected, g->count);
    int end   = start + (g->count < PICKER_WINDOW ? g->count : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        const char *base = path_basename(entries[g->start + i].original_path);
        if (num_input < 0 && i == selected)
            printf("  " ANSI_ACCENT CURSOR_TOKEN ANSI_RESET " " ANSI_BOLD "[%d] %s" ANSI_RESET "\n", i + 1, base);
        else
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, base);
    }
    print_more_below(end, g->count);
    print_picker_footer(num_input, show_error, "Backspace back");
}

/* File-level picker drilled into one folder. Returns the chosen offset within
   the group (0..count-1), -1 to cancel the whole flow (Esc), or -2 to go back
   to the folder picker (Backspace). */
static int run_file_select(IndexEntry *entries, const FolderGroup *g, const char *title) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    render_file_list(entries, g, selected, num_input, show_error, title);

    while (!done) {
        int key = read_key();
        show_error = 0;

        if (num_input >= 0) {
            handle_num_input(key, g->count, &num_input, &selected,
                             &prev_selected, &show_error, &done);
        } else {
            switch (key) {
            case KEY_UP:        if (selected > 0)            selected--; break;
            case KEY_DOWN:      if (selected < g->count - 1) selected++; break;
            case KEY_ENTER:     done = 1;                                break;
            case KEY_ESC:       selected = -1; done = 1;                 break;
            case KEY_BACKSPACE:
            case 127:           selected = -2; done = 1;                 break;
            default:
                if (key >= '1' && key <= '9') {
                    prev_selected = selected;
                    num_input = key - '0';
                }
                break;
            }
        }

        if (!done) render_file_list(entries, g, selected, num_input, show_error, title);
    }
    return selected;
}

int run_list_picker(IndexEntry *entries, int count,
                    const char *title, const char *subtitle) {
    int gcount = 0;
    FolderGroup *groups = group_by_folder(entries, count, &gcount);
    if (!groups || gcount == 0) { free(groups); return -1; }

    printf(ANSI_CURSOR_HIDE);

    int result = -1;
    while (1) {
        int g = run_folder_select(entries, groups, gcount, title, subtitle);
        if (g == -1) break;
        int f = run_file_select(entries, &groups[g], title);
        if (f == -2) continue;   /* Backspace — back to folder picker */
        if (f == -1) break;      /* Esc — cancel the whole flow */
        result = groups[g].start + f;
        break;
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    free(groups);
    return result;
}

/* ── Workspace browser ──────────────────────────────────────────────────────
   The rows and the "/" palette live further down, past the centred dialogs they
   call into; only the selected-workspace frame is here, next to the rendering
   helpers it belongs with. */

/* Most entries to show under the selected workspace before collapsing the rest
   into a "⋯ K more" line, so a large workspace can't overflow. */
#define WS_FRAME_MAX_ENTRIES 8

/* Formats a layout token as "{Left half}" / "{Left half · S2}"; defined with the
   placement chooser below. */
static void placement_brief(const char *layout, char *out, size_t n);

/* Render the selected workspace's entries along a left rail (a dimmed-yellow
   vertical bar, no right border so long paths/URLs are never truncated). Apps
   match the editor's blue highlight and show their placement label; each
   target/link sits on its own yellow line beneath its app. */
static void render_workspace_frame(const Workspace *w) {
    if (w->entry_count == 0) return;
    int lines = 0;
    for (int j = 0; j < w->entry_count; j++) {
        if (lines >= WS_FRAME_MAX_ENTRIES) {
            printf(ANSI_DIM ANSI_BRIGHT_YELLOW "    │ \xe2\x8b\xaf \xe2\x80\xa6" ANSI_RESET "\n");
            break;
        }
        char disp[256];
        ws_display_name(w->entries[j].app, disp, sizeof(disp));

        /* App row: dim-yellow rail, blue-highlighted name, optional placement. */
        char place[80];
        placement_brief(w->entries[j].layout, place, sizeof(place));
        printf(ANSI_DIM_YELLOW "    │ " ANSI_RESET ANSI_APP_HL " %s " ANSI_RESET, disp);
        if (place[0])
            printf(" " ANSI_APP_HL " %s " ANSI_RESET, place);
        printf("\n");
        lines++;

        /* Each target on its own line (single target included). */
        int tc = w->entries[j].target_count;
        for (int k = 0; k < tc && lines < WS_FRAME_MAX_ENTRIES; k++) {
            printf(ANSI_DIM_YELLOW "    │    \xe2\x86\x92 %s" ANSI_RESET "\n",
                   w->entries[j].targets[k]);
            lines++;
        }
    }
}

/* ── Centered yes/no confirmation ──────────────────────────────────────── */

static void term_size(int *cols, int *rows) {
    *cols = 80; *rows = 24;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        *cols = w.ws_col;
        *rows = w.ws_row;
    }
#endif
    if (*cols < 20) *cols = 20;
    if (*rows < 8)  *rows = 8;
}

/* Prints `text` (with `vlen` visible columns) horizontally centred, wrapped in `color`. */
static void print_centered(int cols, const char *color, const char *text, int vlen) {
    int pad = (cols - vlen) / 2;
    if (pad < 0) pad = 0;
    printf("%*s%s%s" ANSI_RESET "\n", pad, "", color, text);
}

/* Full-screen, centred Yes/No confirmation. Returns 1 (yes) or 0 (no/Esc).
   Defaults to No so an accidental Enter never deletes anything. */
static int confirm_centered(const char *question) {
    int choice = 0;   /* 0 = No (safe default), 1 = Yes */
    int done = 0, result = 0;
    /* Both labels padded to the same 20-column width so the boxes match. */
    const char *yes  = "   Yes, remove it   ";
    const char *no   = "    No, keep it     ";
    const char *help = "\xe2\x86\x91/\xe2\x86\x93 move   Enter select   Esc cancel";

    printf(ANSI_CURSOR_HIDE);
    while (!done) {
        int cols, rows;
        term_size(&cols, &rows);
        printf(ANSI_CLEAR ANSI_RESET);

        int block = 6;                       /* question, gap, yes, no, gap, help */
        int top = (rows - block) / 2;
        for (int i = 0; i < top; i++) printf("\n");

        print_centered(cols, ANSI_BOLD, question, (int)strlen(question));
        printf("\n");
        print_centered(cols, choice == 1 ? ANSI_DEL_HL : ANSI_DIM, yes, (int)strlen(yes));
        print_centered(cols, choice == 0 ? ANSI_ADD_HL : ANSI_DIM, no,  (int)strlen(no));
        printf("\n");
        print_centered(cols, ANSI_DIM, help, 35);   /* arrows count as 1 column each */
        fflush(stdout);

        int key = read_key();
        switch (key) {
        case KEY_UP: case KEY_DOWN: choice = !choice;      break;
        case KEY_ENTER:             result = choice; done = 1; break;
        case 'y': case 'Y':         result = 1;      done = 1; break;
        case 'n': case 'N':
        case KEY_ESC:               result = 0;      done = 1; break;
        }
    }
    printf(ANSI_CURSOR_SHOW);
    return result;
}

/* Full-screen, centred single-line text input, matching confirm_centered's layout.
   Seeds the field with prefill (if non-empty). Returns 1 with out filled (Enter on
   a non-empty value), or 0 if cancelled (Esc). */
static int run_text_input_centered(const char *title, const char *label,
                                   char *out, size_t out_size, const char *prefill) {
    char typed[4096] = {0};
    int  typed_len   = 0;
    int  edit_pos    = 0;   /* cursor within typed[] */
    int  done = 0, cancelled = 0;
    const char *help = "\xe2\x86\x90/\xe2\x86\x92 move   Enter confirm   Esc cancel   Backspace delete";
    /* never let typing exceed the caller's buffer or our own */
    size_t cap = out_size < sizeof(typed) ? out_size : sizeof(typed);

    if (prefill && prefill[0]) {
        strncpy(typed, prefill, sizeof(typed) - 1);
        typed[sizeof(typed) - 1] = '\0';
        typed_len = (int)strlen(typed);
        edit_pos  = typed_len;
    }

    printf(ANSI_CURSOR_HIDE);
    while (!done) {
        int cols, rows;
        term_size(&cols, &rows);
        printf(ANSI_CLEAR ANSI_RESET);

        int block = 5;                       /* title, gap, field, gap, help */
        int top = (rows - block) / 2;
        for (int i = 0; i < top; i++) printf("\n");

        print_centered(cols, ANSI_BOLD, title, (int)strlen(title));
        printf("\n");
        /* field "label: value" with a highlight block cursor at edit_pos; only a
           cursor parked past the end adds a trailing column to the visible width. */
        char cur = typed[edit_pos] ? typed[edit_pos] : ' ';
        const char *rest = typed[edit_pos] ? typed + edit_pos + 1 : "";
        int vis = (int)strlen(label) + 2 + typed_len + (typed[edit_pos] ? 0 : 1);
        int pad = (cols - vis) / 2; if (pad < 0) pad = 0;
        printf("%*s" ANSI_CYAN "%s: " ANSI_RESET "%.*s" ANSI_REVERSE "%c" ANSI_REVERSE_OFF "%s" ANSI_RESET "\n",
               pad, "", label, edit_pos, typed, cur, rest);
        printf("\n");
        print_centered(cols, ANSI_DIM, help, vis_cols(help));
        fflush(stdout);

        int key = read_key();
        if (key == KEY_ENTER) {
            if (typed_len > 0) {
                strncpy(out, typed, out_size - 1);
                out[out_size - 1] = '\0';
                done = 1;
            }
        } else if (key == KEY_ESC) {
            cancelled = 1; done = 1;
        } else {
            edit_buffer_key(key, typed, &typed_len, &edit_pos, cap);
        }
    }
    printf(ANSI_CURSOR_SHOW);
    return cancelled ? 0 : 1;
}

/* ── Workspace browser: rows, breadcrumb, "/" palette ───────────────────────
   `mn open` and step 1 of `mn open edit` are the same screen: one level of the
   folder tree, drilled into with Enter and backed out of with Backspace. Rows
   are rebuilt from the store on every keystroke rather than cached, the way the
   editor's build_edit_rows does — there is no tree state to fall out of sync.

   This is its own loop rather than another run_indexed_picker callback: that
   loop is shared with the menu, search and list pickers, and neither drilling
   nor a command palette belongs in them. */

/* Children to preview under a selected folder, matching WS_FRAME_MAX_ENTRIES. */
#define WS_FOLDER_PREVIEW_MAX 6

/* The selected folder's contents, in the same left rail the selected workspace
   gets — a folder is a row like any other, so selecting one has to expand into
   something rather than leave a hole where the frame was. Nested folders keep
   their "▸" so the preview reads as a level, not a flat dump. */
static void render_ws_folder_frame(const FolderList *f, const Workspace *ws, int ws_count,
                                   const char *path) {
    int lines = 0, shown = 0;
    int total = wstree_count_children(f, path, ws, ws_count);

    for (int i = 0; i < f->count && lines < WS_FOLDER_PREVIEW_MAX; i++) {
        if (!wstree_is_child_of(f->paths[i], path)) continue;
        printf(ANSI_DIM_YELLOW "    │ " ANSI_RESET ANSI_DIM ANSI_FOLDER "\xe2\x96\xb8 %s" ANSI_RESET "\n",
               wstree_leaf(f->paths[i]));
        lines++; shown++;
    }
    for (int i = 0; i < ws_count && lines < WS_FOLDER_PREVIEW_MAX; i++) {
        if (strcmp(ws[i].folder, path) != 0) continue;
        printf(ANSI_DIM_YELLOW "    │ " ANSI_RESET ANSI_DIM "%s" ANSI_RESET "\n", ws[i].name);
        lines++; shown++;
    }

    if (total == 0)
        printf(ANSI_DIM_YELLOW "    │ " ANSI_RESET ANSI_DIM "empty" ANSI_RESET "\n");
    else if (shown < total)
        printf(ANSI_DIM_YELLOW "    │ " ANSI_RESET ANSI_DIM "\xe2\x8b\xaf %d more" ANSI_RESET "\n",
               total - shown);
}

/* "mn / NUSY4S1 / CS2030S" — ancestors grey, the level you are on gold, so the
   breadcrumb answers "where am I" before the rows do. Replaces the subtitle. */
static void print_breadcrumb(const char *cwd) {
    printf("  " ANSI_CRUMB "mn" ANSI_RESET);
    const char *p = cwd;
    while (*p) {
        const char *slash = strchr(p, WORKSPACE_FOLDER_SEP);
        int len = slash ? (int)(slash - p) : (int)strlen(p);
        printf(ANSI_CRUMB " / " ANSI_RESET);
        if (slash) printf(ANSI_CRUMB "%.*s" ANSI_RESET, len, p);
        else       printf(ANSI_BOLD ANSI_FOLDER "%.*s" ANSI_RESET, len, p);
        if (!slash) break;
        p = slash + 1;
    }
    printf(ANSI_CRUMB " /" ANSI_RESET "\n\n");
}

/* ── "/" command palette ────────────────────────────────────────────────────
   Modelled on the path-completion dropdown above: an overlay under a field that
   takes keys before the field does, with the same contract so the two feel like
   one mechanism — ↑/↓ move the highlight, Tab accepts it, Esc dismisses, and
   Enter always submits the line rather than completing it.

   A command may carry its argument inline ("/new-folder NUSY4S1"), which skips
   the follow-up prompt; without one, the command asks. */

enum { CMD_NEW_FOLDER, CMD_MOVE, CMD_RENAME, CMD_DELETE, CMD_COUNT };

typedef struct {
    const char *name;
    const char *arg;    /* "" when the command takes none */
    const char *help;
} CmdDef;

static const CmdDef CMDS[CMD_COUNT] = {
    { "/new-folder", " [name]", "create a folder in here"        },
    { "/move",       "",        "file this under another folder" },
    { "/rename",     " [name]", "rename this folder"             },
    { "/delete",     "",        "remove this folder"             },
};

typedef struct {
    int  open;
    char buf[WORKSPACE_FOLDER_MAX];
    int  len, pos;
    int  sel;
    int  match[CMD_COUNT];
    int  nmatch;
} CmdPalette;

/* A command only appears when it has something to act on: /rename with no folder
   selected would be a row you can highlight and not run. Filtering the list is
   how the palette says so — quieter than an error after the fact. */
static int cmd_applies(int cmd, int row_kind) {
    switch (cmd) {
    case CMD_NEW_FOLDER: return 1;
    case CMD_MOVE:       return row_kind == WSROW_FOLDER || row_kind == WSROW_WS;
    case CMD_RENAME:
    case CMD_DELETE:     return row_kind == WSROW_FOLDER;
    }
    return 0;
}

/* Length of the command word in buf — up to the first space, so a typed argument
   doesn't stop the name from matching. */
static int palette_word_len(const CmdPalette *p) {
    const char *sp = strchr(p->buf, ' ');
    return sp ? (int)(sp - p->buf) : p->len;
}

/* Recomputes the visible commands after an edit or a cursor move. The highlight
   returns to the top: the previous pick may no longer be in the list. */
static void palette_refresh(CmdPalette *p, int row_kind) {
    int w = palette_word_len(p);
    p->nmatch = 0;
    for (int i = 0; i < CMD_COUNT; i++) {
        if (!cmd_applies(i, row_kind)) continue;
        if (strncmp(CMDS[i].name, p->buf, (size_t)w) != 0) continue;
        p->match[p->nmatch++] = i;
    }
    p->sel = 0;
}

static void palette_open(CmdPalette *p, int row_kind) {
    memset(p, 0, sizeof(*p));
    p->open = 1;
    p->buf[0] = '/'; p->len = 1; p->pos = 1;
    palette_refresh(p, row_kind);
}

/* Replaces the typed word with the highlighted command, leaving the cursor where
   the argument goes. */
static void palette_complete(CmdPalette *p, int row_kind) {
    if (p->nmatch == 0) return;
    const CmdDef *c = &CMDS[p->match[p->sel]];
    snprintf(p->buf, sizeof(p->buf), "%s%s", c->name, c->arg[0] ? " " : "");
    p->len = (int)strlen(p->buf);
    p->pos = p->len;
    palette_refresh(p, row_kind);
}

/* The inline argument: whatever follows the command word ("" if there is none). */
static void palette_arg(const CmdPalette *p, char *arg, size_t n) {
    const char *rest = p->buf + palette_word_len(p);
    while (*rest == ' ') rest++;
    snprintf(arg, n, "%s", rest);
}

/* The command Enter would run: the highlighted match, or -1 when nothing matches.
   Enter runs the highlight rather than re-resolving the typed text, because the
   two can't disagree — typing is what filters the list, and the highlight is
   always inside it. A bare "/" has no literal meaning to submit instead (unlike
   a path field, where Enter deliberately submits what was typed), so there is no
   second thing for the key to mean. */
static int palette_selected(const CmdPalette *p) {
    return p->nmatch > 0 ? p->match[p->sel] : -1;
}

static void render_palette(const CmdPalette *p) {
    /* The field, with the same block cursor every other text field in here uses. */
    char cur = p->buf[p->pos] ? p->buf[p->pos] : ' ';
    const char *rest = p->buf[p->pos] ? p->buf + p->pos + 1 : "";
    printf("\n  " ANSI_VIOLET "%.*s" ANSI_RESET ANSI_REVERSE "%c" ANSI_REVERSE_OFF
           ANSI_VIOLET "%s" ANSI_RESET,
           p->pos, p->buf, cur, rest);

    if (p->nmatch == 0) {
        printf("\n" ANSI_SILVER "  \xe2\x94\x94 " ANSI_RESET
               ANSI_DIM "no such command  \xe2\x80\xa2  Esc dismiss" ANSI_RESET);
        return;
    }

    printf("\n" ANSI_SILVER "  \xe2\x94\x8c " ANSI_RESET ANSI_DIM "%d command%s" ANSI_RESET "\n",
           p->nmatch, p->nmatch == 1 ? "" : "s");
    for (int i = 0; i < p->nmatch; i++) {
        const CmdDef *c = &CMDS[p->match[i]];
        char label[64];
        snprintf(label, sizeof(label), "%s%s", c->name, c->arg);
        printf(ANSI_SILVER "  \xe2\x94\x82 " ANSI_RESET);
        if (i == p->sel)
            printf(ANSI_VIOLET "> %-22s" ANSI_RESET ANSI_DIM " %s" ANSI_RESET "\n", label, c->help);
        else
            printf(ANSI_DIM "  %-22s %s" ANSI_RESET "\n", label, c->help);
    }
    printf(ANSI_SILVER "  \xe2\x94\x94 " ANSI_RESET ANSI_DIM
           "Tab complete  \xe2\x80\xa2  \xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  Enter run  \xe2\x80\xa2  Esc dismiss" ANSI_RESET);
}

/* ── Browser rendering ─────────────────────────────────────────────────── */

static void render_ws_browser(const WorkspaceStore *st, const WsRow *rows, int nrows,
                              const char *cwd, int cursor, const char *title,
                              int num_input, int show_error, int edit_mode,
                              const char *err, const CmdPalette *pal) {
    print_picker_header(title, NULL);
    print_breadcrumb(cwd);

    if (nrows == 0) {
        printf("  " ANSI_DIM "%s" ANSI_RESET "\n",
               cwd[0] ? "This folder is empty." : "No workspaces yet.");
    }

    int start = picker_window_start(cursor, nrows);
    int end   = start + (nrows < PICKER_WINDOW ? nrows : PICKER_WINDOW);
    print_more_above(start);
    /* The palette needs the rows below it for context but not their detail, and
       an 8-line app frame plus the command list overruns a short terminal and
       scrolls the header away. Keep the highlight, drop the expansion. */
    int expand = !pal->open;

    for (int i = start; i < end; i++) {
        const WsRow *r    = &rows[i];
        int sel           = (num_input < 0 && i == cursor);
        const char *arrow = sel ? ANSI_ACCENT CURSOR_TOKEN ANSI_RESET : " ";

        if (r->kind == WSROW_FOLDER) {
            const char *path = st->folders.paths[r->idx];
            int kids = wstree_count_children(&st->folders, path, st->ws, st->ws_count);
            if (sel) {
                printf("  %s " ANSI_FOLDER_HL " \xe2\x96\xb8 [%d] %s " ANSI_RESET
                       " " ANSI_DIM "%d item%s" ANSI_RESET "\n",
                       arrow, i + 1, wstree_leaf(path), kids, kids == 1 ? "" : "s");
                if (expand)
                    render_ws_folder_frame(&st->folders, st->ws, st->ws_count, path);
            } else {
                printf("    " ANSI_FOLDER "\xe2\x96\xb8 [%d] %s" ANSI_RESET
                       " " ANSI_DIM "%d item%s" ANSI_RESET "\n",
                       i + 1, wstree_leaf(path), kids, kids == 1 ? "" : "s");
            }
        } else {
            const Workspace *w = &st->ws[r->idx];
            if (sel) {
                printf("  %s " ANSI_BOLD "[%d] %s" ANSI_RESET " " ANSI_DIM "(%d app%s)" ANSI_RESET "\n",
                       arrow, i + 1, w->name, w->entry_count, w->entry_count == 1 ? "" : "s");
                if (expand) render_workspace_frame(w);
            } else {
                printf(ANSI_DIM "    [%d] %s (%d app%s)" ANSI_RESET "\n",
                       i + 1, w->name, w->entry_count, w->entry_count == 1 ? "" : "s");
            }
        }
    }
    print_more_below(end, nrows);

    if (err && err[0])
        printf("\n" ANSI_YELLOW "  %s" ANSI_RESET, err);

    if (pal->open) { render_palette(pal); fflush(stdout); return; }

    if (show_error) {
        printf("\n" ANSI_YELLOW "That index doesn't exist." ANSI_RESET);
    } else if (num_input == 0) {
        printf("\n" ANSI_YELLOW "Jump target: -" ANSI_RESET);
    } else if (num_input > 0) {
        printf("\n" ANSI_YELLOW "Jump target: %d" ANSI_RESET, num_input);
    }

    const char *act = (nrows > 0 && rows[cursor].kind == WSROW_FOLDER) ? "Enter open" : "Enter select";
    printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  %s", act);
    if (cwd[0]) printf("  \xe2\x80\xa2  Backspace up");
    if (edit_mode) printf("  \xe2\x80\xa2  " ANSI_RESET ANSI_VIOLET "/" ANSI_RESET ANSI_DIM " commands");
    printf("  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
    fflush(stdout);
}

/* ── Folder chooser (/move) ─────────────────────────────────────────────────
   A plain menu of every folder plus the root, minus anywhere the thing being
   moved cannot go. Returns the chosen path in out, or 0 if cancelled. */
static int choose_folder(const WorkspaceStore *st, const char *what,
                         const char *exclude_subtree, const char *current,
                         char *out, size_t out_size) {
    const char **labels = malloc((size_t)(st->folders.count + 1) * sizeof(*labels));
    int  *ids = malloc((size_t)(st->folders.count + 1) * sizeof(*ids));
    if (labels == NULL || ids == NULL) { free(labels); free(ids); return 0; }

    int n = 0;
    if (strcmp(current, "") != 0) { labels[n] = "/  (root)"; ids[n] = -1; n++; }

    /* Sorted by path, so the chooser lists folders in the same order the browser
       shows them — the registry's insertion order would be an arbitrary third
       ordering to learn. Root, when offered, stays pinned at the top. */
    int first = n;
    for (int i = 0; i < st->folders.count; i++) {
        if (exclude_subtree && exclude_subtree[0] &&
            wstree_is_descendant(st->folders.paths[i], exclude_subtree)) continue;
        if (strcmp(st->folders.paths[i], current) == 0) continue;   /* already there */
        int j = n++;
        while (j > first && strcmp(labels[j - 1], st->folders.paths[i]) > 0) {
            labels[j] = labels[j - 1]; ids[j] = ids[j - 1]; j--;
        }
        labels[j] = st->folders.paths[i];
        ids[j] = i;
    }

    /* Nowhere to go — the only folders are the one it's already in and its own
       subtree. Say so rather than blinking and doing nothing. */
    if (n == 0) {
        free(labels); free(ids);
        return -1;
    }

    int ok = 0;
    char title[220];
    snprintf(title, sizeof(title), "Move '%.60s' to", what);
    int pick = run_menu_picker(title, NULL, labels, n);
    if (pick >= 0) {
        snprintf(out, out_size, "%s", ids[pick] < 0 ? "" : st->folders.paths[ids[pick]]);
        ok = 1;
    }
    free(labels); free(ids);
    return ok;
}

/* ── Command execution ──────────────────────────────────────────────────────
   Runs one palette command against the store. Returns 1 if anything changed.
   Failures are written to err for the browser to show in yellow, the way the
   editor already reports a rejected rename. */
/* A command's name argument: taken as typed when given inline, otherwise asked
   for. Returns 0 if the prompt was cancelled. The centred dialogs re-show the
   cursor on the way out, so it is hidden again here rather than at each caller. */
static int ask_name(const char *arg, const char *title, const char *prefill,
                    char *out, size_t out_size) {
    if (arg[0]) { snprintf(out, out_size, "%s", arg); return 1; }
    int ok = run_text_input_centered(title, "Name", out, out_size, prefill);
    printf(ANSI_CURSOR_HIDE);
    return ok;
}

static int palette_run(int cmd, const char *arg, WorkspaceStore *st, const char *cwd,
                       const WsRow *rows, int nrows, int cursor,
                       char *err, size_t err_size) {
    const WsRow *r = (nrows > 0) ? &rows[cursor] : NULL;
    char name[WORKSPACE_FOLDER_MAX];

    if (cmd == CMD_NEW_FOLDER) {
        if (!ask_name(arg, "New folder", "", name, sizeof(name))) return 0;
        if (!wstree_name_ok(name)) {
            snprintf(err, err_size, "'%.60s' isn't a usable folder name (no '/', no blank ends).", name);
            return 0;
        }
        char path[WORKSPACE_FOLDER_MAX];
        wstree_join(cwd, name, path, sizeof(path));
        int rc = wstree_add(&st->folders, path);
        if (rc == -1) { snprintf(err, err_size, "A folder named '%.60s' is already here.", name); return 0; }
        if (rc == -2) { snprintf(err, err_size, "Out of memory."); return 0; }
        return 1;
    }

    if (cmd == CMD_RENAME) {
        if (r == NULL || r->kind != WSROW_FOLDER) return 0;
        char path[WORKSPACE_FOLDER_MAX];
        snprintf(path, sizeof(path), "%s", st->folders.paths[r->idx]);
        char title[220];
        snprintf(title, sizeof(title), "Rename '%.60s'", wstree_leaf(path));
        if (!ask_name(arg, title, wstree_leaf(path), name, sizeof(name))) return 0;
        if (!wstree_name_ok(name)) {
            snprintf(err, err_size, "'%.60s' isn't a usable folder name (no '/', no blank ends).", name);
            return 0;
        }
        if (wstree_rename(&st->folders, path, name, st->ws, st->ws_count) == -1) {
            snprintf(err, err_size, "A folder named '%.60s' is already here.", name);
            return 0;
        }
        return 1;
    }

    if (cmd == CMD_DELETE) {
        if (r == NULL || r->kind != WSROW_FOLDER) return 0;
        char path[WORKSPACE_FOLDER_MAX];
        snprintf(path, sizeof(path), "%s", st->folders.paths[r->idx]);
        int kids = wstree_count_children(&st->folders, path, st->ws, st->ws_count);

        char parent[WORKSPACE_FOLDER_MAX], q[420];
        wstree_parent(path, parent, sizeof(parent));
        /* Bounded: a folder path can be as long as the buffer, and this question
           has to stay on one centred line. */
        if (kids == 0)
            snprintf(q, sizeof(q), "Remove folder '%.60s'?", wstree_leaf(path));
        else
            snprintf(q, sizeof(q), "Remove folder '%.60s'? Its %d item%s move to %.80s.",
                     wstree_leaf(path), kids, kids == 1 ? "" : "s",
                     parent[0] ? parent : "the top level");
        int yes = confirm_centered(q);
        printf(ANSI_CURSOR_HIDE);
        if (!yes) return 0;

        if (wstree_remove(&st->folders, path, st->ws, st->ws_count) == -1) {
            snprintf(err, err_size, "Can't remove '%.60s': a name inside it already exists in %.80s.",
                     wstree_leaf(path), parent[0] ? parent : "the top level");
            return 0;
        }
        return 1;
    }

    if (cmd == CMD_MOVE) {
        if (r == NULL) return 0;
        char dest[WORKSPACE_FOLDER_MAX];
        if (st->folders.count == 0) {
            snprintf(err, err_size, "There are no folders yet \xe2\x80\x94 make one with /new-folder.");
            return 0;
        }

        if (r->kind == WSROW_FOLDER) {
            char path[WORKSPACE_FOLDER_MAX];
            snprintf(path, sizeof(path), "%s", st->folders.paths[r->idx]);
            char parent[WORKSPACE_FOLDER_MAX];
            wstree_parent(path, parent, sizeof(parent));
            /* A folder can go anywhere except into itself or its own subtree. */
            int ok = choose_folder(st, wstree_leaf(path), path, parent, dest, sizeof(dest));
            printf(ANSI_CURSOR_HIDE);
            if (!ok) return 0;
            int rc = wstree_move(&st->folders, path, dest, st->ws, st->ws_count);
            if (rc == -1) {
                snprintf(err, err_size, "'%.80s' already holds a folder by that name.",
                         dest[0] ? dest : "The top level");
                return 0;
            }
            return rc == 0;
        }

        Workspace *w = &st->ws[r->idx];
        int ok = choose_folder(st, w->name, NULL, w->folder, dest, sizeof(dest));
        printf(ANSI_CURSOR_HIDE);
        if (!ok) return 0;
        snprintf(w->folder, WORKSPACE_FOLDER_MAX, "%s", dest);
        return 1;
    }
    return 0;
}

/* ── Browser loop ──────────────────────────────────────────────────────── */

int run_workspace_browser(WorkspaceStore *st, const char *title,
                          int edit_mode, int *dirty) {
    char cwd[WORKSPACE_FOLDER_MAX] = "";
    WsRow *rows = NULL;
    int rows_cap = 0;
    int cursor = 0, prev_cursor = 0, num_input = -1, show_error = 0;
    int done = 0, result = -1;
    char err[220] = {0};
    CmdPalette pal; memset(&pal, 0, sizeof(pal));

    if (dirty) *dirty = 0;
    int nrows = wstree_build_rows(&st->folders, st->ws, st->ws_count, cwd, &rows, &rows_cap);

    printf(ANSI_CURSOR_HIDE);
    render_ws_browser(st, rows, nrows, cwd, cursor, title, num_input, show_error,
                      edit_mode, err, &pal);

    while (!done) {
        int key = read_key();
        show_error = 0;
        int activate = 0;

        if (pal.open) {
            int row_kind = (nrows > 0) ? rows[cursor].kind : -1;
            if (key == KEY_ESC) {
                pal.open = 0;
            } else if (key == KEY_UP) {
                if (pal.nmatch) pal.sel = (pal.sel > 0) ? pal.sel - 1 : pal.nmatch - 1;
            } else if (key == KEY_DOWN) {
                if (pal.nmatch) pal.sel = (pal.sel + 1) % pal.nmatch;
            } else if (key == KEY_TAB) {
                palette_complete(&pal, row_kind);
            } else if (key == KEY_ENTER) {
                char arg[WORKSPACE_FOLDER_MAX];
                palette_arg(&pal, arg, sizeof(arg));
                int cmd = palette_selected(&pal);
                pal.open = 0;
                err[0] = '\0';
                if (cmd < 0) {
                    snprintf(err, sizeof(err), "No command matches '%.40s'.", pal.buf);
                } else if (palette_run(cmd, arg, st, cwd, rows, nrows, cursor,
                                       err, sizeof(err))) {
                    if (dirty) *dirty = 1;
                    nrows = wstree_build_rows(&st->folders, st->ws, st->ws_count,
                                              cwd, &rows, &rows_cap);
                    if (cursor >= nrows) cursor = nrows > 0 ? nrows - 1 : 0;
                }
            } else if ((key == KEY_BACKSPACE || key == 127) && pal.len <= 1) {
                pal.open = 0;   /* deleting the "/" leaves the palette */
            } else if (edit_buffer_key(key, pal.buf, &pal.len, &pal.pos, sizeof(pal.buf))) {
                palette_refresh(&pal, row_kind);
            }
        } else if (num_input >= 0) {
            handle_num_input(key, nrows, &num_input, &cursor,
                             &prev_cursor, &show_error, &activate);
        } else {
            switch (key) {
            case KEY_UP:    if (cursor > 0)         cursor--; break;
            case KEY_DOWN:  if (cursor < nrows - 1) cursor++; break;
            case KEY_ENTER: activate = 1;                     break;
            case KEY_ESC:   result = -1; done = 1;            break;
            case KEY_BACKSPACE:
            case 127:
                if (cwd[0]) {
                    /* Step out, and land the cursor on the folder just left —
                       backing out should return you to where you came from, not
                       to the top of the parent. */
                    char was[WORKSPACE_FOLDER_MAX];
                    snprintf(was, sizeof(was), "%s", cwd);
                    wstree_parent(was, cwd, sizeof(cwd));
                    err[0] = '\0';
                    nrows = wstree_build_rows(&st->folders, st->ws, st->ws_count,
                                              cwd, &rows, &rows_cap);
                    cursor = 0;
                    for (int i = 0; i < nrows; i++)
                        if (rows[i].kind == WSROW_FOLDER &&
                            strcmp(st->folders.paths[rows[i].idx], was) == 0) { cursor = i; break; }
                }
                break;
            default:
                if (key == '/' && edit_mode) {
                    err[0] = '\0';
                    palette_open(&pal, (nrows > 0) ? rows[cursor].kind : -1);
                } else if (key >= '1' && key <= '9') {
                    prev_cursor = cursor;
                    num_input = key - '0';
                }
                break;
            }
        }

        if (activate && nrows > 0) {
            const WsRow *r = &rows[cursor];
            if (r->kind == WSROW_FOLDER) {
                snprintf(cwd, sizeof(cwd), "%s", st->folders.paths[r->idx]);
                cursor = 0; num_input = -1; err[0] = '\0';
                nrows = wstree_build_rows(&st->folders, st->ws, st->ws_count,
                                          cwd, &rows, &rows_cap);
            } else {
                result = r->idx;
                done = 1;
            }
        }

        if (!done)
            render_ws_browser(st, rows, nrows, cwd, cursor, title, num_input,
                              show_error, edit_mode, err, &pal);
    }

    free(rows);
    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return result;
}

/* ── App screen-placement chooser ('\' in the workspace editor) ──────────────
   A self-contained visual grid for assigning an app a screen partition. The
   placement is stored as a short token ("" = none); only this module knows the
   token<->grid mapping, so it stays decoupled from save/load. */

/* quadrant-fill mask bits */
#define PL_TL 1
#define PL_TR 2
#define PL_BL 4
#define PL_BR 8
typedef struct { const char *token; const char *label; int mask; } Placement;
static const Placement PLACEMENTS[] = {
    /* Empty token = no placement: the app launches wherever it likes (default). */
    { "",       "No choice (app decides)", 0              },
    { "full",   "Full screen",  PL_TL|PL_TR|PL_BL|PL_BR   },
    { "left",   "Left half",    PL_TL|PL_BL               },
    { "right",  "Right half",   PL_TR|PL_BR               },
    { "top",    "Top half",     PL_TL|PL_TR               },
    { "bottom", "Bottom half",  PL_BL|PL_BR               },
    { "tl",     "Top-left",     PL_TL                     },
    { "tr",     "Top-right",    PL_TR                     },
    { "bl",     "Bottom-left",  PL_BL                     },
    { "br",     "Bottom-right", PL_BR                     },
};
#define PLACEMENT_COUNT ((int)(sizeof(PLACEMENTS) / sizeof(PLACEMENTS[0])))

/* Human label for a stored token (falls back to the token itself if unknown). */
static const char *placement_label(const char *token) {
    for (int i = 0; i < PLACEMENT_COUNT; i++)
        if (strcmp(PLACEMENTS[i].token, token) == 0) return PLACEMENTS[i].label;
    return token;
}

/* Bracketed human placement for a layout token: "{Left half}" or
   "{Left half · S2}" (screen suffix only when > 1); "" for no layout. Shared by
   the workspace picker frame and the editor so both render placement identically. */
static void placement_brief(const char *layout, char *out, size_t n) {
    if (layout == NULL || layout[0] == '\0') { out[0] = '\0'; return; }
    int s; char p[16];
    layout_parse(layout, &s, p, sizeof(p));
    if (s > 1)
        snprintf(out, n, "{%s \xc2\xb7 S%d}", placement_label(p), s);
    else
        snprintf(out, n, "{%s}", placement_label(p));
}

/* True if (screen, part) is occupied by another app. Each taken[] token is parsed
   so the same partition on different screens does not collide. */
static int placement_taken(int screen, const char *part,
                           const char **taken, int taken_count) {
    for (int i = 0; i < taken_count; i++) {
        int s; char p[16];
        layout_parse(taken[i], &s, p, sizeof(p));
        if (s == screen && strcmp(p, part) == 0) return 1;
    }
    return 0;
}

/* Prints one 9-wide quadrant cell: shaded with hl when filled, blank otherwise. */
static void pl_cell(int on, const char *hl) {
    if (on) printf("%s         " ANSI_RESET, hl);
    else    printf("         ");
}

/* One quadrant row is two identical text lines of a left and right cell. */
static void pl_row(int pad, int left_on, int right_on, const char *hl) {
    for (int line = 0; line < 2; line++) {
        printf("%*s|", pad, "");
        pl_cell(left_on, hl);  printf("|");
        pl_cell(right_on, hl); printf("|\n");
    }
}

/* Draws the full 2x2 placement grid for `mask`, indented by `pad` columns and
   shaded with `hl`. */
static void print_placement_grid(int pad, int mask, const char *hl) {
    printf("%*s+---------+---------+\n", pad, "");
    pl_row(pad, mask & PL_TL, mask & PL_TR, hl);
    printf("%*s+---------+---------+\n", pad, "");
    pl_row(pad, mask & PL_BL, mask & PL_BR, hl);
    printf("%*s+---------+---------+\n", pad, "");
}

/* Full-screen visual grid; layout is in/out. Returns 1 on Enter (token written),
   0 on Esc (unchanged). ←/→ pick the partition, ↑/↓ cycle the detected screens
   (dead when only one). The first option, "No choice", clears the layout so the
   app decides its own placement (empty token; the screen is ignored). Partitions
   another app already holds on the same screen (taken[]) are shown red and cannot
   be selected. */
static int run_placement_picker(const char *app_display, char *layout, size_t layout_size,
                                const char **taken, int taken_count) {
    int screens = screen_count();
    if (screens < 1) screens = 1;

    /* seed screen + partition from the app's current token */
    int screen = 1;
    char curpart[16];
    layout_parse(layout, &screen, curpart, sizeof(curpart));
    if (screen < 1 || screen > screens) screen = 1;
    int sel = 0;
    for (int i = 0; i < PLACEMENT_COUNT; i++)
        if (strcmp(PLACEMENTS[i].token, curpart) == 0) { sel = i; break; }

    int done = 0, confirmed = 0;
    const char *help = (screens > 1)
        ? "\xe2\x86\x90/\xe2\x86\x92 choose   \xe2\x86\x91/\xe2\x86\x93 screen   Enter place   Esc cancel"
        : "\xe2\x86\x90/\xe2\x86\x92 choose   Enter place   Esc cancel";

    printf(ANSI_CURSOR_HIDE);
    while (!done) {
        int cols, rows;
        term_size(&cols, &rows);
        printf(ANSI_CLEAR ANSI_RESET);

        /* "No choice" ignores the screen, so don't show a per-screen title for it. */
        int is_none = (PLACEMENTS[sel].token[0] == '\0');
        char title[160], subtitle[160];
        if (screens > 1 && !is_none) {
            snprintf(title, sizeof(title), "Screen %d", screen);
            snprintf(subtitle, sizeof(subtitle), "Place \"%s\"", app_display);
        } else {
            snprintf(title, sizeof(title), "Place \"%s\"", app_display);
            subtitle[0] = '\0';
        }

        int block = 13 + (subtitle[0] ? 1 : 0);   /* +subtitle line when shown */
        int top = (rows - block) / 2;
        for (int i = 0; i < top; i++) printf("\n");

        print_centered(cols, ANSI_BOLD, title, vis_cols(title));
        if (subtitle[0]) print_centered(cols, ANSI_DIM, subtitle, vis_cols(subtitle));
        printf("\n");

        int mask = PLACEMENTS[sel].mask;
        int occupied = placement_taken(screen, PLACEMENTS[sel].token, taken, taken_count);
        const char *hl = occupied ? ANSI_DEL_HL : ANSI_APP_HL;   /* red vs blue */
        int pad  = (cols - 21) / 2; if (pad < 0) pad = 0;
        print_placement_grid(pad, mask, hl);
        printf("\n");
        char lbl[80];
        if (occupied)
            snprintf(lbl, sizeof(lbl), "%s - already occupied", PLACEMENTS[sel].label);
        else
            snprintf(lbl, sizeof(lbl), "%s", PLACEMENTS[sel].label);
        print_centered(cols, occupied ? ANSI_RED : ANSI_CYAN, lbl, vis_cols(lbl));
        printf("\n");
        print_centered(cols, ANSI_DIM, help, vis_cols(help));
        fflush(stdout);

        int key = read_key();
        switch (key) {
        case KEY_LEFT:  sel = (sel - 1 + PLACEMENT_COUNT) % PLACEMENT_COUNT; break;
        case KEY_RIGHT: sel = (sel + 1) % PLACEMENT_COUNT;                   break;
        case KEY_UP:    if (screens > 1) screen = (screen - 2 + screens) % screens + 1; break;
        case KEY_DOWN:  if (screens > 1) screen = screen % screens + 1;                 break;
        case KEY_ENTER:
            /* disallow placing onto a partition another app already holds */
            if (!placement_taken(screen, PLACEMENTS[sel].token, taken, taken_count)) {
                confirmed = 1; done = 1;
            }
            break;
        case KEY_ESC:                  done = 1; break;
        }
    }

    if (confirmed) {
        if (PLACEMENTS[sel].token[0] == '\0')
            layout[0] = '\0';                   /* no choice — let the app decide */
        else if (screen <= 1)
            snprintf(layout, layout_size, "%s", PLACEMENTS[sel].token);
        else
            snprintf(layout, layout_size, "%d:%s", screen, PLACEMENTS[sel].token);
    }
    printf(ANSI_CURSOR_SHOW);
    return confirmed;
}

/* ── Workspace editor picker (mn open edit) ────────────────────────────── */

#define WADD_LIST      0
#define WADD_TYPE_LINK 1
#define WADD_TYPE_APP  2
#define WADD_EDIT_LINK 3

/* Friendly, normalized display name for an app value. Delegates to the
   tokenizer so all forms of the same app (path, bare name, UWP marker) and
   known apps render consistently. */
void ws_display_name(const char *app, char *out, size_t out_size) {
    app_display_token(app, out, out_size);
}

/* The editor is navigated as a flat list of rows: one per app, one per link
   under each (non-deleted) app, plus the two trailing pseudo-rows. */
enum { ROW_APP, ROW_LINK, ROW_ADD_APP, ROW_RENAME_WS, ROW_DEL_WS };
typedef struct {
    int kind;
    int app;   /* index into apps[] for APP / LINK rows, else -1 */
    int link;  /* link index within the app's links for LINK rows, else -1 */
} WeditRow;

/* ── EditLinkList helpers ───────────────────────────────────────────────────
   The targetlist_* helpers in workspace.c are typed for char[N] rows and can't
   hold an EditLink, so the editor manages its own list. */

/* Appends a link with the given current text, original text and original index
   (orig_pos = -1 for a link added this session), growing the buffer as needed. */
void editlink_push(EditLinkList *l, const char *text,
                   const char *orig, int orig_pos) {
    if (l->count >= l->cap) {
        int ncap = l->cap ? l->cap * 2 : 8;
        EditLink *p = realloc(l->items, (size_t)ncap * sizeof(EditLink));
        if (p == NULL) return;
        l->items = p; l->cap = ncap;
    }
    EditLink *e = &l->items[l->count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->text, text, WORKSPACE_TARGET_MAX - 1);
    if (orig) strncpy(e->orig, orig, WORKSPACE_TARGET_MAX - 1);
    e->orig_pos = orig_pos;
}

/* Removes the link at idx, shifting the tail down. */
static void editlink_remove(EditLinkList *l, int idx) {
    if (idx < 0 || idx >= l->count) return;
    memmove(&l->items[idx], &l->items[idx + 1],
            (size_t)(l->count - idx - 1) * sizeof(EditLink));
    l->count--;
}

/* Swaps two links in place; all per-link metadata travels with the entry. */
static void editlink_swap(EditLinkList *l, int i, int j) {
    EditLink t = l->items[i];
    l->items[i] = l->items[j];
    l->items[j] = t;
}

void editlink_free(EditLinkList *l) {
    free(l->items);
    l->items = NULL; l->count = 0; l->cap = 0;
}

/* True if the existing link at slot `idx` has been moved from its original
   position: its rank among the entry's existing (orig_pos >= 0) links, in
   current order, differs from its original index. New links (orig_pos < 0) are
   never "moved". */
static int editlink_is_moved(const EditLinkList *l, int idx) {
    const EditLink *e = &l->items[idx];
    if (e->orig_pos < 0) return 0;
    int rank = 0;
    for (int k = 0; k < idx; k++)
        if (l->items[k].orig_pos >= 0) rank++;
    return e->orig_pos != rank;
}

/* True if the existing link's text has been changed this session. */
static int editlink_is_edited(const EditLink *e) {
    return e->orig_pos >= 0 && strcmp(e->text, e->orig) != 0;
}

/* How an app's screen placement differs from the one loaded from disk. A new app
   is always PLACE_SAME: its whole row already reads as an addition, so the badge
   would only repeat that. */
enum { PLACE_SAME, PLACE_ADDED, PLACE_EDITED, PLACE_REMOVED };

static int placement_change(const WsEditorApp *a) {
    if (a->is_new) return PLACE_SAME;
    int had = a->orig_layout[0] != '\0';
    int has = a->layout[0] != '\0';
    if (!had && !has) return PLACE_SAME;
    if (!had)         return PLACE_ADDED;
    if (!has)         return PLACE_REMOVED;
    return strcmp(a->layout, a->orig_layout) != 0 ? PLACE_EDITED : PLACE_SAME;
}

/* Flattens the editor state into the navigable row list, growing the heap buffer
   as needed; returns the row count. rows and cap are in/out. Links of an app staged
   for deletion are collapsed (the whole app is going). */
static int build_edit_rows(const WsEditorApp *apps, int count,
                           WeditRow **rows, int *cap) {
    int need = 3;   /* the three trailing pseudo-rows */
    for (int i = 0; i < count; i++) {
        need++;
        if (!apps[i].marked_delete)
            need += apps[i].links.count;
    }
    if (need > *cap) {
        int ncap = *cap ? *cap : 16;
        while (ncap < need) ncap *= 2;
        WeditRow *p = realloc(*rows, (size_t)ncap * sizeof(WeditRow));
        if (p != NULL) { *rows = p; *cap = ncap; }
    }
    int n = 0;
    WeditRow *rw = *rows;
    for (int i = 0; i < count && n + 3 < *cap; i++) {
        rw[n].kind = ROW_APP; rw[n].app = i; rw[n].link = -1; n++;
        if (apps[i].marked_delete) continue;
        for (int k = 0; k < apps[i].links.count && n + 3 < *cap; k++) {
            rw[n].kind = ROW_LINK; rw[n].app = i; rw[n].link = k; n++;
        }
    }
    rw[n].kind = ROW_ADD_APP;    rw[n].app = -1; rw[n].link = -1; n++;
    rw[n].kind = ROW_RENAME_WS;  rw[n].app = -1; rw[n].link = -1; n++;
    rw[n].kind = ROW_DEL_WS;     rw[n].app = -1; rw[n].link = -1; n++;
    return n;
}

/* Swaps the link at index `link` with its neighbour in the direction of `key`
   (KEY_LEFT = toward 0, KEY_RIGHT = toward the end) within the app's single link
   list, and moves the row cursor to follow it. The whole EditLink (text and all
   metadata) travels with the swap, so any link can move anywhere among the
   entry's links. No-op at a list boundary. */
static void reorder_link(WsEditorApp *a, int link, int key, int *cursor) {
    int j = (key == KEY_LEFT) ? link - 1 : link + 1;
    if (j < 0 || j >= a->links.count) return;   /* at the boundary — nothing to swap */
    editlink_swap(&a->links, link, j);
    *cursor += (key == KEY_LEFT) ? -1 : 1;
}

static void render_workspace_edit(const char *ws_name,
                                  const WsEditorApp *apps,
                                  const WeditRow *rows, int nrows,
                                  int cursor, int mode,
                                  const char *typed, const char *app_error,
                                  int type_app, int reordering, int edit_pos,
                                  const PathSuggest *sg) {
    char title[140];
    snprintf(title, sizeof(title), "Edit '%s'", ws_name);
    print_picker_header(title, NULL);

    int start = picker_window_start(cursor, nrows);
    int end   = start + (nrows < PICKER_WINDOW ? nrows : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        const WeditRow *r = &rows[i];
        const char *arrow = (i == cursor) ? ANSI_ACCENT CURSOR_TOKEN ANSI_RESET : " ";
        if (r->kind == ROW_ADD_APP) {
            printf("  %s " ANSI_ADD_HL " + Add a new app " ANSI_RESET "\n", arrow);
        } else if (r->kind == ROW_RENAME_WS) {
            printf("  %s " ANSI_REN_HL " ~ Rename this workspace " ANSI_RESET "\n", arrow);
        } else if (r->kind == ROW_DEL_WS) {
            printf("  %s " ANSI_DEL_HL " - Remove this workspace " ANSI_RESET "\n", arrow);
        } else if (r->kind == ROW_APP) {
            const WsEditorApp *a = &apps[r->app];
            if (a->marked_delete) {
                printf("  %s " ANSI_RED "- %s" ANSI_RESET, arrow, a->display);
            } else if (a->is_new) {
                printf("  %s " ANSI_GREEN "+ %s" ANSI_RESET, arrow, a->display);
            } else {
                printf("  %s " ANSI_APP_HL " %s " ANSI_RESET, arrow, a->display);
            }
            /* Placement badge. A staged change is spelled the way a changed link
               is — glyph first, then colour — so "+/e/-" mean the same thing on
               both kinds of row. A cleared placement still shows a badge (the old
               one, struck in red); otherwise it would just silently disappear. */
            if (!a->marked_delete) {
                int    ch = placement_change(a);
                char place[80];
                if (ch == PLACE_REMOVED) {
                    placement_brief(a->orig_layout, place, sizeof(place));
                    printf("  " ANSI_DEL_HL " - %s " ANSI_RESET, place);
                } else if (a->layout[0]) {
                    placement_brief(a->layout, place, sizeof(place));
                    if (ch == PLACE_ADDED)
                        printf("  " ANSI_ADD_HL " + %s " ANSI_RESET, place);
                    else if (ch == PLACE_EDITED)
                        printf("  " ANSI_EDIT_HL " e %s " ANSI_RESET, place);
                    else
                        printf("  " ANSI_APP_HL " %s " ANSI_RESET, place);
                }
            }
            printf("\n");
        } else { /* ROW_LINK */
            const WsEditorApp *a = &apps[r->app];
            const EditLink *e = &a->links.items[r->link];
            if (e->deleted) {
                printf("  %s    " ANSI_RED "- %s" ANSI_RESET "\n", arrow, e->text);
            } else if (mode == WADD_EDIT_LINK && i == cursor) {
                /* inline edit: purple field lift with a highlight block cursor at edit_pos */
                char cur = typed[edit_pos] ? typed[edit_pos] : ' ';
                const char *rest = typed[edit_pos] ? typed + edit_pos + 1 : "";
                printf("  %s    " ANSI_EDIT_HL " e %.*s" ANSI_REVERSE "%c" ANSI_REVERSE_OFF "%s " ANSI_RESET "\n",
                       arrow, edit_pos, typed, cur, rest);
            } else if (i == cursor && reordering) {
                printf("  %s    " ANSI_LIFT_HL " \xe2\x86\x94 %s " ANSI_RESET "\n", arrow, e->text);
            } else {
                int edited = editlink_is_edited(e);
                int moved  = editlink_is_moved(&a->links, r->link);
                if (e->orig_pos < 0)
                    printf("  %s    " ANSI_GREEN "+ %s" ANSI_RESET "\n", arrow, e->text);
                else if (edited && moved)
                    printf("  %s    " ANSI_MAGENTA "e" ANSI_CYAN "\xe2\x86\x95 %s" ANSI_RESET "\n", arrow, e->text);
                else if (edited)
                    printf("  %s    " ANSI_MAGENTA "e %s" ANSI_RESET "\n", arrow, e->text);
                else if (moved)
                    printf("  %s    " ANSI_CYAN "\xe2\x86\x95 %s" ANSI_RESET "\n", arrow, e->text);
                else
                    printf("  %s    " ANSI_DIM_YELLOW "\xe2\x86\x92 %s" ANSI_RESET "\n", arrow, e->text);
            }
        }
    }
    print_more_below(end, nrows);

    if (mode == WADD_TYPE_LINK) {
        /* highlight block cursor at edit_pos (a space when parked past the end) */
        char cur = typed[edit_pos] ? typed[edit_pos] : ' ';
        const char *rest = typed[edit_pos] ? typed + edit_pos + 1 : "";
        printf("\n" ANSI_CYAN "  Add link for %s: " ANSI_RESET
               "%.*s" ANSI_REVERSE "%c" ANSI_REVERSE_OFF "%s",
               apps[type_app].display, edit_pos, typed, cur, rest);
        if (suggest_open(sg)) render_suggestions(sg);
        else printf("\n" ANSI_DIM "  \xe2\x86\x90/\xe2\x86\x92 move  \xe2\x80\xa2  Enter add  \xe2\x80\xa2  Esc cancel  \xe2\x80\xa2  Backspace delete" ANSI_RESET);
    } else if (mode == WADD_TYPE_APP) {
        /* highlight block cursor at edit_pos (a space when parked past the end) */
        char cur = typed[edit_pos] ? typed[edit_pos] : ' ';
        const char *rest = typed[edit_pos] ? typed + edit_pos + 1 : "";
        if (app_error && app_error[0])
            printf("\n" ANSI_YELLOW "  %s" ANSI_RESET, app_error);
        printf("\n" ANSI_CYAN "  New app: " ANSI_RESET
               "%.*s" ANSI_REVERSE "%c" ANSI_REVERSE_OFF "%s",
               edit_pos, typed, cur, rest);
        if (suggest_open(sg)) render_suggestions(sg);
        else printf("\n" ANSI_DIM "  \xe2\x86\x90/\xe2\x86\x92 move  \xe2\x80\xa2  Enter add  \xe2\x80\xa2  Esc cancel  \xe2\x80\xa2  Backspace delete" ANSI_RESET);
    } else if (mode == WADD_EDIT_LINK) {
        if (suggest_open(sg)) render_suggestions(sg);
        else printf("\n" ANSI_DIM "\xe2\x86\x90/\xe2\x86\x92 move  \xe2\x80\xa2  type to edit  \xe2\x80\xa2  Enter save  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
    } else {
        const WeditRow *r = &rows[cursor];
        if (app_error && app_error[0])
            printf("\n" ANSI_YELLOW "  %s" ANSI_RESET, app_error);
        if (r->kind == ROW_ADD_APP) {
            printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  type or Enter to add an app  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
        } else if (r->kind == ROW_RENAME_WS) {
            printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  Enter to rename this workspace  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
        } else if (r->kind == ROW_DEL_WS) {
            printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  Enter to remove this workspace  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
        } else if (r->kind == ROW_APP && apps[r->app].marked_delete) {
            printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  Backspace undo remove  \xe2\x80\xa2  Enter save  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
        } else if (r->kind == ROW_APP) {
            printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  type to add link  \xe2\x80\xa2  \\ place  \xe2\x80\xa2  Backspace remove  \xe2\x80\xa2  Enter save  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
        } else { /* link row */
            if (reordering)
                printf("\n" ANSI_DIM "\xe2\x86\x90/\xe2\x86\x92 reorder  \xe2\x80\xa2  Enter to place  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
            else
                printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  \xe2\x86\x90/\xe2\x86\x92 reorder  \xe2\x80\xa2  e edit  \xe2\x80\xa2  Backspace remove  \xe2\x80\xa2  Enter save  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
        }
    }
    fflush(stdout);
}

int run_workspace_edit_picker(char *ws_name, size_t ws_name_size,
                              const char **taken_names, int taken_count,
                              WsEditorApp *apps, int *count, int max,
                              int *delete_workspace) {
    WeditRow *rows = NULL;
    int  rows_cap  = 0;
    int  cursor    = 0;
    int  mode      = WADD_LIST;
    char typed[WORKSPACE_TARGET_MAX] = {0};
    int  typed_len = 0;
    char app_error[128] = {0};
    int  type_app  = 0;   /* app being given a new link in WADD_TYPE_LINK */
    int  reordering = 0;    /* 1 while a link is "lifted" for repositioning (move mode) */
    int  edit_pos  = 0;   /* cursor within typed[] while typing/editing a link */
    int  edit_app  = 0;   /* app whose link is being edited in WADD_EDIT_LINK */
    int  edit_link = 0;   /* link index being edited in WADD_EDIT_LINK */
    int  done      = 0;
    int  cancelled = 0;
    PathSuggest sg;       /* path completion for whichever text field is open */

    suggest_reset(&sg, typed);
    *delete_workspace = 0;
    int nrows = build_edit_rows(apps, *count, &rows, &rows_cap);

    printf(ANSI_CURSOR_HIDE);
    render_workspace_edit(ws_name, apps, rows, nrows, cursor, mode,
                          typed, app_error, type_app, reordering, edit_pos, &sg);

    while (!done) {
        int key = read_key();
        app_error[0] = '\0';

        if (mode == WADD_TYPE_LINK) {
            /* Keying in a new link's path/url. ←/→ move the in-string cursor,
               printable keys insert at it, Backspace deletes the char before it
               (same in-place editing as WADD_EDIT_LINK). Path completion sees
               the key first, so ↑/↓/Tab drive the dropdown when it's open. */
            if (suggest_key(&sg, key, typed, &typed_len, &edit_pos, sizeof(typed))) {
                /* consumed by the dropdown */
            } else if (key == KEY_ENTER) {
                if (typed_len > 0)
                    editlink_push(&apps[type_app].links, typed, "", -1);
                typed[0] = '\0'; typed_len = 0; edit_pos = 0;
                mode = WADD_LIST;
            } else if (key == KEY_ESC) {
                typed[0] = '\0'; typed_len = 0; edit_pos = 0;
                mode = WADD_LIST;
            } else if (edit_buffer_key(key, typed, &typed_len, &edit_pos, sizeof(typed))) {
                suggest_refresh(&sg, typed);
            }
        } else if (mode == WADD_TYPE_APP) {
            if (suggest_key(&sg, key, typed, &typed_len, &edit_pos, sizeof(typed))) {
                /* consumed by the dropdown */
            } else if (key == KEY_ENTER) {
                if (typed_len > 0 && *count < max) {
                    char resolved[WORKSPACE_APP_MAX] = {0};
                    const char *final_app = typed;
                    int valid = 1;
                    const char *launcher = new_window_launcher(typed);
                    if (launcher != NULL) {
                        /* Known IDE launcher — skip resolution, but store the
                           canonical name: "Code" must be saved as the "code"
                           CLI, not handed to a bundle-name lookup that can't
                           find it. */
                        final_app = launcher;
                    } else {
                        if (app_resolve(typed, resolved, sizeof(resolved)) && resolved[0])
                            final_app = resolved;
                        if (!app_value_exists(final_app)) {
                            snprintf(app_error, sizeof(app_error),
                                     "App not found: '%.60s'. Try a full path.", typed);
                            typed[0] = '\0'; typed_len = 0; edit_pos = 0;
                            valid = 0;
                        }
                    }
                    if (valid) {
                        WsEditorApp *na = &apps[*count];
                        memset(na, 0, sizeof(*na));
                        strncpy(na->app, final_app, WORKSPACE_APP_MAX - 1);
                        ws_display_name(final_app, na->display, sizeof(na->display));
                        na->is_new = 1;
                        (*count)++;
                        typed[0] = '\0'; typed_len = 0; edit_pos = 0;
                        mode = WADD_LIST;
                        /* park the cursor on the freshly added app row */
                        nrows = build_edit_rows(apps, *count, &rows, &rows_cap);
                        for (int i = 0; i < nrows; i++)
                            if (rows[i].kind == ROW_APP && rows[i].app == *count - 1) { cursor = i; break; }
                    }
                }
            } else if (key == KEY_ESC) {
                typed[0] = '\0'; typed_len = 0; edit_pos = 0;
                mode = WADD_LIST;
            } else if (edit_buffer_key(key, typed, &typed_len, &edit_pos, sizeof(typed))) {
                suggest_refresh(&sg, typed);
            }
        } else if (mode == WADD_EDIT_LINK) {
            /* ── Inline edit mode ───────────────────────────────────────────
               Editing one link's text in place. ←/→ move the in-string cursor,
               printable keys insert at it, Backspace deletes the char before it.
               Enter commits the buffer back into the link; Esc discards the edit
               (the link keeps its previous text) — neither exits the picker.
               Path completion sees the key first, as in the other text fields. */
            if (suggest_key(&sg, key, typed, &typed_len, &edit_pos, sizeof(typed))) {
                /* consumed by the dropdown */
            } else if (key == KEY_ENTER) {
                strncpy(apps[edit_app].links.items[edit_link].text, typed,
                        WORKSPACE_TARGET_MAX - 1);
                apps[edit_app].links.items[edit_link].text[WORKSPACE_TARGET_MAX - 1] = '\0';
                typed[0] = '\0'; typed_len = 0; edit_pos = 0;
                mode = WADD_LIST;
            } else if (key == KEY_ESC) {
                typed[0] = '\0'; typed_len = 0; edit_pos = 0;
                mode = WADD_LIST;
            } else if (edit_buffer_key(key, typed, &typed_len, &edit_pos, sizeof(typed))) {
                suggest_refresh(&sg, typed);
            }
        } else if (reordering) {
            /* ── Move mode ──────────────────────────────────────────────────
               A link is lifted. Only ←/→ reposition it and Enter locks it in;
               ↑/↓, Backspace and typing are ignored so it can't be deleted or
               left behind mid-move. Esc still cancels the whole edit. */
            const WeditRow *r = &rows[cursor];
            if (key == KEY_LEFT || key == KEY_RIGHT) {
                reorder_link(&apps[r->app], r->link, key, &cursor);
            } else if (key == KEY_ENTER) {
                reordering = 0;   /* lock in; the ↕ marker now reflects the new order */
            } else if (key == KEY_ESC) {
                cancelled = 1; done = 1;
            }
        } else { /* WADD_LIST (normal navigation) */
            const WeditRow *r = &rows[cursor];
            switch (key) {
            case KEY_UP:    if (cursor > 0)         cursor--; break;
            case KEY_DOWN:  if (cursor < nrows - 1) cursor++; break;
            case KEY_LEFT:
            case KEY_RIGHT:
                /* lift a link into move mode (also applies this first nudge) */
                if (r->kind == ROW_LINK && !apps[r->app].links.items[r->link].deleted) {
                    reordering = 1;
                    reorder_link(&apps[r->app], r->link, key, &cursor);
                }
                break;
            case KEY_ENTER:
                if (r->kind == ROW_ADD_APP) {
                    mode = WADD_TYPE_APP;
                    edit_pos = 0;
                    suggest_reset(&sg, typed);
                } else if (r->kind == ROW_RENAME_WS) {
                    char newname[WORKSPACE_NAME_MAX];
                    if (run_text_input_centered("Rename workspace", "Name",
                                                newname, sizeof(newname), ws_name)) {
                        int taken = 0;
                        for (int t = 0; t < taken_count; t++)
                            if (strcmp(newname, taken_names[t]) == 0) { taken = 1; break; }
                        if (taken)
                            snprintf(app_error, sizeof(app_error),
                                     "A workspace named '%.60s' already exists.", newname);
                        else {
                            strncpy(ws_name, newname, ws_name_size - 1);
                            ws_name[ws_name_size - 1] = '\0';
                        }
                    }
                    printf(ANSI_CURSOR_HIDE);   /* input box re-showed the cursor */
                } else if (r->kind == ROW_DEL_WS) {
                    char q[WORKSPACE_NAME_MAX + 32];
                    snprintf(q, sizeof(q), "Remove workspace '%s'?", ws_name);
                    if (confirm_centered(q)) {
                        *delete_workspace = 1;
                        done = 1;
                    } else {
                        printf(ANSI_CURSOR_HIDE);   /* confirm screen re-showed it */
                    }
                } else {
                    done = 1;
                }
                break;
            case KEY_ESC:
                cancelled = 1; done = 1;
                break;
            case KEY_BACKSPACE:
            case 127:
                if (r->kind == ROW_APP) {
                    int ai = r->app;
                    if (apps[ai].is_new) {
                        /* unsaved app → free its heap list, then shift the tail down
                           (a struct move would otherwise leak the freed slot's data). */
                        editlink_free(&apps[ai].links);
                        memmove(&apps[ai], &apps[ai + 1],
                                (size_t)(*count - ai - 1) * sizeof(WsEditorApp));
                        (*count)--;
                        memset(&apps[*count], 0, sizeof(WsEditorApp));
                    } else {
                        apps[ai].marked_delete = !apps[ai].marked_delete;
                    }
                } else if (r->kind == ROW_LINK) {
                    EditLink *e = &apps[r->app].links.items[r->link];
                    if (e->orig_pos >= 0)
                        e->deleted = !e->deleted;       /* existing link → toggle removal */
                    else
                        editlink_remove(&apps[r->app].links, r->link);  /* unsaved → drop */
                }
                /* ROW_DEL_WS: Backspace does nothing; use Enter to confirm removal. */
                break;
            default:
                if (key == '\\' && r->kind == ROW_APP && !apps[r->app].marked_delete) {
                    /* '\' on an app → choose a screen placement. Partitions other
                       (non-deleted) apps already hold are passed in so they can't
                       be picked twice. */
                    const char *taken[WORKSPACE_ENTRIES_MAX];
                    int tk = 0;
                    for (int a = 0; a < *count; a++) {
                        if (a == r->app || apps[a].marked_delete) continue;
                        if (apps[a].layout[0]) taken[tk++] = apps[a].layout;
                    }
                    run_placement_picker(apps[r->app].display, apps[r->app].layout,
                                         sizeof(apps[r->app].layout), taken, tk);
                    printf(ANSI_CURSOR_HIDE);   /* chooser re-showed the cursor */
                } else if (key == 'e' && r->kind == ROW_LINK &&
                    !apps[r->app].links.items[r->link].deleted) {
                    /* 'e' on a link → edit its text in place */
                    edit_app  = r->app;
                    edit_link = r->link;
                    strncpy(typed, apps[r->app].links.items[r->link].text,
                            sizeof(typed) - 1);
                    typed[sizeof(typed) - 1] = '\0';
                    typed_len = (int)strlen(typed);
                    edit_pos  = typed_len;
                    mode      = WADD_EDIT_LINK;
                    suggest_reset(&sg, typed);
                } else if (key >= 33 && key < 127) {
                    if (r->kind == ROW_APP && !apps[r->app].marked_delete) {
                        /* printable key on an app row → start adding a link */
                        mode      = WADD_TYPE_LINK;
                        type_app  = r->app;
                        typed[0]  = (char)key;
                        typed[1]  = '\0';
                        typed_len = 1;
                        edit_pos  = 1;
                        suggest_reset(&sg, typed);
                    } else if (r->kind == ROW_ADD_APP && *count < max) {
                        /* printable key on "[+ Add a new app]" → open app-type mode */
                        mode      = WADD_TYPE_APP;
                        typed[0]  = (char)key;
                        typed[1]  = '\0';
                        typed_len = 1;
                        edit_pos  = 1;
                        suggest_reset(&sg, typed);
                    }
                }
                break;
            }
        }

        if (!done) {
            nrows = build_edit_rows(apps, *count, &rows, &rows_cap);
            if (cursor >= nrows) cursor = nrows - 1;
            if (cursor < 0)      cursor = 0;
            render_workspace_edit(ws_name, apps, rows, nrows, cursor, mode,
                                  typed, app_error, type_app, reordering, edit_pos, &sg);
        }
    }

    free(rows);
    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return cancelled ? 0 : 1;
}

/* ── Single styled text input box ──────────────────────────────────────── */

static void render_text_input(const char *title, const char *subtitle,
                              const char *label, const char *typed, int edit_pos) {
    print_picker_header(title, subtitle);
    /* highlight block cursor at edit_pos (a space when parked past the end) */
    char cur = typed[edit_pos] ? typed[edit_pos] : ' ';
    const char *rest = typed[edit_pos] ? typed + edit_pos + 1 : "";
    printf("\n" ANSI_CYAN "  %s: " ANSI_RESET "%.*s" ANSI_REVERSE "%c" ANSI_REVERSE_OFF "%s",
           label, edit_pos, typed, cur, rest);
    printf("\n\n" ANSI_DIM "\xe2\x86\x90/\xe2\x86\x92 move  \xe2\x80\xa2  Enter confirm  \xe2\x80\xa2  Backspace delete  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
    fflush(stdout);
}

int run_text_input(const char *title, const char *subtitle, const char *label,
                   char *out, size_t out_size, int allow_empty, const char *prefill) {
    char typed[4096] = {0};
    int  typed_len   = 0;
    int  edit_pos    = 0;   /* cursor within typed[] */
    int  done        = 0;
    int  cancelled   = 0;

    if (prefill && prefill[0]) {
        strncpy(typed, prefill, sizeof(typed) - 1);
        typed[sizeof(typed) - 1] = '\0';
        typed_len = (int)strlen(typed);
        edit_pos  = typed_len;
    }

    printf(ANSI_CURSOR_HIDE);
    render_text_input(title, subtitle, label, typed, edit_pos);

    while (!done) {
        int key = read_key();
        if (key == KEY_ENTER) {
            if (typed_len > 0 || allow_empty) {
                strncpy(out, typed, out_size - 1);
                out[out_size - 1] = '\0';
                done = 1;
            }
        } else if (key == KEY_ESC) {
            cancelled = 1; done = 1;
        } else {
            edit_buffer_key(key, typed, &typed_len, &edit_pos, sizeof(typed));
        }
        if (!done) render_text_input(title, subtitle, label, typed, edit_pos);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return cancelled ? 0 : 1;
}
