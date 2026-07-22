#ifndef MP_UI_ICONS_H
#define MP_UI_ICONS_H

#include <stdbool.h>

#include "raylib.h"
#include "theme.h"

typedef enum {
    UI_ICON_PREVIOUS,
    UI_ICON_PLAY,
    UI_ICON_PAUSE,
    UI_ICON_NEXT,
    UI_ICON_REPEAT,
    UI_ICON_REPEAT_ONE,
    UI_ICON_SHUFFLE,
    UI_ICON_PLAYLIST,
    UI_ICON_SETTINGS,
    UI_ICON_PLAYLIST_BACK,
    UI_ICON_PLAYLIST_FORWARD,
    UI_ICON_COUNT,
} Ui_Icon;

typedef struct {
    Texture2D textures[UI_ICON_COUNT];
} Ui_Icon_Set;

typedef struct {
    Ui_Icon_Set current;
    Ui_Icon_Set next;
    System_Theme current_theme;
    System_Theme target_theme;
    float progress;
    bool active;
} Ui_Icon_Transition;

bool ui_application_icon_load(Texture2D *texture);
void ui_application_icon_draw(Texture2D texture, Rectangle bounds);

bool ui_icon_transition_init(Ui_Icon_Transition *transition,
                             System_Theme theme);
bool ui_icon_transition_begin(Ui_Icon_Transition *transition,
                              System_Theme target);
void ui_icon_transition_update(Ui_Icon_Transition *transition,
                               float delta_time);
void ui_icon_transition_draw(const Ui_Icon_Transition *transition, Ui_Icon icon,
                             Rectangle bounds, float opacity);
void ui_icon_transition_draw_rotated(const Ui_Icon_Transition *transition,
                                     Ui_Icon icon, Rectangle bounds,
                                     float rotation, float opacity);
void ui_icon_transition_uninit(Ui_Icon_Transition *transition);

#endif
