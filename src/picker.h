#ifndef PICKER_H
#define PICKER_H

#include "search.h"
#include "index.h"
#include "workspace.h"

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
#define ANSI_CURSOR_HIDE "\033[?25l"
#define ANSI_CURSOR_SHOW "\033[?25h"
#define ANSI_CYAN        "\033[36m"
#define ANSI_MAGENTA     "\033[35m"
#define ANSI_BLUE        "\033[34m"
#define ANSI_YELLOW      "\033[33m"

int read_key(void);
int run_ide_picker(const char **list, int display);
int run_search_picker(SearchResult *results, int display);
int run_list_picker(IndexEntry *entries, int count);
int run_workspace_picker(Workspace *ws, int count);
/* Returns 1 with path_out filled (selected from list or typed), 0 if cancelled. */
int run_path_picker(IndexEntry *entries, int count, char *path_out, size_t path_out_size);
/* App chooser: menu of code / cursor / full-path-typed. Returns 1 with app_out
   filled, 0 if cancelled (Esc). */
int run_app_picker(char *app_out, size_t app_out_size);
/* Single styled text input box. Returns 1 with out filled, 0 if cancelled (Esc).
   When allow_empty is set, Enter on an empty value confirms (used as "skip"). */
int run_text_input(const char *title, const char *subtitle, const char *label,
                   char *out, size_t out_size, int allow_empty);

#endif
