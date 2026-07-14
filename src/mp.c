#include <stdio.h>

#include "log.h"
#include "metadata.h"
#include "player.h"
#include "playlist.h"
#include "raylib.h"
#include "svg.h"

static bool play_track(Player *player, Playlist *playlist, size_t index,
                       Track_Metadata *metadata)
{
    const char *path = playlist_get(playlist, index);

    if (path == NULL || !player_load(player, path)) return false;

    playlist_select(playlist, index);
    const Track_Metadata *selected_metadata =
        playlist_get_metadata(playlist, index);

    if (selected_metadata != NULL) {
        *metadata = *selected_metadata;
    } else {
        metadata_load(path, metadata);
    }

    return true;
}

static bool play_next_track(Player *player, Playlist *playlist,
                            Track_Metadata *metadata)
{
    size_t count = playlist_get_count(playlist);
    size_t index = playlist_get_current(playlist) + 1;

    while (index < count) {
        if (play_track(player, playlist, index, metadata)) {
            return true;
        }

        ++index;
    }

    return false;
}

static bool play_previous_track(Player *player, Playlist *playlist,
                                Track_Metadata *metadata)
{
    size_t index = playlist_get_current(playlist);

    while (index > 0) {
        --index;

        if (play_track(player, playlist, index, metadata)) {
            return true;
        }
    }

    return false;
}

static bool play_random_track(Player *player, Playlist *playlist,
                              Track_Metadata *metadata)
{
    size_t count = playlist_get_count(playlist);

    if (count < 2) return false;

    size_t current = playlist_get_current(playlist);
    size_t offset = (size_t)GetRandomValue(1, (int)count - 1);
    size_t first = (current + offset) % count;
    size_t index = first;

    do {
        if (index != current && play_track(player, playlist, index, metadata)) {
            return true;
        }

        index = (index + 1) % count;
    } while (index != first);

    return false;
}

static bool button_pressed(Rectangle bounds, Vector2 mouse, bool enabled)
{
    return enabled && CheckCollisionPointRec(mouse, bounds) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

typedef enum {
    BUTTON_PREVIOUS,
    BUTTON_PLAY,
    BUTTON_PAUSE,
    BUTTON_NEXT,
    BUTTON_REPEAT,
    BUTTON_REPEAT_ONE,
    BUTTON_SHUFFLE,
} Button_Icon;

typedef enum {
    REPEAT_OFF,
    REPEAT_ALL,
    REPEAT_ONE,
} Repeat_Mode;

typedef struct {
    Texture2D back;
    Texture2D forward;
    Texture2D pause;
    Texture2D play;
    Texture2D repeat;
    Texture2D repeat_one;
    Texture2D shuffle;
    Texture2D playlist_back;
    Texture2D playlist_forward;
    int button_size;
    int toggle_size;
} Ui_Icons;

typedef struct {
    float scale;
    int width;
    int height;
    int title_size;
    int status_size;
    float title_x;
    float title_y;
    float details_y;
    float metadata_y;
    float playlist_top;
    float playlist_item_height;
    float playlist_item_gap;
    int visible_playlist_items;
    Rectangle previous_button;
    Rectangle play_button;
    Rectangle next_button;
    Rectangle repeat_button;
    Rectangle shuffle_button;
    Rectangle progress_bar;
    Rectangle progress_hitbox;
    Rectangle playlist_panel;
    Rectangle playlist_toggle;
} Ui_Layout;

static void unload_ui_icons(Ui_Icons *icons)
{
    Texture2D *textures[] = {
        &icons->back,    &icons->forward,       &icons->pause,
        &icons->play,    &icons->repeat,        &icons->repeat_one,
        &icons->shuffle, &icons->playlist_back, &icons->playlist_forward,
    };

    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        if (IsTextureValid(*textures[i])) UnloadTexture(*textures[i]);
    }

    *icons = (Ui_Icons){0};
}

