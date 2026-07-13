#ifndef MP_PLAYLIST_H
#define MP_PLAYLIST_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char **paths;
    size_t count;
    size_t current;
} Playlist;

void playlist_init(Playlist *playlist);
void playlist_uninit(Playlist *playlist);

bool playlist_replace(Playlist *playlist, const char *const *paths,
                      size_t count);
bool playlist_select(Playlist *playlist, size_t index);

const char *playlist_get(const Playlist *playlist, size_t index);
size_t playlist_get_count(const Playlist *playlist);
size_t playlist_get_current(const Playlist *playlist);

#endif
