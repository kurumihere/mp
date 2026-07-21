#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "album_art.h"
#include "config.h"
#include "file_picker.h"
#include "fs.h"
#include "log.h"
#include "m3u.h"
#include "media_keys.h"
#include "media_session.h"
#include "metadata.h"
#include "playback_order.h"
#include "player.h"
#include "playlist.h"
#include "playlist_search.h"
#include "raylib.h"
#include "session.h"
#include "spectrum.h"
#include "theme.h"
#include "ui_font.h"
#include "ui_icons.h"
#include "ui_layout.h"
#include "ui_theme.h"

#define CONFIG_PATH_SIZE 4096
#define MP_VERSION "0.2.0"
#define SIDE_PANEL_ANIMATION_SPEED 14.0f
#define PLAYLIST_TRACK_NONE ((size_t)-1)
#define PLAYLIST_TOGGLE_ANIMATION_SPEED 18.0f
#define SEEK_STEP_SECONDS 5.0f
#define SESSION_PATH_SIZE 4096
#define SPECTRUM_DISPLAY_MAX_BARS 64
#define SPECTRUM_RESIZE_SETTLE_SECONDS 0.15
#define WINDOW_HEIGHT 700
#define WINDOW_MIN_HEIGHT 480
#define WINDOW_MIN_WIDTH 640
#define WINDOW_WIDTH 1000

#if PLAYER_ANALYSIS_SAMPLE_COUNT != SPECTRUM_SAMPLE_COUNT
#error Player analysis and spectrum sample counts must match
#endif

static int compare_paths(const void *left, const void *right)
{
    const char *left_path = *(const char *const *)left;
    const char *right_path = *(const char *const *)right;
    return strcmp(left_path, right_path);
}

