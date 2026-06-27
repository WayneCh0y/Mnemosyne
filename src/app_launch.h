#ifndef APP_LAUNCH_H
#define APP_LAUNCH_H

/* Launches a workspace entry. `app` is either:
   - "code" / "cursor" — IDE launchers on PATH (uses --new-window + optional path)
   - a full executable path (e.g. C:\...\chrome.exe)
   - a macOS bundle name (e.g. "Google Chrome")
   - a UWP marker ("shell:AppsFolder\<AUMID>") on Windows
   - a protocol URI (e.g. "spotify:playlist:...", "https://...")
   `target` is an optional argument (path / file). For URIs it must be empty;
   the OS protocol handler decides what to do.
   `layout` is a screen-partition token ("" or "full" = maximize on the primary
   screen, else left/right/top/bottom/tl/tr/bl/br); applied after launch on
   Windows, ignored elsewhere. */
void app_launch(const char *app, const char *target, const char *layout);

#ifdef _WIN32
/* Window-placement helpers for launches that don't go through app_launch (e.g.
   the multi-target browser path). Snapshot the existing top-level windows just
   before launching, then move the window that newly appears to `layout`.
   win_place_new frees the snapshot. Both no-op safely on allocation failure. */
void *win_capture_before(void);
void  win_place_new(void *before, const char *layout);
#endif

/* Returns 1 if `value` looks like an RFC-3986 protocol URI: a 2+ character
   scheme made of letters/digits/+/-/. followed by ':'. The 2-char minimum
   excludes Windows drive letters like "C:\...". */
int is_url(const char *value);

/* Returns 1 if `app` is one of the IDE launchers handled with --new-window. */
int is_new_window_app(const char *app);

/* Opens `path` with the OS's registered default application
   (Windows: ShellExecute "open"; macOS: open; Linux: xdg-open). */
void open_with_default_app(const char *path);

#endif
