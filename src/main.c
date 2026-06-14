#include <stdio.h>
#include <string.h>

#include "help.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    return 0;
}