static bool append_directory(Playlist *playlist, const char *path)
{
    Fs_Path_List files = {0};

    if (!fs_list_audio_files(path, &files)) return false;

    if (files.count > 1) {
        qsort(files.items, files.count, sizeof(*files.items), compare_paths);
    }

    bool success = playlist_append(playlist, (const char *const *)files.items,
                                   files.count);
    fs_path_list_uninit(&files);
    return success;
}

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

        if (fs_is_directory(paths[i])) {
            success = append_directory(playlist, paths[i]);
        } else if (m3u_is_path(paths[i])) {
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

    if (!replace && playlist_get_count(playlist) > 0 &&
        !playlist_select(&replacement, playlist_get_current(playlist))) {
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

    mp_log(ERROR, "failed to update playback order");
    return false;
}

static bool add_inputs_to_player(Player *player, Playlist *playlist,
                                 Playback_Order *order, bool shuffled,
                                 const char *const *paths, size_t count,
                                 Track_Metadata *metadata,
                                 bool *finished_handled, size_t *added)
{
    size_t first_new_track = playlist_get_count(playlist);

    if (!update_input_paths(playlist, paths, count, false, added)) {
        mp_log(ERROR, "failed to add files to playlist");
        return false;
    }

    if (player_get_state(player) == PLAYER_EMPTY) {
        for (size_t i = first_new_track; i < playlist_get_count(playlist);
             ++i) {
            if (play_track(player, playlist, i, metadata)) {
                *finished_handled = false;
                break;
            }
        }
    }

    return reset_playback_order(order, playlist, shuffled);
}

static bool add_file_picker_selection(Player *player, Playlist *playlist,
                                      Playback_Order *order, bool shuffled,
                                      char *selection, Track_Metadata *metadata,
                                      bool *finished_handled, size_t *added)
{
    size_t length = strlen(selection);
    size_t selected_count = 1;

    for (size_t i = 0; i < length; ++i) {
        if (selection[i] == '|') ++selected_count;
    }

    if (selected_count > SIZE_MAX / sizeof(char *)) return false;

    char **selected_paths = malloc(selected_count * sizeof(*selected_paths));

    if (selected_paths == NULL) return false;

    size_t path_index = 0;
    selected_paths[path_index++] = selection;

    for (size_t i = 0; i < length; ++i) {
        if (selection[i] != '|') continue;

        selection[i] = '\0';
        selected_paths[path_index++] = selection + i + 1;
    }

    bool result = add_inputs_to_player(
        player, playlist, order, shuffled, (const char *const *)selected_paths,
        selected_count, metadata, finished_handled, added);
    free(selected_paths);
    return result;
}

static bool save_playlist_to_path(const Playlist *playlist,
                                  const char *selected_path, char *saved_path,
                                  size_t saved_path_size)
{
    if (selected_path[0] == '\0') return false;

    const char *extension = m3u_is_path(selected_path) ? "" : ".m3u";
    int written =
        snprintf(saved_path, saved_path_size, "%s%s", selected_path, extension);

    if (written < 0 || (size_t)written >= saved_path_size) return false;

    return m3u_save(playlist, saved_path);
}

static bool select_track(Player *player, Playlist *playlist,
                         Playback_Order *order, size_t index,
                         Track_Metadata *metadata)
{
    if (!play_track(player, playlist, index, metadata)) return false;

    if (!playback_order_select(order, index, random_order_index, NULL)) {
        mp_log(ERROR, "failed to select track in playback order");
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
    REPEAT_OFF,
    REPEAT_ALL,
    REPEAT_ONE,
} Repeat_Mode;

typedef enum {
    SIDE_PANEL_NONE,
    SIDE_PANEL_PLAYLIST,
    SIDE_PANEL_SETTINGS,
} Side_Panel;

static void request_side_panel(Side_Panel target, Side_Panel *side_panel,
                               Side_Panel *animated_side_panel,
                               Side_Panel *queued_side_panel, float animation)
{
    if (*side_panel == target) {
        *side_panel = SIDE_PANEL_NONE;
        *queued_side_panel = SIDE_PANEL_NONE;
        return;
    }

    if (*side_panel != SIDE_PANEL_NONE || animation > 0.001f) {
        *side_panel = SIDE_PANEL_NONE;
        *queued_side_panel = target;
        return;
    }

    *side_panel = target;
    *animated_side_panel = target;
    *queued_side_panel = SIDE_PANEL_NONE;
}

static float clamp_float(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;

    return value;
}

static float animate_towards(float current, float target, float speed,
                             float delta_time)
{
    float frame_time = clamp_float(delta_time, 0.0f, 0.05f);
    float result = target + (current - target) * expf(-speed * frame_time);

    if (fabsf(result - target) < 0.001f) return target;

    return result;
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

static void draw_panel_frame(Rectangle panel, Color color)
{
    panel.width -= 1.0f;
    panel.height -= 1.0f;

    if (panel.width > 0.0f && panel.height > 0.0f) {
        DrawRectangleLinesEx(panel, 1.0f, color);
    }
}

static void draw_panel_surface(Rectangle panel, bool translucent,
                               const Ui_Theme *theme)
{
    Rectangle shadow = panel;
    shadow.x += 5.0f;
    shadow.y += 6.0f;
    DrawRectangleRec(shadow, (Color){0, 0, 0, 28});
    Color surface = translucent ? theme->surface : theme->solid_surface;

    DrawRectangleRec(panel, surface);
    draw_panel_frame(panel, theme->surface_border);
}

static void draw_open_menu(const Open_Menu_Layout *menu, Vector2 mouse,
                           float scale, const Ui_Theme *theme)
{
    int font_size = ui_font_crisp_size(17.0f * scale, 14, 20);
    const Rectangle rows[] = {menu->files, menu->folder};
    const char *labels[] = {"Files", "Folder"};

    draw_panel_surface(menu->panel, true, theme);

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        bool hovered = CheckCollisionPointRec(mouse, rows[i]);
        DrawRectangleRec(rows[i], hovered ? theme->playlist_hover
                                          : theme->playlist_item);
        int width = ui_font_measure(labels[i], font_size);
        ui_font_draw(
            labels[i],
            (int)snap_pixel(rows[i].x + (rows[i].width - (float)width) / 2.0f),
            (int)snap_pixel(rows[i].y + (rows[i].height - font_size) / 2.0f -
                            1.0f),
            font_size, theme->text_primary);
    }
}

static size_t spectrum_bar_count(Rectangle bounds, float scale)
{
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return 0;

    float desired_step = clamp_float(24.0f * scale, 16.0f, 32.0f);
    size_t count = (size_t)(bounds.width / desired_step);

    if (count < 8) count = 8;
    if (count > SPECTRUM_DISPLAY_MAX_BARS) {
        count = SPECTRUM_DISPLAY_MAX_BARS;
    }

    return count;
}

static void draw_spectrum(const Spectrum *spectrum, Rectangle bounds,
                          size_t bar_count, float scale, const Ui_Theme *theme)
{
    if (bar_count == 0 || bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return;
    }

    float bar_gap = snap_pixel(clamp_float(8.0f * scale, 5.0f, 11.0f));
    float bar_width =
        (bounds.width - bar_gap * (float)(bar_count - 1)) / bar_count;

    bar_width = floorf(bar_width);

    if (bar_width < 1.0f) bar_width = 1.0f;

    float available_height = bounds.height;
    float bottom = bounds.y + bounds.height;
    float block_width =
        bar_width * (float)bar_count + bar_gap * (float)(bar_count - 1);
    float start_x = snap_pixel(bounds.x + (bounds.width - block_width) / 2.0f);

    for (size_t bar = 0; bar < bar_count; ++bar) {
        float level = clamp_float(spectrum->levels[bar], 0.0f, 1.0f);
        float height = level * available_height;
        float x = snap_pixel(start_x + (bar_width + bar_gap) * (float)bar);

        if (level <= 0.0f) continue;

        Rectangle column = {
            x,
            bottom - height,
            bar_width,
            height,
        };

        DrawRectangleRec(column, theme->spectrum);
    }
}

static void draw_scrolling_text(const char *text, Rectangle bounds,
                                int font_size, float scale, Color color,
                                bool scrolling, double started_at,
                                const Rectangle *clip)
{
    int text_width = ui_font_measure(text, font_size);
    float offset = 0.0f;

    if ((float)text_width > bounds.width && scrolling) {
        float distance = (float)text_width - bounds.width;
        float speed = clamp_float(48.0f * scale, 36.0f, 80.0f);
        double pause = 1.25;
        double travel_time = distance / speed;
        double cycle_time = pause * 2.0 + travel_time * 2.0;
        double elapsed = GetTime() - started_at;
        double phase = fmod(elapsed < 0.0 ? 0.0 : elapsed, cycle_time);

        if (phase < pause) {
            offset = 0.0f;
        } else if (phase < pause + travel_time) {
            offset = (float)((phase - pause) * speed);
        } else if (phase < pause * 2.0 + travel_time) {
            offset = distance;
        } else {
            offset =
                distance - (float)((phase - pause * 2.0 - travel_time) * speed);
        }
    }

    Rectangle scissor = bounds;

    if (clip != NULL) scissor = GetCollisionRec(scissor, *clip);
    if (scissor.width <= 0.0f || scissor.height <= 0.0f) return;

    BeginScissorMode((int)scissor.x, (int)scissor.y, (int)scissor.width,
                     (int)scissor.height);
    ui_font_draw(text, (int)(bounds.x - snap_pixel(offset)), (int)bounds.y,
                 font_size, color);
    EndScissorMode();
}

static void draw_button(Rectangle bounds, Ui_Icon icon,
                        const Ui_Icon_Transition *icon_transition,
                        Vector2 mouse, bool enabled, bool active,
                        const Ui_Theme *theme)
{
    bool hovered = enabled && CheckCollisionPointRec(mouse, bounds);
    Color fill = enabled ? theme->button : theme->button_disabled;
    float opacity = enabled ? 1.0f : 0.35f;

    if (active && enabled) fill = theme->button_active;
    if (hovered) fill = theme->button_hover;

    DrawRectangleRec(bounds, fill);

    if (enabled && (hovered || active)) {
        draw_panel_frame(bounds, theme->surface_border);
    }

    ui_icon_transition_draw(icon_transition, icon, bounds, opacity);
}

static bool format_track_details(const Track_Metadata *metadata, char *text,
                                 size_t capacity)
{
    bool has_artist = metadata->artist[0] != '\0';
    bool has_album = metadata->album[0] != '\0';

    if (has_artist && has_album) {
        snprintf(text, capacity, "%s | %s", metadata->artist, metadata->album);
    } else if (has_artist) {
        snprintf(text, capacity, "%s", metadata->artist);
    } else if (has_album) {
        snprintf(text, capacity, "%s", metadata->album);
    } else {
        if (capacity > 0) text[0] = '\0';
        return false;
    }

    return true;
}

static bool collect_playlist_font_text(const Playlist *playlist,
                                       const Playlist_Search *search,
                                       const Ui_Layout *layout, float scroll)
{
    size_t count = playlist_search_count(search, playlist);
    size_t first = scroll > 0.0f ? (size_t)floorf(scroll) : 0;

    if (!ui_font_collect(search->query, layout->playlist_search_size)) {
        return false;
    }

    for (int visible_index = 0; visible_index <= layout->visible_playlist_items;
         ++visible_index) {
        size_t result_index = first + (size_t)visible_index;

        if (result_index >= count) break;

        size_t index = playlist_search_track(search, result_index);

        const Track_Metadata *metadata = playlist_get_metadata(playlist, index);

        if (metadata == NULL ||
            !ui_font_collect(metadata->title, layout->playlist_title_size)) {
            return false;
        }

        char details[METADATA_TEXT_SIZE * 2 + 4];

        if (!format_track_details(metadata, details, sizeof(details))) continue;

        if (!ui_font_collect(details, layout->playlist_details_size)) {
            return false;
        }
    }

    return true;
}

static void draw_playlist_panel(const Playlist *playlist,
                                const Playlist_Search *search,
                                const Ui_Layout *layout, float scroll,
                                size_t hovered_track, double text_started_at,
                                bool search_focused, Vector2 mouse,
                                bool picker_busy, const Ui_Theme *theme)
{
    Rectangle panel = layout->playlist_panel;
    int title_size = layout->playlist_title_size;
    int details_size = layout->playlist_details_size;
    size_t playlist_count = playlist_get_count(playlist);
    size_t count = playlist_search_count(search, playlist);
    size_t current = playlist_get_current(playlist);
    size_t first = scroll > 0.0f ? (size_t)floorf(scroll) : 0;
    float step = layout->playlist_item_height + layout->playlist_item_gap;
    float scroll_offset = (scroll - (float)first) * step;

    draw_panel_surface(panel, false, theme);
    ui_font_draw("Playlist", (int)layout->playlist_viewport.x,
                 (int)snap_pixel(panel.y + (layout->playlist_top - panel.y -
                                            layout->playlist_header_size) /
                                               2.0f),
                 layout->playlist_header_size, theme->text_primary);

    if (count == 0) {
        const char *empty = playlist_count == 0 ? "No tracks" : "No matches";
        ui_font_draw(empty, (int)layout->playlist_viewport.x,
                     (int)snap_pixel(layout->playlist_viewport.y + 8.0f),
                     layout->playlist_details_size, theme->text_muted);
    }

    for (int visible_index = 0; visible_index <= layout->visible_playlist_items;
         ++visible_index) {
        size_t result_index = first + (size_t)visible_index;

        if (result_index >= count) break;

        size_t index = playlist_search_track(search, result_index);

        Rectangle item = ui_layout_playlist_item_bounds(layout, visible_index,
                                                        scroll_offset);

        if (!CheckCollisionRecs(item, layout->playlist_viewport)) continue;

        bool hovered = index == hovered_track;
        Color fill =
            index == current ? theme->playlist_current : theme->playlist_item;

        if (hovered && index != current) fill = theme->playlist_hover;

        DrawRectangleRec(GetCollisionRec(item, layout->playlist_viewport),
                         fill);

        const Track_Metadata *metadata = playlist_get_metadata(playlist, index);

        if (metadata == NULL) continue;

        char title[METADATA_TEXT_SIZE + 32];
        snprintf(title, sizeof(title), "%zu. %s", index + 1, metadata->title);

        char details[METADATA_TEXT_SIZE * 2 + 4];
        bool has_details =
            format_track_details(metadata, details, sizeof(details));
        float text_padding =
            snap_pixel(clamp_float(9.0f * layout->scale, 7.0f, 12.0f));
        float text_x = snap_pixel(item.x + text_padding);
        float text_width = item.width - text_padding * 2.0f;
        Color title_color =
            index == current ? theme->text_primary : theme->text_secondary;

        if (!has_details) {
            Rectangle title_bounds = {
                text_x,
                snap_pixel(item.y + (item.height - title_size) / 2.0f),
                text_width,
                (float)title_size,
            };

            draw_scrolling_text(title, title_bounds, title_size, layout->scale,
                                title_color, hovered, text_started_at,
                                &layout->playlist_viewport);
        } else {
            Rectangle title_bounds = {
                text_x,
                snap_pixel(item.y + 5.0f),
                text_width,
                (float)title_size,
            };
            Color details_color =
                index == current ? theme->text_secondary : theme->text_muted;

            Rectangle details_bounds = {
                text_x,
                snap_pixel(item.y + title_size + 7.0f),
                text_width,
                (float)details_size,
            };

            draw_scrolling_text(title, title_bounds, title_size, layout->scale,
                                title_color, hovered, text_started_at,
                                &layout->playlist_viewport);
            draw_scrolling_text(details, details_bounds, details_size,
                                layout->scale, details_color, hovered,
                                text_started_at, &layout->playlist_viewport);
        }
    }

    Rectangle field = layout->playlist_search;
    Color field_fill =
        search_focused ? theme->playlist_hover : theme->playlist_item;
    DrawRectangleRec(field, field_fill);

    const char *search_text = search->query[0] == '\0'
                                  ? search_focused ? "" : "Search playlist"
                                  : search->query;
    Color search_color =
        search->query[0] == '\0' ? theme->text_faint : theme->text_secondary;
    float search_padding =
        snap_pixel(clamp_float(10.0f * layout->scale, 8.0f, 14.0f));
    char result_count[32] = {0};
    float count_width = 0.0f;

    if (playlist_search_active(search)) {
        snprintf(result_count, sizeof(result_count), "%zu", count);
        count_width =
            (float)ui_font_measure(result_count, layout->playlist_search_size);
    }

    Rectangle text_clip = {
        field.x + search_padding,
        field.y,
        field.width - search_padding * 2.0f -
            (count_width > 0.0f ? count_width + search_padding : 0.0f),
        field.height,
    };
    float text_y = snap_pixel(
        field.y + (field.height - layout->playlist_search_size) / 2.0f - 1.0f);
    char query_prefix[PLAYLIST_SEARCH_QUERY_CAPACITY];
    size_t prefix_size = search->cursor;

    if (prefix_size >= sizeof(query_prefix)) prefix_size = 0;

    memcpy(query_prefix, search->query, prefix_size);
    query_prefix[prefix_size] = '\0';

    float cursor_offset =
        (float)ui_font_measure(query_prefix, layout->playlist_search_size);
    float text_x = text_clip.x;
    float caret_width = snap_pixel(
        clamp_float((float)layout->playlist_search_size * 0.52f, 8.0f, 12.0f));

    if (cursor_offset + caret_width > text_clip.width) {
        text_x -= cursor_offset + caret_width - text_clip.width;
    }

    Rectangle caret = {
        snap_pixel(text_x + cursor_offset),
        text_y - 1.0f,
        caret_width,
        (float)layout->playlist_search_size + 2.0f,
    };

    BeginScissorMode((int)text_clip.x, (int)text_clip.y, (int)text_clip.width,
                     (int)text_clip.height);

    if (search_focused) {
        DrawRectangleRec(caret, theme->text_primary);
    }

    ui_font_draw(search_text, (int)snap_pixel(text_x), (int)text_y,
                 layout->playlist_search_size, search_color);

    EndScissorMode();

    if (search_focused && search->query[search->cursor] != '\0') {
        Rectangle inverted_clip = GetCollisionRec(caret, text_clip);

        BeginScissorMode((int)inverted_clip.x, (int)inverted_clip.y,
                         (int)inverted_clip.width, (int)inverted_clip.height);
        ui_font_draw(search_text, (int)snap_pixel(text_x), (int)text_y,
                     layout->playlist_search_size, field_fill);
        EndScissorMode();
    }

    if (result_count[0] != '\0') {
        ui_font_draw(result_count,
                     (int)snap_pixel(field.x + field.width - search_padding -
                                     count_width),
                     (int)text_y, layout->playlist_search_size,
                     theme->text_faint);
    }

    Rectangle open = layout->playlist_open;
    bool open_hovered = CheckCollisionPointRec(mouse, open);
    Color open_fill = picker_busy    ? theme->button_active
                      : open_hovered ? theme->button_hover
                                     : theme->button;
    const char *open_text = picker_busy ? "..." : "Open";
    int open_width = ui_font_measure(open_text, layout->playlist_search_size);

    DrawRectangleRec(open, open_fill);

    if (open_hovered || picker_busy) {
        draw_panel_frame(open, theme->surface_border);
    }

    ui_font_draw(
        open_text,
        (int)snap_pixel(open.x + (open.width - (float)open_width) / 2.0f),
        (int)snap_pixel(open.y +
                        (open.height - layout->playlist_search_size) / 2.0f -
                        1.0f),
        layout->playlist_search_size,
        picker_busy ? theme->text_muted : theme->text_primary);
}

static void draw_setting_toggle(Rectangle option, const char *label,
                                const char *first_line, const char *second_line,
                                bool enabled, Vector2 mouse, float scale,
                                const Ui_Theme *theme)
{
    bool hovered = CheckCollisionPointRec(mouse, option);
    int label_size = ui_font_crisp_size(22.0f * scale, 20, 30);

    DrawRectangleRec(option,
                     hovered ? theme->playlist_hover : theme->playlist_item);
    float switch_width = snap_pixel(44.0f * scale);
    float switch_height = snap_pixel(24.0f * scale);
    Rectangle toggle = snap_rectangle((Rectangle){
        option.x + option.width - switch_width - 14.0f * scale,
        option.y + (option.height - switch_height) / 2.0f,
        switch_width,
        switch_height,
    });
    int label_x = (int)snap_pixel(option.x + 14.0f * scale);
    float available_label_width = toggle.x - (float)label_x - 10.0f * scale;

    if ((float)ui_font_measure(label, label_size) <= available_label_width) {
        int label_y =
            (int)snap_pixel(option.y + (option.height - label_size) / 2.0f);
        ui_font_draw(label, label_x, label_y, label_size,
                     theme->text_secondary);
    } else {
        int line_size = ui_font_crisp_size(18.0f * scale, 20, 20);
        int first_y =
            (int)snap_pixel(option.y + (option.height - line_size * 2) / 2.0f);
        ui_font_draw(first_line, label_x, first_y, line_size,
                     theme->text_secondary);
        ui_font_draw(second_line, label_x, first_y + line_size, line_size,
                     theme->text_secondary);
    }

    Color toggle_fill =
        enabled ? theme->progress_foreground : theme->progress_background;
    DrawRectangleRec(toggle, toggle_fill);

    float knob_padding = snap_pixel(3.0f * scale);
    float knob_size = toggle.height - knob_padding * 2.0f;
    float knob_x = enabled ? toggle.x + toggle.width - knob_padding - knob_size
                           : toggle.x + knob_padding;
    DrawRectangleRec(snap_rectangle((Rectangle){
                         knob_x,
                         toggle.y + knob_padding,
                         knob_size,
                         knob_size,
                     }),
                     theme->surface);
}

static void draw_settings_panel(const Ui_Layout *layout,
                                bool playlist_button_on_side,
                                bool global_media_keys, Vector2 mouse,
                                const Ui_Theme *theme)
{
    Rectangle panel = layout->settings_panel;

    draw_panel_surface(panel, false, theme);
    ui_font_draw("Settings", (int)snap_pixel(panel.x + 20.0f * layout->scale),
                 (int)snap_pixel(panel.y + 20.0f * layout->scale),
                 ui_font_crisp_size(35.0f * layout->scale, 30, 50),
                 theme->text_primary);

    draw_setting_toggle(layout->settings_playlist_side,
                        "Playlist button on side", "Playlist button", "on side",
                        playlist_button_on_side, mouse, layout->scale, theme);
    draw_setting_toggle(layout->settings_global_media_keys, "Global media keys",
                        "Global media", "keys", global_media_keys, mouse,
                        layout->scale, theme);
}

static void draw_playlist_toggle(Rectangle bounds, bool open,
                                 const Ui_Icon_Transition *icon_transition,
                                 Vector2 mouse, float opacity,
                                 const Ui_Theme *theme)
{
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    Color fill = hovered ? theme->button_hover : theme->button;
    fill.a = (unsigned char)((float)fill.a * opacity);
    DrawRectangleRec(bounds, fill);

    if (hovered) draw_panel_frame(bounds, theme->surface_border);

    ui_icon_transition_draw(icon_transition,
                            open ? UI_ICON_PLAYLIST_BACK
                                 : UI_ICON_PLAYLIST_FORWARD,
                            bounds, opacity);
}

typedef struct {
    bool version;
    bool verbose;
    bool verbose_raylib;
    bool help;
} Options;

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "usage: %s [flags] [audio files, playlists, or folders ...]\n",
            program);
    fprintf(stream, "flags:\n");
    fprintf(stream, "  -v\n      print version\n");
    fprintf(stream, "  -V\n      show application info logs\n");
    fprintf(stream, "  -R\n      show raylib info logs\n");
    fprintf(stream, "  -h\n      show help\n");
}

