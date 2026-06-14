#include <stdio.h>
#include <string.h>

#include "command_handler.h"

static void cmd_add(int argc, char *argv[]);
static void cmd_search(int argc, char *argv[]);
static void cmd_list(int argc, char *argv[]);
static void cmd_remove(int argc, char *argv[]);
static void cmd_config(int argc, char *argv[]);

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

void cmd_add(int argc, char *argv[]) {
    return;
}