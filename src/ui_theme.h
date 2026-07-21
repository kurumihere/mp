#ifndef MP_UI_THEME_H
#define MP_UI_THEME_H

#include <stdbool.h>

#include "raylib.h"
#include "theme.h"

#define UI_THEME_TRANSITION_SECONDS 0.5f

typedef struct {
    Color background;
    Color surface;
    Color solid_surface;
    Color surface_border;
    Color spectrum;
    Color button;
    Color button_disabled;
    Color button_active;
    Color button_hover;
    Color text_primary;
    Color text_secondary;
    Color text_muted;
    Color text_faint;
    Color playlist_current;
    Color playlist_item;
    Color playlist_hover;
    Color progress_background;
    Color progress_foreground;
    Color progress_hover;
} Ui_Theme;

typedef struct {
    Ui_Theme current;
    Ui_Theme start;
    Ui_Theme target;
    float progress;
    bool active;
} Ui_Theme_Transition;

void ui_theme_transition_init(Ui_Theme_Transition *transition,
                              System_Theme theme);
void ui_theme_transition_begin(Ui_Theme_Transition *transition,
                               System_Theme target);
void ui_theme_transition_update(Ui_Theme_Transition *transition,
                                float delta_time);
const Ui_Theme *
ui_theme_transition_current(const Ui_Theme_Transition *transition);

#endif
