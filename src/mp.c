#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "m3u.h"
#include "metadata.h"
#include "playback_order.h"
#include "player.h"
#include "playlist.h"
#include "raylib.h"
#include "session.h"
#include "svg.h"

#define ASSET_PATH_SIZE 4096
#define SESSION_PATH_SIZE 4096

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

static bool append_inputs_in_place(Playlist *playlist, const char *const *paths,
                                   size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        bool success;

        if (m3u_is_path(paths[i])) {
            success = m3u_append(playlist, paths[i], NULL);
        } else {
            success = playlist_append(playlist, &paths[i], 1);
        }

        if (!success) return false;
    }

    return true;
}

static bool update_input_paths(Playlist *playlist, const char *const *paths,
                               size_t count, bool replace, size_t *added)
{
    Playlist replacement;
    playlist_init(&replacement);

    if ((!replace &&
         !playlist_append(&replacement, (const char *const *)playlist->paths,
                          playlist->count)) ||
        !append_inputs_in_place(&replacement, paths, count)) {
        playlist_uninit(&replacement);
        return false;
    }

    if (added != NULL) {
        *added = replacement.count - (replace ? 0 : playlist->count);
    }

    playlist_uninit(playlist);
    *playlist = replacement;
    return true;
}

static size_t random_order_index(size_t upper_bound, void *context)
{
    (void)context;

    if (upper_bound < 2) return 0;

    int maximum =
        upper_bound > (size_t)INT_MAX ? INT_MAX : (int)upper_bound - 1;
    return (size_t)GetRandomValue(0, maximum) % upper_bound;
}

static bool reset_playback_order(Playback_Order *order,
                                 const Playlist *playlist, bool shuffled)
{
    if (playback_order_reset(order, playlist_get_count(playlist),
                             playlist_get_current(playlist), shuffled,
                             random_order_index, NULL)) {
        return true;
    }

    mp_log(ERROR, "Failed to update playback order");
    return false;
}

static bool select_track(Player *player, Playlist *playlist,
                         Playback_Order *order, size_t index,
                         Track_Metadata *metadata)
{
    if (!play_track(player, playlist, index, metadata)) return false;

    if (!playback_order_select(order, index, random_order_index, NULL)) {
        mp_log(ERROR, "Failed to select track in playback order");
        return false;
    }

    return true;
}

static bool play_next_track(Player *player, Playlist *playlist,
                            Playback_Order *order, bool repeat_all,
                            Track_Metadata *metadata)
{
    size_t count = playlist_get_count(playlist);

    for (size_t attempt = 0; attempt < count; ++attempt) {
        size_t index;

        if (!playback_order_next(order, repeat_all, random_order_index, NULL,
                                 &index)) {
            return false;
        }

        if (play_track(player, playlist, index, metadata)) return true;
    }

    return false;
}