static bool create_ui_icons(Ui_Icons *icons, int button_size, int toggle_size)
{
    *icons = (Ui_Icons){
        .back = svg_load_texture("assets/back.svg", button_size, 0.74f),
        .forward = svg_load_texture("assets/forward.svg", button_size, 0.74f),
        .pause = svg_load_texture("assets/pause.svg", button_size, 0.72f),
        .play = svg_load_texture("assets/play.svg", button_size, 0.72f),
        .repeat = svg_load_texture("assets/repeat.svg", button_size, 0.68f),
        .repeat_one =
            svg_load_texture("assets/repeat-one.svg", button_size, 0.68f),
        .shuffle = svg_load_texture("assets/shuffle.svg", button_size, 0.68f),
        .playlist_back =
            svg_load_texture("assets/back.svg", toggle_size, 0.88f),
        .playlist_forward =
            svg_load_texture("assets/forward.svg", toggle_size, 0.88f),
        .button_size = button_size,
        .toggle_size = toggle_size,
    };

    Texture2D textures[] = {
        icons->back,    icons->forward,       icons->pause,
        icons->play,    icons->repeat,        icons->repeat_one,
        icons->shuffle, icons->playlist_back, icons->playlist_forward,
    };

    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        if (!IsTextureValid(textures[i])) {
            unload_ui_icons(icons);
            return false;
        }
    }

    return true;
}

static bool update_ui_icons(Ui_Icons *icons, int button_size, int toggle_size)
{
    if (icons->button_size == button_size &&
        icons->toggle_size == toggle_size) {
        return true;
    }

    Ui_Icons replacement;

    if (!create_ui_icons(&replacement, button_size, toggle_size)) return false;

    unload_ui_icons(icons);
    *icons = replacement;
    return true;
}

static float clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;

    return value;
}

static float snap_pixel(float value)
{
    return (float)((int)(value + 0.5f));
}

static Rectangle snap_rectangle(Rectangle rectangle)
{
    rectangle.x = snap_pixel(rectangle.x);
    rectangle.y = snap_pixel(rectangle.y);
    rectangle.width = snap_pixel(rectangle.width);
    rectangle.height = snap_pixel(rectangle.height);

    return rectangle;
}

static int crisp_font_size(float desired, int minimum, int maximum)
{
    int size = ((int)desired + 5) / 10 * 10;

    if (size < minimum) size = minimum;
    if (size > maximum) size = maximum;

    return size;
}

