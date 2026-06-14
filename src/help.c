#include <stdio.h>
#include "help.h"

#define BOLD    "\033[1m"
#define CYAN    "\033[36m"
#define YELLOW  "\033[1;33m"
#define RESET   "\033[0m"

void print_help(void) {
    printf(YELLOW "Mnemosyne" RESET " - personal file search\n\n");

    printf(BOLD "USAGE\n" RESET);
    printf("  mnemosyne " CYAN "<command>" RESET " [arguments]\n\n");

    printf(BOLD "COMMANDS\n" RESET);
    printf("  " CYAN "add" RESET " <file>          Index a file (.txt .md .tex .pdf)\n");
    printf("  " CYAN "search" RESET " <query>      Search indexed files\n");
    printf("  " CYAN "list" RESET "                List all indexed files\n");
    printf("  " CYAN "remove" RESET " <file>       Remove a file from the index\n");
    printf("  " CYAN "config ide" RESET " <name>   Set the IDE to open files in\n\n");

    printf(BOLD "EXAMPLES\n" RESET);
    printf("  mnemosyne " CYAN "add" RESET " notes.txt\n");
    printf("  mnemosyne " CYAN "search" RESET " \"simplex algorithm\"\n");
    printf("  mnemosyne " CYAN "config" RESET " ide code\n");
}
