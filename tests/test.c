#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "m3u.h"
#include "metadata.h"
#include "playback_order.h"
#include "playlist.h"
#include "session.h"

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,   \
                    #condition);                                               \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

typedef struct {
    uint32_t state;
} Test_Random;

static size_t test_random_index(size_t upper_bound, void *context)
{
    Test_Random *random = context;
    random->state = random->state * 1664525u + 1013904223u;

    return upper_bound == 0 ? 0 : random->state % upper_bound;
}

static void test_playback_order(void)
{
    Playback_Order order;
    playback_order_init(&order);

    Test_Random random = {.state = 1};
    CHECK(playback_order_reset(&order, 5, 2, true, test_random_index, &random));

    bool seen[5] = {false};
    seen[2] = true;
    size_t index = 2;

    for (size_t i = 0; i < 4; ++i) {
        CHECK(playback_order_next(&order, false, test_random_index, &random,
                                  &index));
        CHECK(index < 5);
        CHECK(!seen[index]);
        seen[index] = true;
    }

    CHECK(!playback_order_next(&order, false, test_random_index, &random,
                               &index));

    size_t last = index;
    size_t previous;
    CHECK(playback_order_previous(&order, false, &previous));
    CHECK(previous != last);
    CHECK(
        playback_order_next(&order, false, test_random_index, &random, &index));
    CHECK(index == last);

    CHECK(
        playback_order_next(&order, true, test_random_index, &random, &index));
    CHECK(index != last);

    memset(seen, 0, sizeof(seen));
    seen[index] = true;

    for (size_t i = 1; i < 5; ++i) {
        CHECK(playback_order_next(&order, false, test_random_index, &random,
                                  &index));
        CHECK(!seen[index]);
        seen[index] = true;
    }

    CHECK(playback_order_set_shuffled(&order, index, false, test_random_index,
                                      &random));
    CHECK(playback_order_select(&order, 1, test_random_index, &random));
    CHECK(playback_order_previous(&order, false, &index));
    CHECK(index == 0);
    CHECK(playback_order_previous(&order, true, &index));
    CHECK(index == 4);
    CHECK(
        playback_order_next(&order, true, test_random_index, &random, &index));
    CHECK(index == 0);

    playback_order_uninit(&order);
}

static void test_playlist(void)
{
    const char *paths[] = {
        "/missing/first.mp3",
        "/missing/second.flac",
        "/missing/third.wav",
    };

    Playlist playlist;
    playlist_init(&playlist);

    CHECK(playlist_append(&playlist, paths, 3));
    CHECK(playlist_get_count(&playlist) == 3);
    CHECK(strcmp(playlist_get(&playlist, 1), paths[1]) == 0);
    CHECK(strcmp(playlist_get_metadata(&playlist, 2)->title, "third.wav") == 0);
    CHECK(playlist_select(&playlist, 1));
    CHECK(playlist_remove(&playlist, 0));
    CHECK(playlist_get_count(&playlist) == 2);
    CHECK(playlist_get_current(&playlist) == 0);
    CHECK(strcmp(playlist_get(&playlist, 0), paths[1]) == 0);
    CHECK(!playlist_select(&playlist, 2));

    CHECK(playlist_replace(&playlist, &paths[2], 1));
    CHECK(playlist_get_count(&playlist) == 1);
    CHECK(strcmp(playlist_get(&playlist, 0), paths[2]) == 0);

    playlist_clear(&playlist);
    CHECK(playlist_get_count(&playlist) == 0);
    playlist_uninit(&playlist);
}

static void test_m3u(void)
{
    const char *input_path = "build/cache/test-input.m3u";
    const char *output_path = "build/cache/test-output.m3u8";
    FILE *file = fopen(input_path, "wb");

    CHECK(file != NULL);

    if (file != NULL) {
        fputs("\xef\xbb\xbf#EXTM3U\r\n", file);
        fputs("#EXTINF:1,Relative\n", file);
        fputs("relative.mp3\r\n", file);
        fputs("/absolute/track.flac\n", file);
        CHECK(fclose(file) == 0);
    }

    CHECK(m3u_is_path("playlist.M3U"));
    CHECK(m3u_is_path("playlist.m3u8"));
    CHECK(!m3u_is_path("playlist.txt"));

    Playlist playlist;
    playlist_init(&playlist);
    CHECK(m3u_replace(&playlist, input_path));
    CHECK(playlist_get_count(&playlist) == 2);

    const char *relative = playlist_get(&playlist, 0);
    const char *suffix = "/build/cache/relative.mp3";
    size_t suffix_length = strlen(suffix);
    CHECK(relative != NULL);

    if (relative != NULL) {
        size_t relative_length = strlen(relative);
        CHECK(relative_length >= suffix_length);

        if (relative_length >= suffix_length) {
            CHECK(strcmp(relative + relative_length - suffix_length, suffix) ==
                  0);
        }
    }

    CHECK(strcmp(playlist_get(&playlist, 1), "/absolute/track.flac") == 0);

    const char *saved_paths[] = {
        "/music/first.mp3",
        "/music/second.flac",
    };
    CHECK(playlist_replace(&playlist, saved_paths, 2));
    CHECK(m3u_save(&playlist, output_path));

    Playlist restored;
    playlist_init(&restored);
    CHECK(m3u_replace(&restored, output_path));
    CHECK(playlist_get_count(&restored) == 2);
    CHECK(strcmp(playlist_get(&restored, 0), saved_paths[0]) == 0);
    CHECK(strcmp(playlist_get(&restored, 1), saved_paths[1]) == 0);

    size_t added = 0;
    CHECK(m3u_append(&restored, output_path, &added));
    CHECK(added == 2);
    CHECK(playlist_get_count(&restored) == 4);

    playlist_uninit(&restored);
    playlist_uninit(&playlist);
    remove(input_path);
    remove(output_path);
}