static bool parse_options(int argc, char **argv, Options *options,
                          int *first_argument)
{
    int index = 1;

    for (; index < argc; ++index) {
        const char *argument = argv[index];

        if (argument[0] != '-' || argument[1] == '\0') break;

        for (const char *flag = argument + 1; *flag != '\0'; ++flag) {
            switch (*flag) {
            case 'v':
                options->version = true;
                break;
            case 'V':
                options->verbose = true;
                break;
            case 'R':
                options->verbose_raylib = true;
                break;
            case 'h':
                options->help = true;
                break;
            default:
                fprintf(stderr, "ERROR: -%c: unknown option\n", *flag);
                return false;
            }
        }
    }

    *first_argument = index;
    return true;
}

static int mp_main(int argc, char **argv)
{
    const char *program = argv[0];
    Options options = {0};
    int first_argument;

    if (!parse_options(argc, argv, &options, &first_argument)) {
        usage(stderr, program);
        return 1;
    }

    if (options.help) {
        usage(stdout, program);
        return 0;
    }

    if (options.version) {
        printf("mp %s\n", MP_VERSION);
        return 0;
    }

    mp_log_set_level(options.verbose ? INFO : WARNING);
    SetTraceLogLevel(options.verbose_raylib ? LOG_INFO : LOG_WARNING);

    argc -= first_argument;
    argv += first_argument;

    App_Config app_config;
    app_config_defaults(&app_config);
    char config_path[CONFIG_PATH_SIZE];
    bool config_path_available =
        app_config_default_path(config_path, sizeof(config_path));

    if (config_path_available && app_config_load(config_path, &app_config) ==
                                     APP_CONFIG_LOAD_NOT_FOUND) {
        app_config_save(config_path, &app_config);
    }

    Player player;

    if (!player_init(&player)) return 1;

    Playlist playlist;
    playlist_init(&playlist);

    Playback_Order playback_order;
    playback_order_init(&playback_order);

    const char *window_title = "mp";
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

    bool restore_playback = session_loaded && argc == 0;

    if (argc > 0) {
        if (!update_input_paths(&playlist, (const char *const *)argv,
                                (size_t)argc, true, NULL)) {
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

    System_Theme_Monitor system_theme_monitor;
    system_theme_monitor_init(&system_theme_monitor);
    System_Theme system_theme = system_theme_monitor_get(&system_theme_monitor);
    Ui_Theme_Transition theme_transition;
    ui_theme_transition_init(&theme_transition, system_theme);
    const Ui_Theme *theme = ui_theme_transition_current(&theme_transition);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, window_title);
    media_keys_init(app_config.global_media_keys);

    Ui_Icon_Transition icon_transition = {0};
    Texture2D application_icon = {0};

    if (!ui_application_icon_load(&application_icon)) {
        media_keys_uninit();
        system_theme_monitor_uninit(&system_theme_monitor);
        CloseWindow();
        playlist_uninit(&playlist);
        playback_order_uninit(&playback_order);
        player_uninit(&player);
        return 1;
    }

    if (!ui_font_init()) {
        media_keys_uninit();
        system_theme_monitor_uninit(&system_theme_monitor);
        UnloadTexture(application_icon);
        CloseWindow();
        playlist_uninit(&playlist);
        playback_order_uninit(&playback_order);
        player_uninit(&player);
        return 1;
    }

    if (!ui_icon_transition_init(&icon_transition, system_theme)) {
        media_keys_uninit();
        system_theme_monitor_uninit(&system_theme_monitor);
        ui_font_uninit();
        UnloadTexture(application_icon);
        CloseWindow();
        playlist_uninit(&playlist);
        playback_order_uninit(&playback_order);
        player_uninit(&player);
        return 1;
    }

    SetWindowMinSize(WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);
    SetTargetFPS(60);
    mp_log(INFO, "theme: %s",
           system_theme == SYSTEM_THEME_DARK ? "dark" : "light");

    Album_Art album_art = {0};
    File_Picker file_picker;
    file_picker_init(&file_picker);
    Playlist_Search playlist_search = {0};
    Spectrum spectrum;
    spectrum_init(&spectrum);
    int exit_code = 0;
    float playlist_scroll = 0.0f;
    float playlist_scroll_target = 0.0f;
    bool playlist_search_focused = false;
    bool playlist_search_dirty = false;
    Side_Panel side_panel = SIDE_PANEL_NONE;
    Side_Panel animated_side_panel = SIDE_PANEL_NONE;
    Side_Panel queued_side_panel = SIDE_PANEL_NONE;
    float side_panel_animation = 0.0f;
    float playlist_toggle_animation = 0.0f;
    bool playlist_button_on_side = app_config.playlist_button_on_side;
    size_t playlist_text_track = PLAYLIST_TRACK_NONE;
    double playlist_text_started_at = GetTime();
    size_t current_title_text_track = PLAYLIST_TRACK_NONE;
    double current_title_text_started_at = GetTime();
    double spectrum_resume_at = 0.0;
    bool finished_handled = false;
    bool seek_dragging = false;
    bool seek_resume_playback = false;
    bool open_menu_visible = false;
    Vector2 open_menu_center = {0};
    Repeat_Mode repeat_mode = (Repeat_Mode)session_state.repeat_mode;
    Media_Session media_session;
    bool media_session_available =
        media_session_init(&media_session, GetWindowHandle());

    while (!WindowShouldClose()) {
        float ui_frame_time = GetFrameTime();

        char *file_picker_selection = NULL;
        File_Picker_Mode file_picker_mode = FILE_PICKER_NONE;
        bool file_picker_graphical = false;
        bool file_picker_failed = false;

        if (file_picker_take(&file_picker, &file_picker_selection,
                             &file_picker_mode, &file_picker_graphical,
                             &file_picker_failed)) {
            if (!file_picker_graphical) {
                mp_log(ERROR,
                       "no graphical tinyfiledialogs backend is available");
            } else if (file_picker_failed) {
                mp_log(ERROR, "failed to read the file chooser selection");
                exit_code = 1;
            } else if (file_picker_selection != NULL) {
                if (file_picker_mode == FILE_PICKER_SAVE_FILE) {
                    char saved_path[SESSION_PATH_SIZE];

                    if (save_playlist_to_path(&playlist, file_picker_selection,
                                              saved_path, sizeof(saved_path))) {
                        mp_log(INFO, "saved playlist to \"%s\"", saved_path);
                    } else {
                        mp_log(ERROR, "failed to save playlist to \"%s\"",
                               file_picker_selection);
                    }
                } else {
                    size_t added = 0;

                    if (!add_file_picker_selection(
                            &player, &playlist, &playback_order,
                            shuffle_enabled, file_picker_selection, &metadata,
                            &finished_handled, &added)) {
                        mp_log(ERROR, "failed to add selected files");
                        exit_code = 1;
                    } else {
                        playlist_search_dirty = true;
                        mp_log(INFO, "added %zu tracks to playlist", added);
                    }
                }
            }

            free(file_picker_selection);

            if (exit_code != 0) break;
        }

        if (system_theme_monitor_update(&system_theme_monitor, GetTime())) {
            System_Theme detected_theme =
                system_theme_monitor_get(&system_theme_monitor);

            if (ui_icon_transition_begin(&icon_transition, detected_theme)) {
                system_theme = detected_theme;
                ui_theme_transition_begin(&theme_transition, system_theme);
                mp_log(INFO, "theme: %s",
                       system_theme == SYSTEM_THEME_DARK ? "dark" : "light");
            } else {
                mp_log(WARNING, "failed to switch application theme");
            }
        }

        ui_theme_transition_update(&theme_transition, ui_frame_time);
        ui_icon_transition_update(&icon_transition, ui_frame_time);

        if (IsWindowResized()) {
            spectrum_resume_at = GetTime() + SPECTRUM_RESIZE_SETTLE_SECONDS;
        }

        if (IsFileDropped()) {
            FilePathList dropped_files = LoadDroppedFiles();

            if (dropped_files.count > 0) {
                size_t added = 0;

                if (add_inputs_to_player(
                        &player, &playlist, &playback_order, shuffle_enabled,
                        (const char *const *)dropped_files.paths,
                        dropped_files.count, &metadata, &finished_handled,
                        &added)) {
                    playlist_search_dirty = true;
                    mp_log(INFO, "added %zu tracks to playlist", added);
                } else {
                    exit_code = 1;
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

        state = player_get_state(&player);
        bool has_track = state != PLAYER_EMPTY;

        if (!has_track) {
            side_panel = SIDE_PANEL_NONE;
            queued_side_panel = SIDE_PANEL_NONE;
        }

        Vector2 mouse = GetMousePosition();
        float mouse_wheel = GetMouseWheelMove();
        side_panel_animation = animate_towards(
            side_panel_animation,
            side_panel != SIDE_PANEL_NONE && has_track ? 1.0f : 0.0f,
            SIDE_PANEL_ANIMATION_SPEED, ui_frame_time);

        if (side_panel == SIDE_PANEL_NONE && side_panel_animation <= 0.001f) {
            if (queued_side_panel != SIDE_PANEL_NONE && has_track) {
                side_panel = queued_side_panel;
                animated_side_panel = queued_side_panel;
                queued_side_panel = SIDE_PANEL_NONE;
            } else {
                animated_side_panel = SIDE_PANEL_NONE;
            }
        }

        Ui_Layout layout =
            ui_layout_make(GetScreenWidth(), GetScreenHeight(),
                           side_panel_animation, playlist_button_on_side);
        bool playlist_panel_visible =
            has_track && animated_side_panel == SIDE_PANEL_PLAYLIST &&
            side_panel_animation > 0.001f;
        bool settings_panel_visible =
            has_track && animated_side_panel == SIDE_PANEL_SETTINGS &&
            side_panel_animation > 0.001f;
        bool picker_busy = file_picker_busy(&file_picker);
        Open_Menu_Layout open_menu = ui_open_menu_layout_make(
            open_menu_center, layout.scale, layout.width, layout.height);
        bool mouse_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        bool open_menu_consumed_click = false;

        if (open_menu_visible && mouse_pressed) {
            open_menu_consumed_click = true;
            File_Picker_Mode mode = FILE_PICKER_NONE;

            if (CheckCollisionPointRec(mouse, open_menu.files)) {
                mode = FILE_PICKER_FILES;
            } else if (CheckCollisionPointRec(mouse, open_menu.folder)) {
                mode = FILE_PICKER_FOLDER;
            }

            open_menu_visible = false;

            if (mode != FILE_PICKER_NONE &&
                !file_picker_start(&file_picker, mode)) {
                mp_log(ERROR, "failed to start the file chooser");
            }
        } else if (!open_menu_visible && !picker_busy && mouse_pressed &&
                   (!has_track ||
                    (playlist_panel_visible &&
                     CheckCollisionPointRec(mouse, layout.playlist_open)))) {
            open_menu_consumed_click = true;
            open_menu_visible = true;

            if (has_track) {
                open_menu_center = (Vector2){
                    layout.playlist_open.x + layout.playlist_open.width / 2.0f,
                    layout.playlist_open.y - 48.0f * layout.scale,
                };
            } else {
                open_menu_center = mouse;
            }
        }

        picker_busy = file_picker_busy(&file_picker);
        open_menu = ui_open_menu_layout_make(open_menu_center, layout.scale,
                                             layout.width, layout.height);
        bool open_menu_blocks_mouse =
            open_menu_visible || open_menu_consumed_click;
        bool playlist_toggle_target =
            has_track &&
            (side_panel != SIDE_PANEL_NONE ||
             (playlist_button_on_side &&
              CheckCollisionPointRec(mouse, layout.playlist_toggle_reveal)));
        playlist_toggle_animation = animate_towards(
            playlist_toggle_animation, playlist_toggle_target ? 1.0f : 0.0f,
            PLAYLIST_TOGGLE_ANIMATION_SPEED, ui_frame_time);
        bool playlist_toggle_visible =
            has_track && playlist_toggle_animation > 0.001f;
        Rectangle playlist_toggle_bounds = layout.playlist_toggle;
        playlist_toggle_bounds.x = snap_pixel(
            playlist_toggle_bounds.x +
            playlist_toggle_bounds.width * (1.0f - playlist_toggle_animation));

        bool playlist_toggled =
            !open_menu_blocks_mouse && playlist_toggle_visible &&
            button_pressed(playlist_toggle_bounds, mouse, true);

        if (playlist_toggled) {
            open_menu_visible = false;

            if (side_panel != SIDE_PANEL_NONE) {
                side_panel = SIDE_PANEL_NONE;
                queued_side_panel = SIDE_PANEL_NONE;
                playlist_search_focused = false;
            } else {
                request_side_panel(SIDE_PANEL_PLAYLIST, &side_panel,
                                   &animated_side_panel, &queued_side_panel,
                                   side_panel_animation);
                playlist_text_started_at = GetTime();
            }
        }

        bool control_down =
            IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        bool shift_down =
            IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        bool alt_down = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        unsigned int media_keys =
            media_keys_poll(IsWindowFocused(), app_config.global_media_keys);
        Media_Session_Event media_session_event =
            media_session_poll(&media_session);

        if (media_session_available) {
            media_keys &=
                MEDIA_KEY_VOLUME_UP | MEDIA_KEY_VOLUME_DOWN | MEDIA_KEY_MUTE;
        }
        bool search_changed = false;

        if (playlist_panel_visible) {
            if (control_down && IsKeyPressed(KEY_F)) {
                if (!playlist_search_focused) {
                    playlist_search_cursor_end(&playlist_search);
                }
                playlist_search_focused = true;
            }

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                !open_menu_blocks_mouse) {
                bool focus_search =
                    CheckCollisionPointRec(mouse, layout.playlist_search);

                if (focus_search && !playlist_search_focused) {
                    playlist_search_cursor_end(&playlist_search);
                }

                playlist_search_focused = focus_search;
            }

            if (playlist_search_focused) {
                if (IsKeyPressed(KEY_ESCAPE)) {
                    if (playlist_search_active(&playlist_search)) {
                        playlist_search_clear(&playlist_search);
                        search_changed = true;
                    } else {
                        playlist_search_focused = false;
                    }
                }

                bool backspace_pressed = IsKeyPressed(KEY_BACKSPACE) ||
                                         IsKeyPressedRepeat(KEY_BACKSPACE);
                bool delete_pressed =
                    IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE);
                bool left_pressed =
                    IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT);
                bool right_pressed =
                    IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT);

                if (control_down && IsKeyPressed(KEY_A)) {
                    playlist_search_cursor_start(&playlist_search);
                }
                if (control_down && IsKeyPressed(KEY_E)) {
                    playlist_search_cursor_end(&playlist_search);
                }
                if (IsKeyPressed(KEY_HOME)) {
                    playlist_search_cursor_start(&playlist_search);
                }
                if (IsKeyPressed(KEY_END)) {
                    playlist_search_cursor_end(&playlist_search);
                }

                if ((alt_down && IsKeyPressed(KEY_B)) ||
                    (control_down && left_pressed)) {
                    playlist_search_cursor_word_left(&playlist_search);
                } else if (!control_down && !alt_down && left_pressed) {
                    playlist_search_cursor_left(&playlist_search);
                }

                if ((alt_down && IsKeyPressed(KEY_F)) ||
                    (control_down && right_pressed)) {
                    playlist_search_cursor_word_right(&playlist_search);
                } else if (!control_down && !alt_down && right_pressed) {
                    playlist_search_cursor_right(&playlist_search);
                }

                if ((control_down && IsKeyPressed(KEY_W)) ||
                    (alt_down && backspace_pressed) ||
                    (control_down && backspace_pressed)) {
                    if (playlist_search_delete_previous_word(
                            &playlist_search)) {
                        search_changed = true;
                    }
                } else if ((!control_down && !alt_down && backspace_pressed) ||
                           (control_down && IsKeyPressed(KEY_H))) {
                    if (playlist_search_backspace(&playlist_search)) {
                        search_changed = true;
                    }
                }

                if ((control_down && IsKeyPressed(KEY_D)) ||
                    (!control_down && !alt_down && delete_pressed)) {
                    if (playlist_search_delete(&playlist_search)) {
                        search_changed = true;
                    }
                }
                if (control_down && IsKeyPressed(KEY_U)) {
                    if (playlist_search_delete_to_start(&playlist_search)) {
                        search_changed = true;
                    }
                }
                if (control_down && IsKeyPressed(KEY_K)) {
                    if (playlist_search_delete_to_end(&playlist_search)) {
                        search_changed = true;
                    }
                }

                if (!control_down && !alt_down) {
                    for (int codepoint = GetCharPressed(); codepoint > 0;
                         codepoint = GetCharPressed()) {
                        if (codepoint < 0x20 || codepoint == 0x7f) continue;

                        int utf8_size = 0;
                        const char *utf8 =
                            CodepointToUTF8(codepoint, &utf8_size);

                        if (utf8_size > 0 &&
                            playlist_search_append(&playlist_search, utf8,
                                                   (size_t)utf8_size)) {
                            search_changed = true;
                        }
                    }
                }
            }
        } else if (side_panel != SIDE_PANEL_PLAYLIST) {
            playlist_search_focused = false;
        }

        if (search_changed) {
            playlist_search_dirty = true;
            playlist_scroll = 0.0f;
            playlist_scroll_target = 0.0f;
            playlist_text_started_at = GetTime();
        }

        if (playlist_search_dirty) {
            if (!playlist_search_refresh(&playlist_search, &playlist)) {
                mp_log(ERROR, "failed to update playlist search");
                exit_code = 1;
                break;
            }

            playlist_search_dirty = false;
        }

        size_t playlist_view_count =
            playlist_search_count(&playlist_search, &playlist);
        float max_playlist_scroll =
            playlist_view_count > (size_t)layout.visible_playlist_items
                ? (float)(playlist_view_count -
                          (size_t)layout.visible_playlist_items)
                : 0.0f;

        playlist_scroll_target =
            clamp_float(playlist_scroll_target, 0.0f, max_playlist_scroll);
        playlist_scroll =
            clamp_float(playlist_scroll, 0.0f, max_playlist_scroll);

        bool mouse_over_playlist =
            playlist_panel_visible && !open_menu_blocks_mouse &&
            CheckCollisionPointRec(mouse, layout.playlist_panel);
        bool mouse_over_playlist_list =
            playlist_panel_visible && !open_menu_blocks_mouse &&
            CheckCollisionPointRec(mouse, layout.playlist_viewport);
        bool mouse_over_settings =
            settings_panel_visible &&
            CheckCollisionPointRec(mouse, layout.settings_panel);

        if (settings_panel_visible &&
            button_pressed(layout.settings_playlist_side, mouse, true)) {
            playlist_button_on_side = !playlist_button_on_side;
            app_config.playlist_button_on_side = playlist_button_on_side;

            if (config_path_available) {
                app_config_save(config_path, &app_config);
            }

            mp_log(INFO, "playlist button: %s",
                   playlist_button_on_side ? "side" : "controls");
        }

        if (settings_panel_visible &&
            button_pressed(layout.settings_global_media_keys, mouse, true)) {
            app_config.global_media_keys = !app_config.global_media_keys;
            media_keys_set_global(app_config.global_media_keys);

            if (config_path_available) {
                app_config_save(config_path, &app_config);
            }

            mp_log(INFO, "global media keys: %s",
                   app_config.global_media_keys ? "enabled" : "disabled");
        }

        size_t hovered_playlist_track = PLAYLIST_TRACK_NONE;

        if (mouse_over_playlist_list && mouse_wheel != 0.0f) {
            float previous_target = playlist_scroll_target;
            playlist_scroll_target -= mouse_wheel * 2.35f;
            playlist_scroll_target =
                clamp_float(playlist_scroll_target, 0.0f, max_playlist_scroll);

            if (playlist_scroll_target != previous_target) {
                playlist_text_started_at = GetTime();
            }
        }

        playlist_scroll = animate_towards(
            playlist_scroll, playlist_scroll_target, 15.0f, ui_frame_time);

        if (playlist_search_focused && IsKeyPressed(KEY_ENTER) &&
            playlist_view_count > 0) {
            size_t result_index = (size_t)floorf(playlist_scroll_target);
            size_t index =
                playlist_search_track(&playlist_search, result_index);

            if (select_track(&player, &playlist, &playback_order, index,
                             &metadata)) {
                finished_handled = false;
            }
        }

        if (mouse_over_playlist_list && !playlist_toggled) {
            size_t first =
                playlist_scroll > 0.0f ? (size_t)floorf(playlist_scroll) : 0;
            float step = layout.playlist_item_height + layout.playlist_item_gap;
            float scroll_offset = (playlist_scroll - (float)first) * step;

            for (int visible_index = 0;
                 visible_index <= layout.visible_playlist_items;
                 ++visible_index) {
                size_t result_index = first + (size_t)visible_index;

                if (result_index >= playlist_view_count) break;

                size_t index =
                    playlist_search_track(&playlist_search, result_index);

                Rectangle item = ui_layout_playlist_item_bounds(
                    &layout, visible_index, scroll_offset);

                if (!CheckCollisionRecs(item, layout.playlist_viewport) ||
                    !CheckCollisionPointRec(mouse, item)) {
                    continue;
                }

                hovered_playlist_track = index;

                if (button_pressed(item, mouse, true)) {
                    if (select_track(&player, &playlist, &playback_order, index,
                                     &metadata)) {
                        finished_handled = false;
                    }

                    break;
                }

                break;
            }
        }

        if (hovered_playlist_track != playlist_text_track) {
            playlist_text_track = hovered_playlist_track;
            playlist_text_started_at = GetTime();
        }

        if (has_track && !playlist_search_focused && control_down &&
            IsKeyPressed(KEY_S) && !file_picker_busy(&file_picker)) {
            open_menu_visible = false;

            if (!file_picker_start(&file_picker, FILE_PICKER_SAVE_FILE)) {
                mp_log(ERROR, "failed to start the playlist save dialog");
            }
        }

        if (has_track && !playlist_search_focused && control_down &&
            IsKeyPressed(KEY_DELETE)) {
            player_clear(&player);
            playlist_clear(&playlist);
            reset_playback_order(&playback_order, &playlist, shuffle_enabled);
            playlist_scroll = 0.0f;
            playlist_scroll_target = 0.0f;
            playlist_search_dirty = true;
            finished_handled = false;
            seek_dragging = false;
            seek_resume_playback = false;
            metadata = (Track_Metadata){0};
            mp_log(INFO, "playlist cleared");
        } else if (has_track && !playlist_search_focused &&
                   IsKeyPressed(KEY_DELETE) &&
                   playlist_get_count(&playlist) > 0) {
            size_t removed_index = playlist_get_current(&playlist);
            player_clear(&player);
            playlist_remove(&playlist, removed_index);
            playlist_search_dirty = true;
            finished_handled = false;
            seek_dragging = false;
            seek_resume_playback = false;

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

            mp_log(INFO, "removed track from playlist");
        }

        state = player_get_state(&player);
        has_track = state != PLAYER_EMPTY;

        if (!has_track) {
            side_panel = SIDE_PANEL_NONE;
            animated_side_panel = SIDE_PANEL_NONE;
            queued_side_panel = SIDE_PANEL_NONE;
            side_panel_animation = 0.0f;
            playlist_toggle_animation = 0.0f;
            playlist_panel_visible = false;
            settings_panel_visible = false;
            playlist_toggle_visible = false;
            mouse_over_playlist = false;
            mouse_over_settings = false;
            playlist_search_focused = false;
            layout = ui_layout_make(GetScreenWidth(), GetScreenHeight(), 0.0f,
                                    playlist_button_on_side);
        }

        bool sidebar_blocks_mouse =
            mouse_over_playlist || mouse_over_settings ||
            (playlist_toggle_visible &&
             CheckCollisionPointRec(mouse, playlist_toggle_bounds));
        bool controls_enabled =
            has_track && !sidebar_blocks_mouse && !open_menu_blocks_mouse;
        bool repeat_pressed =
            (has_track && !playlist_search_focused && !control_down &&
             IsKeyPressed(KEY_R)) ||
            button_pressed(layout.repeat_button, mouse, controls_enabled);
        bool shuffle_pressed =
            (has_track && !playlist_search_focused && !control_down &&
             IsKeyPressed(KEY_S)) ||
            button_pressed(layout.shuffle_button, mouse, controls_enabled);
        bool playlist_pressed =
            (has_track && !playlist_search_focused && !control_down &&
             IsKeyPressed(KEY_L)) ||
            (!playlist_button_on_side &&
             button_pressed(layout.playlist_button, mouse, controls_enabled));
        bool settings_pressed =
            (has_track && !playlist_search_focused && !control_down &&
             IsKeyPressed(KEY_Q)) ||
            button_pressed(layout.settings_button, mouse, controls_enabled);

        if (playlist_pressed) {
            request_side_panel(SIDE_PANEL_PLAYLIST, &side_panel,
                               &animated_side_panel, &queued_side_panel,
                               side_panel_animation);
            playlist_text_started_at = GetTime();
            open_menu_visible = false;
        }

        if (settings_pressed) {
            request_side_panel(SIDE_PANEL_SETTINGS, &side_panel,
                               &animated_side_panel, &queued_side_panel,
                               side_panel_animation);
            playlist_search_focused = false;
            open_menu_visible = false;
        }

        bool clicked_ui_control =
            CheckCollisionPointRec(mouse, layout.shuffle_button) ||
            CheckCollisionPointRec(mouse, layout.previous_button) ||
            CheckCollisionPointRec(mouse, layout.play_button) ||
            CheckCollisionPointRec(mouse, layout.next_button) ||
            CheckCollisionPointRec(mouse, layout.repeat_button) ||
            (!playlist_button_on_side &&
             CheckCollisionPointRec(mouse, layout.playlist_button)) ||
            CheckCollisionPointRec(mouse, layout.settings_button) ||
            CheckCollisionPointRec(mouse, layout.progress_hitbox) ||
            CheckCollisionPointRec(mouse, playlist_toggle_bounds);

        if (side_panel != SIDE_PANEL_NONE &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            !CheckCollisionPointRec(mouse, layout.playlist_panel) &&
            !clicked_ui_control && !open_menu_blocks_mouse) {
            side_panel = SIDE_PANEL_NONE;
            queued_side_panel = SIDE_PANEL_NONE;
            playlist_search_focused = false;
            open_menu_visible = false;
        }

        if (media_session_event.repeat_requested) {
            switch (media_session_event.repeat) {
            case MEDIA_SESSION_REPEAT_NONE:
                repeat_mode = REPEAT_OFF;
                break;
            case MEDIA_SESSION_REPEAT_TRACK:
                repeat_mode = REPEAT_ONE;
                break;
            case MEDIA_SESSION_REPEAT_PLAYLIST:
                repeat_mode = REPEAT_ALL;
                break;
            }
        } else if (repeat_pressed) {
            switch (repeat_mode) {
            case REPEAT_OFF:
                repeat_mode = REPEAT_ALL;
                mp_log(INFO, "repeat mode: all");
                break;
            case REPEAT_ALL:
                repeat_mode = REPEAT_ONE;
                mp_log(INFO, "repeat mode: one");
                break;
            case REPEAT_ONE:
                repeat_mode = REPEAT_OFF;
                mp_log(INFO, "repeat mode: off");
                break;
            }
        }

        if (shuffle_pressed || media_session_event.shuffle_requested) {
            bool enabled = media_session_event.shuffle_requested
                               ? media_session_event.shuffle
                               : !shuffle_enabled;

            if (!playback_order_set_shuffled(
                    &playback_order, playlist_get_current(&playlist), enabled,
                    random_order_index, NULL)) {
                mp_log(ERROR, "failed to change shuffle mode");
                exit_code = 1;
                break;
            }

            shuffle_enabled = enabled;
            mp_log(INFO, "shuffle: %s", shuffle_enabled ? "on" : "off");
        }

        bool repeat_all = repeat_mode == REPEAT_ALL;
        bool can_previous = has_track && playback_order_can_previous(
                                             &playback_order, repeat_all);
        bool can_next =
            has_track && playback_order_can_next(&playback_order, repeat_all);

        if (!seek_dragging &&
            ((((media_keys & MEDIA_KEY_NEXT) != 0 ||
               (media_session_event.commands & MEDIA_SESSION_COMMAND_NEXT) !=
                   0) &&
              can_next) ||
             (!playlist_search_focused && shift_down &&
              IsKeyPressed(KEY_RIGHT) && can_next) ||
             button_pressed(layout.next_button, mouse,
                            can_next && !sidebar_blocks_mouse))) {
            play_next_track(&player, &playlist, &playback_order, repeat_all,
                            &metadata);
        }

        if (!seek_dragging &&
            ((((media_keys & MEDIA_KEY_PREVIOUS) != 0 ||
               (media_session_event.commands &
                MEDIA_SESSION_COMMAND_PREVIOUS) != 0) &&
              can_previous) ||
             (!playlist_search_focused && shift_down &&
              IsKeyPressed(KEY_LEFT) && can_previous) ||
             button_pressed(layout.previous_button, mouse,
                            can_previous && !sidebar_blocks_mouse))) {
            play_previous_track(&player, &playlist, &playback_order, repeat_all,
                                &metadata);
        }

        state = player_get_state(&player);
        bool session_toggle =
            (media_session_event.commands & MEDIA_SESSION_COMMAND_TOGGLE) !=
                0 ||
            ((media_session_event.commands & MEDIA_SESSION_COMMAND_PLAY) != 0 &&
             state != PLAYER_PLAYING) ||
            ((media_session_event.commands & MEDIA_SESSION_COMMAND_PAUSE) !=
                 0 &&
             state == PLAYER_PLAYING);
        bool toggle_requested =
            has_track && !seek_dragging &&
            (session_toggle || ((media_keys & MEDIA_KEY_PLAY_PAUSE) != 0) ||
             (!playlist_search_focused &&
              (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_P))) ||
             button_pressed(layout.play_button, mouse, !sidebar_blocks_mouse));

        if (toggle_requested && !player_toggle(&player)) {
            exit_code = 1;
            break;
        }

        bool stop_requested =
            (media_keys & MEDIA_KEY_STOP) != 0 ||
            (media_session_event.commands & MEDIA_SESSION_COMMAND_STOP) != 0;

        if (has_track && !seek_dragging && stop_requested &&
            !player_stop(&player)) {
            exit_code = 1;
            break;
        }

        float volume_change =
            has_track && !sidebar_blocks_mouse ? mouse_wheel * 0.05f : 0.0f;

        if (has_track && !playlist_search_focused && IsKeyPressed(KEY_UP)) {
            volume_change += 0.05f;
        }
        if (has_track && !playlist_search_focused && IsKeyPressed(KEY_DOWN)) {
            volume_change -= 0.05f;
        }
        if (has_track && (media_keys & MEDIA_KEY_VOLUME_UP) != 0) {
            volume_change += 0.05f;
        }
        if (has_track && (media_keys & MEDIA_KEY_VOLUME_DOWN) != 0) {
            volume_change -= 0.05f;
        }

        if (volume_change != 0.0f) {
            player_adjust_volume(&player, volume_change);
        }

        if (has_track && media_session_event.volume_requested) {
            player_set_volume(&player, (float)media_session_event.volume);
        }

        float volume_padding = snap_pixel(4.0f * layout.scale);
        int volume_slot_width =
            ui_font_measure("Volume 100%", layout.status_size);
        Rectangle volume_bounds = snap_rectangle((Rectangle){
            layout.progress_bar.x + layout.progress_bar.width -
                (float)volume_slot_width - volume_padding,
            layout.metadata_y - volume_padding,
            (float)volume_slot_width + volume_padding * 2.0f,
            (float)layout.status_size + volume_padding * 2.0f,
        });

        if ((has_track && (media_keys & MEDIA_KEY_MUTE) != 0) ||
            (has_track && !playlist_search_focused && IsKeyPressed(KEY_M)) ||
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
                             ui_font_measure(volume_text, layout.status_size));
        bool volume_hovered = has_track && !sidebar_blocks_mouse &&
                              CheckCollisionPointRec(mouse, volume_bounds);

        float cursor = player_get_cursor(&player);
        float length = player_get_length(&player);
        float keyboard_seek = 0.0f;

        if (has_track && !seek_dragging && media_session_event.seek_requested) {
            float requested_position = (float)media_session_event.seek_position;

            if (requested_position < 0.0f) requested_position = 0.0f;
            if (length > 0.0f && requested_position > length) {
                requested_position = length;
            }

            if (!player_seek(&player, requested_position)) {
                exit_code = 1;
                break;
            }
            cursor = requested_position;
        }

        if (has_track && !playlist_search_focused && !seek_dragging &&
            !control_down && IsKeyPressed(KEY_X)) {
            if (!player_seek(&player, 0.0f)) {
                exit_code = 1;
                break;
            }

            cursor = 0.0f;
        }

        if (has_track && !playlist_search_focused && !seek_dragging &&
            !shift_down) {
            if (IsKeyPressed(KEY_LEFT)) keyboard_seek -= SEEK_STEP_SECONDS;
            if (IsKeyPressed(KEY_RIGHT)) keyboard_seek += SEEK_STEP_SECONDS;
        }

        if (keyboard_seek != 0.0f) {
            cursor += keyboard_seek;

            if (cursor < 0.0f) cursor = 0.0f;
            if (length > 0.0f && cursor > length) cursor = length;

            if (!player_seek(&player, cursor)) {
                exit_code = 1;
                break;
            }
        }

        bool progress_hovered =
            has_track && !sidebar_blocks_mouse &&
            CheckCollisionPointRec(mouse, layout.progress_hitbox);

        if (progress_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            seek_resume_playback = player_get_state(&player) == PLAYER_PLAYING;

            if (seek_resume_playback && !player_toggle(&player)) {
                exit_code = 1;
                break;
            }

            seek_dragging = true;
        }

        bool seek_finished =
            seek_dragging && !IsMouseButtonDown(MOUSE_BUTTON_LEFT);

        if (seek_dragging && length > 0.0f) {
            float progress =
                (mouse.x - layout.progress_bar.x) / layout.progress_bar.width;

            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;

            cursor = length * progress;
        }

        if (seek_finished) {
            seek_dragging = false;

            if (length > 0.0f && !player_seek(&player, cursor)) {
                exit_code = 1;
                break;
            }

            if (seek_resume_playback && !player_toggle(&player)) {
                exit_code = 1;
                break;
            }

            seek_resume_playback = false;
        }

        state = player_get_state(&player);
        has_track = state != PLAYER_EMPTY;
        const char *album_art_path =
            !has_track
                ? NULL
                : playlist_get(&playlist, playlist_get_current(&playlist));
        album_art_update(&album_art, album_art_path);
        album_art_advance(&album_art, ui_frame_time);
        bool session_repeat_all = repeat_mode == REPEAT_ALL;
        bool session_can_previous =
            has_track &&
            playback_order_can_previous(&playback_order, session_repeat_all);
        bool session_can_next =
            has_track &&
            playback_order_can_next(&playback_order, session_repeat_all);
        Media_Session_Playback session_playback = MEDIA_SESSION_EMPTY;

        if (state == PLAYER_PLAYING) {
            session_playback = MEDIA_SESSION_PLAYING;
        } else if (state == PLAYER_PAUSED) {
            session_playback = MEDIA_SESSION_PAUSED;
        } else if (has_track) {
            session_playback = MEDIA_SESSION_STOPPED;
        }

        Media_Session_Repeat session_repeat = MEDIA_SESSION_REPEAT_NONE;

        if (repeat_mode == REPEAT_ONE) {
            session_repeat = MEDIA_SESSION_REPEAT_TRACK;
        } else if (repeat_mode == REPEAT_ALL) {
            session_repeat = MEDIA_SESSION_REPEAT_PLAYLIST;
        }

        media_session_update(
            &media_session, album_art_path, has_track ? &metadata : NULL,
            session_playback, player_get_cursor(&player),
            player_get_length(&player),
            player_is_muted(&player) ? 0.0 : player_get_volume(&player),
            session_can_previous, session_can_next, shuffle_enabled,
            session_repeat);
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
            status = "Stopped";
            break;
        case PLAYER_EMPTY:
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
                            ui_font_measure(time_text, layout.status_size)) /
                               2.0f);

        char playlist_text[64];

        snprintf(playlist_text, sizeof(playlist_text), "%zu | %zu",
                 playlist_get_current(&playlist) + 1,
                 playlist_get_count(&playlist));

        int playlist_x = status_x +
                         ui_font_measure(status, layout.status_size) +
                         (int)snap_pixel(18.0f * layout.scale);

        char details_text[METADATA_TEXT_SIZE * 2 + 8];
        format_track_details(&metadata, details_text, sizeof(details_text));

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

        float current_title_right = (float)layout.width - layout.title_x;

        if (playlist_panel_visible || settings_panel_visible) {
            current_title_right = layout.playlist_panel.x - layout.title_x;
        }

        Rectangle current_title_bounds = snap_rectangle((Rectangle){
            layout.title_x,
            layout.title_y,
            current_title_right - layout.title_x,
            (float)layout.title_size,
        });

        if (current_title_bounds.width < 0.0f) {
            current_title_bounds.width = 0.0f;
        }

        bool current_title_hovered =
            has_track && !sidebar_blocks_mouse &&
            CheckCollisionPointRec(mouse, current_title_bounds);
        size_t hovered_current_title = current_title_hovered
                                           ? playlist_get_current(&playlist)
                                           : PLAYLIST_TRACK_NONE;

        if (hovered_current_title != current_title_text_track) {
            current_title_text_track = hovered_current_title;
            current_title_text_started_at = GetTime();
        }

        size_t bar_count = spectrum_bar_count(layout.spectrum, layout.scale);
        bool spectrum_paused = GetTime() < spectrum_resume_at;

        if (has_track && bar_count > 0) {
            if (spectrum_paused) {
                spectrum_decay(&spectrum, GetFrameTime());
            } else {
                float samples[SPECTRUM_SAMPLE_COUNT];
                unsigned int sample_rate = 0;
                size_t copied = player_copy_analysis_samples(
                    &player, samples, SPECTRUM_SAMPLE_COUNT, &sample_rate);

                if (copied == SPECTRUM_SAMPLE_COUNT) {
                    spectrum_update(&spectrum, samples, sample_rate, bar_count,
                                    GetFrameTime());
                }
            }
        } else {
            spectrum_reset(&spectrum);
        }

        const char *drop_title = "Drag & drop music here";
        const char *drop_hint = "or click to open files or a folder";
        int drop_title_size = ui_font_crisp_size(40.0f * layout.scale, 30, 60);
        int drop_hint_size = ui_font_crisp_size(20.0f * layout.scale, 16, 28);
        bool fonts_ready = ui_font_collect(metadata.title, layout.title_size) &&
                           ui_font_collect(details_text, layout.status_size);

        if (fonts_ready && !has_track) {
            fonts_ready = ui_font_collect(drop_title, drop_title_size) &&
                          ui_font_collect(drop_hint, drop_hint_size);
        }

        if (fonts_ready && playlist_panel_visible) {
            fonts_ready = collect_playlist_font_text(
                &playlist, &playlist_search, &layout, playlist_scroll);
        }

        if (!fonts_ready || !ui_font_rebuild()) {
            mp_log(ERROR, "failed to update Noto Sans glyphs");
            exit_code = 1;
            break;
        }

        BeginDrawing();
        ClearBackground(theme->background);

        if (!has_track) {
            float icon_size =
                snap_pixel(clamp_float(150.0f * layout.scale, 104.0f, 190.0f));
            float icon_gap = snap_pixel(24.0f * layout.scale);
            float hint_gap = snap_pixel(14.0f * layout.scale);
            float group_height = icon_size + icon_gap + (float)drop_title_size +
                                 hint_gap + (float)drop_hint_size;
            float group_y =
                snap_pixel(((float)layout.height - group_height) / 2.0f);
            Rectangle icon_bounds = snap_rectangle((Rectangle){
                ((float)layout.width - icon_size) / 2.0f,
                group_y,
                icon_size,
                icon_size,
            });
            int drop_title_x =
                (layout.width - ui_font_measure(drop_title, drop_title_size)) /
                2;
            int drop_title_y = (int)snap_pixel(group_y + icon_size + icon_gap);
            int drop_hint_x =
                (layout.width - ui_font_measure(drop_hint, drop_hint_size)) / 2;
            int drop_hint_y = (int)snap_pixel((float)drop_title_y +
                                              drop_title_size + hint_gap);

            ui_application_icon_draw(application_icon, icon_bounds);
            ui_font_draw(drop_title, drop_title_x, drop_title_y,
                         drop_title_size, theme->text_primary);
            ui_font_draw(drop_hint, drop_hint_x, drop_hint_y, drop_hint_size,
                         theme->text_muted);
        } else {
            album_art_draw(&album_art, layout.album_art, theme->background);
            draw_spectrum(&spectrum, layout.spectrum, bar_count, layout.scale,
                          theme);
            draw_scrolling_text(metadata.title, current_title_bounds,
                                layout.title_size, layout.scale,
                                theme->text_primary, current_title_hovered,
                                current_title_text_started_at, NULL);
            ui_font_draw(details_text, (int)layout.title_x,
                         (int)layout.details_y, layout.status_size,
                         theme->text_muted);
            ui_font_draw(status, status_x, (int)layout.metadata_y,
                         layout.status_size, theme->text_secondary);
            ui_font_draw(playlist_text, playlist_x, (int)layout.metadata_y,
                         layout.status_size, theme->text_faint);
            ui_font_draw(time_text, time_x, (int)layout.metadata_y,
                         layout.status_size, theme->text_muted);
            ui_font_draw(volume_text, volume_x, (int)layout.metadata_y,
                         layout.status_size,
                         volume_hovered ? theme->text_secondary
                                        : theme->text_muted);

            DrawRectangleRec(layout.progress_bar, theme->progress_background);
            DrawRectangleRec(progress_fill, progress_hovered
                                                ? theme->progress_hover
                                                : theme->progress_foreground);
            float handle_scale = progress_hovered ? 14.0f : 10.0f;
            float handle_size = snap_pixel(handle_scale * layout.scale);
            Color handle_color = progress_hovered ? theme->progress_hover
                                                  : theme->progress_foreground;
            DrawRectangleRec(snap_rectangle((Rectangle){
                                 progress_handle.x - handle_size / 2.0f,
                                 progress_handle.y - handle_size / 2.0f,
                                 handle_size,
                                 handle_size,
                             }),
                             handle_color);

            draw_button(layout.shuffle_button, UI_ICON_SHUFFLE,
                        &icon_transition, mouse, true, shuffle_enabled, theme);
            draw_button(layout.previous_button, UI_ICON_PREVIOUS,
                        &icon_transition, mouse, can_previous, false, theme);
            draw_button(layout.play_button,
                        state == PLAYER_PLAYING ? UI_ICON_PAUSE : UI_ICON_PLAY,
                        &icon_transition, mouse, true, false, theme);
            draw_button(layout.next_button, UI_ICON_NEXT, &icon_transition,
                        mouse, can_next, false, theme);
            draw_button(layout.repeat_button,
                        repeat_mode == REPEAT_ONE ? UI_ICON_REPEAT_ONE
                                                  : UI_ICON_REPEAT,
                        &icon_transition, mouse, true,
                        repeat_mode != REPEAT_OFF, theme);
            if (!playlist_button_on_side) {
                draw_button(layout.playlist_button, UI_ICON_PLAYLIST,
                            &icon_transition, mouse, true,
                            side_panel == SIDE_PANEL_PLAYLIST, theme);
            }
            draw_button(layout.settings_button, UI_ICON_SETTINGS,
                        &icon_transition, mouse, true,
                        side_panel == SIDE_PANEL_SETTINGS, theme);
        }

        if (playlist_panel_visible) {
            draw_playlist_panel(
                &playlist, &playlist_search, &layout, playlist_scroll,
                hovered_playlist_track, playlist_text_started_at,
                playlist_search_focused, mouse, picker_busy, theme);
        }

        if (settings_panel_visible) {
            draw_settings_panel(&layout, playlist_button_on_side,
                                app_config.global_media_keys, mouse, theme);
        }

        if (playlist_toggle_visible) {
            draw_playlist_toggle(
                playlist_toggle_bounds, side_panel != SIDE_PANEL_NONE,
                &icon_transition, mouse, playlist_toggle_animation, theme);
        }

        if (open_menu_visible) {
            draw_open_menu(&open_menu, mouse, layout.scale, theme);
        }

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
    media_session_uninit(&media_session);
    ui_icon_transition_uninit(&icon_transition);
    UnloadTexture(application_icon);
    ui_font_uninit();
    media_keys_uninit();
    file_picker_uninit(&file_picker);
    system_theme_monitor_uninit(&system_theme_monitor);
    CloseWindow();

    playlist_search_uninit(&playlist_search);
    playlist_uninit(&playlist);
    playback_order_uninit(&playback_order);
    player_uninit(&player);

    return exit_code;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    (void)argc;
    (void)argv;

    if (!fs_windows_command_line(&argc, &argv)) return 1;

    int result = mp_main(argc, argv);
    fs_windows_command_line_free(argc, argv);
    return result;
#else
    return mp_main(argc, argv);
#endif
}
