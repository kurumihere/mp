#include "ui_theme.h"

static const Ui_Theme ui_themes[] = {
    [SYSTEM_THEME_LIGHT] =
        {
            .background = {250, 250, 250, 255},
            .surface = {240, 240, 240, 255},
            .surface_border = {205, 205, 205, 255},
            .spectrum = {50, 50, 50, 255},
            .button = {232, 232, 232, 255},
            .button_disabled = {245, 245, 245, 255},
            .button_active = {210, 210, 210, 255},
            .button_hover = {200, 200, 200, 255},
            .text_primary = {24, 24, 24, 255},
            .text_secondary = {65, 65, 65, 255},
            .text_muted = {105, 105, 105, 255},
            .text_faint = {155, 155, 155, 255},
            .playlist_current = {218, 218, 218, 255},
            .playlist_item = {244, 244, 244, 255},
            .playlist_hover = {230, 230, 230, 255},
            .progress_background = {205, 205, 205, 255},
            .progress_foreground = {45, 45, 45, 255},
            .progress_hover = {0, 0, 0, 255},
        },
    [SYSTEM_THEME_DARK] =
        {
            .background = {18, 18, 18, 255},
            .surface = {24, 24, 24, 255},
            .surface_border = {55, 55, 55, 255},
            .spectrum = {210, 210, 210, 255},
            .button = {36, 36, 36, 255},
            .button_disabled = {24, 24, 24, 255},
            .button_active = {52, 52, 52, 255},
            .button_hover = {68, 68, 68, 255},
            .text_primary = {245, 245, 245, 255},
            .text_secondary = {200, 200, 200, 255},
            .text_muted = {130, 130, 130, 255},
            .text_faint = {80, 80, 80, 255},
            .playlist_current = {58, 58, 58, 255},
            .playlist_item = {31, 31, 31, 255},
            .playlist_hover = {44, 44, 44, 255},
            .progress_background = {55, 55, 55, 255},
            .progress_foreground = {230, 230, 230, 255},
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
