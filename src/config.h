#ifndef MP_CONFIG_H
#define MP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    APP_CONFIG_LOAD_NOT_FOUND,
    APP_CONFIG_LOAD_OK,
    APP_CONFIG_LOAD_ERROR,
} App_Config_Load_Result;

typedef struct {
    bool playlist_button_on_side;
    bool global_media_keys;
    bool save_session;
} App_Config;

void app_config_defaults(App_Config *config);
bool app_config_default_path(char *path, size_t capacity);
App_Config_Load_Result app_config_load(const char *path, App_Config *config);
bool app_config_save(const char *path, const App_Config *config);

#endif
