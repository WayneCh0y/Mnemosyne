#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "md.h"

static int is_hr(const char *line) {
    while (*line == ' ') line++;
    char c = *line;
    if (c != '-' && c != '*' && c != '_') return 0;
    int count = 0;
    while (*line != '\0') {
        if (*line == c)      count++;
        else if (*line != ' ') return 0;
        line++;
    }
    return count >= 3;
}

static int count_leading_hashes(const char *line) {
    int h = 0;
    while (line[h] == '#') h++;
    if (h == 0 || h > 6) return 0;
    if (line[h] != ' ')  return 0;
    return h;
}

static int is_list_item(const char *line) {
    if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ')
        return 1;
    int i = 0;
    while (isdigit((unsigned char)line[i])) i++;
    if (i > 0 && line[i] == '.' && line[i + 1] == ' ')
        return 1;
    return 0;
}

static const char *list_item_text(const char *line) {
    if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ')
        return line + 2;
    int i = 0;
    while (isdigit((unsigned char)line[i])) i++;
    if (i > 0 && line[i] == '.' && line[i + 1] == ' ')
        return line + i + 2;
    return line;
}

static void process_inline(const char *text, char *out, int out_size) {
    int len = (int)strlen(text);
    int i = 0, j = 0;

    while (i < len && j < out_size - 1) {

        /* Image: ![...](...) → strip entirely */
        if (text[i] == '!' && i + 1 < len && text[i + 1] == '[') {
            i += 2;
            while (i < len && text[i] != ']') i++;
            if (i < len) i++;
            if (i < len && text[i] == '(') {
                i++;
                while (i < len && text[i] != ')') i++;
                if (i < len) i++;
            }
            continue;
        }

        /* Link: [...](...)  → substitute with [LINK] token */
        if (text[i] == '[') {
            i++;
            while (i < len && text[i] != ']') i++;
            if (i < len) i++;
            if (i < len && text[i] == '(') {
                i++;
                while (i < len && text[i] != ')') i++;
                if (i < len) i++;
            }
            const char *tok = "[LINK]";
            int tlen = 6;
            if (j + tlen < out_size - 1) { memcpy(out + j, tok, tlen); j += tlen; }
            continue;
        }

        /* Inline $$...$$ → keep content, strip delimiters */
        if (text[i] == '$' && i + 1 < len && text[i + 1] == '$') {
            i += 2;
            while (i < len) {
                if (text[i] == '$' && i + 1 < len && text[i + 1] == '$') { i += 2; break; }
                if (j < out_size - 1) out[j++] = (char)tolower((unsigned char)text[i]);
                i++;
            }
            continue;
        }

        /* Inline $...$ → keep content, strip delimiters */
        if (text[i] == '$') {
            i++;
            while (i < len && text[i] != '$') {
                if (j < out_size - 1) out[j++] = (char)tolower((unsigned char)text[i]);
                i++;
            }
            if (i < len) i++;
            continue;
        }

        /* Bold **...** or __...__ → keep content */
        if ((text[i] == '*' && i + 1 < len && text[i + 1] == '*') ||
            (text[i] == '_' && i + 1 < len && text[i + 1] == '_')) {
            char d = text[i];
            i += 2;
            while (i < len) {
                if (text[i] == d && i + 1 < len && text[i + 1] == d) { i += 2; break; }
                if (j < out_size - 1) out[j++] = (char)tolower((unsigned char)text[i]);
                i++;
            }
            continue;
        }

        /* Italic *...* or _..._ → keep content */
        if (text[i] == '*' || text[i] == '_') {
            char d = text[i];
            i++;
            while (i < len && text[i] != d) {
                if (j < out_size - 1) out[j++] = (char)tolower((unsigned char)text[i]);
                i++;
            }
            if (i < len) i++;
            continue;
        }

        /* Code span `...` → keep content */
        if (text[i] == '`') {
            i++;
            while (i < len && text[i] != '`') {
                if (j < out_size - 1) out[j++] = (char)tolower((unsigned char)text[i]);
                i++;
            }
            if (i < len) i++;
            continue;
        }

        out[j++] = (char)tolower((unsigned char)text[i]);
        i++;
    }
    out[j] = '\0';
}

