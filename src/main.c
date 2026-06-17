#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "help.h"
#include "command_handler.h"
#include "init.h"

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) {
        print_help();
        return 1;
    }

    check_init();

    handle_command(argc, argv);
    
    return 0;
}