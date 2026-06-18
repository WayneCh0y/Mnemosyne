#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

int load_config(void);
int set_data_path(const char *path);
int set_ide(const char *new_ide);
const char *get_data_path(void);
const char *get_ide(void);
const char **get_ide_list(size_t *count);

#endif
