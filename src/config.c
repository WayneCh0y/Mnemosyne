#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

static char data_path[1024];
static char conf_path[1024];
static char ide[32];
static int conf_path_built = 0;

static const char *get_home(void) {
#ifdef _WIN32
    return getenv("USERPROFILE");
#else
    return getenv("HOME");
#endif
}

static void ensure_conf_path(void) {
    if (conf_path_built) return;
    const char *home = get_home();
    snprintf(conf_path, sizeof(conf_path), "%s/.mnemosyne.conf", home);
    conf_path_built = 1;
}

static const char *ide_list[] = {
    "code",
    "cursor",
    "nvim",
    "vim",
    "nano",
    "idea"
};

static int is_valid_ide(const char *input) {
    for (size_t i = 0; i < sizeof(ide_list) / sizeof(ide_list[0]); i++) {
        if (strcmp(input, ide_list[i]) == 0) return 1;
    }
    return 0;
}

static int write_config(void) {
    ensure_conf_path();
    FILE *conf = fopen(conf_path, "w");
    if (conf == NULL) {
        fprintf(stderr, "error: could not write config file: %s\n", conf_path);
        return -1;
    }
    fprintf(conf, "%s\n", data_path);
    fprintf(conf, "%s\n", ide);
    fclose(conf);
    return 0;
}

int load_config(void) {
    ensure_conf_path();
    FILE *conf = fopen(conf_path, "r");
    if (conf == NULL) return -1;

    int got_path = 0;
    int got_ide = 0;

    if (fgets(data_path, sizeof(data_path), conf) != NULL) {
        data_path[strcspn(data_path, "\n")] = '\0';
        got_path = 1;
    }
    if (fgets(ide, sizeof(ide), conf) != NULL) {
        ide[strcspn(ide, "\n")] = '\0';
        got_ide = 1;
    }
    fclose(conf);

    return (got_path && got_ide) ? 0 : -1;
}

int set_data_path(const char *path) {
    strncpy(data_path, path, sizeof(data_path) - 1);
    data_path[sizeof(data_path) - 1] = '\0';
    return write_config();
}

static int is_ide_installed(const char *ide) {
    char cmd[64];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "where %s >NUL 2>&1", ide);
#else
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", ide);
#endif
    return system(cmd) == 0;
}

int set_ide(const char *new_ide) {
    if (!is_valid_ide(new_ide)) {
        fprintf(stderr, "error: invalid IDE. Supported options are:\n");
        for (size_t i = 0; i < sizeof(ide_list) / sizeof(ide_list[0]); i++) {
            fprintf(stderr, "  - %s\n", ide_list[i]);
        }
        return -1;
    }

    if (!is_ide_installed(new_ide)) {
        fprintf(stderr, "error: IDE '%s' is not installed or not in PATH.\n", new_ide);
        fprintf(stderr, "See the README section 'Enabling GUI IDE launchers' for setup help.\n");
        return -1;
    }

    strncpy(ide, new_ide, sizeof(ide) - 1);
    ide[sizeof(ide) - 1] = '\0';

    return write_config();
}

const char *get_data_path(void) {
    return data_path;
}

const char *get_ide(void) {
    return ide;
}

const char **get_ide_list(size_t *count) {
    if (count) {
        *count = sizeof(ide_list) / sizeof(ide_list[0]);
    }
    return ide_list;
}
