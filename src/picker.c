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
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    char c;
    read(STDIN_FILENO, &c, 1);

    int result = (int)(unsigned char)c;
    if (c == '\033') {
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        char seq[2] = {0, 0};
        read(STDIN_FILENO, &seq[0], 1);
        read(STDIN_FILENO, &seq[1], 1);
        if (seq[0] == '[') {
            if (seq[1] == 'A') result = KEY_UP;
            if (seq[1] == 'B') result = KEY_DOWN;
            if (seq[1] == 'C') result = KEY_RIGHT;
            if (seq[1] == 'D') result = KEY_LEFT;
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
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

/* ── Generic menu picker ───────────────────────────────────────────────── */

static void print_picker_header(const char *title, const char *subtitle) {
    printf(ANSI_CLEAR ANSI_RESET);
    printf(ANSI_BOLD ANSI_CYAN "▶ %s" ANSI_RESET "\n", title);
    printf(ANSI_DIM "%s" ANSI_RESET "\n\n", subtitle);
}

static void print_picker_footer(int num_input, int show_error) {
    if (show_error) {
        printf("\n" ANSI_YELLOW "That index doesn't exist." ANSI_RESET);
    } else if (num_input >= 0) {
        if (num_input == 0)
            printf("\n" ANSI_YELLOW "Jump target: -" ANSI_RESET);
        else
            printf("\n" ANSI_YELLOW "Jump target: %d" ANSI_RESET, num_input);
    }
    printf("\n" ANSI_DIM "↑/↓ move  •  Enter select  •  Esc cancel  •  1-9 jump" ANSI_RESET);
    fflush(stdout);
}

static void render_menu_list(const char *title, const char *subtitle,
                             const char **list, int display, int selected,
                             int num_input, int show_error) {
    print_picker_header(title, subtitle);
    for (int i = 0; i < display; i++) {
        if (num_input < 0 && i == selected) {
            printf("   " ANSI_GREEN CURSOR_TOKEN ANSI_SEL_HL "[%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        } else {
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        }
    }
    print_picker_footer(num_input, show_error);
}

int run_menu_picker(const char *title, const char *subtitle,
                    const char **list, int display) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    printf(ANSI_CURSOR_HIDE);
    render_menu_list(title, subtitle, list, display, selected, num_input, show_error);

    while (!done) {
        int key = read_key();
        show_error = 0;

        if (num_input >= 0) {
            handle_num_input(key, display, &num_input, &selected,
                             &prev_selected, &show_error, &done);
        } else {
            switch (key) {
            case KEY_UP:    if (selected > 0)           selected--; break;
            case KEY_DOWN:  if (selected < display - 1) selected++; break;
            case KEY_ENTER: done = 1;                               break;
            case KEY_ESC:   selected = -1; done = 1;                break;
            default:
                if (key >= '1' && key <= '9') {
                    prev_selected = selected;
                    num_input = key - '0';
                }
                break;
            }
        }

        if (!done)
            render_menu_list(title, subtitle, list, display, selected, num_input, show_error);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
}

int run_ide_picker(const char **list, int display) {
    return run_menu_picker("Choose a default IDE",
                           "Use the arrow keys to move, Enter to confirm, Esc to cancel.",
                           list, display);
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
                               int cursor, int mode, const char *typed, int type_app) {
    print_picker_header(title, subtitle);
    int start = picker_window_start(cursor, nrows);
    int end   = start + (nrows < PICKER_WINDOW ? nrows : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        const MsRow *r = &rows[i];
        const char *cur = (i == cursor) ? ANSI_GREEN CURSOR_TOKEN ANSI_RESET : " ";
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
        printf("\n" ANSI_CYAN "  Add link for %s: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET,
               labels[type_app], typed);
        printf("\n" ANSI_DIM "  Enter add  •  Esc cancel  •  Backspace delete" ANSI_RESET);
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

    int nrows = build_ms_rows(count, selected, links, &rows, &rows_cap);
    printf(ANSI_CURSOR_HIDE);
    render_multiselect(title, subtitle, labels, selected, links,
                       rows, nrows, cursor, mode, typed, type_app);

    while (!done) {
        int key = read_key();

        if (mode == MS_MODE_TYPE) {
            if (key == KEY_ENTER) {
                if (typed_len > 0)
                    targetlist_push(&links[type_app].items, &links[type_app].cap,
                                    &links[type_app].count, typed);
                mode = MS_MODE_LIST; typed[0] = '\0'; typed_len = 0;
            } else if (key == KEY_ESC) {
                mode = MS_MODE_LIST; typed[0] = '\0'; typed_len = 0;
            } else if (key == KEY_BACKSPACE || key == 127) {
                if (typed_len > 0) typed[--typed_len] = '\0';
            } else if (key >= 32 && key < 127) {
                if (typed_len < (int)sizeof(typed) - 1) {
                    typed[typed_len++] = (char)key;
                    typed[typed_len]   = '\0';
                }
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
                }
                break;
            }
        }

        if (!done) {
            nrows = build_ms_rows(count, selected, links, &rows, &rows_cap);
            if (cursor >= nrows) cursor = nrows - 1;
            if (cursor < 0)      cursor = 0;
            render_multiselect(title, subtitle, labels, selected, links,
                               rows, nrows, cursor, mode, typed, type_app);
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
    int at_line_start = 1;
    if (dimmed) printf(ANSI_DIM);
    printf("    ");
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
    print_picker_header("Search results", "Use the arrow keys to move, Enter to open, Esc to cancel.");
    int start = picker_window_start(selected, count);
    int end   = start + (count < PICKER_WINDOW ? count : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        if (i > start) print_result_divider(i != selected || num_input >= 0);
        if (num_input < 0 && i == selected) {
            printf("   " ANSI_GREEN CURSOR_TOKEN ANSI_SEL_HL "[%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 0);
        } else {
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 1);
        }
        printf("\n");
    }
    print_more_below(end, count);
    print_picker_footer(num_input, show_error);
}

int run_search_picker(SearchResult *results, int count) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    printf(ANSI_CURSOR_HIDE);
    render_results(results, count, selected, num_input, show_error);

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

        if (!done) render_results(results, count, selected, num_input, show_error);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
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

static void render_list(IndexEntry *entries, int count, int selected,
                        int num_input, int show_error,
                        const char *title, const char *subtitle) {
    print_picker_header(title, subtitle);
    int start = picker_window_start(selected, count);
    int end   = start + (count < PICKER_WINDOW ? count : PICKER_WINDOW);
    print_more_above(start);
    
    char prev_dir[4096] = "";
    for (int i = start; i < end; i++) {
        char dir[4096];
        find_parent_dir(entries[i].original_path, dir, sizeof(dir));

        /* Print a dim header only when the directory changes from the previous row. */
        if (strcmp(dir, prev_dir) != 0) {
#ifdef _WIN32
            printf(ANSI_DIM "%s\\" ANSI_RESET "\n", dir);
#else
            printf(ANSI_DIM "%s/" ANSI_RESET "\n", dir);
#endif
            strcpy(prev_dir, dir);
        }

        /* Then the existing row, but show just the basename now since the dir is in the header. */
#ifdef _WIN32
        const char *base = strrchr(entries[i].original_path, '\\');
#else
        const char *base = strrchr(entries[i].original_path, '/');
#endif
        base = base ? base + 1 : entries[i].original_path;

        if (num_input < 0 && i == selected)
            printf("   " ANSI_GREEN CURSOR_TOKEN ANSI_SEL_HL "[%d] %s" ANSI_RESET "\n", i + 1, base);
        else
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, base);
    }

    print_more_below(end, count);
    print_picker_footer(num_input, show_error);
}

int run_list_picker(IndexEntry *entries, int count,
                    const char *title, const char *subtitle) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    printf(ANSI_CURSOR_HIDE);
    render_list(entries, count, selected, num_input, show_error, title, subtitle);

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

        if (!done) render_list(entries, count, selected, num_input, show_error, title, subtitle);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
}

/* ── Workspace picker ──────────────────────────────────────────────────── */

/* Most entries to show under the selected workspace before collapsing the rest
   into a "⋯ K more" line, so a large workspace can't overflow. */
#define WS_FRAME_MAX_ENTRIES 8

/* Render the selected workspace's entries along a left rail (a dimmed-yellow
   vertical bar per line, no right border so long paths/URLs are never
   truncated). */
static void render_workspace_frame(const Workspace *w) {
    if (w->entry_count == 0) return;
    int lines = 0;
    for (int j = 0; j < w->entry_count; j++) {
        if (lines >= WS_FRAME_MAX_ENTRIES) {
            printf(ANSI_DIM_YELLOW "    │ \xe2\x8b\xaf \xe2\x80\xa6" ANSI_RESET "\n");
            break;
        }
        char disp[256];
        ws_display_name(w->entries[j].app, disp, sizeof(disp));
        int tc = w->entries[j].target_count;
        if (tc == 0) {
            printf(ANSI_DIM_YELLOW "    │ %s" ANSI_RESET "\n", disp);
            lines++;
        } else if (tc == 1) {
            printf(ANSI_DIM_YELLOW "    │ %s \xe2\x86\x92 %s" ANSI_RESET "\n",
                   disp, w->entries[j].targets[0]);
            lines++;
        } else {
            /* Show app name on its own line, then each target indented. */
            if (lines < WS_FRAME_MAX_ENTRIES) {
                printf(ANSI_DIM_YELLOW "    │ %s" ANSI_RESET "\n", disp);
                lines++;
            }
            for (int k = 0; k < tc && lines < WS_FRAME_MAX_ENTRIES; k++) {
                printf(ANSI_DIM_YELLOW "    │    \xe2\x86\x92 %s" ANSI_RESET "\n",
                       w->entries[j].targets[k]);
                lines++;
            }
        }
    }
}

static void render_workspace_list(Workspace *ws, int count, int selected,
                                  int num_input, int show_error,
                                  const char *title, const char *subtitle) {
    print_picker_header(title, subtitle);
    int start = picker_window_start(selected, count);
    int end   = start + (count < PICKER_WINDOW ? count : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        if (num_input < 0 && i == selected) {
            printf("   " ANSI_GREEN CURSOR_TOKEN ANSI_SEL_HL "[%d] %s (%d app%s)" ANSI_RESET "\n",
                   i + 1, ws[i].name, ws[i].entry_count, ws[i].entry_count == 1 ? "" : "s");
            /* Expand the apps of the selected workspace only. */
            render_workspace_frame(&ws[i]);
        } else {
            printf(ANSI_DIM "    [%d] %s (%d app%s)" ANSI_RESET "\n",
                   i + 1, ws[i].name, ws[i].entry_count, ws[i].entry_count == 1 ? "" : "s");
        }
    }
    print_more_below(end, count);
    print_picker_footer(num_input, show_error);
}

int run_workspace_picker(Workspace *ws, int count,
                         const char *title, const char *subtitle) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    printf(ANSI_CURSOR_HIDE);
    render_workspace_list(ws, count, selected, num_input, show_error, title, subtitle);

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
            render_workspace_list(ws, count, selected, num_input, show_error, title, subtitle);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
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
    int  done = 0, cancelled = 0;
    const char *help = "Enter confirm   Esc cancel   Backspace delete";

    if (prefill && prefill[0]) {
        strncpy(typed, prefill, sizeof(typed) - 1);
        typed[sizeof(typed) - 1] = '\0';
        typed_len = (int)strlen(typed);
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
        /* field "label: value_" — centred by its visible width (cursor included) */
        int vis = (int)strlen(label) + 2 + typed_len + 1;
        int pad = (cols - vis) / 2; if (pad < 0) pad = 0;
        printf("%*s" ANSI_CYAN "%s: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET "\n",
               pad, "", label, typed);
        printf("\n");
        print_centered(cols, ANSI_DIM, help, (int)strlen(help));
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
        } else if (key == KEY_BACKSPACE || key == 127) {
            if (typed_len > 0) typed[--typed_len] = '\0';
        } else if (key >= 32 && key < 127) {
            if (typed_len < (int)out_size - 1 && typed_len < (int)sizeof(typed) - 1) {
                typed[typed_len++] = (char)key;
                typed[typed_len]   = '\0';
            }
        }
    }
    printf(ANSI_CURSOR_SHOW);
    return cancelled ? 0 : 1;
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
                                  int type_app, int reordering, int edit_pos) {
    char title[140];
    snprintf(title, sizeof(title), "Edit '%s'", ws_name);
    print_picker_header(title,
                        "Type to add a link, Backspace to remove, Enter to save.");

    int start = picker_window_start(cursor, nrows);
    int end   = start + (nrows < PICKER_WINDOW ? nrows : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        const WeditRow *r = &rows[i];
        const char *arrow = (i == cursor) ? ANSI_GREEN "\xe2\x96\x8c" ANSI_RESET : " ";
        if (r->kind == ROW_ADD_APP) {
            printf("  %s " ANSI_ADD_HL " + Add a new app " ANSI_RESET "\n", arrow);
        } else if (r->kind == ROW_RENAME_WS) {
            printf("  %s " ANSI_REN_HL " ~ Rename this workspace " ANSI_RESET "\n", arrow);
        } else if (r->kind == ROW_DEL_WS) {
            printf("  %s " ANSI_DEL_HL " - Remove this workspace " ANSI_RESET "\n", arrow);
        } else if (r->kind == ROW_APP) {
            const WsEditorApp *a = &apps[r->app];
            if (a->marked_delete) {
                printf("  %s " ANSI_RED "- %s" ANSI_RESET "\n", arrow, a->display);
            } else if (a->is_new) {
                printf("  %s " ANSI_GREEN "+ %s" ANSI_RESET "\n", arrow, a->display);
            } else {
                printf("  %s " ANSI_APP_HL " %s " ANSI_RESET "\n", arrow, a->display);
            }
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
        printf("\n" ANSI_CYAN "  Add link for %s: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET,
               apps[type_app].display, typed);
        printf("\n" ANSI_DIM "  Enter add  \xe2\x80\xa2  Esc cancel  \xe2\x80\xa2  Backspace delete" ANSI_RESET);
    } else if (mode == WADD_TYPE_APP) {
        if (app_error && app_error[0])
            printf("\n" ANSI_YELLOW "  %s" ANSI_RESET, app_error);
        printf("\n" ANSI_CYAN "  New app: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET, typed);
        printf("\n" ANSI_DIM "  Enter add  \xe2\x80\xa2  Esc cancel  \xe2\x80\xa2  Backspace delete" ANSI_RESET);
    } else if (mode == WADD_EDIT_LINK) {
        printf("\n" ANSI_DIM "\xe2\x86\x90/\xe2\x86\x92 move  \xe2\x80\xa2  type to edit  \xe2\x80\xa2  Enter save  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
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
            printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  type to add link  \xe2\x80\xa2  Backspace remove  \xe2\x80\xa2  Enter save  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
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
    int  edit_pos  = 0;   /* cursor within typed[] while in WADD_EDIT_LINK */
    int  edit_app  = 0;   /* app whose link is being edited in WADD_EDIT_LINK */
    int  edit_link = 0;   /* link index being edited in WADD_EDIT_LINK */
    int  done      = 0;
    int  cancelled = 0;

    *delete_workspace = 0;
    int nrows = build_edit_rows(apps, *count, &rows, &rows_cap);

    printf(ANSI_CURSOR_HIDE);
    render_workspace_edit(ws_name, apps, rows, nrows, cursor, mode,
                          typed, app_error, type_app, reordering, edit_pos);

    while (!done) {
        int key = read_key();
        app_error[0] = '\0';

        if (mode == WADD_TYPE_LINK) {
            if (key == KEY_ENTER) {
                if (typed_len > 0)
                    editlink_push(&apps[type_app].links, typed, "", -1);
                typed[0] = '\0'; typed_len = 0;
                mode = WADD_LIST;
            } else if (key == KEY_ESC) {
                typed[0] = '\0'; typed_len = 0;
                mode = WADD_LIST;
            } else if (key == KEY_BACKSPACE || key == 127) {
                if (typed_len > 0) typed[--typed_len] = '\0';
            } else if (key >= 32 && key < 127) {
                if (typed_len < (int)sizeof(typed) - 1) {
                    typed[typed_len++] = (char)key;
                    typed[typed_len]   = '\0';
                }
            }
        } else if (mode == WADD_TYPE_APP) {
            if (key == KEY_ENTER) {
                if (typed_len > 0 && *count < max) {
                    char resolved[WORKSPACE_APP_MAX] = {0};
                    const char *final_app = typed;
                    int valid = 1;
                    if (is_new_window_app(typed)) {
                        /* known IDE launcher — skip resolution */
                    } else {
                        if (app_resolve(typed, resolved, sizeof(resolved)) && resolved[0])
                            final_app = resolved;
                        if (!app_value_exists(final_app)) {
                            snprintf(app_error, sizeof(app_error),
                                     "App not found: '%.60s'. Try a full path.", typed);
                            typed[0] = '\0'; typed_len = 0;
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
                        typed[0] = '\0'; typed_len = 0;
                        mode = WADD_LIST;
                        /* park the cursor on the freshly added app row */
                        nrows = build_edit_rows(apps, *count, &rows, &rows_cap);
                        for (int i = 0; i < nrows; i++)
                            if (rows[i].kind == ROW_APP && rows[i].app == *count - 1) { cursor = i; break; }
                    }
                }
            } else if (key == KEY_ESC) {
                typed[0] = '\0'; typed_len = 0;
                mode = WADD_LIST;
            } else if (key == KEY_BACKSPACE || key == 127) {
                if (typed_len > 0) typed[--typed_len] = '\0';
            } else if (key >= 32 && key < 127) {
                if (typed_len < (int)sizeof(typed) - 1) {
                    typed[typed_len++] = (char)key;
                    typed[typed_len]   = '\0';
                }
            }
        } else if (mode == WADD_EDIT_LINK) {
            /* ── Inline edit mode ───────────────────────────────────────────
               Editing one link's text in place. ←/→ move the in-string cursor,
               printable keys insert at it, Backspace deletes the char before it.
               Enter commits the buffer back into the link; Esc discards the edit
               (the link keeps its previous text) — neither exits the picker. */
            if (key == KEY_ENTER) {
                strncpy(apps[edit_app].links.items[edit_link].text, typed,
                        WORKSPACE_TARGET_MAX - 1);
                apps[edit_app].links.items[edit_link].text[WORKSPACE_TARGET_MAX - 1] = '\0';
                typed[0] = '\0'; typed_len = 0; edit_pos = 0;
                mode = WADD_LIST;
            } else if (key == KEY_ESC) {
                typed[0] = '\0'; typed_len = 0; edit_pos = 0;
                mode = WADD_LIST;
            } else if (key == KEY_LEFT) {
                if (edit_pos > 0) edit_pos--;
            } else if (key == KEY_RIGHT) {
                if (edit_pos < typed_len) edit_pos++;
            } else if (key == KEY_BACKSPACE || key == 127) {
                if (edit_pos > 0) {
                    memmove(&typed[edit_pos - 1], &typed[edit_pos],
                            (size_t)(typed_len - edit_pos) + 1);   /* include NUL */
                    typed_len--; edit_pos--;
                }
            } else if (key >= 32 && key < 127) {
                if (typed_len < (int)sizeof(typed) - 1) {
                    memmove(&typed[edit_pos + 1], &typed[edit_pos],
                            (size_t)(typed_len - edit_pos) + 1);   /* include NUL */
                    typed[edit_pos] = (char)key;
                    typed_len++; edit_pos++;
                }
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
                if (key == 'e' && r->kind == ROW_LINK &&
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
                } else if (key >= 33 && key < 127) {
                    if (r->kind == ROW_APP && !apps[r->app].marked_delete) {
                        /* printable key on an app row → start adding a link */
                        mode      = WADD_TYPE_LINK;
                        type_app  = r->app;
                        typed[0]  = (char)key;
                        typed[1]  = '\0';
                        typed_len = 1;
                    } else if (r->kind == ROW_ADD_APP && *count < max) {
                        /* printable key on "[+ Add a new app]" → open app-type mode */
                        mode      = WADD_TYPE_APP;
                        typed[0]  = (char)key;
                        typed[1]  = '\0';
                        typed_len = 1;
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
                                  typed, app_error, type_app, reordering, edit_pos);
        }
    }

    free(rows);
    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return cancelled ? 0 : 1;
}

/* ── Single styled text input box ──────────────────────────────────────── */

static void render_text_input(const char *title, const char *subtitle,
                              const char *label, const char *typed) {
    print_picker_header(title, subtitle);
    printf("\n" ANSI_CYAN "  %s: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET, label, typed);
    printf("\n\n" ANSI_DIM "Enter confirm  •  Backspace delete  •  Esc cancel" ANSI_RESET);
    fflush(stdout);
}

int run_text_input(const char *title, const char *subtitle, const char *label,
                   char *out, size_t out_size, int allow_empty, const char *prefill) {
    char typed[4096] = {0};
    int  typed_len   = 0;
    int  done        = 0;
    int  cancelled   = 0;

    if (prefill && prefill[0]) {
        strncpy(typed, prefill, sizeof(typed) - 1);
        typed[sizeof(typed) - 1] = '\0';
        typed_len = (int)strlen(typed);
    }

    printf(ANSI_CURSOR_HIDE);
    render_text_input(title, subtitle, label, typed);

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
        } else if (key == KEY_BACKSPACE || key == 127) {
            if (typed_len > 0) typed[--typed_len] = '\0';
        } else if (key >= 32 && key < 127) {
            if (typed_len < (int)sizeof(typed) - 1) {
                typed[typed_len++] = (char)key;
                typed[typed_len]   = '\0';
            }
        }
        if (!done) render_text_input(title, subtitle, label, typed);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return cancelled ? 0 : 1;
}