static void test_session(void)
{
    const char *directory = "build/cache/test-session";
    const char *path = "build/cache/test-session/state";
    const char *tracks[] = {
        "/music/session-first.mp3",
        "session-second.flac",
    };

    remove(path);
    rmdir(directory);

    Playlist playlist;
    playlist_init(&playlist);
    CHECK(playlist_append(&playlist, tracks, 2));
    CHECK(playlist_select(&playlist, 1));

    Session_State state = {
        .current = 1,
        .cursor = 42.5f,
        .volume = 0.75f,
        .repeat_mode = 2,
        .muted = true,
        .shuffled = true,
    };

    CHECK(session_save(path, &playlist, &state));

    Playlist restored;
    playlist_init(&restored);
    Session_State restored_state;
    session_state_defaults(&restored_state);

    CHECK(session_load(path, &restored, &restored_state) == SESSION_LOAD_OK);
    CHECK(playlist_get_count(&restored) == 2);
    CHECK(strcmp(playlist_get(&restored, 0), tracks[0]) == 0);

    char working_directory[4096];
    char absolute_track[4096];
    bool have_working_directory =
        getcwd(working_directory, sizeof(working_directory)) != NULL;
    CHECK(have_working_directory);

    if (have_working_directory) {
        int written = snprintf(absolute_track, sizeof(absolute_track), "%s/%s",
                               working_directory, tracks[1]);
        CHECK(written >= 0 && (size_t)written < sizeof(absolute_track));

        if (written >= 0 && (size_t)written < sizeof(absolute_track)) {
            CHECK(strcmp(playlist_get(&restored, 1), absolute_track) == 0);
        }
    }
    CHECK(restored_state.current == state.current);
    CHECK(restored_state.cursor == state.cursor);
    CHECK(restored_state.volume == state.volume);
    CHECK(restored_state.repeat_mode == state.repeat_mode);
    CHECK(restored_state.muted == state.muted);
    CHECK(restored_state.shuffled == state.shuffled);

    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);

    if (file != NULL) {
        fputs("invalid", file);
        CHECK(fclose(file) == 0);
    }

    CHECK(session_load(path, &restored, &restored_state) == SESSION_LOAD_ERROR);
    CHECK(playlist_get_count(&restored) == 2);
    CHECK(restored_state.cursor == state.cursor);

    remove(path);
    CHECK(session_load(path, &restored, &restored_state) ==
          SESSION_LOAD_NOT_FOUND);
    rmdir(directory);
    playlist_uninit(&restored);
    playlist_uninit(&playlist);
}

static void write_u24_be(FILE *file, uint32_t value)
{
    unsigned char bytes[] = {
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 8),
        (unsigned char)value,
    };
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void write_u32_be(FILE *file, uint32_t value)
{
    unsigned char bytes[] = {
        (unsigned char)(value >> 24),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 8),
        (unsigned char)value,
    };
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void write_u32_le(FILE *file, uint32_t value)
{
    unsigned char bytes[] = {
        (unsigned char)value,
        (unsigned char)(value >> 8),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 24),
    };
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void write_synchsafe(FILE *file, uint32_t value)
{
    unsigned char bytes[] = {
        (unsigned char)((value >> 21) & 0x7f),
        (unsigned char)((value >> 14) & 0x7f),
        (unsigned char)((value >> 7) & 0x7f),
        (unsigned char)(value & 0x7f),
    };
    fwrite(bytes, 1, sizeof(bytes), file);
}

static void write_id3_frame(FILE *file, const char id[4], const char *value)
{
    fwrite(id, 1, 4, file);
    write_u32_be(file, (uint32_t)strlen(value) + 1);
    fputc(0, file);
    fputc(0, file);
    fputc(3, file);
    fwrite(value, 1, strlen(value), file);
}

