#ifndef THEME_H
#define THEME_H

/*
 * theme.h — the single source of truth for Mnemosyne's terminal look.
 *
 * Brand identity: gold + silver (the goddess of memory), cyan for commands,
 * with green / red / yellow reserved for semantic status. Decorative output
 * uses the TH_* colour macros; status lines should go through the ui_* helpers
 * below, which add a glyph, pick the right stream, and honour NO_COLOR / pipes.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <io.h>
  #define TH_ISATTY(fd) _isatty(fd)
  #define TH_FILENO(f)  _fileno(f)
#else
  #include <unistd.h>
  #define TH_ISATTY(fd) isatty(fd)
  #define TH_FILENO(f)  fileno(f)
#endif

/* ---- Palette (brand + semantic) ---------------------------------------- */

#define TH_RESET   "\033[0m"
#define TH_BOLD    "\033[1m"
#define TH_DIM     "\033[2m"

#define TH_GOLD    "\033[38;2;212;175;55m"   /* brand primary  */
#define TH_SILVER  "\033[38;2;192;192;192m"  /* brand frame    */
#define TH_CYAN    "\033[36m"                /* commands       */

#define TH_OK      "\033[32m"                /* success        */
#define TH_ERR     "\033[31m"                /* error          */
#define TH_WARN    "\033[33m"                /* warning        */
#define TH_INFO    "\033[36m"                /* info / neutral */

/* Status glyphs (UTF-8; console is put in UTF-8 mode at startup). */
#define TH_GLYPH_OK   "✓"   /* checkmark  */
#define TH_GLYPH_ERR  "✗"   /* ballot x   */
#define TH_GLYPH_WARN "▲"   /* triangle   */
#define TH_GLYPH_INFO "›"   /* single guillemet */
#define TH_GLYPH_HINT "→"   /* arrow      */

/* ---- Colour gating ------------------------------------------------------ */

/* Colour is on only for real terminals, and never when NO_COLOR is set
 * (see https://no-color.org). Evaluated once per stream, then cached. */
static inline int th_color_for(FILE *stream) {
    if (stream == stdout) {
        static int v = -1;
        if (v < 0) v = (getenv("NO_COLOR") == NULL) && TH_ISATTY(TH_FILENO(stdout));
        return v;
    }
    static int v = -1;
    if (v < 0) v = (getenv("NO_COLOR") == NULL) && TH_ISATTY(TH_FILENO(stderr));
    return v;
}

/* ---- Semantic status helpers -------------------------------------------
 * Emit "<glyph> message\n", coloured when the target stream is a TTY.
 * ok / info / hint -> stdout;  err / warn -> stderr.                        */

static inline void th_emit(FILE *s, const char *color, const char *glyph,
                           const char *fmt, va_list ap) {
    int c = th_color_for(s);
    if (c) fprintf(s, "%s%s%s ", color, glyph, TH_RESET);
    else   fprintf(s, "%s ", glyph);
    vfprintf(s, fmt, ap);
    fputc('\n', s);
}

static inline void ui_ok(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    th_emit(stdout, TH_OK, TH_GLYPH_OK, fmt, ap); va_end(ap);
}
static inline void ui_err(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    th_emit(stderr, TH_ERR, TH_GLYPH_ERR, fmt, ap); va_end(ap);
}
static inline void ui_warn(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    th_emit(stderr, TH_WARN, TH_GLYPH_WARN, fmt, ap); va_end(ap);
}
static inline void ui_info(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    th_emit(stdout, TH_INFO, TH_GLYPH_INFO, fmt, ap); va_end(ap);
}
/* y/N prompt on stdout, reads one line from stdin. Auto-proceeds (returns 1)
   when stdin isn't a TTY so scripted usage doesn't hang. */
static inline int ui_confirm(const char *fmt, ...) {
    if (!TH_ISATTY(TH_FILENO(stdin))) return 1;

    va_list ap; va_start(ap, fmt);
    int c = th_color_for(stdout);
    if (c) fprintf(stdout, "%s%s%s ", TH_WARN, TH_GLYPH_WARN, TH_RESET);
    else   fprintf(stdout, "%s ", TH_GLYPH_WARN);
    vfprintf(stdout, fmt, ap);
    fputs(" [y/N] ", stdout);
    fflush(stdout);
    va_end(ap);

    char buf[16];
    if (fgets(buf, sizeof(buf), stdin) == NULL) return 0;
    return buf[0] == 'y' || buf[0] == 'Y';
}

/* A dimmed follow-up line ("→ do this next"), stdout, no leading glyph colour. */
static inline void ui_hint(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int c = th_color_for(stdout);
    if (c) fprintf(stdout, "%s%s ", TH_DIM, TH_GLYPH_HINT);
    else   fprintf(stdout, "%s ", TH_GLYPH_HINT);
    vfprintf(stdout, fmt, ap);
    if (c) fputs(TH_RESET, stdout);
    fputc('\n', stdout);
    va_end(ap);
}

#endif /* THEME_H */
