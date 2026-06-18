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

static void render_ide_list(const char **list, int display, int selected,
                            int num_input, int show_error) {
    printf(ANSI_CLEAR ANSI_RESET);
    printf("Instructions: Use arrow keys to navigate, Enter to select, Esc to cancel.\n\n");
    for (int i = 0; i < display; i++) {
        if (num_input < 0 && i == selected) {
            printf(ANSI_SEL "[%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        } else {
            printf(ANSI_DIM "[%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        }
    }
    if (show_error) {
        printf("\n" ANSI_YELLOW "no such index!" ANSI_RESET);
    } else if (num_input >= 0) {
        if (num_input == 0)
            printf("\n" ANSI_YELLOW "selected index: -" ANSI_RESET);
        else
            printf("\n" ANSI_YELLOW "selected index: %d" ANSI_RESET, num_input);
    }
    fflush(stdout);
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

static void render_results(SearchResult *results, int display, int selected,
                           int num_input, int show_error) {
    printf(ANSI_CLEAR ANSI_RESET);
    printf("Instructions: Use arrow keys to navigate, Enter to select, Esc to cancel.\n\n");
    for (int i = 0; i < display; i++) {
        if (num_input < 0 && i == selected) {
            printf(ANSI_SEL "[%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 0);
            printf("\n");
        } else {
            printf(ANSI_DIM "[%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 1);
            printf("\n");
        }
    }
    if (show_error) {
        printf("\n" ANSI_YELLOW "no such index!" ANSI_RESET);
    } else if (num_input >= 0) {
        if (num_input == 0)
            printf("\n" ANSI_YELLOW "selected index: -" ANSI_RESET);
        else
            printf("\n" ANSI_YELLOW "selected index: %d" ANSI_RESET, num_input);
    }
    fflush(stdout);
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
    printf(ANSI_CLEAR ANSI_RESET);
    printf("Instructions: Use arrow keys to navigate, Enter to select, Esc to cancel.\n\n");
    for (int i = 0; i < count; i++) {
        if (num_input < 0 && i == selected)
            printf(ANSI_SEL "[%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
        else
            printf(ANSI_DIM "[%d] %s" ANSI_RESET "\n", i + 1, entries[i].original_path);
    }
    if (show_error) {
        printf("\n" ANSI_YELLOW "no such index!" ANSI_RESET);
    } else if (num_input >= 0) {
        if (num_input == 0)
            printf("\n" ANSI_YELLOW "selected index: -" ANSI_RESET);
        else
            printf("\n" ANSI_YELLOW "selected index: %d" ANSI_RESET, num_input);
    }
    fflush(stdout);
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
