#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "txt.h"
#include "md.h"
#include "pdf.h"

char *parse_file(const char *path, FileType type) {
    switch (type)
    {
    case FILE_TYPE_TXT:     return parse_txt(path);
    case FILE_TYPE_MD:      return parse_md(path);
    case FILE_TYPE_TEX:     return NULL;
    case FILE_TYPE_PDF:     return parse_pdf(path);
    default: 
        return NULL;
    }
}
