#include <stdio.h>
#include <string.h>

#include "help.h"
#include "command_handler.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    handle_command(argc, argv);

    return 0;
}