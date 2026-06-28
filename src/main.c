#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

/* Mark the process DPI-aware before any monitor/window work so workspace
   partition placement uses real pixels on scaled displays (otherwise the OS
   virtualizes coordinates and windows land in the wrong spot/size). Prefer
   per-monitor-v2 (Win10 1703+); fall back to system-DPI aware on older builds.
   Resolved at runtime so it links against any Windows SDK / MinGW headers. */
static void enable_dpi_awareness(void) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32 != NULL) {
        typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
        SetCtxFn set_ctx = (SetCtxFn)(void *)GetProcAddress(
            user32, "SetProcessDpiAwarenessContext");
        /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4 */
        if (set_ctx != NULL && set_ctx((HANDLE)(LONG_PTR)-4))
            return;
    }
    SetProcessDPIAware();   /* fallback: system-DPI aware */
}
#endif

#include "help.h"
#include "command_handler.h"
#include "init.h"

int main(int argc, char *argv[]) {
#ifdef _WIN32
    enable_dpi_awareness();
    SetConsoleOutputCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    if (argc < 2) {
        print_help();
        return 1;
    }

    check_init();

    handle_command(argc, argv);
    
    return 0;
}