#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

/* Writes a friendly, normalized display name for a stored app value (a full
   path, a bare exe name, or a UWP marker "shell:AppsFolder\<AUMID>") into out.

   The same application added in different forms (for example "msedge",
   "C:\...\msedge.exe", or the Edge Store AUMID) maps to the same label.
   Known apps get a curated friendly name (for example "Microsoft Edge");
   unknown apps fall back to the basename with a trailing ".exe" stripped.

   This is a display-only mapping. It never changes the stored value used to
   launch the app. Always NUL-terminates when out_size is greater than 0. */
void app_display_token(const char *app, char *out, size_t out_size);

#endif