char *parse_md(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *buf = malloc(size + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    long out_max = size * 2 + 256;
    char *out = malloc(out_max);
    if (out == NULL) { free(buf); return NULL; }
    long out_pos = 0;

    int in_code_block = 0;
    int in_math_block = 0;
    int in_list       = 0;

    char list_buf[4096];
    int  list_pos = 0;

    char line[4096];
    char processed[4096];

#define EMIT(s, n) do { \
    if (out_pos + (n) < out_max) { memcpy(out + out_pos, (s), (n)); out_pos += (n); } \
} while (0)
#define EMIT_STR(s) do { int _n = (int)strlen(s); EMIT((s), _n); } while (0)
#define EMIT_CHAR(c) do { if (out_pos < out_max - 1) out[out_pos++] = (c); } while (0)
#define CLOSE_LIST() do { \
    EMIT_STR("[LIST] "); \
    EMIT(list_buf, list_pos); \
    EMIT_STR(" [/LIST]\n"); \
    in_list = 0; list_pos = 0; \
} while (0)

    char *p = buf;
    while (*p != '\0') {
        /* Extract one line */
        int llen = 0;
        while (*p != '\0' && *p != '\n' && llen < (int)sizeof(line) - 1)
            line[llen++] = *p++;
        if (*p == '\n') p++;
        line[llen] = '\0';
        if (llen > 0 && line[llen - 1] == '\r') line[--llen] = '\0';

        /* Blank line check */
        int blank = 1;
        for (int i = 0; i < llen; i++) {
            if (line[i] != ' ' && line[i] != '\t') { blank = 0; break; }
        }

        /* Fenced code block */
        if (in_code_block) {
            if (strncmp(line, "```", 3) == 0) {
                in_code_block = 0;
            } else {
                for (int i = 0; i < llen; i++)
                    EMIT_CHAR((char)tolower((unsigned char)line[i]));
                EMIT_CHAR('\n');
            }
            continue;
        }

        /* Block math */
        if (in_math_block) {
            const char *t = line;
            while (*t == ' ') t++;
            if (strcmp(t, "$$") == 0) {
                in_math_block = 0;
            } else {
                for (int i = 0; i < llen; i++)
                    EMIT_CHAR((char)tolower((unsigned char)line[i]));
                EMIT_CHAR('\n');
            }
            continue;
        }

        /* Detect fenced code block opening */
        if (strncmp(line, "```", 3) == 0) { in_code_block = 1; continue; }

        /* Detect block math opening */
        {
            const char *t = line;
            while (*t == ' ') t++;
            if (strcmp(t, "$$") == 0) { in_math_block = 1; continue; }
        }

        /* Blank line */
        if (blank) {
            if (in_list) { CLOSE_LIST(); }
            EMIT_CHAR('\n');
            continue;
        }

        /* Horizontal rule */
        if (is_hr(line)) continue;

        /* Heading */
        int h = count_leading_hashes(line);
        if (h > 0) {
            if (in_list) { CLOSE_LIST(); }
            for (int i = 0; i < h; i++) EMIT_CHAR('#');
            EMIT_CHAR(' ');
            process_inline(line + h + 1, processed, sizeof(processed));
            EMIT_STR(processed);
            EMIT_CHAR('\n');
            continue;
        }

        /* Blockquote */
        if (line[0] == '>') {
            if (in_list) { CLOSE_LIST(); }
            const char *content = line + 1;
            if (*content == ' ') content++;
            process_inline(content, processed, sizeof(processed));
            EMIT_STR(processed);
            EMIT_CHAR('\n');
            continue;
        }

        /* List item */
        if (is_list_item(line)) {
            if (!in_list) { in_list = 1; list_pos = 0; }
            else if (list_pos + 3 < (int)sizeof(list_buf) - 1) {
                list_buf[list_pos++] = ' ';
                list_buf[list_pos++] = '|';
                list_buf[list_pos++] = ' ';
            }
            process_inline(list_item_text(line), processed, sizeof(processed));
            int plen = (int)strlen(processed);
            int copy = plen < (int)sizeof(list_buf) - list_pos - 1 ? plen : (int)sizeof(list_buf) - list_pos - 1;
            memcpy(list_buf + list_pos, processed, copy);
            list_pos += copy;
            list_buf[list_pos] = '\0';
            continue;
        }

        /* Normal paragraph line */
        if (in_list) { CLOSE_LIST(); }
        process_inline(line, processed, sizeof(processed));
        EMIT_STR(processed);
        EMIT_CHAR('\n');
    }

    if (in_list) { CLOSE_LIST(); }
    out[out_pos] = '\0';

#undef EMIT
#undef EMIT_STR
#undef EMIT_CHAR
#undef CLOSE_LIST

    free(buf);
    return out;
}