static bool play_previous_track(Player *player, Playlist *playlist,
                                Playback_Order *order, bool repeat_all,
                                Track_Metadata *metadata)
{
    size_t count = playlist_get_count(playlist);

    for (size_t attempt = 0; attempt < count; ++attempt) {
        size_t index;

        if (!playback_order_previous(order, repeat_all, &index)) {
            return false;
        }

        if (play_track(player, playlist, index, metadata)) return true;
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
    Texture2D texture;
    char *track_path;
} Album_Art;

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
    Rectangle album_art;
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

static bool make_asset_path(char *path, size_t capacity, const char *directory,
                            const char *name)
{
    int written = snprintf(path, capacity, "%s/%s", directory, name);

    return written >= 0 && (size_t)written < capacity;
}

static bool asset_directory_valid(const char *directory)
{
    char path[ASSET_PATH_SIZE];

    return make_asset_path(path, sizeof(path), directory, "play.svg") &&
           FileExists(path);
}

static bool resolve_asset_directory(char *directory, size_t capacity)
{
    if (asset_directory_valid("assets")) {
        int written = snprintf(directory, capacity, "assets");
        return written >= 0 && (size_t)written < capacity;
    }

    const char *application_directory = GetApplicationDirectory();
    const char *relative_directories[] = {"assets", "../assets"};

    for (size_t i = 0;
         i < sizeof(relative_directories) / sizeof(relative_directories[0]);
         ++i) {
        int written = snprintf(directory, capacity, "%s%s",
                               application_directory, relative_directories[i]);

        if (written >= 0 && (size_t)written < capacity &&
            asset_directory_valid(directory)) {
            return true;
        }
    }

    return false;
}

static Texture2D load_asset_texture(const char *directory, const char *name,
                                    int size, float content_scale)
{
    char path[ASSET_PATH_SIZE];

    if (!make_asset_path(path, sizeof(path), directory, name)) {
        mp_log(ERROR, "Asset path is too long: %s", name);
        return (Texture2D){0};
    }

    return svg_load_texture(path, size, content_scale);
}

static bool create_ui_icons(Ui_Icons *icons, const char *asset_directory,
                            int button_size, int toggle_size)
{
    *icons = (Ui_Icons){
        .back =
            load_asset_texture(asset_directory, "back.svg", button_size, 0.74f),
        .forward = load_asset_texture(asset_directory, "forward.svg",
                                      button_size, 0.74f),
        .pause = load_asset_texture(asset_directory, "pause.svg", button_size,
                                    0.72f),
        .play =
            load_asset_texture(asset_directory, "play.svg", button_size, 0.72f),
        .repeat = load_asset_texture(asset_directory, "repeat.svg", button_size,
                                     0.68f),
        .repeat_one = load_asset_texture(asset_directory, "repeat-one.svg",
                                         button_size, 0.68f),
        .shuffle = load_asset_texture(asset_directory, "shuffle.svg",
                                      button_size, 0.68f),
        .playlist_back =
            load_asset_texture(asset_directory, "back.svg", toggle_size, 0.88f),
        .playlist_forward = load_asset_texture(asset_directory, "forward.svg",
                                               toggle_size, 0.88f),
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

static bool update_ui_icons(Ui_Icons *icons, const char *asset_directory,
                            int button_size, int toggle_size)
{
    if (icons->button_size == button_size &&
        icons->toggle_size == toggle_size) {
        return true;
    }

    Ui_Icons replacement;

    if (!create_ui_icons(&replacement, asset_directory, button_size,
                         toggle_size)) {
        return false;
    }

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

static void album_art_clear(Album_Art *album_art)
{
    if (IsTextureValid(album_art->texture)) {
        UnloadTexture(album_art->texture);
    }

    free(album_art->track_path);
    *album_art = (Album_Art){0};
}

static void album_art_update(Album_Art *album_art, const char *track_path)
{
    if ((track_path == NULL && album_art->track_path == NULL) ||
        (track_path != NULL && album_art->track_path != NULL &&
         strcmp(track_path, album_art->track_path) == 0)) {
        return;
    }

    album_art_clear(album_art);

    if (track_path == NULL) return;

    size_t path_size = strlen(track_path) + 1;
    album_art->track_path = malloc(path_size);

    if (album_art->track_path == NULL) {
        mp_log(WARNING, "Failed to remember album art path");
        return;
    }

    memcpy(album_art->track_path, track_path, path_size);

    Track_Cover cover;

    if (!metadata_cover_load(track_path, &cover)) return;

    const char *file_type = cover.format == TRACK_COVER_JPEG ? ".jpg" : ".png";
    Image image = {0};

    if (cover.size <= INT_MAX) {
        image = LoadImageFromMemory(file_type, cover.data, (int)cover.size);
    }

    metadata_cover_unload(&cover);

    if (!IsImageValid(image)) {
        mp_log(WARNING, "Failed to decode album art for \"%s\"", track_path);
        return;
    }

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);

    if (!IsTextureValid(texture)) {
        mp_log(WARNING, "Failed to upload album art for \"%s\"", track_path);
        return;
    }

    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    album_art->texture = texture;
}

static void draw_album_art(const Album_Art *album_art, Rectangle bounds)
{
    if (!IsTextureValid(album_art->texture) || bounds.width <= 0.0f ||
        bounds.height <= 0.0f) {
        return;
    }

    float horizontal_scale = bounds.width / album_art->texture.width;
    float vertical_scale = bounds.height / album_art->texture.height;
    float scale =
        horizontal_scale < vertical_scale ? horizontal_scale : vertical_scale;
    Rectangle destination = snap_rectangle((Rectangle){
        bounds.x +
            (bounds.width - (float)album_art->texture.width * scale) / 2.0f,
        bounds.y +
            (bounds.height - (float)album_art->texture.height * scale) / 2.0f,
        (float)album_art->texture.width * scale,
        (float)album_art->texture.height * scale,
    });
    Rectangle source = {
        0.0f,
        0.0f,
        (float)album_art->texture.width,
        (float)album_art->texture.height,
    };

    DrawRectangleRec(bounds, (Color){24, 24, 24, 255});
    DrawTexturePro(album_art->texture, source, destination, (Vector2){0}, 0.0f,
                   WHITE);
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
    float title_y = snap_pixel(controls_y - 128.0f * scale);
    float album_art_gap = snap_pixel(20.0f * scale);
    float album_art_size =
        snap_pixel(clamp_float(360.0f * scale, 120.0f, 420.0f));
    float max_album_art_width = (float)width - padding * 2.0f;
    float max_album_art_height = title_y - album_art_gap - padding;

    if (album_art_size > max_album_art_width) {
        album_art_size = max_album_art_width;
    }

    if (album_art_size > max_album_art_height) {
        album_art_size = max_album_art_height;
    }

    if (album_art_size < 0.0f) album_art_size = 0.0f;

    Rectangle album_art = {
        padding,
        title_y - album_art_gap - album_art_size,
        album_art_size,
        album_art_size,
    };

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
        .title_y = title_y,
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
        .album_art = album_art,
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
    layout.album_art = snap_rectangle(layout.album_art);
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

    Playback_Order playback_order;
    playback_order_init(&playback_order);

    const int w_width = 1000;
    const int w_height = 700;

    const char *window_title = "Music Player";
    const char *playlist_file_path =
        argc == 2 && m3u_is_path(argv[1]) ? argv[1] : "playlist.m3u";
    Track_Metadata metadata = {0};
    Session_State session_state;
    session_state_defaults(&session_state);

    char session_path[SESSION_PATH_SIZE];
    bool session_path_available =
        session_default_path(session_path, sizeof(session_path));
    bool session_loaded = false;

    if (session_path_available) {
        session_loaded = session_load(session_path, &playlist,
                                      &session_state) == SESSION_LOAD_OK;
    }

    bool restore_playback = session_loaded && argc == 1;

    if (argc > 1) {
        if (!update_input_paths(&playlist, (const char *const *)&argv[1],
                                (size_t)(argc - 1), true, NULL)) {
            playlist_clear(&playlist);
        }

        session_state.current = 0;
    }

    player_set_volume(&player, session_state.volume);

    if (session_state.muted) player_toggle_mute(&player);

    bool loaded = false;
    size_t loaded_index = 0;
    size_t track_count = playlist_get_count(&playlist);
    size_t first_index = restore_playback ? session_state.current : 0;

    if (restore_playback && track_count > 0) {
        playlist_select(&playlist, session_state.current);
    }

    for (size_t i = 0; i < track_count; ++i) {
        size_t index = (first_index + i) % track_count;

        if (play_track(&player, &playlist, index, &metadata)) {
            loaded = true;
            loaded_index = index;
            break;
        }
    }

    if (!loaded && !restore_playback) playlist_clear(&playlist);

    if (loaded && restore_playback) {
        if (!player_toggle(&player)) {
            playlist_uninit(&playlist);
            playback_order_uninit(&playback_order);
            player_uninit(&player);
            return 1;
        }

        if (loaded_index == session_state.current &&
            session_state.cursor > 0.0f) {
            float length = player_get_length(&player);
            float cursor = session_state.cursor;

            if (length > 0.0f && cursor >= length) cursor = 0.0f;

            player_seek(&player, cursor);
        }
    }

    bool shuffle_enabled = session_state.shuffled;

    if (!reset_playback_order(&playback_order, &playlist, shuffle_enabled)) {
        playlist_uninit(&playlist);
        playback_order_uninit(&playback_order);
        player_uninit(&player);
        return 1;
    }

    const Color background = {18, 18, 18, 255};
    const Color progress_background = {55, 55, 55, 255};
    const Color progress_foreground = {230, 230, 230, 255};
    const Color progress_hover = {255, 255, 255, 255};

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(w_width, w_height, window_title);

    Ui_Icons icons = {0};
    char asset_directory[ASSET_PATH_SIZE];

    if (!resolve_asset_directory(asset_directory, sizeof(asset_directory))) {
        mp_log(ERROR, "Failed to locate application assets");
        CloseWindow();
        playlist_uninit(&playlist);
        playback_order_uninit(&playback_order);
        player_uninit(&player);
        return 1;
    }

    SetWindowMinSize(480, 320);
    SetTextureFilter(GetFontDefault().texture, TEXTURE_FILTER_POINT);
    SetTargetFPS(60);

    Album_Art album_art = {0};
    int exit_code = 0;
    int playlist_scroll = 0;
    bool playlist_open = false;
    bool finished_handled = false;
    bool seek_dragging = false;
    Repeat_Mode repeat_mode = (Repeat_Mode)session_state.repeat_mode;

    while (!WindowShouldClose()) {
        if (IsFileDropped()) {
            FilePathList dropped_files = LoadDroppedFiles();

            if (dropped_files.count > 0) {
                size_t first_new_track = playlist_get_count(&playlist);
                size_t added = 0;

                if (update_input_paths(&playlist,
                                       (const char *const *)dropped_files.paths,
                                       dropped_files.count, false, &added)) {
                    if (player_get_state(&player) == PLAYER_STOPPED) {
                        for (size_t i = first_new_track;
                             i < playlist_get_count(&playlist); ++i) {
                            if (play_track(&player, &playlist, i, &metadata)) {
                                finished_handled = false;
                                break;
                            }
                        }
                    }

                    if (!reset_playback_order(&playback_order, &playlist,
                                              shuffle_enabled)) {
                        exit_code = 1;
                    }

                    mp_log(INFO, "Added %zu tracks to playlist", added);
                }
            }

            UnloadDroppedFiles(dropped_files);

            if (exit_code != 0) break;
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
                } else {
                    continued =
                        play_next_track(&player, &playlist, &playback_order,
                                        repeat_mode == REPEAT_ALL, &metadata);
                }

                if (continued) {
                    finished_handled = false;
                }
            }
        } else {
            finished_handled = false;
        }

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

        if (!update_ui_icons(&icons, asset_directory, button_icon_size,
                             toggle_icon_size)) {
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
                    if (select_track(&player, &playlist, &playback_order, index,
                                     &metadata)) {
                        finished_handled = false;
                    }

                    break;
                }
            }
        }

        bool control_down =
            IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

        if (control_down && IsKeyPressed(KEY_S) &&
            m3u_save(&playlist, playlist_file_path)) {
            mp_log(INFO, "Saved playlist to \"%s\"", playlist_file_path);
        }

        if (control_down && IsKeyPressed(KEY_DELETE)) {
            player_clear(&player);
            playlist_clear(&playlist);
            reset_playback_order(&playback_order, &playlist, shuffle_enabled);
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

            if (!reset_playback_order(&playback_order, &playlist,
                                      shuffle_enabled)) {
                exit_code = 1;
                break;
            }

            for (size_t i = 0; i < remaining; ++i) {
                size_t index = (removed_index + i) % remaining;

                if (select_track(&player, &playlist, &playback_order, index,
                                 &metadata)) {
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
            bool enabled = !shuffle_enabled;

            if (!playback_order_set_shuffled(
                    &playback_order, playlist_get_current(&playlist), enabled,
                    random_order_index, NULL)) {
                mp_log(ERROR, "Failed to change shuffle mode");
                exit_code = 1;
                break;
            }

            shuffle_enabled = enabled;
            mp_log(INFO, "Shuffle: %s", shuffle_enabled ? "On" : "Off");
        }

        bool repeat_all = repeat_mode == REPEAT_ALL;
        bool can_previous = has_track && playback_order_can_previous(
                                             &playback_order, repeat_all);
        bool can_next =
            has_track && playback_order_can_next(&playback_order, repeat_all);

        if ((IsKeyPressed(KEY_RIGHT) && can_next) ||
            button_pressed(layout.next_button, mouse,
                           can_next && !sidebar_blocks_mouse)) {
            play_next_track(&player, &playlist, &playback_order, repeat_all,
                            &metadata);
        }

        if ((IsKeyPressed(KEY_LEFT) && can_previous) ||
            button_pressed(layout.previous_button, mouse,
                           can_previous && !sidebar_blocks_mouse)) {
            play_previous_track(&player, &playlist, &playback_order, repeat_all,
                                &metadata);
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
        const char *album_art_path =
            state == PLAYER_STOPPED
                ? NULL
                : playlist_get(&playlist, playlist_get_current(&playlist));
        album_art_update(&album_art, album_art_path);
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
            draw_album_art(&album_art, layout.album_art);
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

    if (session_path_available) {
        size_t count = playlist_get_count(&playlist);

        Session_State saved_state = {
            .current = count == 0 ? 0 : playlist_get_current(&playlist),
            .cursor = player_get_cursor(&player),
            .volume = player_get_volume(&player),
            .repeat_mode = repeat_mode,
            .muted = player_is_muted(&player),
            .shuffled = shuffle_enabled,
        };

        session_save(session_path, &playlist, &saved_state);
    }

    album_art_clear(&album_art);
    unload_ui_icons(&icons);
    CloseWindow();

    playlist_uninit(&playlist);
    playback_order_uninit(&playback_order);
    player_uninit(&player);

    return exit_code;
}
