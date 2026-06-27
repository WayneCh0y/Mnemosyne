#ifndef REMOVE_H
#define REMOVE_H

void remove_file(const char *path);
void remove_folder(const char *path);
void remove_path(const char *path);
int  remove_entry_by_abs_path(const char *abs_path);

#endif