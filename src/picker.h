#ifndef PICKER_H
#define PICKER_H

#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_ENTER 13
#define KEY_ESC   27

#define ANSI_CLEAR       "\033[H\033[2J"
#define ANSI_RESET       "\033[0m"
#define ANSI_DIM         "\033[2m"
#define ANSI_SEL         "\033[1;38;5;196m"
#define ANSI_CURSOR_HIDE "\033[?25l"
#define ANSI_CURSOR_SHOW "\033[?25h"

int read_key(void);
int run_ide_picker(const char **list, int display);

#endif
