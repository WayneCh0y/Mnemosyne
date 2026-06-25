#ifndef APP_ENUM_H
#define APP_ENUM_H

#include "workspace.h"

typedef struct {
    char app[WORKSPACE_APP_MAX];  /* launch value: full exe path (Win/Linux) or bundle name (macOS) */
    char display[256];            /* short friendly label for the picker (basename / app name) */
} RunningApp;

/* Enumerates the user's currently-open GUI applications into out[0..max).
   Returns the number found (>=0), or -1 if unsupported/failed on this platform
   (e.g. wmctrl missing on Linux). Each window is a separate entry. */
int app_enum_running(RunningApp *out, int max);

#endif
