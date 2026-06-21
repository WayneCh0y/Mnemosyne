#ifndef PICKER_H
#define PICKER_H

#include "search.h"
#include "index.h"

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

#endif
