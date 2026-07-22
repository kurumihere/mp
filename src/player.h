#ifndef MP_PLAYER_H
#define MP_PLAYER_H

#include <stdbool.h>
#include <stddef.h>

#include "miniaudio.h"

#define PLAYER_ANALYSIS_SAMPLE_COUNT 1024
#define PLAYER_ANALYSIS_BUFFER_SIZE (PLAYER_ANALYSIS_SAMPLE_COUNT * 2)

typedef enum {
    PLAYER_EMPTY,
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
    bool stopped;
    ma_atomic_float analysis_samples[PLAYER_ANALYSIS_BUFFER_SIZE];
    ma_atomic_uint32 analysis_cursor;
    ma_atomic_uint32 analysis_channels;
    ma_atomic_uint32 analysis_sample_rate;
} Player;

bool player_init(Player *player);
bool player_load(Player *player, const char *path);
void player_clear(Player *player);
void player_uninit(Player *player);

bool player_toggle(Player *player);
bool player_stop(Player *player);
bool player_seek(Player *player, float position);
void player_set_volume(Player *player, float volume);
void player_adjust_volume(Player *player, float amount);
void player_toggle_mute(Player *player);

Player_State player_get_state(const Player *player);
float player_get_cursor(const Player *player);
float player_get_length(const Player *player);
float player_get_volume(const Player *player);
bool player_is_muted(const Player *player);
size_t player_copy_analysis_samples(const Player *player, float *samples,
                                    size_t sample_count,
                                    unsigned int *sample_rate);

#endif
