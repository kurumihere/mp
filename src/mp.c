#include <stdio.h>

#include "log.h"
#include "player.h"
#include "playlist.h"
#include "raylib.h"

static bool play_track(Player *player, Playlist *playlist, size_t index,
                       char *file_name, size_t file_name_size)
{
    const char *path = playlist_get(playlist, index);

    if (path == NULL || !player_load(player, path)) return false;

    playlist_select(playlist, index);
    snprintf(file_name, file_name_size, "%s", GetFileName(path));

    return true;
}

static bool play_next_track(Player *player, Playlist *playlist, char *file_name,
                            size_t file_name_size)
{
    size_t count = playlist_get_count(playlist);
    size_t index = playlist_get_current(playlist) + 1;

    while (index < count) {
        if (play_track(player, playlist, index, file_name, file_name_size)) {
            return true;
        }

        ++index;
    }

    return false;
}

static bool play_previous_track(Player *player, Playlist *playlist,
                                char *file_name, size_t file_name_size)
{
    size_t index = playlist_get_current(playlist);

    while (index > 0) {
        --index;

        if (play_track(player, playlist, index, file_name, file_name_size)) {
            return true;
        }
    }

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
} Button_Icon;

typedef struct {
    float scale;
    int width;
    int height;
    int title_size;
    int status_size;
    float title_x;
    float title_y;
    float metadata_y;
    float playlist_top;
    float playlist_item_height;
    float playlist_item_gap;
    int visible_playlist_items;
    Rectangle previous_button;
    Rectangle play_button;
    Rectangle next_button;
    Rectangle progress_bar;
    Rectangle progress_hitbox;
    Rectangle playlist_panel;
    Rectangle playlist_toggle;
} Ui_Layout;

static float clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;

    return value;
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

    float button_size = clamp_float(48.0f * scale, 32.0f, 72.0f);
    float progress_height = clamp_float(6.0f * scale, 4.0f, 10.0f);
    float padding = clamp_float(12.0f * scale, 8.0f, 24.0f);
    float gap = clamp_float(8.0f * scale, 6.0f, 16.0f);
    float controls_y = (float)height - padding - button_size;
    float panel_width = clamp_float(320.0f * scale, 220.0f, 460.0f);
    float panel_height =
        clamp_float(420.0f * scale, 240.0f, (float)height - padding * 2.0f);
    float toggle_width = clamp_float(32.0f * scale, 24.0f, 48.0f);
    float toggle_height = clamp_float(72.0f * scale, 52.0f, 108.0f);
    float next_x = (float)width - padding - button_size;
    float play_x = next_x - gap - button_size;
    float previous_x = play_x - gap - button_size;
    float panel_x = (float)width - panel_width;
    float panel_y = ((float)height - panel_height) / 2.0f;

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
        .title_y = controls_y - 100.0f * scale,
        .metadata_y = controls_y - 37.0f * scale,
        .playlist_top = panel_y + 64.0f * scale,
        .playlist_item_height = 42.0f * scale,
        .playlist_item_gap = 4.0f * scale,
        .previous_button = {previous_x, controls_y, button_size, button_size},
        .play_button = {play_x, controls_y, button_size, button_size},
        .next_button = {next_x, controls_y, button_size, button_size},
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

    layout.progress_hitbox = (Rectangle){
        layout.progress_bar.x,
        layout.progress_bar.y - 9.0f * scale,
        layout.progress_bar.width,
        24.0f * scale,
    };

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
    return (Rectangle){
        layout->playlist_panel.x + 12.0f * layout->scale,
        layout->playlist_top +
            (float)visible_index *
                (layout->playlist_item_height + layout->playlist_item_gap),
        layout->playlist_panel.width - 24.0f * layout->scale,
        layout->playlist_item_height,
    };
}