static Ui_Layout make_ui_layout(int width, int height, bool playlist_open)
{
    float horizontal_scale = (float)width / 1000.0f;
    float vertical_scale = (float)height / 700.0f;
    float scale =
        horizontal_scale < vertical_scale ? horizontal_scale : vertical_scale;

    scale = clamp_float(scale, 0.65f, 2.0f);

    float button_size = snap_pixel(clamp_float(48.0f * scale, 32.0f, 72.0f));
    float progress_height = snap_pixel(clamp_float(6.0f * scale, 4.0f, 10.0f));
    float padding = snap_pixel(clamp_float(12.0f * scale, 8.0f, 24.0f));
    float gap = snap_pixel(clamp_float(8.0f * scale, 6.0f, 16.0f));
    float controls_y = (float)height - padding - button_size;
    float panel_width = snap_pixel(clamp_float(320.0f * scale, 220.0f, 460.0f));
    float panel_max_height = (float)height - padding * 2.0f;
    float panel_height =
        snap_pixel(clamp_float(420.0f * scale, 240.0f, panel_max_height));
    float toggle_width = snap_pixel(clamp_float(32.0f * scale, 24.0f, 48.0f));
    float toggle_height = snap_pixel(clamp_float(72.0f * scale, 52.0f, 108.0f));
    float shuffle_x = (float)width - padding - button_size;
    float repeat_x = shuffle_x - gap - button_size;
    float next_x = repeat_x - gap - button_size;
    float play_x = next_x - gap - button_size;
    float previous_x = play_x - gap - button_size;
    float panel_x = (float)width - panel_width;
    float panel_y = snap_pixel(((float)height - panel_height) / 2.0f);

    if (panel_width + toggle_width + gap + padding > (float)width) {
        panel_width = (float)width - toggle_width - gap - padding;
        panel_x = (float)width - panel_width;
    }

    Ui_Layout layout = {
        .scale = scale,
        .width = width,
        .height = height,
        .title_size = crisp_font_size(32.0f * scale, 20, 50),
        .status_size = crisp_font_size(18.0f * scale, 10, 30),
        .title_x = padding,
        .title_y = snap_pixel(controls_y - 128.0f * scale),
        .details_y = snap_pixel(controls_y - 82.0f * scale),
        .metadata_y = snap_pixel(controls_y - 37.0f * scale),
        .playlist_top = snap_pixel(panel_y + 64.0f * scale),
        .playlist_item_height = snap_pixel(62.0f * scale),
        .playlist_item_gap = snap_pixel(4.0f * scale),
        .previous_button = {previous_x, controls_y, button_size, button_size},
        .play_button = {play_x, controls_y, button_size, button_size},
        .next_button = {next_x, controls_y, button_size, button_size},
        .repeat_button = {repeat_x, controls_y, button_size, button_size},
        .shuffle_button = {shuffle_x, controls_y, button_size, button_size},
        .progress_bar =
            {
                padding,
                controls_y + (button_size - progress_height) / 2.0f,
                previous_x - gap - padding,
                progress_height,
            },
        .playlist_panel = {panel_x, panel_y, panel_width, panel_height},
        .playlist_toggle =
            {
                playlist_open ? panel_x - gap - toggle_width
                              : (float)width - toggle_width,
                ((float)height - toggle_height) / 2.0f,
                toggle_width,
                toggle_height,
            },
    };

    layout.previous_button = snap_rectangle(layout.previous_button);
    layout.play_button = snap_rectangle(layout.play_button);
    layout.next_button = snap_rectangle(layout.next_button);
    layout.repeat_button = snap_rectangle(layout.repeat_button);
    layout.shuffle_button = snap_rectangle(layout.shuffle_button);
    layout.progress_bar = snap_rectangle(layout.progress_bar);
    layout.playlist_panel = snap_rectangle(layout.playlist_panel);
    layout.playlist_toggle = snap_rectangle(layout.playlist_toggle);

    layout.progress_hitbox = snap_rectangle((Rectangle){
        layout.progress_bar.x,
        layout.progress_bar.y - 9.0f * scale,
        layout.progress_bar.width,
        24.0f * scale,
    });

    float playlist_space = layout.playlist_panel.y +
                           layout.playlist_panel.height - layout.playlist_top -
                           12.0f * scale;
    float playlist_step =
        layout.playlist_item_height + layout.playlist_item_gap;
    layout.visible_playlist_items = (int)(playlist_space / playlist_step);

    if (layout.visible_playlist_items < 1) {
        layout.visible_playlist_items = 1;
    }

    return layout;
}

static Rectangle playlist_item_bounds(const Ui_Layout *layout,
                                      int visible_index)
{
    return snap_rectangle((Rectangle){
        layout->playlist_panel.x + 12.0f * layout->scale,
        layout->playlist_top +
            (float)visible_index *
                (layout->playlist_item_height + layout->playlist_item_gap),
        layout->playlist_panel.width - 24.0f * layout->scale,
        layout->playlist_item_height,
    });
}

static void draw_texture_icon(Texture2D texture, Rectangle bounds, Color tint)
{
    float size = bounds.width;
    Rectangle source = {0.0f, 0.0f, (float)texture.width,
                        (float)texture.height};
    Rectangle destination = {bounds.x + bounds.width / 2.0f,
                             bounds.y + bounds.height / 2.0f, size, size};
    Vector2 origin = {size / 2.0f, size / 2.0f};

    DrawTexturePro(texture, source, destination, origin, 0.0f, tint);
}

static void draw_button(Rectangle bounds, Button_Icon icon,
                        const Ui_Icons *icons, Vector2 mouse, bool enabled,
                        bool active)
{
    bool hovered = enabled && CheckCollisionPointRec(mouse, bounds);
    Color fill = enabled ? (Color){36, 36, 36, 255} : (Color){24, 24, 24, 255};
    Color icon_color = enabled ? RAYWHITE : DARKGRAY;
    Texture2D texture = {0};

    if (active && enabled) fill = (Color){52, 52, 52, 255};
    if (hovered) fill = (Color){68, 68, 68, 255};

    DrawRectangleRec(bounds, fill);

    switch (icon) {
    case BUTTON_PREVIOUS:
        texture = icons->back;
        break;
    case BUTTON_PLAY:
        texture = icons->play;
        break;
    case BUTTON_PAUSE:
        texture = icons->pause;
        break;
    case BUTTON_NEXT:
        texture = icons->forward;
        break;
    case BUTTON_REPEAT:
        texture = icons->repeat;
        break;
    case BUTTON_REPEAT_ONE:
        texture = icons->repeat_one;
        break;
    case BUTTON_SHUFFLE:
        texture = icons->shuffle;
        break;
    }

    draw_texture_icon(texture, bounds, icon_color);
}

