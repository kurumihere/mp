#include "ui_icons.h"

#include <limits.h>
#include <stddef.h>

#include "assets.h"
#include "log.h"
#include "svg.h"
#include "ui_theme.h"

#define UI_BUTTON_ICON_SIZE 72
#define UI_TOGGLE_ICON_SIZE 48

static void unload_icon_set(Ui_Icon_Set *icons)
{
    for (int i = 0; i < UI_ICON_COUNT; ++i) {
        if (IsTextureValid(icons->textures[i])) {
            UnloadTexture(icons->textures[i]);
        }
    }

    *icons = (Ui_Icon_Set){0};
}

static void draw_texture_icon(Texture2D texture, Rectangle bounds,
                              float rotation, Color tint)
{
    float size = bounds.width;
    Rectangle source = {0.0f, 0.0f, (float)texture.width,
                        (float)texture.height};
    Rectangle destination = {bounds.x + bounds.width / 2.0f,
                             bounds.y + bounds.height / 2.0f, size, size};
    Vector2 origin = {size / 2.0f, size / 2.0f};

    DrawTexturePro(texture, source, destination, origin, rotation, tint);
}

bool ui_application_icon_load(Texture2D *texture)
{
    Embedded_Asset asset = asset_get(ASSET_ICON_PNG);

    if (asset.data == NULL || asset.size > INT_MAX) {
        mp_log(ERROR, "invalid embedded asset: %s", asset.name);
        return false;
    }

    Image icon = LoadImageFromMemory(".png", asset.data, (int)asset.size);

    if (!IsImageValid(icon)) {
        mp_log(ERROR, "failed to load application icon: %s", asset.name);
        return false;
    }

    ImageFormat(&icon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    if (!IsImageValid(icon) ||
        icon.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        mp_log(ERROR, "failed to convert application icon: %s", asset.name);
        UnloadImage(icon);
        return false;
    }

    SetWindowIcon(icon);
    *texture = LoadTextureFromImage(icon);
    UnloadImage(icon);

    if (!IsTextureValid(*texture)) {
        mp_log(ERROR, "failed to create application icon texture");
        return false;
    }

    GenTextureMipmaps(texture);
    SetTextureFilter(*texture, TEXTURE_FILTER_TRILINEAR);
    return true;
}

void ui_application_icon_draw(Texture2D texture, Rectangle bounds)
{
    draw_texture_icon(texture, bounds, 0.0f, WHITE);
}

static Texture2D load_asset_texture(Asset_Id id, int size, float content_scale)
{
    Embedded_Asset asset = asset_get(id);

    if (asset.data == NULL) {
        mp_log(ERROR, "invalid embedded asset: %s", asset.name);
        return (Texture2D){0};
    }

    return svg_load_texture(asset.name, asset.data, asset.size, size,
                            content_scale);
}

static bool create_icon_set(Ui_Icon_Set *icons, System_Theme theme)
{
    bool dark = theme == SYSTEM_THEME_DARK;
    Asset_Id back = dark ? ASSET_BACK_WHITE_SVG : ASSET_BACK_BLACK_SVG;
    Asset_Id forward = dark ? ASSET_FORWARD_WHITE_SVG : ASSET_FORWARD_BLACK_SVG;
    Asset_Id pause = dark ? ASSET_PAUSE_WHITE_SVG : ASSET_PAUSE_BLACK_SVG;
    Asset_Id play = dark ? ASSET_PLAY_WHITE_SVG : ASSET_PLAY_BLACK_SVG;
    Asset_Id repeat = dark ? ASSET_REPEAT_WHITE_SVG : ASSET_REPEAT_BLACK_SVG;
    Asset_Id repeat_one =
        dark ? ASSET_REPEAT_ONE_WHITE_SVG : ASSET_REPEAT_ONE_BLACK_SVG;
    Asset_Id shuffle = dark ? ASSET_SHUFFLE_WHITE_SVG : ASSET_SHUFFLE_BLACK_SVG;
    Asset_Id playlist =
        dark ? ASSET_PLAYLIST_WHITE_SVG : ASSET_PLAYLIST_BLACK_SVG;
    Asset_Id settings =
        dark ? ASSET_SETTINGS_WHITE_SVG : ASSET_SETTINGS_BLACK_SVG;

    *icons = (Ui_Icon_Set){
        .textures =
            {
                [UI_ICON_PREVIOUS] =
                    load_asset_texture(back, UI_BUTTON_ICON_SIZE, 0.74f),
                [UI_ICON_PLAY] =
                    load_asset_texture(play, UI_BUTTON_ICON_SIZE, 0.72f),
                [UI_ICON_PAUSE] =
                    load_asset_texture(pause, UI_BUTTON_ICON_SIZE, 0.72f),
                [UI_ICON_NEXT] =
                    load_asset_texture(forward, UI_BUTTON_ICON_SIZE, 0.74f),
                [UI_ICON_REPEAT] =
                    load_asset_texture(repeat, UI_BUTTON_ICON_SIZE, 0.68f),
                [UI_ICON_REPEAT_ONE] =
                    load_asset_texture(repeat_one, UI_BUTTON_ICON_SIZE, 0.68f),
                [UI_ICON_SHUFFLE] =
                    load_asset_texture(shuffle, UI_BUTTON_ICON_SIZE, 0.68f),
                [UI_ICON_PLAYLIST] =
                    load_asset_texture(playlist, UI_BUTTON_ICON_SIZE, 0.72f),
                [UI_ICON_SETTINGS] =
                    load_asset_texture(settings, UI_BUTTON_ICON_SIZE, 0.68f),
                [UI_ICON_PLAYLIST_BACK] =
                    load_asset_texture(back, UI_TOGGLE_ICON_SIZE, 0.88f),
                [UI_ICON_PLAYLIST_FORWARD] =
                    load_asset_texture(forward, UI_TOGGLE_ICON_SIZE, 0.88f),
            },
    };

    for (int i = 0; i < UI_ICON_COUNT; ++i) {
        if (!IsTextureValid(icons->textures[i])) {
            unload_icon_set(icons);
            return false;
        }
    }

    return true;
}

bool ui_icon_transition_init(Ui_Icon_Transition *transition, System_Theme theme)
{
    *transition = (Ui_Icon_Transition){
        .current_theme = theme,
        .target_theme = theme,
    };
    return create_icon_set(&transition->current, theme);
}

bool ui_icon_transition_begin(Ui_Icon_Transition *transition,
                              System_Theme target)
{
    if (transition->active) {
        if (target == transition->target_theme) return true;

        if (target == transition->current_theme) {
            Ui_Icon_Set icons = transition->current;
            transition->current = transition->next;
            transition->next = icons;

            System_Theme theme = transition->current_theme;
            transition->current_theme = transition->target_theme;
            transition->target_theme = theme;
            transition->progress = 1.0f - transition->progress;
            return true;
        }
    }

    if (target == transition->current_theme) return true;

    Ui_Icon_Set next = {0};

    if (!create_icon_set(&next, target)) return false;

    transition->next = next;
    transition->target_theme = target;
    transition->progress = 0.0f;
    transition->active = true;
    return true;
}

void ui_icon_transition_update(Ui_Icon_Transition *transition, float delta_time)
{
    if (!transition->active) return;

    transition->progress += delta_time / UI_THEME_TRANSITION_SECONDS;

    if (transition->progress < 1.0f) return;

    unload_icon_set(&transition->current);
    transition->current = transition->next;
    transition->next = (Ui_Icon_Set){0};
    transition->current_theme = transition->target_theme;
    transition->progress = 0.0f;
    transition->active = false;
}

static float transition_amount(const Ui_Icon_Transition *transition)
{
    if (!transition->active) return 0.0f;

    float progress = transition->progress;
    return progress * progress * (3.0f - 2.0f * progress);
}

void ui_icon_transition_draw(const Ui_Icon_Transition *transition, Ui_Icon icon,
                             Rectangle bounds, float opacity)
{
    ui_icon_transition_draw_rotated(transition, icon, bounds, 0.0f, opacity);
}

void ui_icon_transition_draw_rotated(const Ui_Icon_Transition *transition,
                                     Ui_Icon icon, Rectangle bounds,
                                     float rotation, float opacity)
{
    if (icon < 0 || icon >= UI_ICON_COUNT) return;

    Texture2D current = transition->current.textures[icon];

    if (!transition->active) {
        draw_texture_icon(current, bounds, rotation, Fade(WHITE, opacity));
        return;
    }

    float amount = transition_amount(transition);
    Texture2D next = transition->next.textures[icon];
    draw_texture_icon(current, bounds, rotation,
                      Fade(WHITE, opacity * (1.0f - amount)));
    draw_texture_icon(next, bounds, rotation, Fade(WHITE, opacity * amount));
}

void ui_icon_transition_uninit(Ui_Icon_Transition *transition)
{
    unload_icon_set(&transition->current);
    unload_icon_set(&transition->next);
    *transition = (Ui_Icon_Transition){0};
}
