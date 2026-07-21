#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "log.h"

#define CONFIG_LINE_SIZE 1024

static char *trim(char *text)
{
    while (isspace((unsigned char)*text))
        ++text;

    char *end = text + strlen(text);

    while (end > text && isspace((unsigned char)end[-1]))
        --end;

    *end = '\0';
    return text;
}

void app_config_defaults(App_Config *config)
{
    *config = (App_Config){
        .playlist_button_on_side = true,
        .global_media_keys = true,
        .save_session = true,
    };
}

bool app_config_default_path(char *path, size_t capacity)
{
    char *config_home = fs_environment("XDG_CONFIG_HOME");
    char *home = fs_environment("HOME");
    int written = -1;

    if (config_home != NULL) {
        written = snprintf(path, capacity, "%s/mp/config.toml", config_home);
    } else if (home != NULL) {
        written = snprintf(path, capacity, "%s/.config/mp/config.toml", home);
#ifdef _WIN32
    } else {
        char *app_data = fs_environment("APPDATA");

        if (app_data != NULL) {
            written = snprintf(path, capacity, "%s/mp/config.toml", app_data);
        }
        free(app_data);
#else
    } else {
        written = -1;
#endif
    }

    free(home);
    free(config_home);
    return written >= 0 && (size_t)written < capacity;
}

App_Config_Load_Result app_config_load(const char *path, App_Config *config)
{
    FILE *file = fs_fopen(path, "r");

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

        bool *setting = NULL;

        if (strcmp(key, "playlist_button_on_side") == 0) {
            setting = &loaded.playlist_button_on_side;
        } else if (strcmp(key, "global_media_keys") == 0) {
            setting = &loaded.global_media_keys;
        } else if (strcmp(key, "save_session") == 0) {
            setting = &loaded.save_session;
        } else {
            continue;
        }

        if (strcmp(value, "true") == 0) {
            *setting = true;
        } else if (strcmp(value, "false") == 0) {
            *setting = false;
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
    if (!fs_make_parent_directories(path)) {
        mp_log(ERROR, "failed to prepare config path: %s", path);
        return false;
    }

    size_t path_length = strlen(path);

    if (path_length > SIZE_MAX - sizeof(".tmp")) return false;

    char *temporary = malloc(path_length + sizeof(".tmp"));

    if (temporary == NULL) return false;

    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp", sizeof(".tmp"));

    FILE *file = fs_fopen(temporary, "w");
    bool success = file != NULL;

    if (success) {
        success =
            fprintf(file, "playlist_button_on_side = %s\n",
                    config->playlist_button_on_side ? "true" : "false") > 0 &&
            fprintf(file, "global_media_keys = %s\n",
                    config->global_media_keys ? "true" : "false") > 0 &&
            fprintf(file, "save_session = %s\n",
                    config->save_session ? "true" : "false") > 0;
        if (fclose(file) != 0) success = false;
    }

    if (success && fs_rename(temporary, path) != 0) success = false;

    if (!success) {
        fs_remove(temporary);
        mp_log(ERROR, "failed to save config: %s", path);
    }

    free(temporary);
    return success;
}
