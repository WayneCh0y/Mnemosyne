#include <string.h>

#include "tokenizer.h"
#include "app_resolve.h"

static char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static const char *base_name(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

static int ascii_iendswith(const char *s, size_t n, const char *suffix) {
    size_t sn = strlen(suffix);
    if (n < sn) return 0;
    s += n - sn;
    for (size_t i = 0; i < sn; i++)
        if (lower_ascii(s[i]) != lower_ascii(suffix[i]))
            return 0;
    return 1;
}

static size_t len_without_launcher_ext(const char *name) {
    size_t n = strlen(name);
    if (ascii_iendswith(name, n, ".exe") || ascii_iendswith(name, n, ".lnk"))
        n -= 4;
    return n;
}

static void copy_n(char *out, size_t out_size, const char *src, size_t n) {
    if (out_size == 0) return;
    if (n > out_size - 1) n = out_size - 1;
    memcpy(out, src, n);
    out[n] = '\0';
}

static void make_key(const char *app, char *key, size_t key_size) {
    const char *start;
    size_t n;

    if (is_uwp_app(app)) {
        start = base_name(app);
        const char *us = strchr(start, '_');
        n = us ? (size_t)(us - start) : strlen(start);
    } else {
        start = base_name(app);
        n = len_without_launcher_ext(start);
    }

    if (n > key_size - 1) n = key_size - 1;
    size_t i = 0;
    for (; i < n; i++) key[i] = lower_ascii(start[i]);
    key[i] = '\0';
}

static const struct { const char *key; const char *name; } FRIENDLY[] = {
    { "msedge",                         "Microsoft Edge"   },
    { "microsoft edge",                 "Microsoft Edge"   },
    { "microsoft.microsoftedge",        "Microsoft Edge"   },
    { "microsoft.microsoftedge.stable", "Microsoft Edge"   },
    { "chrome",                         "Google Chrome"    },
    { "google chrome",                  "Google Chrome"    },
    { "firefox",                        "Firefox"          },
    { "code",                           "VS Code"          },
    { "visual studio code",             "VS Code"          },
    { "cursor",                         "Cursor"           },
    { "devenv",                         "Visual Studio"    },
    { "visual studio",                  "Visual Studio"    },
    { "windows terminal",               "Windows Terminal" },
    { "windowsterminal",                "Windows Terminal" },
    { "wt",                             "Windows Terminal" },
    { "microsoft.windowsterminal",      "Windows Terminal" },
    { "explorer",                       "File Explorer"    },
    { "notepad",                        "Notepad"          },
    { "notepad++",                      "Notepad++"        },
    { "spotify",                        "Spotify"          },
    { "discord",                        "Discord"          },
    { "slack",                          "Slack"            },
};

void app_display_token(const char *app, char *out, size_t out_size) {
    if (out_size == 0) return;
    if (app == NULL || app[0] == '\0') {
        out[0] = '\0';
        return;
    }

    char key[256];
    make_key(app, key, sizeof(key));

    for (size_t i = 0; i < sizeof(FRIENDLY) / sizeof(FRIENDLY[0]); i++) {
        if (strcmp(key, FRIENDLY[i].key) == 0) {
            copy_n(out, out_size, FRIENDLY[i].name, strlen(FRIENDLY[i].name));
            return;
        }
    }

    const char *b = base_name(app);
    if (is_uwp_app(app)) {
        const char *us = strchr(b, '_');
        copy_n(out, out_size, b, us ? (size_t)(us - b) : strlen(b));
    } else {
        copy_n(out, out_size, b, len_without_launcher_ext(b));
    }
}
