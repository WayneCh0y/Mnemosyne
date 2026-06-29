#include <string.h>

#include "normalise.h"

static const struct { const char *from; const char *to; } replacements[] = {
    { "\xE2\x80\x98", "'"   },  /* ‘  U+2018  left single quotation mark  */
    { "\xE2\x80\x99", "'"   },  /* ’  U+2019  right single quotation mark */
    { "\xE2\x80\x9C", "\""  },  /* “  U+201C  left double quotation mark  */
    { "\xE2\x80\x9D", "\""  },  /* ”  U+201D  right double quotation mark */
    { "\xE2\x80\x93", "-"   },  /* –  U+2013  en dash                     */
    { "\xE2\x80\x94", "-"   },  /* —  U+2014  em dash                     */
    { "\xE2\x80\xA6", "..." },  /* …  U+2026  horizontal ellipsis         */
    { NULL,           NULL  }
};

void normalise_punctuation(char *text) {
    char *r = text;  /* read pointer (walks through original bytes)        */
    char *w = text;  /* write pointer (Writes normalised bytes into buffer) */

    while (*r != '\0') {
        int matched = 0;

        for (int i = 0; replacements[i].from != NULL; i++) {
            size_t from_len = strlen(replacements[i].from);
            if (memcmp(r, replacements[i].from, from_len) == 0) {
                size_t to_len = strlen(replacements[i].to);
                memcpy(w, replacements[i].to, to_len);
                w += to_len;
                r += from_len;
                matched = 1;
                break;
            }
        }

        if (!matched) {
            *w++ = *r++;
        }
    }

    *w = '\0';
}

