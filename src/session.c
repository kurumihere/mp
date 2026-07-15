#include "session.h"

#include <errno.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#endif

#include "log.h"

#define MAX_SESSION_PATH ((uint64_t)1024 * 1024)
#define MAX_SESSION_TRACKS ((uint64_t)1000000)

enum {
    SESSION_MUTED = 1u << 0,
    SESSION_SHUFFLED = 1u << 1,
    SESSION_KNOWN_FLAGS = SESSION_MUTED | SESSION_SHUFFLED,
};

static const unsigned char SESSION_MAGIC[] = {'M', 'P', 'S', 'T',
                                              'A', 'T', 'E', 1};

static bool write_bytes(FILE *file, const void *data, size_t size)
{
    return fwrite(data, 1, size, file) == size;
}

static bool write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[] = {
        (unsigned char)value,
        (unsigned char)(value >> 8),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 24),
    };

    return write_bytes(file, bytes, sizeof(bytes));
}

static bool write_u64(FILE *file, uint64_t value)
{
    unsigned char bytes[8];

    for (size_t i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (unsigned char)(value >> (i * 8));
    }

    return write_bytes(file, bytes, sizeof(bytes));
}

static bool read_bytes(FILE *file, void *data, size_t size)
{
    return fread(data, 1, size, file) == size;
}

static bool read_u32(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];

    if (!read_bytes(file, bytes, sizeof(bytes))) return false;

    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return true;
}

static bool read_u64(FILE *file, uint64_t *value)
{
    unsigned char bytes[8];

    if (!read_bytes(file, bytes, sizeof(bytes))) return false;

    *value = 0;

    for (size_t i = 0; i < sizeof(bytes); ++i) {
        *value |= (uint64_t)bytes[i] << (i * 8);
    }

    return true;
}

static uint32_t float_to_bits(float value)
{
    uint32_t bits = 0;

    if (sizeof(value) == sizeof(bits)) memcpy(&bits, &value, sizeof(bits));

    return bits;
}

static float float_from_bits(uint32_t bits)
{
    float value = 0.0f;

    if (sizeof(value) == sizeof(bits)) memcpy(&value, &bits, sizeof(value));

    return value;
}

static bool state_valid(const Session_State *state, size_t track_count)
{
    bool current_valid =
        track_count == 0 ? state->current == 0 : state->current < track_count;

    return sizeof(float) == sizeof(uint32_t) && current_valid &&
           state->cursor == state->cursor && state->cursor >= 0.0f &&
           state->cursor <= FLT_MAX && state->volume == state->volume &&
           state->volume >= 0.0f && state->volume <= 1.0f &&
           state->repeat_mode >= 0 && state->repeat_mode <= 2;
}

static bool make_parent_directories(const char *path)
{
    size_t length = strlen(path) + 1;
    char *copy = malloc(length);

    if (copy == NULL) return false;

    memcpy(copy, path, length);

    for (char *character = copy + 1; *character != '\0'; ++character) {
        if (*character != '/') continue;

        *character = '\0';

#ifdef _WIN32
        int result = _mkdir(copy);
#else
        int result = mkdir(copy, 0700);
#endif

        if (result != 0 && errno != EEXIST) {
            free(copy);
            return false;
        }

        *character = '/';
    }

    free(copy);
    return true;
}

static char *absolute_path(const char *path)
{
    size_t path_length = strlen(path);

    if (path[0] == '/') {
        char *copy = malloc(path_length + 1);

        if (copy != NULL) memcpy(copy, path, path_length + 1);

        return copy;
    }

    size_t capacity = 256;
    char *directory = NULL;

    while (capacity <= MAX_SESSION_PATH) {
        directory = malloc(capacity);

        if (directory == NULL) return NULL;
        if (getcwd(directory, capacity) != NULL) break;

        int error = errno;
        free(directory);
        directory = NULL;

        if (error != ERANGE) return NULL;

        capacity *= 2;
    }

    if (directory == NULL) return NULL;

    size_t directory_length = strlen(directory);
    bool needs_separator =
        directory_length > 0 && directory[directory_length - 1] != '/';

    if (directory_length > SIZE_MAX - path_length - 2) {
        free(directory);
        return NULL;
    }

    size_t length =
        directory_length + path_length + (needs_separator ? 2u : 1u);
    char *absolute = malloc(length);

    if (absolute != NULL) {
        int written = snprintf(absolute, length, "%s%s%s", directory,
                               needs_separator ? "/" : "", path);

        if (written < 0 || (size_t)written >= length) {
            free(absolute);
            absolute = NULL;
        }
    }

    free(directory);
    return absolute;
}

static void free_paths(char **paths, size_t count)
{
    if (paths == NULL) return;

    for (size_t i = 0; i < count; ++i) {
        free(paths[i]);
    }

    free(paths);
}

void session_state_defaults(Session_State *state)
{
    *state = (Session_State){
        .volume = 0.5f,
    };
}

bool session_default_path(char *path, size_t capacity)
{
    const char *state_home = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    int written;

    if (state_home != NULL && state_home[0] != '\0') {
        written = snprintf(path, capacity, "%s/mp/state", state_home);
    } else if (home != NULL && home[0] != '\0') {
        written = snprintf(path, capacity, "%s/.local/state/mp/state", home);
    } else {
        return false;
    }

    return written >= 0 && (size_t)written < capacity;
}

