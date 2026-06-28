#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "txt.h"

char *parse_txt(const char *path) {
    FILE *f = fopen(path, "rb");
    
    if (f == NULL) { return NULL; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *buf = malloc(size + 1);
    if (buf == NULL) { fclose(f); return NULL; }

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    return buf;
}