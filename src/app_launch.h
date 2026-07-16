#ifndef APP_LAUNCH_H
#define APP_LAUNCH_H

#include <stddef.h>   /* size_t */

/* Launches a workspace entry. `app` is either:
   - "code" / "cursor" — IDE launchers on PATH (uses --new-window + optional path)
   - a full executable path (e.g. C:\...\chrome.exe)
   - a macOS bundle name (e.g. "Google Chrome")
   - a UWP marker ("shell:AppsFolder\<AUMID>") on Windows
   - a protocol URI (e.g. "spotify:playlist:...", "https://...")
   `target` is an optional argument (path / file). For URIs it must be empty;
   the OS protocol handler decides what to do.
   `layout` is a screen-partition token ("" or "full" = maximize on the primary
   screen, else left/right/top/bottom/tl/tr/bl/br, optionally prefixed "N:" to
   name a screen); applied after launch on Windows, ignored elsewhere (on macOS
   the caller drives mac_place_window). */
void app_launch(const char *app, const char *target, const char *layout);

#ifdef _WIN32
/* Window-placement helpers for launches that don't go through app_launch (e.g.
   the multi-target browser path). Snapshot the existing top-level windows just
   before launching, then move the window that newly appears to `layout`.
   win_place_new frees the snapshot. Both no-op safely on allocation failure. */
void *win_capture_before(void);
void  win_place_new(void *before, const char *layout);
#endif

#ifdef __APPLE__
/* Standard windows `app` owns right now (0 if it isn't running). Sample this
   before a launch that adds a window, and pass it as mac_place_window's
   `prior_windows`. */
int mac_window_count(const char *app);

/* Moves a window of `app` into the partition encoded by `layout`, on the screen
   the layout names, using AppleScript / System Events. A no-op for "" layout.
   `app` is resolved to its bundle identifier, so both the name a workspace
   stores ("Visual Studio Code") and the launcher name ("code") find the process
   System Events calls "Code".
   `prior_windows` is the window count from before the launch: placement waits
   for a window beyond it, so an already-running app doesn't get an old window
   snapped in place of the one just opened. Pass 0 when the launch reuses an
   existing window and there is nothing new to wait for.
   Needs Accessibility permission for the controlling terminal — prints a hint
   and does nothing harmful if it isn't granted. */
void mac_place_window(const char *app, const char *layout, int prior_windows);
#endif

/* Number of screens (always >= 1; 1 on Linux, which has no placement support).
   Ordered primary-first, then left-to-right, then top-to-bottom, identically on
   Windows and macOS — so "screen N" is stable between the chooser and launch. */
int screen_count(void);

/* Splits a layout token "screen:partition" (e.g. "2:left") into its parts.
   A bare partition ("left") or "" means screen 1; "" partition means "full".
   *screen is 1-based; part is written bounded by psize. */
void layout_parse(const char *token, int *screen, char *part, size_t psize);

/* Given a screen work area (X,Y,W,H) and a partition token, writes the target
   window rect into *x,*y,*w,*h. Returns 1 for a sized partition
   (left/right/top/bottom/tl/tr/bl/br), or 0 for "full"/unknown — the caller
   should maximize in that case. Pure: no platform calls. */
int partition_rect(const char *part, int X, int Y, int W, int H,
                   int *x, int *y, int *w, int *h);

/* Returns 1 if `value` looks like an RFC-3986 protocol URI: a 2+ character
   scheme made of letters/digits/+/-/. followed by ':'. The 2-char minimum
   excludes Windows drive letters like "C:\...". */
int is_url(const char *value);

/* The canonical launcher name for one of the IDE apps we drive with
   --new-window, or NULL if `app` isn't one. Matching ignores case and an
   optional .exe suffix, so "Code", "code.exe" and "code" all answer "code" —
   store the returned value rather than what the user typed, so the launcher
   is invoked by the exact name it has on PATH. */
const char *new_window_launcher(const char *app);

/* Returns 1 if `app` is one of the IDE launchers handled with --new-window. */
int is_new_window_app(const char *app);

/* Opens `path` with the OS's registered default application
   (Windows: ShellExecute "open"; macOS: open; Linux: xdg-open). */
void open_with_default_app(const char *path);

#endif
