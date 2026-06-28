#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "pdf.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

/* Fills `out` with the command used to invoke pdftotext:
   - On Windows, prefers a bundled binary next to mn.exe (or in a `poppler/bin/`
     sibling folder), then falls back to PATH lookup.
   - On Unix, just checks PATH.
   Returns 0 on success, -1 if pdftotext could not be located. */
static int find_pdftotext(char *out, size_t out_size) {
#ifdef _WIN32
    char exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    if (n > 0 && n < sizeof(exe_path)) {
        char *slash = strrchr(exe_path, '\\');
        if (slash != NULL) {
            *slash = '\0';

            snprintf(out, out_size, "%s\\pdftotext.exe", exe_path);
            if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return 0;

            snprintf(out, out_size, "%s\\poppler\\bin\\pdftotext.exe", exe_path);
            if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return 0;
        }
    }

    if (system("where pdftotext >nul 2>nul") == 0) {
        snprintf(out, out_size, "pdftotext");
        return 0;
    }
#else
    if (system("command -v pdftotext >/dev/null 2>&1") == 0) {
        snprintf(out, out_size, "pdftotext");
        return 0;
    }
#endif
    return -1;
}

static void make_temp_path(char *out, size_t out_size) {
    const char *tmp_dir;
#ifdef _WIN32
    tmp_dir = getenv("TEMP");
    if (tmp_dir == NULL) tmp_dir = ".";
#else
    tmp_dir = "/tmp";
#endif
    snprintf(out, out_size, "%s/mn_pdf_%ld_%d.txt",
             tmp_dir, (long)time(NULL), (int)getpid());
}

#ifdef _WIN32
static int run_pdftotext(const char *exe, const char *input, const char *output) {
    /* Windows processes receive a single command-line string, not an argv
       array. The CRT joins argv elements with spaces but does NOT quote
       elements containing whitespace — that's the caller's job. Without
       this, paths like "My Notes.pdf" get re-split by the child. */
    char input_q[4096], output_q[4096];
    snprintf(input_q,  sizeof(input_q),  "\"%s\"", input);
    snprintf(output_q, sizeof(output_q), "\"%s\"", output);

    const char *argv[] = {
        exe, "-raw", "-nopgbrk", "-enc", "UTF-8",
        input_q, output_q, NULL
    };
    /* _spawnvp uses `exe` directly if it contains a path separator,
       otherwise it searches PATH. _P_WAIT blocks until exit. */
    intptr_t rc = _spawnvp(_P_WAIT, exe, argv);
    return rc == 0 ? 0 : -1;
}
#else
static int run_pdftotext(const char *exe, const char *input, const char *output) {
    char *argv[] = {
        (char *)exe, "-raw", "-nopgbrk", "-enc", "UTF-8",
        (char *)input, (char *)output, NULL
    };
    pid_t pid;
    if (posix_spawnp(&pid, exe, NULL, NULL, argv, environ) != 0)
        return -1;
    int status;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}
#endif

static char* read_file(const char *path) {
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

    return buf;
}

char *parse_pdf(const char *path) {
    char exe[4096];
    if (find_pdftotext(exe, sizeof(exe)) != 0) {
        fprintf(stderr,
            "error: pdftotext not found.\n"
            "PDF support requires poppler-utils.\n"
#ifdef _WIN32
            "Windows: drop pdftotext.exe (and its DLLs) next to mn.exe,\n"
            "         or in a poppler\\bin\\ folder next to mn.exe.\n"
            "         Prebuilt: https://github.com/oschwartz10612/poppler-windows\n"
#else
            "Install: apt install poppler-utils  |  brew install poppler\n"
#endif
        );
        return NULL;
    }

    char temp_path[4096];
    make_temp_path(temp_path, sizeof(temp_path));

    if (run_pdftotext(exe, path, temp_path) != 0) {
        fprintf(stderr, "error: failed to run pdftotext on '%s'\n", path);
        return NULL;
    }

    char *text = read_file(temp_path);
    remove(temp_path);

    return text;
}