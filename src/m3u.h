#ifndef MP_M3U_H
#define MP_M3U_H

#include <stdbool.h>
#include <stddef.h>

#include "playlist.h"

bool m3u_is_path(const char *path);
bool m3u_append(Playlist *playlist, const char *path, size_t *added);
bool m3u_replace(Playlist *playlist, const char *path);
bool m3u_save(const Playlist *playlist, const char *path);

#endif
