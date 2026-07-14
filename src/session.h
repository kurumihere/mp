#ifndef MP_SESSION_H
#define MP_SESSION_H

#include <stdbool.h>
#include <stddef.h>

#include "playlist.h"

typedef enum {
    SESSION_LOAD_NOT_FOUND,
    SESSION_LOAD_OK,
    SESSION_LOAD_ERROR,
} Session_Load_Result;

typedef struct {
    size_t current;
    float cursor;
    float volume;
    int repeat_mode;
    bool muted;
    bool shuffled;
} Session_State;

void session_state_defaults(Session_State *state);
bool session_default_path(char *path, size_t capacity);
Session_Load_Result session_load(const char *path, Playlist *playlist,
                                 Session_State *state);
bool session_save(const char *path, const Playlist *playlist,
                  const Session_State *state);

#endif