Session_Load_Result session_load(const char *path, Playlist *playlist,
                                 Session_State *state)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        if (errno == ENOENT) return SESSION_LOAD_NOT_FOUND;

        mp_log(WARNING, "Failed to open saved session \"%s\": %s", path,
               strerror(errno));
        return SESSION_LOAD_ERROR;
    }

    unsigned char magic[sizeof(SESSION_MAGIC)];
    uint32_t flags = 0;
    uint32_t repeat_mode = 0;
    uint32_t cursor_bits = 0;
    uint32_t volume_bits = 0;
    uint64_t current = 0;
    uint64_t track_count = 0;

    bool valid = read_bytes(file, magic, sizeof(magic)) &&
                 memcmp(magic, SESSION_MAGIC, sizeof(magic)) == 0 &&
                 read_u32(file, &flags) && read_u32(file, &repeat_mode) &&
                 read_u64(file, &current) && read_u32(file, &cursor_bits) &&
                 read_u32(file, &volume_bits) && read_u64(file, &track_count) &&
                 (flags & ~SESSION_KNOWN_FLAGS) == 0 &&
                 track_count <= MAX_SESSION_TRACKS && track_count <= SIZE_MAX &&
                 current <= SIZE_MAX;

    char **paths = NULL;
    size_t count = valid ? (size_t)track_count : 0;

    if (valid && count > 0) {
        if (count > SIZE_MAX / sizeof(*paths)) {
            valid = false;
        } else {
            paths = calloc(count, sizeof(*paths));
            valid = paths != NULL;
        }
    }

    for (size_t i = 0; valid && i < count; ++i) {
        uint64_t path_length;
        valid = read_u64(file, &path_length) && path_length > 0 &&
                path_length <= MAX_SESSION_PATH && path_length < SIZE_MAX;

        if (!valid) break;

        size_t length = (size_t)path_length;
        paths[i] = malloc(length + 1);
        valid = paths[i] != NULL && read_bytes(file, paths[i], length);

        if (valid) {
            paths[i][length] = '\0';
            valid = memchr(paths[i], '\0', length) == NULL;
        }
    }

    if (valid) {
        int trailing = fgetc(file);
        valid = trailing == EOF && !ferror(file);
    }

    if (fclose(file) != 0) valid = false;

    Session_State replacement = {
        .current = (size_t)current,
        .cursor = float_from_bits(cursor_bits),
        .volume = float_from_bits(volume_bits),
        .repeat_mode = (int)repeat_mode,
        .muted = (flags & SESSION_MUTED) != 0,
        .shuffled = (flags & SESSION_SHUFFLED) != 0,
    };

    if (valid) valid = state_valid(&replacement, count);

    if (valid) {
        valid = playlist_replace(playlist, (const char *const *)paths, count);
    }

    free_paths(paths, count);

    if (!valid) {
        mp_log(WARNING, "Ignoring invalid saved session: %s", path);
        return SESSION_LOAD_ERROR;
    }

    *state = replacement;
    return SESSION_LOAD_OK;
}

bool session_save(const char *path, const Playlist *playlist,
                  const Session_State *state)
{
    size_t count = playlist_get_count(playlist);

    if (!state_valid(state, count) || count > MAX_SESSION_TRACKS ||
        !make_parent_directories(path)) {
        mp_log(ERROR, "Failed to prepare saved session: %s", path);
        return false;
    }

    size_t path_length = strlen(path);

    if (path_length > SIZE_MAX - sizeof(".tmp")) return false;

    char *temporary = malloc(path_length + sizeof(".tmp"));

    if (temporary == NULL) return false;

    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp", sizeof(".tmp"));

    FILE *file = fopen(temporary, "wb");

    if (file == NULL) {
        mp_log(ERROR, "Failed to create saved session \"%s\": %s", path,
               strerror(errno));
        free(temporary);
        return false;
    }

    uint32_t flags = (state->muted ? SESSION_MUTED : 0u) |
                     (state->shuffled ? SESSION_SHUFFLED : 0u);

    bool success = write_bytes(file, SESSION_MAGIC, sizeof(SESSION_MAGIC)) &&
                   write_u32(file, flags) &&
                   write_u32(file, (uint32_t)state->repeat_mode) &&
                   write_u64(file, state->current) &&
                   write_u32(file, float_to_bits(state->cursor)) &&
                   write_u32(file, float_to_bits(state->volume)) &&
                   write_u64(file, count);

    for (size_t i = 0; success && i < count; ++i) {
        const char *track = playlist_get(playlist, i);
        char *absolute = track == NULL ? NULL : absolute_path(track);
        size_t length = absolute == NULL ? 0 : strlen(absolute);

        if (length == 0 || length > MAX_SESSION_PATH) {
            success = false;
        } else {
            success =
                write_u64(file, length) && write_bytes(file, absolute, length);
        }

        free(absolute);
    }

    if (fclose(file) != 0) success = false;

    if (success && rename(temporary, path) != 0) success = false;

    if (!success) {
        remove(temporary);
        mp_log(ERROR, "Failed to save session: %s", path);
    }

    free(temporary);
    return success;
}
