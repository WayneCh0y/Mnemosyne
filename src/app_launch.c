#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_launch.h"
#include "app_resolve.h"
#include "workspace.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

int is_new_window_app(const char *app) {
    return strcmp(app, "code") == 0 || strcmp(app, "cursor") == 0;
}

int is_url(const char *value) {
    if (value == NULL || value[0] == '\0') return 0;
    if (!((value[0] >= 'a' && value[0] <= 'z') ||
          (value[0] >= 'A' && value[0] <= 'Z'))) return 0;

    for (int i = 1; i < 32 && value[i]; i++) {
        char c = value[i];
        if (c == ':') return i >= 2;  /* min 2 chars excludes "C:\..." */
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') ||
                 c == '+' || c == '-' || c == '.';
        if (!ok) return 0;
    }
    return 0;
}

#ifdef _WIN32
static void launch_url_win(const char *url) {
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "open";
    sei.lpFile = url;
    sei.nShow  = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei))
        fprintf(stderr, "error: failed to open '%s'\n", url);
}

/* Launches an app + optional target on Windows.
   code/cursor are .cmd launchers on PATH, so they go through a hidden cmd.exe.
   UWP / Microsoft Store apps (shell:AppsFolder\<AUMID>) are launched via
   explorer.exe. Every other app is a full executable path supplied by the
   user, launched directly via ShellExecuteEx. */
static void launch_app_win(const char *app, const char *target) {
    if (is_uwp_app(app)) {
        char params[WORKSPACE_APP_MAX + 4];
        snprintf(params, sizeof(params), "\"%s\"", app);
        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = "open";
        sei.lpFile       = "explorer.exe";
        sei.lpParameters = params;
        sei.nShow        = SW_SHOWNORMAL;
        if (!ShellExecuteExA(&sei))
            fprintf(stderr, "error: failed to launch '%s'\n", app);
        (void)target;
        return;
    }

    if (is_new_window_app(app)) {
        char cmd_params[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 64];
        if (target[0])
            snprintf(cmd_params, sizeof(cmd_params),
                     "/c %s --new-window \"%s\"", app, target);
        else
            snprintf(cmd_params, sizeof(cmd_params), "/c %s --new-window", app);

        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize       = sizeof(sei);
        sei.lpVerb       = "open";
        sei.lpFile       = "cmd.exe";
        sei.lpParameters = cmd_params;
        sei.nShow        = SW_HIDE;
        if (!ShellExecuteExA(&sei))
            fprintf(stderr, "error: failed to launch '%s'\n", app);
        return;
    }

    char params[WORKSPACE_TARGET_MAX + 4];
    params[0] = '\0';
    if (target[0])
        snprintf(params, sizeof(params), "\"%s\"", target);

    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize       = sizeof(sei);
    sei.lpVerb       = "open";
    sei.lpFile       = app;
    sei.lpParameters = params[0] ? params : NULL;
    sei.nShow        = SW_SHOWNORMAL;
    if (!ShellExecuteExA(&sei))
        fprintf(stderr, "error: failed to launch '%s'\n", app);
}
#endif

void app_launch(const char *app, const char *target) {
    /* If the target is a protocol URI (spotify:..., https://..., etc.), let
       the OS route it to the right handler. The chosen app is informational
       — Windows / macOS / Linux already know which app owns each scheme. */
    if (is_url(target)) {
#ifdef _WIN32
        launch_url_win(target);
#elif defined(__APPLE__)
        char cmd[WORKSPACE_TARGET_MAX + 16];
        snprintf(cmd, sizeof(cmd), "open \"%s\"", target);
        system(cmd);
#else
        char cmd[WORKSPACE_TARGET_MAX + 32];
        snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" &", target);
        system(cmd);
#endif
        (void)app;
        return;
    }

#ifdef _WIN32
    launch_app_win(app, target);
#elif defined(__APPLE__)
    {
        const char *new_win = is_new_window_app(app) ? " --new-window" : "";
        char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 64];
        /* 'open -a <app>' resolves GUI apps by bundle name (case-insensitive).
           code/cursor are in PATH via their installer — use bare name. */
        if (!is_new_window_app(app)) {
            if (target[0] != '\0')
                snprintf(cmd, sizeof(cmd), "open -a \"%s\" \"%s\"", app, target);
            else
                snprintf(cmd, sizeof(cmd), "open -a \"%s\"", app);
        } else {
            if (target[0] != '\0')
                snprintf(cmd, sizeof(cmd), "%s%s \"%s\"", app, new_win, target);
            else
                snprintf(cmd, sizeof(cmd), "%s%s", app, new_win);
        }
        system(cmd);
    }
#else
    {
        /* code/cursor return immediately; a user-supplied executable path is a
           GUI app that would otherwise block, so background it with '&'. */
        const char *new_win = is_new_window_app(app) ? " --new-window" : "";
        const char *bg      = is_new_window_app(app) ? "" : " &";
        char cmd[WORKSPACE_APP_MAX + WORKSPACE_TARGET_MAX + 32];
        if (target[0] != '\0')
            snprintf(cmd, sizeof(cmd), "\"%s\"%s \"%s\"%s", app, new_win, target, bg);
        else
            snprintf(cmd, sizeof(cmd), "\"%s\"%s%s", app, new_win, bg);
        system(cmd);
    }
#endif
}
