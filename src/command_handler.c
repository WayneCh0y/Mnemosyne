#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "command_handler.h"
#include "help.h"
#include "ingest.h"
#include "index.h"
#include "search.h"
#include "config.h"

#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_ENTER 13
#define KEY_ESC   27

#define ANSI_CLEAR       "\033[2J\033[H"
#define ANSI_RESET       "\033[0m"
#define ANSI_DIM         "\033[2m"
#define ANSI_MAGENTA     "\033[35m"
#define ANSI_BLUE        "\033[34m"
#define ANSI_SEL         "\033[1;38;5;196m"
#define ANSI_CURSOR_HIDE "\033[?25l"
#define ANSI_CURSOR_SHOW "\033[?25h"

#ifdef _WIN32
static int read_key(void) {
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
static int read_key(void) {
    struct termios orig, raw;
    tcgetattr(STDIN_FILENO, &orig);
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
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

static int is_valid_add(int argc) {
    if (argc == 3) {
        return 1;
    }
    return 0;
}

static void cmd_add(int argc, char *argv[]) {
    if (!is_valid_add(argc)) { print_help(); return; }

    ingest_file(argv[2]);

    return;
}

static int is_valid_search(int argc) {
    if (argc >= 3) { return 1; }
    return 0;
}

static void update_files(void) {
    int count;
    IndexEntry *entries = index_get_entries(&count);
    if (entries == NULL) return;

    for (int i = 0; i < count; i++) {
        struct stat st;
        if (stat(entries[i].original_path, &st) != 0) continue;
        if ((long)st.st_mtime > entries[i].last_modified)
            ingest_file(entries[i].original_path);
    }

    free(entries);
}

static char* build_query(int argc, char *argv[]) {
    static char query[256];
    query[0] = '\0';

    for (int i = 2; i < argc; i++) {
        strcat(query, argv[i]);
        if (i < argc - 1) { strcat(query, " "); }
    }

    for (int i = 0; query[i]; i++)
        query[i] = (char)tolower((unsigned char)query[i]);

    return query;
}

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

static void render_results(SearchResult *results, int display, int selected) {
    printf(ANSI_CLEAR ANSI_RESET);
    printf("Instructions: Use arrow keys to navigate, Enter to select, Esc to cancel.\n\n");
    for (int i = 0; i < display; i++) {
        if (i == selected) {
            printf(ANSI_SEL "[%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 0);
            printf("\n");
        } else {
            printf(ANSI_DIM "[%d] %s" ANSI_RESET "\n", i + 1, results[i].original_path);
            print_context(&results[i], 1);
            printf("\n");
        }
    }
    fflush(stdout);
}

static void handle_enter(SearchResult *results, int selected) {
    const char *file_path = results[selected].original_path;
    const char *open_path = file_path;
    int entry_count;
    IndexEntry *entries = index_get_entries(&entry_count);
    if (entries) {
        for (int i = 0; i < entry_count; i++) {
            if (strcmp(entries[i].original_path, file_path) == 0) {
                if (strcmp(entries[i].repository, "none") != 0)
                    open_path = entries[i].repository;
                break;
            }
        }
    }
    char launch[8192];
    snprintf(launch, sizeof(launch), "%s \"%s\"", get_ide(), open_path);
    free(entries);
    system(launch);
}

static int run_picker(SearchResult *results, int display) {
    int selected = 0;
    int done = 0;
    printf(ANSI_CURSOR_HIDE);
    render_results(results, display, selected);
    while (!done) {
        int key = read_key();
        switch (key) {
        case KEY_UP:    if (selected > 0)           selected--; break;
        case KEY_DOWN:  if (selected < display - 1) selected++; break;
        case KEY_ENTER: done = 1;                               break;
        case KEY_ESC:   selected = -1; done = 1;               break;
        default: break;
        }
        if (!done) render_results(results, display, selected);
    }
    printf(ANSI_CURSOR_SHOW);
    fflush(stdout);
    return selected;
}

static void cmd_search(int argc, char *argv[]) {
    /*
        We update files regardless, when a search command is triggered.
    */
    update_files();

    if (!is_valid_search(argc)) { print_help(); return; }

    char* query = build_query(argc, argv);
    
    int count;
    SearchResult* results = search(query, &count);

    /*
        Nothing found :/
    */
    if (count == 0) {
        printf("No results!\n");
        free(results);
        return;
    }

    // We cap display lines to 5.
    int display = count < 5 ? count : 5;

    int chosen = run_picker(results, display);
    printf(ANSI_CLEAR ANSI_RESET);
    if (chosen != -1)
        handle_enter(results, chosen);
    free(results);

    return;
}

static void cmd_list(int argc, char *argv[])   { (void)argc; (void)argv; }
static void cmd_remove(int argc, char *argv[]) { (void)argc; (void)argv; }

static int is_valid_config(int argc, char *argv[]) {
    if (argc == 4 && strcmp(argv[2], "ide") == 0) {
        return 1;
    }
    return 0;
}

static void cmd_config(int argc, char *argv[]) { 
    if (!is_valid_config(argc, argv)) { print_help(); return; }

    if (set_ide(argv[3]) == 0) {
        printf("Default IDE updated to: %s\n", argv[3]);
    }
}

void handle_command(int argc, char *argv[]) {
    const char *cmd = argv[1];

    if (strcmp(cmd, "add") == 0)         { cmd_add(argc, argv);    return; }
    if (strcmp(cmd, "search") == 0)      { cmd_search(argc, argv); return; }
    if (strcmp(cmd, "list") == 0)        { cmd_list(argc, argv);   return; }
    if (strcmp(cmd, "remove") == 0)      { cmd_remove(argc, argv); return; }
    if (strcmp(cmd, "config") == 0)      { cmd_config(argc, argv); return; }
    if (strcmp(cmd, "help") == 0)        { print_help();           return; }

    fprintf(stderr, "Unknown command: %s\n", cmd);
}