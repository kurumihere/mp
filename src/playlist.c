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
    free(playlist->metadata);
    *playlist = (Playlist){0};
}

bool playlist_replace(Playlist *playlist, const char *const *paths,
                      size_t count)
{
    Playlist replacement;
    playlist_init(&replacement);

    if (!playlist_append(&replacement, paths, count)) return false;

    playlist_uninit(playlist);
    *playlist = replacement;

    return true;
}

bool playlist_append(Playlist *playlist, const char *const *paths, size_t count)
{
    if (count == 0) return true;
    if (count > SIZE_MAX - playlist->count) {
        mp_log(ERROR, "Playlist is too large");
        return false;
    }

    size_t new_count = playlist->count + count;

    if (new_count > SIZE_MAX / sizeof(*playlist->paths) ||
        new_count > SIZE_MAX / sizeof(*playlist->metadata)) {
        mp_log(ERROR, "Playlist is too large");
        return false;
    }

    char **new_paths = malloc(new_count * sizeof(*new_paths));
    Track_Metadata *new_metadata = malloc(new_count * sizeof(*new_metadata));

    if (new_paths == NULL || new_metadata == NULL) {
        free(new_paths);
        free(new_metadata);
        mp_log(ERROR, "Failed to grow playlist");
        return false;
    }

    if (playlist->count > 0) {
        memcpy(new_paths, playlist->paths,
               playlist->count * sizeof(*new_paths));
        memcpy(new_metadata, playlist->metadata,
               playlist->count * sizeof(*new_metadata));
    }

    size_t added = 0;

    for (; added < count; ++added) {
        size_t index = playlist->count + added;
        new_paths[index] = copy_string(paths[added]);

        if (new_paths[index] == NULL) break;

        metadata_load(new_paths[index], &new_metadata[index]);
    }

    if (added != count) {
        for (size_t i = 0; i < added; ++i) {
            free(new_paths[playlist->count + i]);
        }

        free(new_paths);
        free(new_metadata);
        mp_log(ERROR, "Failed to copy playlist path");
        return false;
    }

    free(playlist->paths);
    free(playlist->metadata);
    playlist->paths = new_paths;
    playlist->metadata = new_metadata;
    playlist->count = new_count;
    return true;
}

bool playlist_remove(Playlist *playlist, size_t index)
{
    if (index >= playlist->count) return false;

    free(playlist->paths[index]);
    memmove(&playlist->paths[index], &playlist->paths[index + 1],
            (playlist->count - index - 1) * sizeof(*playlist->paths));
    memmove(&playlist->metadata[index], &playlist->metadata[index + 1],
            (playlist->count - index - 1) * sizeof(*playlist->metadata));
    --playlist->count;

    if (playlist->count == 0) {
        free(playlist->paths);
        free(playlist->metadata);
        playlist->paths = NULL;
        playlist->metadata = NULL;
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

const Track_Metadata *playlist_get_metadata(const Playlist *playlist,
                                            size_t index)
{
    if (index >= playlist->count) return NULL;

    return &playlist->metadata[index];
}

size_t playlist_get_count(const Playlist *playlist)
{
    return playlist->count;
}

size_t playlist_get_current(const Playlist *playlist)
{
    return playlist->current;
}
