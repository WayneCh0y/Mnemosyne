#include <stdio.h>
#include "help.h"

#define BOLD   "\033[1m"
#define DIM    "\033[2m"
#define CYAN   "\033[36m"
#define GOLD   "\033[38;2;212;175;55m"
#define SILVER "\033[38;2;192;192;192m"
#define RESET  "\033[0m"

void print_help(void) {
    printf(SILVER "╭─────────────────────────────────────────────╮" RESET "\n");
    printf(SILVER "│" RESET "                                             " SILVER "│" RESET "\n");
    printf(SILVER "│" RESET "   " BOLD GOLD "Mnemosyne" RESET "                                 " SILVER "│" RESET "\n");
    printf(SILVER "│" RESET "   " DIM "personal file search & recall" RESET "             " SILVER "│" RESET "\n");
    printf(SILVER "│" RESET "                                             " SILVER "│" RESET "\n");
    printf(SILVER "╰─────────────────────────────────────────────╯" RESET "\n\n");

    printf(BOLD "USAGE" RESET "\n");
    printf("  mn " CYAN "<command>" RESET " [arguments]\n\n");

    printf(BOLD "COMMANDS" RESET "\n");
    printf("  " CYAN "add" RESET " <file>           Index a file (.txt .md .tex .pdf)\n");
    printf("  " CYAN "search" RESET " <query>       Search indexed files\n");
    printf("  " CYAN "list" RESET "                 Browse all indexed files\n");
    printf("  " CYAN "remove" RESET " <file>        Remove a file from the index\n");
    printf("  " CYAN "config ide" RESET " [name]    Set the default IDE\n\n");

    printf(BOLD "PICKER CONTROLS" RESET DIM "  (search · list · config ide)" RESET "\n");
    printf("  " CYAN "↑ / ↓" RESET "        Navigate\n");
    printf("  " CYAN "1–9" RESET "          Type an index number, then Enter to jump\n");
    printf("  " CYAN "Backspace" RESET "    Erase last digit  (– when all cleared)\n");
    printf("  " CYAN "Enter" RESET "        Confirm\n");
    printf("  " CYAN "Esc" RESET "          Cancel\n\n");

    printf(BOLD "EXAMPLES" RESET "\n");
    printf("  mn " CYAN "add" RESET " notes.txt\n");
    printf("  mn " CYAN "search" RESET " \"simplex algorithm\"\n");
    printf("  mn " CYAN "config" RESET " ide\n");
    printf("  mn " CYAN "config" RESET " ide code\n");
}
