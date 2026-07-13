#include "player.h"

#include "log.h"

static ma_sound *player_get_sound(Player *player)
{
    if (!player->sound_initialized[player->active_sound]) return NULL;

    return &player->sounds[player->active_sound];
}

static const ma_sound *player_get_sound_const(const Player *player)
{
    if (!player->sound_initialized[player->active_sound]) return NULL;

    return &player->sounds[player->active_sound];
}

bool player_init(Player *player)
{
    *player = (Player){0};
    player->volume = 0.5f;

    ma_result result = ma_engine_init(NULL, &player->engine);

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "Failed to initialize audio engine: %s",
               ma_result_description(result));

        return false;
    }

    player->engine_initialized = true;

    return true;
}

bool player_load(Player *player, const char *path)
{
    bool had_sound = player->sound_initialized[player->active_sound];
    int next_sound =
        had_sound ? 1 - player->active_sound : player->active_sound;

    ma_result result =
        ma_sound_init_from_file(&player->engine, path, MA_SOUND_FLAG_STREAM,
                                NULL, NULL, &player->sounds[next_sound]);

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "Failed to load \"%s\": %s", path,
               ma_result_description(result));

        return false;
    }

    player->sound_initialized[next_sound] = true;
    ma_sound_set_volume(&player->sounds[next_sound],
                        player->muted ? 0.0f : player->volume);

    result = ma_sound_start(&player->sounds[next_sound]);

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "Failed to start playback: %s",
               ma_result_description(result));

        ma_sound_uninit(&player->sounds[next_sound]);
        player->sound_initialized[next_sound] = false;
        return false;
    }

    if (had_sound) {
        ma_sound_uninit(&player->sounds[player->active_sound]);
        player->sound_initialized[player->active_sound] = false;
    }

    player->active_sound = next_sound;

    mp_log(INFO, "Playing \"%s\"", path);
    return true;
}

void player_set_volume(Player *player, float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;

    player->volume = volume;
    player->muted = false;

    ma_sound *sound = player_get_sound(player);

    if (sound != NULL) ma_sound_set_volume(sound, volume);
}

void player_adjust_volume(Player *player, float amount)
{
    player_set_volume(player, player->volume + amount);
}

void player_toggle_mute(Player *player)
{
    player->muted = !player->muted;
    ma_sound *sound = player_get_sound(player);

    if (sound != NULL) {
        ma_sound_set_volume(sound, player->muted ? 0.0f : player->volume);
    }
}

void player_uninit(Player *player)
{
    for (int i = 0; i < 2; ++i) {
        if (player->sound_initialized[i]) {
            ma_sound_uninit(&player->sounds[i]);
            player->sound_initialized[i] = false;
        }
    }

    if (player->engine_initialized) {
        ma_engine_uninit(&player->engine);
        player->engine_initialized = false;
    }
}

bool player_toggle(Player *player)
{
    ma_sound *sound = player_get_sound(player);

    if (sound == NULL) return true;

    ma_result result;

    if (ma_sound_is_playing(sound)) {
        result = ma_sound_stop(sound);
    } else {
        if (ma_sound_at_end(sound)) {
            result = ma_sound_seek_to_pcm_frame(sound, 0);

            if (result != MA_SUCCESS) {
                mp_log(ERROR, "Failed to restart sound: %s",
                       ma_result_description(result));

                return false;
            }
        }

        result = ma_sound_start(sound);
    }

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "Failed to change playback state: %s",
               ma_result_description(result));

        return false;
    }

    return true;
}

bool player_seek(Player *player, float position)
{
    ma_sound *sound = player_get_sound(player);

    if (sound == NULL) return false;

    if (position < 0.0f) position = 0.0f;

    float length = player_get_length(player);

    if (length > 0.0f && position > length) {
        position = length;
    }

    ma_result result = ma_sound_seek_to_second(sound, position);

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "Failed to seek to %.2f seconds: %s", position,
               ma_result_description(result));

        return false;
    }

    return true;
}

Player_State player_get_state(const Player *player)
{
    const ma_sound *sound = player_get_sound_const(player);

    if (sound == NULL) return PLAYER_STOPPED;
    if (ma_sound_at_end(sound)) return PLAYER_FINISHED;
    if (ma_sound_is_playing(sound)) return PLAYER_PLAYING;

    return PLAYER_PAUSED;
}

float player_get_cursor(const Player *player)
{
    const ma_sound *sound = player_get_sound_const(player);
    float cursor = 0.0f;

    if (sound != NULL) ma_sound_get_cursor_in_seconds(sound, &cursor);

    return cursor;
}

float player_get_length(const Player *player)
{
    const ma_sound *sound = player_get_sound_const(player);
    float length = 0.0f;

    if (sound != NULL) ma_sound_get_length_in_seconds(sound, &length);

    return length;
}

float player_get_volume(const Player *player)
{
    return player->volume;
}

bool player_is_muted(const Player *player)
{
    return player->muted;
}
