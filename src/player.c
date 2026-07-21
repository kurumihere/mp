#include "player.h"

#include <stdlib.h>

#include "fs.h"
#include "log.h"

static ma_uint32 atomic_uint32_load(const ma_atomic_uint32 *value)
{
    return __atomic_load_n(&value->value, __ATOMIC_ACQUIRE);
}

static void atomic_uint32_store(ma_atomic_uint32 *target, ma_uint32 value)
{
    __atomic_store_n(&target->value, value, __ATOMIC_RELEASE);
}

static float atomic_float_load(const ma_atomic_float *value)
{
    float result;
    __atomic_load(&value->value, &result, __ATOMIC_ACQUIRE);
    return result;
}

static void atomic_float_store(ma_atomic_float *target, float value)
{
    __atomic_store(&target->value, &value, __ATOMIC_RELEASE);
}

static void player_process_audio(void *user_data, float *frames,
                                 ma_uint64 frame_count)
{
    Player *player = user_data;
    ma_uint32 channels = atomic_uint32_load(&player->analysis_channels);

    if (frames == NULL || channels == 0) return;

    ma_uint32 cursor = atomic_uint32_load(&player->analysis_cursor);

    for (ma_uint64 frame = 0; frame < frame_count; ++frame) {
        size_t frame_offset = (size_t)frame * channels;
        float sample = 0.0f;

        for (ma_uint32 channel = 0; channel < channels; ++channel) {
            sample += frames[frame_offset + channel];
        }

        sample /= (float)channels;
        atomic_float_store(&player->analysis_samples[cursor], sample);

        ++cursor;

        if (cursor == PLAYER_ANALYSIS_BUFFER_SIZE) cursor = 0;
    }

    atomic_uint32_store(&player->analysis_cursor, cursor);
}

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

    ma_engine_config config = ma_engine_config_init();
    config.onProcess = player_process_audio;
    config.pProcessUserData = player;
    ma_result result = ma_engine_init(&config, &player->engine);

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "failed to initialize audio engine: %s",
               ma_result_description(result));

        return false;
    }

    player->engine_initialized = true;
    atomic_uint32_store(&player->analysis_channels,
                        ma_engine_get_channels(&player->engine));
    atomic_uint32_store(&player->analysis_sample_rate,
                        ma_engine_get_sample_rate(&player->engine));

    return true;
}

bool player_load(Player *player, const char *path)
{
    bool had_sound = player->sound_initialized[player->active_sound];
    int next_sound =
        had_sound ? 1 - player->active_sound : player->active_sound;

    ma_result result;

#ifdef _WIN32
    wchar_t *wide_path = fs_utf8_to_utf16(path);
    result = wide_path == NULL
                 ? MA_INVALID_FILE
                 : ma_sound_init_from_file_w(&player->engine, wide_path,
                                             MA_SOUND_FLAG_STREAM, NULL, NULL,
                                             &player->sounds[next_sound]);
    free(wide_path);
#else
    result =
        ma_sound_init_from_file(&player->engine, path, MA_SOUND_FLAG_STREAM,
                                NULL, NULL, &player->sounds[next_sound]);
#endif

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "failed to load \"%s\": %s", path,
               ma_result_description(result));

        return false;
    }

    player->sound_initialized[next_sound] = true;
    ma_sound_set_volume(&player->sounds[next_sound],
                        player->muted ? 0.0f : player->volume);

    result = ma_sound_start(&player->sounds[next_sound]);

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "failed to start playback: %s",
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
    player->stopped = false;

    mp_log(INFO, "playing \"%s\"", path);
    return true;
}

void player_clear(Player *player)
{
    ma_sound *sound = player_get_sound(player);

    if (sound == NULL) return;

    ma_sound_uninit(sound);
    player->sound_initialized[player->active_sound] = false;
    player->stopped = false;
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
                mp_log(ERROR, "failed to restart sound: %s",
                       ma_result_description(result));

                return false;
            }
        }

        result = ma_sound_start(sound);
    }

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "failed to change playback state: %s",
               ma_result_description(result));

        return false;
    }

    player->stopped = false;
    return true;
}

bool player_stop(Player *player)
{
    ma_sound *sound = player_get_sound(player);

    if (sound == NULL) return true;

    ma_result result = ma_sound_stop(sound);

    if (result == MA_SUCCESS) result = ma_sound_seek_to_pcm_frame(sound, 0);

    if (result != MA_SUCCESS) {
        mp_log(ERROR, "failed to stop playback: %s",
               ma_result_description(result));
        return false;
    }

    player->stopped = true;
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
        mp_log(ERROR, "failed to seek to %.2f seconds: %s", position,
               ma_result_description(result));

        return false;
    }

    return true;
}

Player_State player_get_state(const Player *player)
{
    const ma_sound *sound = player_get_sound_const(player);

    if (sound == NULL) return PLAYER_EMPTY;
    if (player->stopped) return PLAYER_STOPPED;
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

size_t player_copy_analysis_samples(const Player *player, float *samples,
                                    size_t sample_count,
                                    unsigned int *sample_rate)
{
    if (sample_rate != NULL) {
        *sample_rate = atomic_uint32_load(&player->analysis_sample_rate);
    }

    if (samples == NULL || sample_count == 0) return 0;

    if (sample_count > PLAYER_ANALYSIS_BUFFER_SIZE) {
        sample_count = PLAYER_ANALYSIS_BUFFER_SIZE;
    }

    ma_uint32 cursor = atomic_uint32_load(&player->analysis_cursor);
    size_t start = (cursor + PLAYER_ANALYSIS_BUFFER_SIZE - sample_count) %
                   PLAYER_ANALYSIS_BUFFER_SIZE;

    for (size_t i = 0; i < sample_count; ++i) {
        size_t index = (start + i) % PLAYER_ANALYSIS_BUFFER_SIZE;
        samples[i] = atomic_float_load(&player->analysis_samples[index]);
    }

    return sample_count;
}
