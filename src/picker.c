#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "picker.h"
#include "app_resolve.h"
#include "app_launch.h"

#ifdef _WIN32
int read_key(void) {
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        c = _getch();
        if (c == 72) return KEY_UP;
        if (c == 80) return KEY_DOWN;
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
            printf(ANSI_SEL "  ▶ [%d] %s" ANSI_RESET "\n", i + 1, list[i]);
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

static void render_multiselect(const char *title, const char *subtitle,
                               const char **labels, int count,
                               const int *selected, const AppLinks *links,
                               int cursor, int mode, const char *typed) {
    print_picker_header(title, subtitle);
    int start = picker_window_start(cursor, count);
    int end   = start + (count < PICKER_WINDOW ? count : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        const char *arrow = (i == cursor) ? ANSI_WHITE "▶" ANSI_RESET : " ";
        const char *color = selected[i] ? ANSI_GREEN : ANSI_DIM;
        printf("  %s %s%s" ANSI_RESET "\n", arrow, color, labels[i]);
        if (links && selected[i])
            for (int k = 0; k < links[i].count; k++)
                printf(ANSI_DIM_YELLOW "        \xe2\x86\x92 %s" ANSI_RESET "\n",
                       links[i].items[k]);
    }
    print_more_below(end, count);
    if (mode == MS_MODE_TYPE) {
        printf("\n" ANSI_CYAN "  Add link for %s: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET,
               labels[cursor], typed);
        printf("\n" ANSI_DIM "  Enter add  •  Esc cancel  •  Backspace delete" ANSI_RESET);
    } else if (links) {
        printf("\n" ANSI_DIM "↑/↓ move  •  Space toggle  •  type to add link  •  Enter confirm  •  Esc cancel" ANSI_RESET);
    } else {
        printf("\n" ANSI_DIM "↑/↓ move  •  Space toggle  •  Enter confirm  •  Esc cancel" ANSI_RESET);
    }
    fflush(stdout);
}

int run_multiselect_picker(const char *title, const char *subtitle,
                           const char **labels, int count, int *selected,
                           AppLinks *links) {
    int  cursor    = 0;
    int  done      = 0;
    int  cancelled = 0;
    int  mode      = MS_MODE_LIST;
    char typed[WORKSPACE_TARGET_MAX] = {0};
    int  typed_len = 0;

    printf(ANSI_CURSOR_HIDE);
    render_multiselect(title, subtitle, labels, count, selected, links, cursor, mode, typed);

    while (!done) {
        int key = read_key();

        if (mode == MS_MODE_TYPE) {
            if (key == KEY_ENTER) {
                if (typed_len > 0 && links[cursor].count < SNAP_LINKS_MAX) {
                    int k = links[cursor].count;
                    strncpy(links[cursor].items[k], typed, WORKSPACE_TARGET_MAX - 1);
                    links[cursor].items[k][WORKSPACE_TARGET_MAX - 1] = '\0';
                    links[cursor].count++;
                }
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
            switch (key) {
            case KEY_UP:    if (cursor > 0)         cursor--; break;
            case KEY_DOWN:  if (cursor < count - 1) cursor++; break;
            case ' ':       selected[cursor] = !selected[cursor]; break;
            case KEY_ENTER: done = 1; break;
            case KEY_ESC:   cancelled = 1; done = 1; break;
            default:
                /* Any printable key (not Space) on a selected row → start adding a
                   link inline, seeded with that first character. */
                if (links && selected[cursor] && key >= 33 && key < 127
                    && links[cursor].count < SNAP_LINKS_MAX) {
                    mode      = MS_MODE_TYPE;
                    typed[0]  = (char)key;
                    typed[1]  = '\0';
                    typed_len = 1;
                }
                break;
            }
        }

        if (!done)
            render_multiselect(title, subtitle, labels, count, selected, links, cursor, mode, typed);
    }

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
            printf(ANSI_SEL "  ▶ [%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
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
            printf(ANSI_SEL "  ▶ [%d] %s" ANSI_RESET "\n", i + 1, base);
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
            printf(ANSI_SEL "  ▶ [%d] %s (%d app%s)" ANSI_RESET "\n",
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

/* ── Entry picker (choose one app within a workspace) ───────────────────── */

static void render_entry_list(const Workspace *w, int selected,
                              int num_input, int show_error,
                              const char *title, const char *subtitle) {
    print_picker_header(title, subtitle);
    if (w->entry_count == 0) {
        printf(ANSI_DIM "    No apps in this workspace." ANSI_RESET "\n");
        printf("\n" ANSI_DIM "Esc to go back" ANSI_RESET);
        fflush(stdout);
        return;
    }
    int start = picker_window_start(selected, w->entry_count);
    int end   = start + (w->entry_count < PICKER_WINDOW ? w->entry_count : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        int sel = (num_input < 0 && i == selected);
        char disp[256];
        ws_display_name(w->entries[i].app, disp, sizeof(disp));
        int tc = w->entries[i].target_count;
        if (tc == 0) {
            printf(sel ? ANSI_SEL "  \xe2\x96\xb6 [%d] %s" ANSI_RESET "\n"
                       : ANSI_DIM "    [%d] %s" ANSI_RESET "\n",
                   i + 1, disp);
        } else if (tc == 1) {
            printf(sel ? ANSI_SEL "  \xe2\x96\xb6 [%d] %s \xe2\x86\x92 %s" ANSI_RESET "\n"
                       : ANSI_DIM "    [%d] %s \xe2\x86\x92 %s" ANSI_RESET "\n",
                   i + 1, disp, w->entries[i].targets[0]);
        } else {
            printf(sel ? ANSI_SEL "  \xe2\x96\xb6 [%d] %s" ANSI_RESET "\n"
                       : ANSI_DIM "    [%d] %s" ANSI_RESET "\n",
                   i + 1, disp);
            for (int k = 0; k < tc; k++)
                printf(sel ? ANSI_SEL "         \xe2\x86\x92 %s" ANSI_RESET "\n"
                           : ANSI_DIM "         \xe2\x86\x92 %s" ANSI_RESET "\n",
                       w->entries[i].targets[k]);
        }
    }
    print_more_below(end, w->entry_count);
    print_picker_footer(num_input, show_error);
}

int run_entry_picker(const Workspace *ws, const char *title, const char *subtitle) {
    int count         = ws->entry_count;
    int selected      = 0;
    int prev_selected = 0;
    int num_input     = -1;
    int show_error    = 0;
    int done          = 0;

    printf(ANSI_CURSOR_HIDE);
    render_entry_list(ws, selected, num_input, show_error, title, subtitle);

    while (!done) {
        int key = read_key();
        show_error = 0;

        if (count == 0) {
            if (key == KEY_ESC || key == KEY_ENTER) { selected = -1; done = 1; }
        } else if (num_input >= 0) {
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

        if (!done)
            render_entry_list(ws, selected, num_input, show_error, title, subtitle);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
}

/* ── Path picker (list + inline type mode) ─────────────────────────────── */

#define PATH_PICKER_MODE_LIST 0
#define PATH_PICKER_MODE_TYPE 1

static void render_path_picker(IndexEntry *entries, int count, int selected,
                                int num_input, int show_error,
                                int mode, const char *typed) {
    int start = picker_window_start(selected, count);
    int end   = start + (count < PICKER_WINDOW ? count : PICKER_WINDOW);
    if (mode == PATH_PICKER_MODE_LIST) {
        print_picker_header("Select a path",
                            "↑/↓ move  •  Enter select  •  Esc cancel  •  type to enter path");
        print_more_above(start);
        for (int i = start; i < end; i++) {
            if (num_input < 0 && i == selected)
                printf(ANSI_SEL "  ▶ [%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
            else
                printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
        }
        print_more_below(end, count);
        print_picker_footer(num_input, show_error);
    } else {
        print_picker_header("Select a path",
                            "Enter to confirm  •  Esc to return to list  •  Backspace to delete");
        print_more_above(start);
        for (int i = start; i < end; i++)
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
        print_more_below(end, count);
        printf("\n" ANSI_CYAN "  Path: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET, typed);
        printf("\n" ANSI_DIM "  Backspace to delete  •  Esc to return to list" ANSI_RESET);
        fflush(stdout);
    }
}

int run_path_picker(IndexEntry *entries, int count, char *path_out, size_t path_out_size) {
    int selected     = 0;
    int prev_selected = 0;
    int num_input    = -1;
    int show_error   = 0;
    int mode         = PATH_PICKER_MODE_LIST;
    char typed[4096] = {0};
    int  typed_len   = 0;
    int  done        = 0;
    int  cancelled   = 0;

    printf(ANSI_CURSOR_HIDE);
    render_path_picker(entries, count, selected, num_input, show_error, mode, typed);

    while (!done) {
        int key = read_key();
        show_error = 0;

        if (mode == PATH_PICKER_MODE_TYPE) {
            if (key == KEY_ENTER) {
                if (typed_len > 0) {
                    strncpy(path_out, typed, path_out_size - 1);
                    path_out[path_out_size - 1] = '\0';
                    done = 1;
                }
            } else if (key == KEY_ESC) {
                typed_len = 0; typed[0] = '\0';
                mode = PATH_PICKER_MODE_LIST;
            } else if (key == KEY_BACKSPACE || key == 127) {
                if (typed_len > 0)
                    typed[--typed_len] = '\0';
                /* Backspace on an empty buffer is a no-op; use Esc to go back. */
            } else if (key >= 32 && key < 127) {
                if (typed_len < (int)sizeof(typed) - 1) {
                    typed[typed_len++] = (char)key;
                    typed[typed_len]   = '\0';
                }
            }
        } else {
            /* LIST mode */
            if (num_input >= 0) {
                if (key >= '1' && key <= '9') {
                    num_input = num_input * 10 + (key - '0');
                } else if (key == '0') {
                    if (num_input > 0) num_input = num_input * 10;
                } else if (key == KEY_BACKSPACE || key == 127) {
                    if (num_input > 0) num_input /= 10;
                } else if (key == KEY_ENTER) {
                    if (num_input >= 1 && num_input <= count) {
                        selected = num_input - 1;
                        const char *res = strcmp(entries[selected].repository, "none") != 0
                                          ? entries[selected].repository
                                          : entries[selected].original_path;
                        strncpy(path_out, res, path_out_size - 1);
                        path_out[path_out_size - 1] = '\0';
                        done = 1;
                    } else {
                        show_error = 1;
                        selected   = prev_selected;
                        num_input  = -1;
                    }
                } else if (key == KEY_ESC) {
                    selected  = prev_selected;
                    num_input = -1;
                } else if (key == KEY_UP) {
                    num_input = -1; selected = prev_selected;
                    if (selected > 0) selected--;
                } else if (key == KEY_DOWN) {
                    num_input = -1; selected = prev_selected;
                    if (selected < count - 1) selected++;
                }
            } else {
                switch (key) {
                case KEY_UP:    if (selected > 0)         selected--; break;
                case KEY_DOWN:  if (selected < count - 1) selected++; break;
                case KEY_ENTER:
                    {
                        const char *res = strcmp(entries[selected].repository, "none") != 0
                                          ? entries[selected].repository
                                          : entries[selected].original_path;
                        strncpy(path_out, res, path_out_size - 1);
                        path_out[path_out_size - 1] = '\0';
                        done = 1;
                    }
                    break;
                case KEY_ESC:
                    cancelled = 1; done = 1;
                    break;
                default:
                    if (key >= '1' && key <= '9') {
                        prev_selected = selected;
                        num_input     = key - '0';
                    } else if (key >= 32 && key < 127) {
                        /* Any other printable char → switch to type mode */
                        mode          = PATH_PICKER_MODE_TYPE;
                        typed[0]      = (char)key;
                        typed[1]      = '\0';
                        typed_len     = 1;
                    }
                    break;
                }
            }
        }

        if (!done)
            render_path_picker(entries, count, selected, num_input, show_error, mode, typed);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return cancelled ? 0 : 1;
}

/* ── Workspace editor picker (mn open add) ─────────────────────────────── */

#define WADD_LIST      0
#define WADD_TYPE_LINK 1
#define WADD_TYPE_APP  2

/* An editor entry is full once its saved + this-session links reach the per-entry
   target cap; no further links may be added to it. */
static int wadd_is_full(const WsEditorApp *a) {
    return a->existing_links.count + a->new_links.count >= WORKSPACE_ENTRY_TARGETS_MAX;
}

/* Strip directory prefix and trailing .exe from app path for a short display name. */
void ws_display_name(const char *app, char *out, size_t out_size) {
    const char *b = app;
    for (const char *p = app; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    size_t n = strlen(b);
    if (n >= 4) {
        const char *ext = b + n - 4;
        if (ext[0] == '.' &&
            (ext[1] == 'e' || ext[1] == 'E') &&
            (ext[2] == 'x' || ext[2] == 'X') &&
            (ext[3] == 'e' || ext[3] == 'E'))
            n -= 4;
    }
    size_t copy = n < out_size - 1 ? n : out_size - 1;
    memcpy(out, b, copy);
    out[copy] = '\0';
}

static void render_workspace_add(const char *ws_name,
                                 const WsEditorApp *apps, int count,
                                 int cursor, int mode,
                                 const char *typed, const char *app_error) {
    char title[140];
    snprintf(title, sizeof(title), "Add to '%s'", ws_name);
    print_picker_header(title,
                        "Add links to existing apps, or navigate to [+ Add a new app].");

    int total = count + 1;   /* +1 for the "[+ Add a new app]" pseudo-item */
    int start = picker_window_start(cursor, total);
    int end   = start + (total < PICKER_WINDOW ? total : PICKER_WINDOW);
    print_more_above(start);
    for (int i = start; i < end; i++) {
        const char *arrow = (i == cursor) ? ANSI_WHITE "\xe2\x96\xb6" ANSI_RESET : " ";
        if (i == count) {
            printf("  %s " ANSI_CYAN "[+ Add a new app]" ANSI_RESET "\n", arrow);
        } else {
            const char *color = apps[i].is_new ? ANSI_GREEN : "";
            const char *full_tag = wadd_is_full(&apps[i])
                                 ? " " ANSI_DIM_YELLOW "(full)" ANSI_RESET : "";
            printf("  %s %s%s" ANSI_RESET "%s\n", arrow, color, apps[i].display, full_tag);
            for (int k = 0; k < apps[i].existing_links.count; k++)
                printf(ANSI_DIM_YELLOW "        \xe2\x86\x92 %s" ANSI_RESET "\n",
                       apps[i].existing_links.items[k]);
            for (int k = 0; k < apps[i].new_links.count; k++)
                printf(ANSI_GREEN "        + %s" ANSI_RESET "\n",
                       apps[i].new_links.items[k]);
        }
    }
    print_more_below(end, total);

    if (mode == WADD_TYPE_LINK) {
        printf("\n" ANSI_CYAN "  Add link for %s: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET,
               apps[cursor].display, typed);
        printf("\n" ANSI_DIM "  Enter add  \xe2\x80\xa2  Esc cancel  \xe2\x80\xa2  Backspace delete" ANSI_RESET);
    } else if (mode == WADD_TYPE_APP) {
        if (app_error && app_error[0])
            printf("\n" ANSI_YELLOW "  %s" ANSI_RESET, app_error);
        printf("\n" ANSI_CYAN "  New app: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET, typed);
        printf("\n" ANSI_DIM "  Enter add  \xe2\x80\xa2  Esc cancel  \xe2\x80\xa2  Backspace delete" ANSI_RESET);
    } else if (cursor < count) {
        if (wadd_is_full(&apps[cursor]))
            printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  app is full  \xe2\x80\xa2  Enter finish  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
        else
            printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  type to add link  \xe2\x80\xa2  Enter finish  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
    } else {
        printf("\n" ANSI_DIM "\xe2\x86\x91/\xe2\x86\x93 move  \xe2\x80\xa2  type or Enter to add app  \xe2\x80\xa2  Esc cancel" ANSI_RESET);
    }
    fflush(stdout);
}

int run_workspace_add_picker(const char *ws_name,
                             WsEditorApp *apps, int *count, int max) {
    int  cursor    = 0;
    int  mode      = WADD_LIST;
    char typed[WORKSPACE_TARGET_MAX] = {0};
    int  typed_len = 0;
    char app_error[128] = {0};
    int  done      = 0;
    int  cancelled = 0;

    printf(ANSI_CURSOR_HIDE);
    render_workspace_add(ws_name, apps, *count, cursor, mode, typed, app_error);

    while (!done) {
        int key = read_key();
        app_error[0] = '\0';

        if (mode == WADD_TYPE_LINK) {
            if (key == KEY_ENTER) {
                if (typed_len > 0 && !wadd_is_full(&apps[cursor])) {
                    int k = apps[cursor].new_links.count;
                    strncpy(apps[cursor].new_links.items[k], typed, WORKSPACE_TARGET_MAX - 1);
                    apps[cursor].new_links.items[k][WORKSPACE_TARGET_MAX - 1] = '\0';
                    apps[cursor].new_links.count++;
                }
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
                        cursor = (*count)++;
                        typed[0] = '\0'; typed_len = 0;
                        mode = WADD_LIST;
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
        } else { /* WADD_LIST */
            int total = *count + 1;
            switch (key) {
            case KEY_UP:    if (cursor > 0)          cursor--; break;
            case KEY_DOWN:  if (cursor < total - 1)  cursor++; break;
            case KEY_ENTER:
                if (cursor == *count) {
                    mode = WADD_TYPE_APP;
                } else {
                    done = 1;
                }
                break;
            case KEY_ESC:
                cancelled = 1; done = 1;
                break;
            default:
                if (key >= 33 && key < 127) {
                    if (cursor < *count && !wadd_is_full(&apps[cursor])) {
                        /* printable key on an app row → start adding a link */
                        mode      = WADD_TYPE_LINK;
                        typed[0]  = (char)key;
                        typed[1]  = '\0';
                        typed_len = 1;
                    } else if (cursor == *count && *count < max) {
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

        if (!done)
            render_workspace_add(ws_name, apps, *count, cursor, mode, typed, app_error);
    }

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
                   char *out, size_t out_size, int allow_empty) {
    char typed[4096] = {0};
    int  typed_len   = 0;
    int  done        = 0;
    int  cancelled   = 0;

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
