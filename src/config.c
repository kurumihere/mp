#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

#include "log.h"

#define CONFIG_LINE_SIZE 1024

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

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) ++text;

    char *end = text + strlen(text);

    while (end > text && isspace((unsigned char)end[-1])) --end;

    *end = '\0';
    return text;
}

void app_config_defaults(App_Config *config)
{
    *config = (App_Config){
        .playlist_button_on_side = true,
    };
}

bool app_config_default_path(char *path, size_t capacity)
{
    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int written;

    if (config_home != NULL && config_home[0] != '\0') {
        written = snprintf(path, capacity, "%s/mp/config.toml", config_home);
    } else if (home != NULL && home[0] != '\0') {
        written =
            snprintf(path, capacity, "%s/.config/mp/config.toml", home);
#ifdef _WIN32
    } else {
        const char *app_data = getenv("APPDATA");

        if (app_data == NULL || app_data[0] == '\0') return false;

        written = snprintf(path, capacity, "%s/mp/config.toml", app_data);
#else
    } else {
        return false;
#endif
    }

    return written >= 0 && (size_t)written < capacity;
}

App_Config_Load_Result app_config_load(const char *path, App_Config *config)
{
    FILE *file = fopen(path, "r");

    if (file == NULL) {
        if (errno == ENOENT) return APP_CONFIG_LOAD_NOT_FOUND;

        mp_log(WARNING, "failed to open config \"%s\": %s", path,
               strerror(errno));
        return APP_CONFIG_LOAD_ERROR;
    }

    App_Config loaded;
    app_config_defaults(&loaded);
    char line[CONFIG_LINE_SIZE];
    bool valid = true;

    while (valid && fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);

        if (length == sizeof(line) - 1 && line[length - 1] != '\n' &&
            !feof(file)) {
            valid = false;
            break;
        }

        char *entry = trim(line);

        if (*entry == '\0' || *entry == '#') continue;

        char *equals = strchr(entry, '=');

        if (equals == NULL) {
            valid = false;
            break;
        }

        *equals = '\0';
        char *key = trim(entry);
        char *value = trim(equals + 1);
        char *comment = strchr(value, '#');

        if (comment != NULL) {
            *comment = '\0';
            value = trim(value);
        }

        if (strcmp(key, "playlist_button_on_side") != 0) continue;

        if (strcmp(value, "true") == 0) {
            loaded.playlist_button_on_side = true;
        } else if (strcmp(value, "false") == 0) {
            loaded.playlist_button_on_side = false;
        } else {
            valid = false;
        }
    }

    if (ferror(file) || fclose(file) != 0) valid = false;

    if (!valid) {
        mp_log(WARNING, "ignoring invalid config: %s", path);
        return APP_CONFIG_LOAD_ERROR;
    }

    *config = loaded;
    return APP_CONFIG_LOAD_OK;
}

bool app_config_save(const char *path, const App_Config *config)
{
    if (!make_parent_directories(path)) {
        mp_log(ERROR, "failed to prepare config path: %s", path);
        return false;
    }

    size_t path_length = strlen(path);

    if (path_length > SIZE_MAX - sizeof(".tmp")) return false;

    char *temporary = malloc(path_length + sizeof(".tmp"));

    if (temporary == NULL) return false;

    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp", sizeof(".tmp"));

    FILE *file = fopen(temporary, "w");
    bool success = file != NULL;

    if (success) {
        success = fprintf(file, "playlist_button_on_side = %s\n",
                          config->playlist_button_on_side ? "true" : "false") >
                  0;
        if (fclose(file) != 0) success = false;
    }

    if (success && rename(temporary, path) != 0) success = false;

    if (!success) {
        remove(temporary);
        mp_log(ERROR, "failed to save config: %s", path);
    }

    free(temporary);
    return success;
}
