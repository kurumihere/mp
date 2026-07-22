#include "ui_theme.h"

static const Ui_Theme ui_themes[] = {
    [SYSTEM_THEME_LIGHT] =
        {
            .background = {243, 243, 243, 255},
            .surface = {255, 255, 255, 196},
            .solid_surface = {240, 240, 240, 255},
            .surface_border = {20, 20, 20, 38},
            .spectrum = {42, 42, 42, 235},
            .button = {255, 255, 255, 112},
            .button_disabled = {255, 255, 255, 52},
            .button_active = {20, 20, 20, 34},
            .button_hover = {255, 255, 255, 225},
            .text_primary = {24, 24, 24, 255},
            .text_secondary = {58, 58, 58, 245},
            .text_muted = {92, 92, 92, 220},
            .text_faint = {125, 125, 125, 180},
            .playlist_current = {20, 20, 20, 28},
            .playlist_item = {255, 255, 255, 62},
            .playlist_hover = {255, 255, 255, 190},
            .progress_background = {20, 20, 20, 38},
            .progress_foreground = {45, 45, 45, 245},
            .progress_hover = {0, 0, 0, 255},
        },
    [SYSTEM_THEME_DARK] =
        {
            .background = {15, 15, 15, 255},
            .surface = {38, 38, 38, 205},
            .solid_surface = {24, 24, 24, 255},
            .surface_border = {255, 255, 255, 34},
            .spectrum = {225, 225, 225, 230},
            .button = {255, 255, 255, 14},
            .button_disabled = {255, 255, 255, 5},
            .button_active = {255, 255, 255, 38},
            .button_hover = {255, 255, 255, 29},
            .text_primary = {245, 245, 245, 255},
            .text_secondary = {215, 215, 215, 245},
            .text_muted = {155, 155, 155, 220},
            .text_faint = {105, 105, 105, 180},
            .playlist_current = {255, 255, 255, 30},
            .playlist_item = {255, 255, 255, 7},
            .playlist_hover = {255, 255, 255, 22},
            .progress_background = {255, 255, 255, 32},
            .progress_foreground = {235, 235, 235, 245},
            .progress_hover = {255, 255, 255, 255},
        },
};

static const Ui_Theme *theme_for(System_Theme theme)
{
    return &ui_themes[theme == SYSTEM_THEME_DARK ? SYSTEM_THEME_DARK
                                                 : SYSTEM_THEME_LIGHT];
}

static Color blend_color(Color start, Color target, float amount)
{
    return (Color){
        (unsigned char)(start.r + (target.r - start.r) * amount + 0.5f),
        (unsigned char)(start.g + (target.g - start.g) * amount + 0.5f),
        (unsigned char)(start.b + (target.b - start.b) * amount + 0.5f),
        (unsigned char)(start.a + (target.a - start.a) * amount + 0.5f),
    };
}

static Ui_Theme blend_theme(const Ui_Theme *start, const Ui_Theme *target,
                            float amount)
{
#define BLEND_THEME_COLOR(field)                                               \
    .field = blend_color(start->field, target->field, amount)
    return (Ui_Theme){
        BLEND_THEME_COLOR(background),
        BLEND_THEME_COLOR(surface),
        BLEND_THEME_COLOR(solid_surface),
        BLEND_THEME_COLOR(surface_border),
        BLEND_THEME_COLOR(spectrum),
        BLEND_THEME_COLOR(button),
        BLEND_THEME_COLOR(button_disabled),
        BLEND_THEME_COLOR(button_active),
        BLEND_THEME_COLOR(button_hover),
        BLEND_THEME_COLOR(text_primary),
        BLEND_THEME_COLOR(text_secondary),
        BLEND_THEME_COLOR(text_muted),
        BLEND_THEME_COLOR(text_faint),
        BLEND_THEME_COLOR(playlist_current),
        BLEND_THEME_COLOR(playlist_item),
        BLEND_THEME_COLOR(playlist_hover),
        BLEND_THEME_COLOR(progress_background),
        BLEND_THEME_COLOR(progress_foreground),
        BLEND_THEME_COLOR(progress_hover),
    };
#undef BLEND_THEME_COLOR
}

void ui_theme_transition_init(Ui_Theme_Transition *transition,
                              System_Theme theme)
{
    *transition = (Ui_Theme_Transition){
        .current = *theme_for(theme),
        .target = *theme_for(theme),
    };
}

void ui_theme_transition_begin(Ui_Theme_Transition *transition,
                               System_Theme target)
{
    transition->start = transition->current;
    transition->target = *theme_for(target);
    transition->progress = 0.0f;
    transition->active = true;
}

void ui_theme_transition_update(Ui_Theme_Transition *transition,
                                float delta_time)
{
    if (!transition->active) return;

    transition->progress += delta_time / UI_THEME_TRANSITION_SECONDS;

    if (transition->progress >= 1.0f) {
        transition->current = transition->target;
        transition->progress = 1.0f;
        transition->active = false;
        return;
    }

    float amount = transition->progress * transition->progress *
                   (3.0f - 2.0f * transition->progress);
    transition->current =
        blend_theme(&transition->start, &transition->target, amount);
}

const Ui_Theme *
ui_theme_transition_current(const Ui_Theme_Transition *transition)
{
    return &transition->current;
}
