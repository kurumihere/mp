#include "m3u.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

#define MAX_M3U_LINE ((size_t)1024 * 1024)

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} Path_List;

static char *copy_string(const char *source)
{
    size_t length = strlen(source) + 1;
    char *copy = malloc(length);

    if (copy != NULL) memcpy(copy, source, length);

    return copy;
}

static char *join_path(const char *directory, const char *path)
{
    size_t directory_length = strlen(directory);
    size_t path_length = strlen(path);
    bool needs_separator =
        directory_length > 0 && directory[directory_length - 1] != '/';

    if (directory_length > SIZE_MAX - path_length - 2) return NULL;

    size_t length =
        directory_length + path_length + (needs_separator ? 2u : 1u);
    char *joined = malloc(length);

    if (joined == NULL) return NULL;

    int written = snprintf(joined, length, "%s%s%s", directory,
                           needs_separator ? "/" : "", path);

    if (written < 0 || (size_t)written >= length) {
        free(joined);
        return NULL;
    }

    return joined;
}

static char *current_directory(void)
{
    size_t capacity = 256;

    while (capacity <= MAX_M3U_LINE) {
        char *directory = malloc(capacity);

        if (directory == NULL) return NULL;

        if (getcwd(directory, capacity) != NULL) return directory;

        int error = errno;
        free(directory);

        if (error != ERANGE) return NULL;

        capacity *= 2;
    }

    return NULL;
}

static char *absolute_path(const char *path)
{
    if (path[0] == '/') return copy_string(path);

    char *directory = current_directory();

    if (directory == NULL) return NULL;

    char *absolute = join_path(directory, path);
    free(directory);
    return absolute;
}

static char *playlist_directory(const char *path)
{
    char *directory = absolute_path(path);

    if (directory == NULL) return NULL;

    char *slash = strrchr(directory, '/');

    if (slash == directory) {
        slash[1] = '\0';
    } else if (slash != NULL) {
        *slash = '\0';
    }

    return directory;
}

static void path_list_uninit(Path_List *paths)
{
    for (size_t i = 0; i < paths->count; ++i) {
        free(paths->items[i]);
    }

    free(paths->items);
    *paths = (Path_List){0};
}

static bool path_list_append(Path_List *paths, char *path)
{
    if (paths->count == paths->capacity) {
        size_t capacity = paths->capacity == 0 ? 16 : paths->capacity * 2;

        if (capacity < paths->capacity ||
            capacity > SIZE_MAX / sizeof(*paths->items)) {
            return false;
        }

        char **items = realloc(paths->items, capacity * sizeof(*items));

        if (items == NULL) return false;

        paths->items = items;
        paths->capacity = capacity;
    }

    paths->items[paths->count++] = path;
    return true;
}

static int read_line(FILE *file, char **result)
{
    size_t capacity = 256;
    size_t length = 0;
    char *line = malloc(capacity);

    if (line == NULL) return -1;

    int character;

    while ((character = fgetc(file)) != EOF && character != '\n') {
        if (length + 1 >= capacity) {
            if (capacity >= MAX_M3U_LINE) {
                free(line);
                return -1;
            }

            size_t new_capacity = capacity * 2;

            if (new_capacity > MAX_M3U_LINE) new_capacity = MAX_M3U_LINE;

            char *replacement = realloc(line, new_capacity);

            if (replacement == NULL) {
                free(line);
                return -1;
            }

            line = replacement;
            capacity = new_capacity;
        }

        line[length++] = (char)character;
    }

    if (character == EOF && ferror(file)) {
        free(line);
        return -1;
    }

    if (character == EOF && length == 0) {
        free(line);
        return 0;
    }

    if (length > 0 && line[length - 1] == '\r') --length;

    line[length] = '\0';
    *result = line;
    return 1;
}

static bool load_paths(const char *path, Path_List *paths)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        mp_log(ERROR, "Failed to open playlist \"%s\": %s", path,
               strerror(errno));
        return false;
    }

    char *directory = playlist_directory(path);

    if (directory == NULL) {
        fclose(file);
        mp_log(ERROR, "Failed to resolve playlist path: %s", path);
        return false;
    }

    bool success = true;
    size_t line_number = 0;

    for (;;) {
        char *line;
        int status = read_line(file, &line);

        if (status == 0) break;

        if (status < 0) {
            success = false;
            break;
        }

        if (line_number == 0 && strlen(line) >= 3 &&
            memcmp(line, "\xef\xbb\xbf", strlen("\xef\xbb\xbf")) == 0) {
            memmove(line, line + 3, strlen(line + 3) + 1);
        }

        ++line_number;

        if (line[0] == '\0' || line[0] == '#') {
            free(line);
            continue;
        }

        char *entry =
            line[0] == '/' ? copy_string(line) : join_path(directory, line);
        free(line);

        if (entry == NULL || !path_list_append(paths, entry)) {
            free(entry);
            success = false;
            break;
        }
    }

    free(directory);

    if (fclose(file) != 0) success = false;

    if (!success) {
        path_list_uninit(paths);
        mp_log(ERROR, "Failed to read playlist: %s", path);
    }

    return success;
}

static bool extension_matches(const char *extension, const char *expected)
{
    while (*extension != '\0' && *expected != '\0') {
        unsigned char character = (unsigned char)*extension++;

        if (tolower(character) != *expected++) return false;
    }

    return *extension == '\0' && *expected == '\0';
}

bool m3u_is_path(const char *path)
{
    const char *extension = strrchr(path, '.');

    if (extension == NULL) return false;

    return extension_matches(extension, ".m3u") ||
           extension_matches(extension, ".m3u8");
}

bool m3u_append(Playlist *playlist, const char *path, size_t *added)
{
    Path_List paths = {0};

    if (!load_paths(path, &paths)) return false;

    bool success = playlist_append(playlist, (const char *const *)paths.items,
                                   paths.count);

    if (success && added != NULL) *added = paths.count;

    path_list_uninit(&paths);
    return success;
}

bool m3u_replace(Playlist *playlist, const char *path)
{
    Path_List paths = {0};

    if (!load_paths(path, &paths)) return false;

    bool success = playlist_replace(playlist, (const char *const *)paths.items,
                                    paths.count);
    path_list_uninit(&paths);
    return success;
}

bool m3u_save(const Playlist *playlist, const char *path)
{
    size_t path_length = strlen(path);

    if (path_length > SIZE_MAX - sizeof(".tmp")) return false;

    char *temporary = malloc(path_length + sizeof(".tmp"));

    if (temporary == NULL) return false;

    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp", sizeof(".tmp"));

    FILE *file = fopen(temporary, "wb");

    if (file == NULL) {
        mp_log(ERROR, "Failed to create playlist \"%s\": %s", path,
               strerror(errno));
        free(temporary);
        return false;
    }

    bool success = fputs("#EXTM3U\n", file) >= 0;

    for (size_t i = 0; success && i < playlist_get_count(playlist); ++i) {
        const char *track = playlist_get(playlist, i);

        if (track == NULL || strchr(track, '\n') != NULL ||
            strchr(track, '\r') != NULL) {
            success = false;
            break;
        }

        char *absolute = absolute_path(track);

        if (absolute == NULL || fprintf(file, "%s\n", absolute) < 0) {
            success = false;
        }

        free(absolute);
    }

    if (fclose(file) != 0) success = false;

    if (success && rename(temporary, path) != 0) success = false;

    if (!success) {
        remove(temporary);
        mp_log(ERROR, "Failed to save playlist: %s", path);
    }

    free(temporary);
    return success;
}
