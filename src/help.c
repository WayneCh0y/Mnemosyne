#include <stdio.h>
#include "help.h"

#define BOLD   "\033[1m"
#define DIM    "\033[2m"
#define CYAN   "\033[36m"
#define GOLD   "\033[38;2;212;175;55m"
#define SILVER "\033[38;2;192;192;192m"
#define RESET  "\033[0m"

static void print_section(const char *title) {
    printf(DIM "─────────────────────────────────────────────────" RESET "\n");
    printf(BOLD CYAN "%s" RESET "\n", title);
}

void print_help(void) {
    printf(SILVER "╭─────────────────────────────────────────────╮" RESET "\n");
    printf(SILVER "│" RESET "   " BOLD GOLD "Mnemosyne" RESET "                                 " SILVER "│" RESET "\n");
    printf(SILVER "│" RESET "   " DIM "personal file search & recall" RESET "             " SILVER "│" RESET "\n");
    printf(SILVER "╰─────────────────────────────────────────────╯" RESET "\n");
    printf(DIM "Search, browse, and launch files from anywhere." RESET "\n\n");

    print_section("USAGE");
    printf("  mn " CYAN "<command>" RESET " [arguments]\n\n");

    print_section("COMMANDS");
    printf("  " CYAN "add" RESET "        Index a file (.txt .md .tex .pdf)\n");
    printf("  " CYAN "search" RESET "     Search indexed files\n");
    printf("  " CYAN "list" RESET "       Browse all indexed files\n");
    printf("  " CYAN "remove" RESET "     Remove a file from the index (picker)\n");
    printf("  " CYAN "open" RESET "       Open a workspace (launch apps)\n");
    printf("  " CYAN "config ide" RESET " Set the default IDE\n\n");

    print_section("WORKSPACES");
    printf(DIM "  Named sets of apps / URLs to launch together." RESET "\n");
    printf("  mn " CYAN "open" RESET "                        Interactive picker\n");
    printf("  mn " CYAN "open create" RESET " <name>          Create a workspace\n");
    printf("  mn " CYAN "open snap" RESET "                   Snapshot running apps into a workspace\n");
    printf("  mn " CYAN "open edit" RESET "                   Edit a workspace: add/remove apps & links (picker)\n\n");

    print_section("EXAMPLES");
    printf("  mn " CYAN "add" RESET " notes.txt\n");
    printf("  mn " CYAN "search" RESET " \"simplex algorithm\"\n");
    printf("  mn " CYAN "open create" RESET " work\n");
    printf("  mn " CYAN "open snap" RESET "\n");
    printf("  mn " CYAN "open edit" RESET "\n");
    printf("  mn " CYAN "open\n" RESET);
    printf("  mn " CYAN "config" RESET " ide\n");
}