static void draw_button(Rectangle bounds, Button_Icon icon, Vector2 mouse,
                        bool enabled)
{
    bool hovered = enabled && CheckCollisionPointRec(mouse, bounds);
    Color fill = enabled ? (Color){36, 36, 36, 255} : (Color){24, 24, 24, 255};
    Color icon_color = enabled ? RAYWHITE : DARKGRAY;
    float center_x = bounds.x + bounds.width / 2.0f;
    float center_y = bounds.y + bounds.height / 2.0f;
    float icon_scale = bounds.width / 48.0f;
    int thin_width = (int)clamp_float(3.0f * icon_scale, 2.0f, 5.0f);
    int thick_width = (int)clamp_float(5.0f * icon_scale, 3.0f, 8.0f);
    int icon_height = (int)(20.0f * icon_scale);

    if (hovered) fill = (Color){58, 58, 58, 255};

    DrawRectangleRec(bounds, fill);

    switch (icon) {
    case BUTTON_PREVIOUS:
        DrawRectangle((int)(center_x - 10.0f * icon_scale),
                      (int)(center_y - 10.0f * icon_scale), thin_width,
                      icon_height, icon_color);
        DrawTriangle((Vector2){center_x - 5.0f * icon_scale, center_y},
                     (Vector2){center_x + 8.0f * icon_scale,
                               center_y + 10.0f * icon_scale},
                     (Vector2){center_x + 8.0f * icon_scale,
                               center_y - 10.0f * icon_scale},
                     icon_color);
        break;
    case BUTTON_PLAY:
        DrawTriangle((Vector2){center_x - 7.0f * icon_scale,
                               center_y - 10.0f * icon_scale},
                     (Vector2){center_x - 7.0f * icon_scale,
                               center_y + 10.0f * icon_scale},
                     (Vector2){center_x + 9.0f * icon_scale, center_y},
                     icon_color);
        break;
    case BUTTON_PAUSE:
        DrawRectangle((int)(center_x - 8.0f * icon_scale),
                      (int)(center_y - 10.0f * icon_scale), thick_width,
                      icon_height, icon_color);
        DrawRectangle((int)(center_x + 3.0f * icon_scale),
                      (int)(center_y - 10.0f * icon_scale), thick_width,
                      icon_height, icon_color);
        break;
    case BUTTON_NEXT:
        DrawTriangle((Vector2){center_x - 8.0f * icon_scale,
                               center_y - 10.0f * icon_scale},
                     (Vector2){center_x - 8.0f * icon_scale,
                               center_y + 10.0f * icon_scale},
                     (Vector2){center_x + 5.0f * icon_scale, center_y},
                     icon_color);
        DrawRectangle((int)(center_x + 7.0f * icon_scale),
                      (int)(center_y - 10.0f * icon_scale), thin_width,
                      icon_height, icon_color);
        break;
    }
}

