#ifndef MP_PLAYER_H
#define MP_PLAYER_H

#include <stdbool.h>

#include "miniaudio.h"

typedef enum {
    PLAYER_STOPPED,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_FINISHED,
} Player_State;

typedef struct {
    ma_engine engine;
    ma_sound sounds[2];
    bool engine_initialized;
    bool sound_initialized[2];
    int active_sound;
    float volume;
    bool muted;
} Player;

bool player_init(Player *player);
bool player_load(Player *player, const char *path);
void player_uninit(Player *player);

bool player_toggle(Player *player);
bool player_seek(Player *player, float position);
void player_set_volume(Player *player, float volume);
void player_adjust_volume(Player *player, float amount);
void player_toggle_mute(Player *player);

Player_State player_get_state(const Player *player);
float player_get_cursor(const Player *player);
float player_get_length(const Player *player);
float player_get_volume(const Player *player);
bool player_is_muted(const Player *player);

#endif
