#include <stdio.h>

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

static void render_ide_list(const char **list, int display, int selected) {
    printf(ANSI_CLEAR ANSI_RESET);
    printf("Instructions: Use arrow keys to navigate, Enter to select, Esc to cancel.\n\n");
    for (int i = 0; i < display; i++) {
        if (i == selected) {
            printf(ANSI_SEL "[%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        } else {
            printf(ANSI_DIM "[%d] %s" ANSI_RESET "\n", i + 1, list[i]);
        }
    }
    fflush(stdout);
}

int run_ide_picker(const char **list, int display) {
    int selected = 0;
    int done = 0;

    printf(ANSI_CURSOR_HIDE);
    render_ide_list(list, display, selected);

    while (!done) {
        int key = read_key();
        switch (key) {
        case KEY_UP:    if (selected > 0)           selected--; break;
        case KEY_DOWN:  if (selected < display - 1) selected++; break;
        case KEY_ENTER: done = 1;                               break;
        case KEY_ESC:   selected = -1; done = 1;                break;
        default: break;
        }
        if (!done) render_ide_list(list, display, selected);
    }

    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
}
