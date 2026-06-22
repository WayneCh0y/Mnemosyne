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

/* ── IDE config picker ─────────────────────────────────────────────────── */

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

static void render_ide_list(const char **list, int display, int selected,
                            int num_input, int show_error) {
    print_picker_header("Choose a default IDE", "Use the arrow keys to move, Enter to confirm, Esc to cancel.");
    for (int i = 0; i < display; i++) {
        if (num_input < 0 && i == selected) {
            printf(ANSI_SEL "  ▶ [%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        } else {
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        }
    }
    print_picker_footer(num_input, show_error);
}

int run_ide_picker(const char **list, int display) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    printf(ANSI_CURSOR_HIDE);
    render_ide_list(list, display, selected, num_input, show_error);

    while (!done) {
        int key = read_key();
        show_error = 0;

        if (num_input >= 0) {
            if (key >= '1' && key <= '9') {
                num_input = num_input * 10 + (key - '0');
            } else if (key == '0') {
                if (num_input > 0) num_input = num_input * 10;
            } else if (key == KEY_BACKSPACE || key == 127) {
                if (num_input > 0) num_input /= 10;
            } else if (key == KEY_ENTER) {
                if (num_input >= 1 && num_input <= display) {
                    selected = num_input - 1;
                    done = 1;
                } else {
                    show_error = 1;
                    selected = prev_selected;
                    num_input = -1;
                }
            } else if (key == KEY_ESC) {
                selected = prev_selected;
                num_input = -1;
            } else if (key == KEY_UP) {
                num_input = -1;
                selected = prev_selected;
                if (selected > 0) selected--;
            } else if (key == KEY_DOWN) {
                num_input = -1;
                selected = prev_selected;
                if (selected < display - 1) selected++;
            }
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

        if (!done) render_ide_list(list, display, selected, num_input, show_error);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
}

/* ── Search results picker ─────────────────────────────────────────────── */

static void print_context(const SearchResult *r, int dimmed) {
    const char *reset  = dimmed ? "\033[0;2m"  : ANSI_RESET;
    const char *color1 = dimmed ? "\033[2;35m" : ANSI_MAGENTA;
    const char *color2 = dimmed ? "\033[2;34m" : ANSI_BLUE;
    int is_md = (strcmp(r->file_type, "md") == 0);
    int at_line_start = 1;
    if (dimmed) printf(ANSI_DIM);
    printf("    ");
    const char *p = r->context;
    while (*p != '\0') {
        if (is_md && at_line_start && *p == '#') {
            while (*p == '#') p++;
            if (*p == ' ') p++;
            printf("%s", color1);
            while (*p != '\0' && *p != '\n') putchar(*p++);
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
    printf(ANSI_RESET "\n");
}

static void print_result_divider(int dimmed) {
    printf(dimmed ? ANSI_DIM : ANSI_RESET);
    printf("    ─────────────────────────────────────────\n");
    printf(ANSI_RESET);
}

static void render_results(SearchResult *results, int display, int selected,
                           int num_input, int show_error) {
    print_picker_header("Search results", "Use the arrow keys to move, Enter to open, Esc to cancel.");
    for (int i = 0; i < display; i++) {
        if (i > 0) print_result_divider(i != selected || num_input >= 0);
        if (num_input < 0 && i == selected) {
            printf(ANSI_SEL "  ▶ [%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 0);
        } else {
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 1);
        }
        printf("\n");
    }
    print_picker_footer(num_input, show_error);
}

int run_search_picker(SearchResult *results, int display) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    printf(ANSI_CURSOR_HIDE);
    render_results(results, display, selected, num_input, show_error);

    while (!done) {
        int key = read_key();
        show_error = 0;

        if (num_input >= 0) {
            if (key >= '1' && key <= '9') {
                num_input = num_input * 10 + (key - '0');
            } else if (key == '0') {
                if (num_input > 0) num_input = num_input * 10;
            } else if (key == KEY_BACKSPACE || key == 127) {
                if (num_input > 0) num_input /= 10;
            } else if (key == KEY_ENTER) {
                if (num_input >= 1 && num_input <= display) {
                    selected = num_input - 1;
                    done = 1;
                } else {
                    show_error = 1;
                    selected = prev_selected;
                    num_input = -1;
                }
            } else if (key == KEY_ESC) {
                selected = prev_selected;
                num_input = -1;
            } else if (key == KEY_UP) {
                num_input = -1;
                selected = prev_selected;
                if (selected > 0) selected--;
            } else if (key == KEY_DOWN) {
                num_input = -1;
                selected = prev_selected;
                if (selected < display - 1) selected++;
            }
        } else {
            switch (key) {
            case KEY_UP:    if (selected > 0)           selected--; break;
            case KEY_DOWN:  if (selected < display - 1) selected++; break;
            case KEY_ENTER: done = 1;                               break;
            case KEY_ESC:   selected = -1; done = 1;               break;
            default:
                if (key >= '1' && key <= '9') {
                    prev_selected = selected;
                    num_input = key - '0';
                }
                break;
            }
        }

        if (!done) render_results(results, display, selected, num_input, show_error);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
}

/* ── Index list picker ─────────────────────────────────────────────────── */

static void render_list(IndexEntry *entries, int count, int selected,
                        int num_input, int show_error) {
    print_picker_header("Browse indexed files", "Use the arrow keys to move, Enter to open, Esc to cancel.");
    for (int i = 0; i < count; i++) {
        if (num_input < 0 && i == selected)
            printf(ANSI_SEL "  ▶ [%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
        else
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
    }
    print_picker_footer(num_input, show_error);
}

int run_list_picker(IndexEntry *entries, int count) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    printf(ANSI_CURSOR_HIDE);
    render_list(entries, count, selected, num_input, show_error);

    while (!done) {
        int key = read_key();
        show_error = 0;

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
                    done = 1;
                } else {
                    show_error = 1;
                    selected = prev_selected;
                    num_input = -1;
                }
            } else if (key == KEY_ESC) {
                selected = prev_selected;
                num_input = -1;
            } else if (key == KEY_UP) {
                num_input = -1;
                selected = prev_selected;
                if (selected > 0) selected--;
            } else if (key == KEY_DOWN) {
                num_input = -1;
                selected = prev_selected;
                if (selected < count - 1) selected++;
            }
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

        if (!done) render_list(entries, count, selected, num_input, show_error);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
}

/* ── Workspace picker ──────────────────────────────────────────────────── */

static void render_workspace_list(Workspace *ws, int count, int selected,
                                  int num_input, int show_error) {
    print_picker_header("Open a workspace", "Use the arrow keys to move, Enter to open, Esc to cancel.");
    for (int i = 0; i < count; i++) {
        if (num_input < 0 && i == selected)
            printf(ANSI_SEL "  ▶ [%d] %s (%d app%s)" ANSI_RESET "\n",
                   i + 1, ws[i].name, ws[i].entry_count, ws[i].entry_count == 1 ? "" : "s");
        else
            printf(ANSI_DIM "    [%d] %s (%d app%s)" ANSI_RESET "\n",
                   i + 1, ws[i].name, ws[i].entry_count, ws[i].entry_count == 1 ? "" : "s");

        /* List each entry (app, and its target if any) indented underneath. */
        for (int j = 0; j < ws[i].entry_count; j++) {
            if (ws[i].entries[j].target[0] != '\0')
                printf(ANSI_DIM "        [%d] %s \xe2\x86\x92 %s" ANSI_RESET "\n",
                       j + 1, ws[i].entries[j].app, ws[i].entries[j].target);
            else
                printf(ANSI_DIM "        [%d] %s" ANSI_RESET "\n",
                       j + 1, ws[i].entries[j].app);
        }
    }
    print_picker_footer(num_input, show_error);
}

int run_workspace_picker(Workspace *ws, int count) {
    int selected = 0;
    int prev_selected = 0;
    int num_input = -1;
    int show_error = 0;
    int done = 0;

    printf(ANSI_CURSOR_HIDE);
    render_workspace_list(ws, count, selected, num_input, show_error);

    while (!done) {
        int key = read_key();
        show_error = 0;

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
                    done = 1;
                } else {
                    show_error = 1;
                    selected = prev_selected;
                    num_input = -1;
                }
            } else if (key == KEY_ESC) {
                selected = prev_selected;
                num_input = -1;
            } else if (key == KEY_UP) {
                num_input = -1;
                selected = prev_selected;
                if (selected > 0) selected--;
            } else if (key == KEY_DOWN) {
                num_input = -1;
                selected = prev_selected;
                if (selected < count - 1) selected++;
            }
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

        if (!done) render_workspace_list(ws, count, selected, num_input, show_error);
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
    if (mode == PATH_PICKER_MODE_LIST) {
        print_picker_header("Select a path",
                            "↑/↓ move  •  Enter select  •  Esc cancel  •  type to enter path");
        for (int i = 0; i < count; i++) {
            if (num_input < 0 && i == selected)
                printf(ANSI_SEL "  ▶ [%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
            else
                printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
        }
        print_picker_footer(num_input, show_error);
    } else {
        print_picker_header("Select a path",
                            "Enter to confirm  •  Esc to return to list  •  Backspace to delete");
        for (int i = 0; i < count; i++)
            printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
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
                else
                    mode = PATH_PICKER_MODE_LIST;
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

/* ── App chooser (menu + inline type mode) ─────────────────────────────── */

#define APP_PICKER_OPT_PATH 2   /* index of the "Full path…" menu item */

static const char *const APP_PICKER_ITEMS[] = {
    "code", "cursor", "Full path to an executable\xe2\x80\xa6"
};
#define APP_PICKER_COUNT 3

static void render_app_picker(int selected, int num_input, int show_error,
                              int mode, const char *typed) {
    if (mode == PATH_PICKER_MODE_LIST) {
        print_picker_header("Add an app",
                            "Choose an app or enter a full path to an executable.");
        for (int i = 0; i < APP_PICKER_COUNT; i++) {
            if (num_input < 0 && i == selected)
                printf(ANSI_SEL "  ▶ [%d] %s" ANSI_RESET "\n", i + 1, APP_PICKER_ITEMS[i]);
            else
                printf(ANSI_DIM "    [%d] %s" ANSI_RESET "\n", i + 1, APP_PICKER_ITEMS[i]);
        }
        print_picker_footer(num_input, show_error);
    } else {
        print_picker_header("Add an app", "Type the full path to an executable.");
        printf("\n" ANSI_CYAN "  Path: " ANSI_RESET "%s" ANSI_SEL "_" ANSI_RESET, typed);
        printf("\n\n" ANSI_DIM "Enter confirm  •  Backspace delete  •  Esc back" ANSI_RESET);
        fflush(stdout);
    }
}

int run_app_picker(char *app_out, size_t app_out_size) {
    int selected      = 0;
    int prev_selected = 0;
    int num_input     = -1;
    int show_error    = 0;
    int mode          = PATH_PICKER_MODE_LIST;
    char typed[4096]  = {0};
    int  typed_len    = 0;
    int  done         = 0;
    int  cancelled    = 0;

    printf(ANSI_CURSOR_HIDE);
    render_app_picker(selected, num_input, show_error, mode, typed);

    while (!done) {
        int key = read_key();
        show_error = 0;

        if (mode == PATH_PICKER_MODE_TYPE) {
            if (key == KEY_ENTER) {
                if (typed_len > 0) {
                    strncpy(app_out, typed, app_out_size - 1);
                    app_out[app_out_size - 1] = '\0';
                    done = 1;
                }
            } else if (key == KEY_ESC) {
                typed_len = 0; typed[0] = '\0';
                mode = PATH_PICKER_MODE_LIST;
            } else if (key == KEY_BACKSPACE || key == 127) {
                if (typed_len > 0) typed[--typed_len] = '\0';
                else mode = PATH_PICKER_MODE_LIST;
            } else if (key >= 32 && key < 127) {
                if (typed_len < (int)sizeof(typed) - 1) {
                    typed[typed_len++] = (char)key;
                    typed[typed_len]   = '\0';
                }
            }
        } else if (num_input >= 0) {
            if (key >= '1' && key <= '9') {
                num_input = num_input * 10 + (key - '0');
            } else if (key == '0') {
                if (num_input > 0) num_input = num_input * 10;
            } else if (key == KEY_BACKSPACE || key == 127) {
                if (num_input > 0) num_input /= 10;
            } else if (key == KEY_ENTER) {
                if (num_input >= 1 && num_input <= APP_PICKER_COUNT) {
                    selected = num_input - 1;
                    num_input = -1;
                    if (selected == APP_PICKER_OPT_PATH) {
                        mode = PATH_PICKER_MODE_TYPE;
                    } else {
                        strncpy(app_out, APP_PICKER_ITEMS[selected], app_out_size - 1);
                        app_out[app_out_size - 1] = '\0';
                        done = 1;
                    }
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
                if (selected < APP_PICKER_COUNT - 1) selected++;
            }
        } else {
            switch (key) {
            case KEY_UP:    if (selected > 0)                    selected--; break;
            case KEY_DOWN:  if (selected < APP_PICKER_COUNT - 1) selected++; break;
            case KEY_ENTER:
                if (selected == APP_PICKER_OPT_PATH) {
                    mode = PATH_PICKER_MODE_TYPE;
                } else {
                    strncpy(app_out, APP_PICKER_ITEMS[selected], app_out_size - 1);
                    app_out[app_out_size - 1] = '\0';
                    done = 1;
                }
                break;
            case KEY_ESC:   cancelled = 1; done = 1; break;
            default:
                if (key >= '1' && key <= '9') {
                    prev_selected = selected;
                    num_input     = key - '0';
                }
                break;
            }
        }

        if (!done) render_app_picker(selected, num_input, show_error, mode, typed);
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
