#ifndef APP_RESOLVE_H
#define APP_RESOLVE_H

#include <stddef.h>

/* Resolves a bare program name (e.g. "chrome") to a launchable value.
   - Windows: full path from App Paths registry, PATH, or a Start Menu /
     Desktop shortcut (.lnk); or "shell:AppsFolder\<AUMID>" for UWP /
     Microsoft Store apps.
   - macOS/Linux: returns 0 — `open -a` (macOS) handles bundle names at
     launch time; Linux has no resolver yet.
   Returns 0 if `name` already looks like a path or is empty / not found. */
int app_resolve(const char *name, char *out, size_t out_size);

/* Returns 1 if `app` is a Windows UWP / Microsoft Store entry
   (i.e. starts with "shell:AppsFolder\"). */
int is_uwp_app(const char *app);

/* Returns 1 if `app` should be considered valid for the current machine.
   Filesystem paths are checked with stat/GetFileAttributes; UWP markers
   and bare bundle names (macOS) are trusted without filesystem checks. */
int app_value_exists(const char *app);

#endif