static void draw_playlist_panel(const Playlist *playlist,
                                const Ui_Layout *layout, Vector2 mouse,
                                int scroll)
{
    Rectangle panel = layout->playlist_panel;
    int title_size = crisp_font_size(25.0f * layout->scale, 20, 40);
    int details_size = crisp_font_size(16.0f * layout->scale, 10, 30);
    size_t count = playlist_get_count(playlist);
    size_t current = playlist_get_current(playlist);

    DrawRectangleRec(panel, (Color){24, 24, 24, 255});
    DrawLine((int)panel.x, (int)panel.y, (int)panel.x,
             (int)(panel.y + panel.height), (Color){55, 55, 55, 255});
    DrawText("Playlist", (int)snap_pixel(panel.x + 20.0f * layout->scale),
             (int)snap_pixel(panel.y + 20.0f * layout->scale),
             crisp_font_size(26.0f * layout->scale, 20, 40), RAYWHITE);

    if (count == 0) {
        DrawText("No tracks", (int)snap_pixel(panel.x + 20.0f * layout->scale),
                 (int)snap_pixel(panel.y + 70.0f * layout->scale),
                 crisp_font_size(20.0f * layout->scale, 10, 30), GRAY);
        return;
    }

    for (int visible_index = 0; visible_index < layout->visible_playlist_items;
         ++visible_index) {
        size_t index = (size_t)scroll + (size_t)visible_index;

        if (index >= count) break;

        Rectangle item = playlist_item_bounds(layout, visible_index);

        bool hovered = CheckCollisionPointRec(mouse, item);
        Color fill = index == current ? (Color){58, 58, 58, 255}
                                      : (Color){31, 31, 31, 255};

        if (hovered && index != current) fill = (Color){44, 44, 44, 255};

        DrawRectangleRec(item, fill);

        const Track_Metadata *metadata = playlist_get_metadata(playlist, index);

        if (metadata == NULL) continue;

        char title[METADATA_TEXT_SIZE + 32];
        snprintf(title, sizeof(title), "%zu. %s", index + 1, metadata->title);

        BeginScissorMode((int)item.x + 10, (int)item.y, (int)item.width - 20,
                         (int)item.height);
        int text_x = (int)item.x + 10;

        bool has_artist = metadata->artist[0] != '\0';
        bool has_album = metadata->album[0] != '\0';

        if (!has_artist && !has_album) {
            DrawText(title, text_x,
                     (int)(item.y + (item.height - title_size) / 2.0f),
                     title_size, index == current ? RAYWHITE : LIGHTGRAY);
        } else {
            DrawText(title, text_x,
                     (int)snap_pixel(item.y + 5.0f * layout->scale), title_size,
                     index == current ? RAYWHITE : LIGHTGRAY);

            int details_y = (int)snap_pixel(item.y + 39.0f * layout->scale);
            Color details_color = index == current ? LIGHTGRAY : GRAY;

            if (has_artist) {
                DrawText(metadata->artist, text_x, details_y, details_size,
                         details_color);
                text_x += MeasureText(metadata->artist, details_size);
            }

            if (has_artist && has_album) {
                int separator_gap = (int)snap_pixel(5.0f * layout->scale);
                text_x += separator_gap;
                DrawText("|", text_x, details_y, details_size, DARKGRAY);
                text_x += MeasureText("|", details_size) + separator_gap;
            }

            if (has_album) {
                DrawText(metadata->album, text_x, details_y, details_size,
                         details_color);
            }
        }

        EndScissorMode();
    }
}

static void draw_playlist_toggle(Rectangle bounds, bool open,
                                 const Ui_Icons *icons, Vector2 mouse)
{
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    Color fill = hovered ? (Color){58, 58, 58, 255} : (Color){36, 36, 36, 255};

    DrawRectangleRec(bounds, fill);
    draw_texture_icon(open ? icons->playlist_back : icons->playlist_forward,
                      bounds, RAYWHITE);
}

