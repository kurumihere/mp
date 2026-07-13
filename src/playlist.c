#include "playlist.h"

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
