#include "playlist.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

static char *copy_string(const char *source)
{
    size_t length = strlen(source) + 1;
    char *copy = malloc(length);

    if (copy != NULL) memcpy(copy, source, length);

    return copy;
}

void playlist_init(Playlist *playlist)
{
    *playlist = (Playlist){0};
}

void playlist_uninit(Playlist *playlist)
{
    for (size_t i = 0; i < playlist->count; ++i) {
        free(playlist->paths[i]);
    }

    free(playlist->paths);
    *playlist = (Playlist){0};
}

bool playlist_replace(Playlist *playlist, const char *const *paths,
                      size_t count)
{
    Playlist replacement;
    playlist_init(&replacement);

    if (count > 0) {
        replacement.paths = calloc(count, sizeof(*replacement.paths));

        if (replacement.paths == NULL) {
            mp_log(ERROR, "Failed to allocate playlist");
            return false;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        replacement.paths[i] = copy_string(paths[i]);

        if (replacement.paths[i] == NULL) {
            replacement.count = i;
            playlist_uninit(&replacement);
            mp_log(ERROR, "Failed to copy playlist path");
            return false;
        }

        replacement.count = i + 1;
    }

    playlist_uninit(playlist);
    *playlist = replacement;

    return true;
}

bool playlist_append(Playlist *playlist, const char *const *paths,
                     size_t count)
{
    if (count == 0) return true;
    if (count > SIZE_MAX - playlist->count) {
        mp_log(ERROR, "Playlist is too large");
        return false;
    }

    char **copies = calloc(count, sizeof(*copies));

    if (copies == NULL) {
        mp_log(ERROR, "Failed to allocate playlist additions");
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        copies[i] = copy_string(paths[i]);

        if (copies[i] == NULL) {
            for (size_t j = 0; j < i; ++j) free(copies[j]);
            free(copies);
            mp_log(ERROR, "Failed to copy playlist path");
            return false;
        }
    }

    size_t new_count = playlist->count + count;

    if (new_count > SIZE_MAX / sizeof(*playlist->paths)) {
        for (size_t i = 0; i < count; ++i) free(copies[i]);
        free(copies);
        mp_log(ERROR, "Playlist is too large");
        return false;
    }

    char **new_paths = realloc(playlist->paths,
                               new_count * sizeof(*playlist->paths));

    if (new_paths == NULL) {
        for (size_t i = 0; i < count; ++i) free(copies[i]);
        free(copies);
        mp_log(ERROR, "Failed to grow playlist");
        return false;
    }

    playlist->paths = new_paths;

    for (size_t i = 0; i < count; ++i) {
        playlist->paths[playlist->count + i] = copies[i];
    }

    free(copies);
    playlist->count = new_count;
    return true;
}

bool playlist_remove(Playlist *playlist, size_t index)
{
    if (index >= playlist->count) return false;

    free(playlist->paths[index]);
    memmove(&playlist->paths[index], &playlist->paths[index + 1],
            (playlist->count - index - 1) * sizeof(*playlist->paths));
    --playlist->count;

    if (playlist->count == 0) {
        free(playlist->paths);
        playlist->paths = NULL;
        playlist->current = 0;
    } else if (playlist->current > index) {
        --playlist->current;
    } else if (playlist->current >= playlist->count) {
        playlist->current = playlist->count - 1;
    }

    return true;
}

void playlist_clear(Playlist *playlist)
{
    playlist_uninit(playlist);
}

bool playlist_select(Playlist *playlist, size_t index)
{
    if (index >= playlist->count) return false;

    playlist->current = index;
    return true;
}

const char *playlist_get(const Playlist *playlist, size_t index)
{
    if (index >= playlist->count) return NULL;

    return playlist->paths[index];
}

size_t playlist_get_count(const Playlist *playlist)
{
    return playlist->count;
}

size_t playlist_get_current(const Playlist *playlist)
{
    return playlist->current;
}
