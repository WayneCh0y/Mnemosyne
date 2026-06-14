#include "help.h"

#include <stdio.h>

void print_help(void) {
    printf("Mnemosyne, personal file search\n\n");
    printf("Usage: mnemosyne <command> [arguments]\n\n");
    printf("Commands:\n");
    printf("  add <file>          Index a file (.txt .md .tex .pdf)\n");
    printf("  search <query>      Search indexed files\n");
    printf("  list                List all indexed files\n");
    printf("  remove <file>       Remove a file from the index\n");
    printf("  config ide <name>   Set the IDE to open files in\n\n");
    printf("Examples:\n");
    printf("  mnemosyne add notes.txt\n");
    printf("  mnemosyne search \"simplex algorithm\"\n");
    printf("  mnemosyne config ide code\n");
}
