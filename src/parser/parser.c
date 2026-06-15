#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "txt.h"

char *parse_file(const char *path, FileType type) {
    switch (type)
    {
    case FILE_TYPE_TXT:     return parse_txt(path);
    case FILE_TYPE_MD:      return NULL;
    case FILE_TYPE_TEX:     return NULL;
    case FILE_TYPE_PDF:     return NULL;
    default: 
        return NULL;
    }
}