static void draw_playlist_panel(const Playlist *playlist,
                                const Ui_Layout *layout, Vector2 mouse,
                                int scroll)
{
    Rectangle panel = layout->playlist_panel;
    int item_text_size = crisp_font_size(18.0f * layout->scale, 10, 30);
    size_t count = playlist_get_count(playlist);
    size_t current = playlist_get_current(playlist);

    DrawRectangleRec(panel, (Color){24, 24, 24, 255});
    DrawLine((int)panel.x, (int)panel.y, (int)panel.x,
             (int)(panel.y + panel.height), (Color){55, 55, 55, 255});
    DrawText("Playlist", (int)(panel.x + 20.0f * layout->scale),
             (int)(panel.y + 20.0f * layout->scale),
             crisp_font_size(26.0f * layout->scale, 20, 40), RAYWHITE);

    if (count == 0) {
        DrawText("No tracks", (int)(panel.x + 20.0f * layout->scale),
                 (int)(panel.y + 70.0f * layout->scale),
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

        char item_text[1024];
        const char *path = playlist_get(playlist, index);

        snprintf(item_text, sizeof(item_text), "%zu. %s", index + 1,
                 GetFileName(path));

        BeginScissorMode((int)item.x + 10, (int)item.y, (int)item.width - 20,
                         (int)item.height);
        DrawText(item_text, (int)item.x + 10,
                 (int)(item.y + (item.height - item_text_size) / 2.0f),
                 item_text_size, index == current ? RAYWHITE : LIGHTGRAY);
        EndScissorMode();
    }
}

static void draw_playlist_toggle(Rectangle bounds, bool open, Vector2 mouse)
{
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    Color fill = hovered ? (Color){58, 58, 58, 255} : (Color){36, 36, 36, 255};
    Color icon_color = RAYWHITE;
    float center_x = bounds.x + bounds.width / 2.0f;
    float center_y = bounds.y + bounds.height / 2.0f;
    float icon_scale = bounds.width / 32.0f;

    DrawRectangleRec(bounds, fill);

    if (open) {
        DrawLineEx((Vector2){center_x - 5.0f * icon_scale,
                             center_y - 10.0f * icon_scale},
                   (Vector2){center_x + 5.0f * icon_scale, center_y},
                   3.0f * icon_scale, icon_color);
        DrawLineEx((Vector2){center_x + 5.0f * icon_scale, center_y},
                   (Vector2){center_x - 5.0f * icon_scale,
                             center_y + 10.0f * icon_scale},
                   3.0f * icon_scale, icon_color);
    } else {
        DrawLineEx((Vector2){center_x + 5.0f * icon_scale,
                             center_y - 10.0f * icon_scale},
                   (Vector2){center_x - 5.0f * icon_scale, center_y},
                   3.0f * icon_scale, icon_color);
        DrawLineEx((Vector2){center_x - 5.0f * icon_scale, center_y},
                   (Vector2){center_x + 5.0f * icon_scale,
                             center_y + 10.0f * icon_scale},
                   3.0f * icon_scale, icon_color);
    }
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
    char file_name[1024];

    snprintf(file_name, sizeof(file_name), "Drop an audio file");

    if (argc > 1) {
        bool loaded = false;

        if (playlist_replace(&playlist, (const char *const *)&argv[1],
                             (size_t)(argc - 1))) {
            for (size_t i = 0; i < playlist_get_count(&playlist); ++i) {
                const char *path = playlist_get(&playlist, i);

                if (player_load(&player, path)) {
                    playlist_select(&playlist, i);
                    snprintf(file_name, sizeof(file_name), "%s",
                             GetFileName(path));
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
    SetWindowMinSize(480, 320);
    SetTextureFilter(GetFontDefault().texture, TEXTURE_FILTER_POINT);
    SetTargetFPS(60);

    int exit_code = 0;
    int playlist_scroll = 0;
    bool playlist_open = false;
    bool finished_handled = false;
    bool seek_dragging = false;

    while (!WindowShouldClose()) {
        if (IsFileDropped()) {
            FilePathList dropped_files = LoadDroppedFiles();

            if (dropped_files.count > 0) {
                Playlist dropped_playlist;
                playlist_init(&dropped_playlist);

                if (playlist_replace(&dropped_playlist,
                                     (const char *const *)dropped_files.paths,
                                     dropped_files.count)) {
                    bool loaded = false;

                    for (size_t i = 0; i < dropped_files.count; ++i) {
                        if (play_track(&player, &dropped_playlist, i, file_name,
                                       sizeof(file_name))) {
                            loaded = true;
                            break;
                        }
                    }

                    if (loaded) {
                        playlist_uninit(&playlist);
                        playlist = dropped_playlist;
                        playlist_scroll = 0;
                        finished_handled = false;
                        mp_log(INFO, "Playlist loaded: %zu tracks",
                               playlist_get_count(&playlist));
                    } else {
                        playlist_uninit(&dropped_playlist);
                    }
                }
            }

            UnloadDroppedFiles(dropped_files);
        }

        Player_State state = player_get_state(&player);

        if (state == PLAYER_FINISHED) {
            if (!finished_handled) {
                finished_handled = true;

                if (play_next_track(&player, &playlist, file_name,
                                    sizeof(file_name))) {
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
                    if (play_track(&player, &playlist, index, file_name,
                                   sizeof(file_name))) {
                        finished_handled = false;
                    }

                    break;
                }
            }
        }

        bool sidebar_blocks_mouse =
            mouse_over_playlist ||
            CheckCollisionPointRec(mouse, layout.playlist_toggle);
        state = player_get_state(&player);
        size_t track_index = playlist_get_current(&playlist);
        bool has_track = state != PLAYER_STOPPED;
        bool can_previous = has_track && track_index > 0;
        bool can_next = has_track && track_index + 1 < track_count;

        if (IsKeyPressed(KEY_RIGHT) ||
            button_pressed(layout.next_button, mouse,
                           can_next && !sidebar_blocks_mouse)) {
            play_next_track(&player, &playlist, file_name, sizeof(file_name));
        }

        if (IsKeyPressed(KEY_LEFT) ||
            button_pressed(layout.previous_button, mouse,
                           can_previous && !sidebar_blocks_mouse)) {
            play_previous_track(&player, &playlist, file_name,
                                sizeof(file_name));
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

        if (IsKeyPressed(KEY_M)) {
            player_toggle_mute(&player);
        }

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

        snprintf(time_text, sizeof(time_text), "%d:%02d / %d:%02d",
                 (int)cursor / 60, (int)cursor % 60, (int)length / 60,
                 (int)length % 60);

        char volume_text[64];

        if (player_is_muted(&player)) {
            snprintf(volume_text, sizeof(volume_text), "Muted");
        } else {
            snprintf(volume_text, sizeof(volume_text), "Volume %d%%",
                     (int)(player_get_volume(&player) * 100.0f + 0.5f));
        }

        int status_x = (int)layout.progress_bar.x;

        int time_x = (int)(layout.progress_bar.x +
                           (layout.progress_bar.width -
                            MeasureText(time_text, layout.status_size)) /
                               2.0f);

        int volume_x = (int)(layout.progress_bar.x + layout.progress_bar.width -
                             MeasureText(volume_text, layout.status_size));

        char playlist_text[64];

        snprintf(playlist_text, sizeof(playlist_text), "%zu / %zu",
                 playlist_get_current(&playlist) + 1,
                 playlist_get_count(&playlist));

        int playlist_x = status_x + MeasureText(status, layout.status_size) +
                         (int)(18.0f * layout.scale);

        float progress = length > 0.0f ? cursor / length : 0.0f;

        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        Rectangle progress_fill = {
            layout.progress_bar.x,
            layout.progress_bar.y,
            layout.progress_bar.width * progress,
            layout.progress_bar.height,
        };

        Vector2 progress_handle = {
            progress_fill.x + progress_fill.width,
            layout.progress_bar.y + layout.progress_bar.height / 2.0f,
        };

        int title_size = layout.title_size;
        int title_width = MeasureText(file_name, title_size);
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
            DrawText(file_name, (int)layout.title_x, (int)layout.title_y,
                     title_size, RAYWHITE);
            DrawText(status, status_x, (int)layout.metadata_y,
                     layout.status_size, LIGHTGRAY);
            DrawText(playlist_text, playlist_x, (int)layout.metadata_y,
                     layout.status_size, DARKGRAY);
            DrawText(time_text, time_x, (int)layout.metadata_y,
                     layout.status_size, GRAY);
            DrawText(volume_text, volume_x, (int)layout.metadata_y,
                     layout.status_size, GRAY);

            DrawRectangleRec(layout.progress_bar, progress_background);
            DrawRectangleRec(progress_fill, progress_hovered
                                                ? progress_hover
                                                : progress_foreground);
            DrawCircleV(progress_handle,
                        (progress_hovered ? 7.0f : 5.0f) * layout.scale,
                        progress_hovered ? progress_hover
                                         : progress_foreground);

            draw_button(layout.previous_button, BUTTON_PREVIOUS, mouse,
                        can_previous);
            draw_button(layout.play_button,
                        state == PLAYER_PLAYING ? BUTTON_PAUSE : BUTTON_PLAY,
                        mouse, true);
            draw_button(layout.next_button, BUTTON_NEXT, mouse, can_next);
        }

        if (playlist_open) {
            draw_playlist_panel(&playlist, &layout, mouse, playlist_scroll);
        }

        draw_playlist_toggle(layout.playlist_toggle, playlist_open, mouse);

        EndDrawing();
    }

    CloseWindow();

    playlist_uninit(&playlist);
    player_uninit(&player);

    return exit_code;
}
