#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
  #include <direct.h>
  #define make_dir(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #define make_dir(p) mkdir(p, 0755)
#endif

#include "init.h"

#define BOLD    "\033[1m"
#define CYAN    "\033[36m"
#define YELLOW  "\033[1;33m"
#define RESET   "\033[0m"

static char data_path[1024];
static char conf_path[1024];

static const char *get_home(void) {
#ifdef _WIN32
    return getenv("USERPROFILE");
#else
    return getenv("HOME");
#endif
}

static void build_conf_path(void) {
    const char *home = get_home();
    snprintf(conf_path, sizeof(conf_path), "%s/.mnemosyne.conf", home);
}

static int create_dirs(void) {
    char path[1024];

    if (make_dir(data_path) != 0 && errno != EEXIST) return -1;

    snprintf(path, sizeof(path), "%s/index", data_path);
    if (make_dir(path) != 0 && errno != EEXIST) return -1;

    snprintf(path, sizeof(path), "%s/index/docs", data_path);
    if (make_dir(path) != 0 && errno != EEXIST) return -1;

    return 0;
}

static void first_time_setup(void) {
    const char *home = get_home();
    char default_path[1024];
    char input[1024];

    snprintf(default_path, sizeof(default_path), "%s/.mnemosyne", home);

    printf(YELLOW "Welcome to Mnemosyne!\n" RESET);
    printf("This looks like your first time running the program.\n\n");
    printf("Where would you like to store your data?\n");
    printf(CYAN "[default: %s]: " RESET, default_path);
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL) {
        strncpy(data_path, default_path, sizeof(data_path) - 1);
    } else {
        input[strcspn(input, "\n")] = '\0';
        if (strlen(input) == 0) {
            strncpy(data_path, default_path, sizeof(data_path) - 1);
        } else {
            strncpy(data_path, input, sizeof(data_path) - 1);
        }
    }

    printf("\nCreating storage at: %s\n", data_path);

    if (create_dirs() != 0) {
        fprintf(stderr, "error: could not create storage directory: %s\n", data_path);
        fprintf(stderr, "Check that the path is valid and you have write permission.\n");
        return;
    }

    FILE *conf = fopen(conf_path, "w");
    if (conf == NULL) {
        fprintf(stderr, "error: could not write config file: %s\n", conf_path);
        return;
    }
    fprintf(conf, "%s\n", data_path);
    fclose(conf);

    printf(BOLD "Setup complete.\n\n" RESET);
}

void check_init(void) {
    build_conf_path();

    FILE *conf = fopen(conf_path, "r");
    if (conf != NULL) {
        if (fgets(data_path, sizeof(data_path), conf) != NULL)
            data_path[strcspn(data_path, "\n")] = '\0';
        fclose(conf);
        return;
    }

    first_time_setup();
}

const char *get_data_path(void) {
    return data_path;
}
