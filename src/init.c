#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
  #include <direct.h>
  #include <sys/stat.h>
  #include <io.h>
  #define make_dir(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #include <unistd.h>
  #define make_dir(p) mkdir(p, 0755)
#endif

#ifndef F_OK
  #define F_OK 0
#endif

#include "init.h"
#include "config.h"
#include "picker.h"
#include "theme.h"

/* Local aliases onto the shared palette (see theme.h). */
#define BOLD    TH_BOLD
#define CYAN    TH_CYAN
#define GOLD    TH_GOLD
#define RESET   TH_RESET

static const char *get_home(void) {
#ifdef _WIN32
    return getenv("USERPROFILE");
#else
    return getenv("HOME");
#endif
}

static int is_valid_path(const char *path) {
#ifdef _WIN32
    return strlen(path) >= 2 && path[1] == ':';
#else
    return path[0] == '/';
#endif
}

static int dir_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (st.st_mode & S_IFDIR) != 0;
}

static int create_dirs(const char *base) {
    char path[2048];

    if (make_dir(base) != 0 && errno != EEXIST) return -1;

    snprintf(path, sizeof(path), "%s/index", base);
    if (make_dir(path) != 0 && errno != EEXIST) return -1;

    snprintf(path, sizeof(path), "%s/index/docs", base);
    if (make_dir(path) != 0 && errno != EEXIST) return -1;

    snprintf(path, sizeof(path), "%s/index/manifest.json", base);
    if (access(path, F_OK) != 0) {
        FILE *f = fopen(path, "w");
        if (f == NULL) return -1;
        fprintf(f, "[]\n");
        fclose(f);
    }

    snprintf(path, sizeof(path), "%s/workspaces.json", base);
    if (access(path, F_OK) != 0) {
        FILE *f = fopen(path, "w");
        if (f == NULL) return -1;
        fprintf(f, "[]\n");
        fclose(f);
    }

    return 0;
}

static void read_with_default(char *dest, size_t dest_size, const char *default_val) {
    char input[1024];
    const char *src = default_val;

    if (fgets(input, sizeof(input), stdin) != NULL) {
        input[strcspn(input, "\n")] = '\0';
        if (input[0] != '\0') src = input;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static int first_time_setup(void) {
    const char *home = get_home();
    char default_path[1024];
    char chosen_path[1024];

    snprintf(default_path, sizeof(default_path), "%s/.mnemosyne", home);

    printf(BOLD GOLD "Welcome to Mnemosyne!\n" RESET);
    printf("This looks like your first time running the program.\n\n");
    printf("Where would you like to store your data?\n");

    while (1) {
        printf(CYAN "[default: %s]: " RESET, default_path);
        fflush(stdout);

        read_with_default(chosen_path, sizeof(chosen_path), default_path);

        if (!is_valid_path(chosen_path)) {
#ifdef _WIN32
            ui_err("please enter an absolute path (e.g. C:\\Users\\Wayne\\notes)");
#else
            ui_err("please enter an absolute path (e.g. /home/user/notes)");
#endif
            fprintf(stderr, "\n");
            continue;
        }

        printf("\nCreating storage at: %s\n", chosen_path);

        if (create_dirs(chosen_path) == 0) {
            if (set_data_path(chosen_path) != 0) return -1;
            break;
        }

        ui_err("could not create directory: %s", chosen_path);
        fprintf(stderr, "Please enter a valid path.\n\n");
    }

    printf("\nWhich IDE do you want to set as your default?\n\n");

    size_t ide_count;
    const char **ide_list = get_ide_list(&ide_count);
    int chosen;
    do {
        chosen = run_ide_picker(ide_list, (int)ide_count);
    } while (chosen == -1);
    set_ide(ide_list[chosen]);
    printf(ANSI_CLEAR ANSI_RESET);
    printf("Using %s as your default IDE.\n\n", ide_list[chosen]);

    ui_ok("Setup complete.");
    printf("\n");
    return 0;
}

void check_init(void) {
    if (load_config() == 0 && dir_exists(get_data_path())) {
        char manifest[2048];
        snprintf(manifest, sizeof(manifest), "%s/index/manifest.json", get_data_path());
        if (access(manifest, F_OK) != 0) {
            FILE *f = fopen(manifest, "w");
            if (f != NULL) { fprintf(f, "[]\n"); fclose(f); }
        }
        char ws_file[2048];
        snprintf(ws_file, sizeof(ws_file), "%s/workspaces.json", get_data_path());
        if (access(ws_file, F_OK) != 0) {
            FILE *f = fopen(ws_file, "w");
            if (f != NULL) { fprintf(f, "[]\n"); fclose(f); }
        }
        return;
    }

    ui_warn("config incomplete or storage missing, re-running setup.");

    if (first_time_setup() != 0) {
        ui_err("setup failed. Exiting.");
        exit(1);
    }
}
