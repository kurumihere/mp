#ifndef MP_FS_H
#define MP_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef _WIN32
#include <wchar.h>
#endif

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} Fs_Path_List;

FILE *fs_fopen(const char *path, const char *mode);
int fs_rename(const char *source, const char *destination);
int fs_remove(const char *path);
bool fs_make_parent_directories(const char *path);
bool fs_is_directory(const char *path);
bool fs_path_is_absolute(const char *path);
char *fs_absolute_path(const char *path);
char *fs_environment(const char *name);
bool fs_list_audio_files(const char *directory, Fs_Path_List *paths);
void fs_path_list_uninit(Fs_Path_List *paths);

#ifdef _WIN32
wchar_t *fs_utf8_to_utf16(const char *text);
bool fs_windows_command_line(int *argc, char ***argv);
void fs_windows_command_line_free(int argc, char **argv);
#endif

#endif