int main(int argc, char **argv)
{
    Player player;

    if (!player_init(&player)) return 1;

    Playlist playlist;
    playlist_init(&playlist);

    const int w_width = 1000;
    const int w_height = 700;

    const char *window_title = "Music Player";
    Track_Metadata metadata = {0};

    if (argc > 1) {
        bool loaded = false;

        if (playlist_replace(&playlist, (const char *const *)&argv[1],
                             (size_t)(argc - 1))) {
            for (size_t i = 0; i < playlist_get_count(&playlist); ++i) {
                if (play_track(&player, &playlist, i, &metadata)) {
                    loaded = true;
                    break;
                }
            }
        }

        if (!loaded) {
            playlist_uninit(&playlist);
        }
    }

    const Color background = {18, 18, 18, 255};
    const Color progress_background = {55, 55, 55, 255};
    const Color progress_foreground = {230, 230, 230, 255};
    const Color progress_hover = {255, 255, 255, 255};

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(w_width, w_height, window_title);

    Ui_Icons icons = {0};

    SetWindowMinSize(480, 320);
    SetTextureFilter(GetFontDefault().texture, TEXTURE_FILTER_POINT);
    SetTargetFPS(60);

    int exit_code = 0;
    int playlist_scroll = 0;
    bool playlist_open = false;
    bool finished_handled = false;
    bool seek_dragging = false;
    Repeat_Mode repeat_mode = REPEAT_OFF;
    bool shuffle_enabled = false;

    while (!WindowShouldClose()) {
        if (IsFileDropped()) {
            FilePathList dropped_files = LoadDroppedFiles();

            if (dropped_files.count > 0) {
                size_t first_new_track = playlist_get_count(&playlist);

                if (playlist_append(&playlist,
                                    (const char *const *)dropped_files.paths,
                                    dropped_files.count)) {
                    if (player_get_state(&player) == PLAYER_STOPPED) {
                        for (size_t i = first_new_track;
                             i < playlist_get_count(&playlist); ++i) {
                            if (play_track(&player, &playlist, i, &metadata)) {
                                finished_handled = false;
                                break;
                            }
                        }
                    }

                    mp_log(INFO, "Added %u tracks to playlist",
                           dropped_files.count);
                }
            }

            UnloadDroppedFiles(dropped_files);
        }

        Player_State state = player_get_state(&player);

        if (state == PLAYER_FINISHED) {
            if (!finished_handled) {
                finished_handled = true;
                bool continued = false;

                if (repeat_mode == REPEAT_ONE) {
                    continued =
                        play_track(&player, &playlist,
                                   playlist_get_current(&playlist), &metadata);
                } else if (shuffle_enabled) {
                    continued =
                        play_random_track(&player, &playlist, &metadata);
                } else {
                    continued = play_next_track(&player, &playlist, &metadata);
                }

                if (!continued && repeat_mode == REPEAT_ALL &&
                    playlist_get_count(&playlist) > 0) {
                    continued = play_track(&player, &playlist, 0, &metadata);
                }

                if (continued) {
                    finished_handled = false;
                }
            }
        } else {
            finished_handled = false;
        }

        state = player_get_state(&player);
        Vector2 mouse = GetMousePosition();
        float mouse_wheel = GetMouseWheelMove();
        Ui_Layout layout =
            make_ui_layout(GetScreenWidth(), GetScreenHeight(), playlist_open);

        bool playlist_toggled =
            button_pressed(layout.playlist_toggle, mouse, true);

        if (playlist_toggled) {
            playlist_open = !playlist_open;
            layout = make_ui_layout(GetScreenWidth(), GetScreenHeight(),
                                    playlist_open);
        }

        int button_icon_size = (int)(layout.play_button.width + 0.5f);
        int toggle_icon_size = (int)(layout.playlist_toggle.width + 0.5f);

        if (!update_ui_icons(&icons, button_icon_size, toggle_icon_size)) {
            exit_code = 1;
            break;
        }

        size_t track_count = playlist_get_count(&playlist);
        int max_playlist_scroll =
            track_count > (size_t)layout.visible_playlist_items
                ? (int)track_count - layout.visible_playlist_items
                : 0;

        if (playlist_scroll > max_playlist_scroll) {
            playlist_scroll = max_playlist_scroll;
        }

        bool mouse_over_playlist =
            playlist_open &&
            CheckCollisionPointRec(mouse, layout.playlist_panel);

        if (mouse_over_playlist) {
            if (mouse_wheel > 0.0f) --playlist_scroll;
            if (mouse_wheel < 0.0f) ++playlist_scroll;

            if (playlist_scroll < 0) playlist_scroll = 0;
            if (playlist_scroll > max_playlist_scroll) {
                playlist_scroll = max_playlist_scroll;
            }
        }

        if (mouse_over_playlist && !playlist_toggled) {
            for (int visible_index = 0;
                 visible_index < layout.visible_playlist_items;
                 ++visible_index) {
                size_t index = (size_t)playlist_scroll + (size_t)visible_index;

                if (index >= track_count) break;

                Rectangle item = playlist_item_bounds(&layout, visible_index);

                if (button_pressed(item, mouse, true)) {
                    if (play_track(&player, &playlist, index, &metadata)) {
                        finished_handled = false;
                    }

                    break;
                }
            }
        }

        bool control_down =
            IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

        if (control_down && IsKeyPressed(KEY_DELETE)) {
            player_clear(&player);
            playlist_clear(&playlist);
            playlist_scroll = 0;
            finished_handled = false;
            seek_dragging = false;
            metadata = (Track_Metadata){0};
            mp_log(INFO, "Playlist cleared");
        } else if (IsKeyPressed(KEY_DELETE) &&
                   playlist_get_count(&playlist) > 0) {
            size_t removed_index = playlist_get_current(&playlist);
            player_clear(&player);
            playlist_remove(&playlist, removed_index);
            finished_handled = false;
            seek_dragging = false;

            size_t remaining = playlist_get_count(&playlist);
            bool loaded = false;

            for (size_t i = 0; i < remaining; ++i) {
                size_t index = (removed_index + i) % remaining;

                if (play_track(&player, &playlist, index, &metadata)) {
                    loaded = true;
                    break;
                }
            }

            if (!loaded) {
                metadata = (Track_Metadata){0};
            }

            mp_log(INFO, "Removed track from playlist");
        }

        bool sidebar_blocks_mouse =
            mouse_over_playlist ||
            CheckCollisionPointRec(mouse, layout.playlist_toggle);
        state = player_get_state(&player);
        size_t track_index = playlist_get_current(&playlist);
        bool has_track = state != PLAYER_STOPPED;
        bool controls_enabled = has_track && !sidebar_blocks_mouse;
        bool repeat_pressed =
            button_pressed(layout.repeat_button, mouse, controls_enabled);
        bool shuffle_pressed =
            button_pressed(layout.shuffle_button, mouse, controls_enabled);

        if (repeat_pressed) {
            switch (repeat_mode) {
            case REPEAT_OFF:
                repeat_mode = REPEAT_ALL;
                mp_log(INFO, "Repeat mode: All");
                break;
            case REPEAT_ALL:
                repeat_mode = REPEAT_ONE;
                mp_log(INFO, "Repeat mode: One");
                break;
            case REPEAT_ONE:
                repeat_mode = REPEAT_OFF;
                mp_log(INFO, "Repeat mode: Off");
                break;
            }
        }

        if (shuffle_pressed) {
            shuffle_enabled = !shuffle_enabled;
            mp_log(INFO, "Shuffle: %s", shuffle_enabled ? "On" : "Off");
        }

        bool can_wrap = repeat_mode == REPEAT_ALL && track_count > 1;
        bool can_previous = has_track && (track_index > 0 || can_wrap);
        bool can_next =
            has_track && ((shuffle_enabled && track_count > 1) ||
                          track_index + 1 < track_count || can_wrap);

        if ((IsKeyPressed(KEY_RIGHT) && can_next) ||
            button_pressed(layout.next_button, mouse,
                           can_next && !sidebar_blocks_mouse)) {
            bool moved = shuffle_enabled
                             ? play_random_track(&player, &playlist, &metadata)
                             : play_next_track(&player, &playlist, &metadata);

            if (!moved && repeat_mode == REPEAT_ALL && track_count > 0) {
                play_track(&player, &playlist, 0, &metadata);
            }
        }

        if ((IsKeyPressed(KEY_LEFT) && can_previous) ||
            button_pressed(layout.previous_button, mouse,
                           can_previous && !sidebar_blocks_mouse)) {
            bool moved = play_previous_track(&player, &playlist, &metadata);

            if (!moved && repeat_mode == REPEAT_ALL && track_count > 0) {
                play_track(&player, &playlist, track_count - 1, &metadata);
            }
        }

        bool toggle_requested =
            IsKeyPressed(KEY_SPACE) ||
            button_pressed(layout.play_button, mouse,
                           has_track && !sidebar_blocks_mouse);

        if (toggle_requested && !player_toggle(&player)) {
            exit_code = 1;
            break;
        }

        float volume_change = mouse_over_playlist ? 0.0f : mouse_wheel * 0.05f;

        if (IsKeyPressed(KEY_UP)) volume_change += 0.05f;
        if (IsKeyPressed(KEY_DOWN)) volume_change -= 0.05f;

        if (volume_change != 0.0f) {
            player_adjust_volume(&player, volume_change);
        }

        float volume_padding = snap_pixel(4.0f * layout.scale);
        int volume_slot_width = MeasureText("Volume 100%", layout.status_size);
        Rectangle volume_bounds = snap_rectangle((Rectangle){
            layout.progress_bar.x + layout.progress_bar.width -
                (float)volume_slot_width - volume_padding,
            layout.metadata_y - volume_padding,
            (float)volume_slot_width + volume_padding * 2.0f,
            (float)layout.status_size + volume_padding * 2.0f,
        });

        if (IsKeyPressed(KEY_M) ||
            button_pressed(volume_bounds, mouse,
                           has_track && !sidebar_blocks_mouse)) {
            player_toggle_mute(&player);
        }

        char volume_text[64];

        if (player_is_muted(&player)) {
            snprintf(volume_text, sizeof(volume_text), "Muted");
        } else {
            snprintf(volume_text, sizeof(volume_text), "Volume %d%%",
                     (int)(player_get_volume(&player) * 100.0f + 0.5f));
        }

        int volume_x = (int)(layout.progress_bar.x + layout.progress_bar.width -
                             MeasureText(volume_text, layout.status_size));
        bool volume_hovered = has_track && !sidebar_blocks_mouse &&
                              CheckCollisionPointRec(mouse, volume_bounds);

        float cursor = player_get_cursor(&player);
        float length = player_get_length(&player);
        bool progress_hovered =
            CheckCollisionPointRec(mouse, layout.progress_hitbox);

        if (!sidebar_blocks_mouse && progress_hovered &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            seek_dragging = true;
        }

        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            seek_dragging = false;
        }

        if (seek_dragging && length > 0.0f) {
            float progress =
                (mouse.x - layout.progress_bar.x) / layout.progress_bar.width;

            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;

            if (!player_seek(&player, length * progress)) {
                exit_code = 1;
                break;
            }

            cursor = player_get_cursor(&player);
        }

        state = player_get_state(&player);
        const char *status;

        switch (state) {
        case PLAYER_PLAYING:
            status = "Playing";
            break;
        case PLAYER_PAUSED:
            status = "Paused";
            break;
        case PLAYER_FINISHED:
            status = "Finished";
            break;
        case PLAYER_STOPPED:
            status = "";
            break;
        }

        char time_text[64];

        snprintf(time_text, sizeof(time_text), "%d:%02d | %d:%02d",
                 (int)cursor / 60, (int)cursor % 60, (int)length / 60,
                 (int)length % 60);

        int status_x = (int)layout.progress_bar.x;

        int time_x = (int)(layout.progress_bar.x +
                           (layout.progress_bar.width -
                            MeasureText(time_text, layout.status_size)) /
                               2.0f);

        char playlist_text[64];

        snprintf(playlist_text, sizeof(playlist_text), "%zu | %zu",
                 playlist_get_current(&playlist) + 1,
                 playlist_get_count(&playlist));

        int playlist_x = status_x + MeasureText(status, layout.status_size) +
                         (int)snap_pixel(18.0f * layout.scale);

        char details_text[METADATA_TEXT_SIZE * 2 + 8];

        if (metadata.artist[0] != '\0' && metadata.album[0] != '\0') {
            snprintf(details_text, sizeof(details_text), "%s | %s",
                     metadata.artist, metadata.album);
        } else if (metadata.artist[0] != '\0') {
            snprintf(details_text, sizeof(details_text), "%s", metadata.artist);
        } else {
            snprintf(details_text, sizeof(details_text), "%s", metadata.album);
        }

        float progress = length > 0.0f ? cursor / length : 0.0f;

        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        Rectangle progress_fill = {
            layout.progress_bar.x,
            layout.progress_bar.y,
            snap_pixel(layout.progress_bar.width * progress),
            layout.progress_bar.height,
        };

        Vector2 progress_handle = {
            snap_pixel(progress_fill.x + progress_fill.width),
            snap_pixel(layout.progress_bar.y +
                       layout.progress_bar.height / 2.0f),
        };

        int title_size = layout.title_size;
        int title_width = MeasureText(metadata.title, title_size);
        int title_max_width = layout.width - (int)(layout.title_x * 2.0f);

        if (title_width > title_max_width) {
            title_size = title_size * title_max_width / title_width;
            title_size = title_size / 10 * 10;

            if (title_size < 10) title_size = 10;
        }

        BeginDrawing();
        ClearBackground(background);

        if (state == PLAYER_STOPPED) {
            const char *drop_text = "Drag & Drop Files";
            int drop_text_size = crisp_font_size(30.0f * layout.scale, 20, 50);
            int drop_text_x =
                (layout.width - MeasureText(drop_text, drop_text_size)) / 2;
            int drop_text_y = (layout.height - drop_text_size) / 2;

            DrawText(drop_text, drop_text_x, drop_text_y, drop_text_size,
                     RAYWHITE);
        } else {
            DrawText(metadata.title, (int)layout.title_x, (int)layout.title_y,
                     title_size, RAYWHITE);
            DrawText(details_text, (int)layout.title_x, (int)layout.details_y,
                     layout.status_size, GRAY);
            DrawText(status, status_x, (int)layout.metadata_y,
                     layout.status_size, LIGHTGRAY);
            DrawText(playlist_text, playlist_x, (int)layout.metadata_y,
                     layout.status_size, DARKGRAY);
            DrawText(time_text, time_x, (int)layout.metadata_y,
                     layout.status_size, GRAY);
            DrawText(volume_text, volume_x, (int)layout.metadata_y,
                     layout.status_size, volume_hovered ? LIGHTGRAY : GRAY);

            DrawRectangleRec(layout.progress_bar, progress_background);
            DrawRectangleRec(progress_fill, progress_hovered
                                                ? progress_hover
                                                : progress_foreground);
            float radius_scale = progress_hovered ? 7.0f : 5.0f;
            float handle_radius = snap_pixel(radius_scale * layout.scale);
            Color handle_color =
                progress_hovered ? progress_hover : progress_foreground;
            DrawCircleV(progress_handle, handle_radius, handle_color);

            draw_button(layout.previous_button, BUTTON_PREVIOUS, &icons, mouse,
                        can_previous, false);
            draw_button(layout.play_button,
                        state == PLAYER_PLAYING ? BUTTON_PAUSE : BUTTON_PLAY,
                        &icons, mouse, true, false);
            draw_button(layout.next_button, BUTTON_NEXT, &icons, mouse,
                        can_next, false);
            draw_button(layout.repeat_button,
                        repeat_mode == REPEAT_ONE ? BUTTON_REPEAT_ONE
                                                  : BUTTON_REPEAT,
                        &icons, mouse, true, repeat_mode != REPEAT_OFF);
            draw_button(layout.shuffle_button, BUTTON_SHUFFLE, &icons, mouse,
                        true, shuffle_enabled);
        }

        if (playlist_open) {
            draw_playlist_panel(&playlist, &layout, mouse, playlist_scroll);
        }

        draw_playlist_toggle(layout.playlist_toggle, playlist_open, &icons,
                             mouse);

        EndDrawing();
    }

    unload_ui_icons(&icons);
    CloseWindow();

    playlist_uninit(&playlist);
    player_uninit(&player);

    return exit_code;
}