static bool write_id3_fixture(const char *path)
{
    const char *values[] = {"ID3 Title", "ID3 Artist", "ID3 Album"};
    uint32_t tag_size = 0;

    for (size_t i = 0; i < 3; ++i) {
        tag_size += 11u + (uint32_t)strlen(values[i]);
    }

    FILE *file = fopen(path, "wb");

    if (file == NULL) return false;

    fwrite("ID3", 1, 3, file);
    fputc(3, file);
    fputc(0, file);
    fputc(0, file);
    write_synchsafe(file, tag_size);
    write_id3_frame(file, "TIT2", values[0]);
    write_id3_frame(file, "TPE1", values[1]);
    write_id3_frame(file, "TALB", values[2]);

    return fclose(file) == 0;
}

static bool write_flac_fixture(const char *path)
{
    const char *vendor = "mp";
    const char *comments[] = {
        "TITLE=FLAC Title",
        "ARTIST=FLAC Artist",
        "ALBUM=FLAC Album",
    };
    uint32_t block_size = 8u + (uint32_t)strlen(vendor);

    for (size_t i = 0; i < 3; ++i) {
        block_size += 4u + (uint32_t)strlen(comments[i]);
    }

    FILE *file = fopen(path, "wb");

    if (file == NULL) return false;

    fwrite("fLaC", 1, 4, file);
    fputc(0x84, file);
    write_u24_be(file, block_size);
    write_u32_le(file, (uint32_t)strlen(vendor));
    fwrite(vendor, 1, strlen(vendor), file);
    write_u32_le(file, 3);

    for (size_t i = 0; i < 3; ++i) {
        write_u32_le(file, (uint32_t)strlen(comments[i]));
        fwrite(comments[i], 1, strlen(comments[i]), file);
    }

    return fclose(file) == 0;
}

static void write_wav_info(FILE *file, const char id[4], const char *value)
{
    uint32_t size = (uint32_t)strlen(value) + 1;
    fwrite(id, 1, 4, file);
    write_u32_le(file, size);
    fwrite(value, 1, size, file);

    if ((size & 1u) != 0) fputc(0, file);
}

static bool write_wav_fixture(const char *path)
{
    const char *values[] = {"WAV Title", "WAV Artist", "WAV Album"};
    uint32_t list_size = 4;

    for (size_t i = 0; i < 3; ++i) {
        uint32_t size = (uint32_t)strlen(values[i]) + 1;
        list_size += 8u + size + (size & 1u);
    }

    FILE *file = fopen(path, "wb");

    if (file == NULL) return false;

    fwrite("RIFF", 1, 4, file);
    write_u32_le(file, 4u + 8u + list_size);
    fwrite("WAVE", 1, 4, file);
    fwrite("LIST", 1, 4, file);
    write_u32_le(file, list_size);
    fwrite("INFO", 1, 4, file);
    write_wav_info(file, "INAM", values[0]);
    write_wav_info(file, "IART", values[1]);
    write_wav_info(file, "IPRD", values[2]);

    return fclose(file) == 0;
}

static void check_metadata(const char *path, const char *title,
                           const char *artist, const char *album)
{
    Track_Metadata metadata;
    metadata_load(path, &metadata);
    CHECK(strcmp(metadata.title, title) == 0);
    CHECK(strcmp(metadata.artist, artist) == 0);
    CHECK(strcmp(metadata.album, album) == 0);
}

static void test_metadata(void)
{
    const char *id3_path = "build/cache/test-metadata.mp3";
    const char *flac_path = "build/cache/test-metadata.flac";
    const char *wav_path = "build/cache/test-metadata.wav";

    CHECK(write_id3_fixture(id3_path));
    CHECK(write_flac_fixture(flac_path));
    CHECK(write_wav_fixture(wav_path));

    check_metadata(id3_path, "ID3 Title", "ID3 Artist", "ID3 Album");
    check_metadata(flac_path, "FLAC Title", "FLAC Artist", "FLAC Album");
    check_metadata(wav_path, "WAV Title", "WAV Artist", "WAV Album");

    Track_Metadata missing;
    metadata_load("/missing/Fallback.mp3", &missing);
    CHECK(strcmp(missing.title, "Fallback.mp3") == 0);
    CHECK(missing.artist[0] == '\0');
    CHECK(missing.album[0] == '\0');

    remove(id3_path);
    remove(flac_path);
    remove(wav_path);
}

int main(void)
{
    test_playback_order();
    test_playlist();
    test_m3u();
    test_session();
    test_metadata();

    if (failures != 0) {
        fprintf(stderr, "%d test checks failed\n", failures);
        return 1;
    }

    puts("All tests passed");
    return 0;
}
