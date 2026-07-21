#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "config.h"
#include "fs.h"
#include "font_renderer.h"
#include "log.h"
#include "m3u.h"
#include "metadata.h"
#include "playback_order.h"
#include "player.h"
#include "playlist.h"
#include "playlist_search.h"
#include "raylib.h"
#include "session.h"
#include "spectrum.h"
#include "svg.h"
#include "theme.h"
#include "tinyfiledialogs/tinyfiledialogs.h"
#include "worker_thread.h"

#define CONFIG_PATH_SIZE 4096
#define MP_VERSION "0.2.0"
#define SIDE_PANEL_ANIMATION_SPEED 14.0f
#define PLAYLIST_TRACK_NONE ((size_t)-1)
#define PLAYLIST_TOGGLE_ANIMATION_SPEED 18.0f
#define SEEK_STEP_SECONDS 5.0f
#define SESSION_PATH_SIZE 4096
#define SPECTRUM_DISPLAY_MAX_BARS 64
#define SPECTRUM_RESIZE_SETTLE_SECONDS 0.15
#define THEME_TRANSITION_SECONDS 0.5f
#define UI_BUTTON_ICON_SIZE 72
#define UI_TOGGLE_ICON_SIZE 48
#define WINDOW_HEIGHT 700
#define WINDOW_MIN_HEIGHT 480
#define WINDOW_MIN_WIDTH 640
#define WINDOW_WIDTH 1000

typedef enum {
    FONT_FACE_BASE,
    FONT_FACE_JP,
    FONT_FACE_KR,
    FONT_FACE_TC,
    FONT_FACE_COUNT,
} Font_Face;

typedef struct {
    int *values;
    int count;
    int capacity;
} Font_Codepoints;

static const int font_sizes[] = {14, 16, 18, 20, 22,
                                 24, 30, 40, 50, 60};
#define FONT_COUNT ((int)(sizeof(font_sizes) / sizeof(font_sizes[0])))

static Font fonts[FONT_COUNT];
static Font_Renderer font_renderer;
static Font_Codepoints font_codepoints[FONT_COUNT][FONT_FACE_COUNT];
static Font_Codepoints font_unsupported[FONT_COUNT];
static bool font_dirty[FONT_COUNT];

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

    if (player_get_state(player) == PLAYER_STOPPED) {
        for (size_t i = first_new_track; i < playlist_get_count(playlist); ++i) {
            if (play_track(player, playlist, i, metadata)) {
                *finished_handled = false;
                break;
            }
        }
    }

    return reset_playback_order(order, playlist, shuffled);
}

static bool add_file_picker_selection(
    Player *player, Playlist *playlist, Playback_Order *order, bool shuffled,
    char *selection, Track_Metadata *metadata, bool *finished_handled,
    size_t *added)
{
    size_t length = strlen(selection);
    size_t selected_count = 1;

    for (size_t i = 0; i < length; ++i) {
        if (selection[i] == '|') ++selected_count;
    }

    if (selected_count > SIZE_MAX / sizeof(char *)) return false;

    char **selected_paths =
        malloc(selected_count * sizeof(*selected_paths));

    if (selected_paths == NULL) return false;

    size_t path_index = 0;
    selected_paths[path_index++] = selection;

    for (size_t i = 0; i < length; ++i) {
        if (selection[i] != '|') continue;

        selection[i] = '\0';
        selected_paths[path_index++] = selection + i + 1;
    }

    bool result = add_inputs_to_player(
        player, playlist, order, shuffled,
        (const char *const *)selected_paths, selected_count, metadata,
        finished_handled, added);
    free(selected_paths);
    return result;
}

static bool save_playlist_to_path(const Playlist *playlist,
                                  const char *selected_path, char *saved_path,
                                  size_t saved_path_size)
{
    if (selected_path[0] == '\0') return false;

    const char *extension = m3u_is_path(selected_path) ? "" : ".m3u";
    int written = snprintf(saved_path, saved_path_size, "%s%s", selected_path,
                           extension);

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
    BUTTON_PREVIOUS,
    BUTTON_PLAY,
    BUTTON_PAUSE,
    BUTTON_NEXT,
    BUTTON_REPEAT,
    BUTTON_REPEAT_ONE,
    BUTTON_SHUFFLE,
    BUTTON_PLAYLIST,
    BUTTON_SETTINGS,
} Button_Icon;

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
                               Side_Panel *queued_side_panel,
                               float animation)
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

typedef enum {
    FILE_PICKER_NONE,
    FILE_PICKER_FILES,
    FILE_PICKER_FOLDER,
    FILE_PICKER_SAVE_FILE,
} File_Picker_Mode;

typedef enum {
    FILE_PICKER_IDLE,
    FILE_PICKER_RUNNING,
    FILE_PICKER_COMPLETE,
} File_Picker_State;

typedef struct {
    Worker_Thread thread;
    atomic_int state;
    File_Picker_Mode mode;
    char *selection;
    bool graphical;
    bool failed;
} File_Picker;

static int file_picker_run(void *context)
{
    File_Picker *picker = context;
    char *selection = NULL;

    if (picker->mode == FILE_PICKER_FILES) {
        const char *filters[] = {
            "*.flac", "*.FLAC", "*.mp3",  "*.MP3", "*.wav",
            "*.WAV",  "*.m3u",  "*.M3U", "*.m3u8", "*.M3U8",
        };
        int filter_count = (int)(sizeof(filters) / sizeof(filters[0]));

        picker->graphical =
            tinyfd_openFileDialog("tinyfd_query", "", filter_count, filters,
                                  "Audio and playlist files", 1) != NULL;

        if (picker->graphical) {
            selection = tinyfd_openFileDialog(
                "Open audio files", "", filter_count, filters,
                "Audio and playlist files", 1);
        }
    } else if (picker->mode == FILE_PICKER_FOLDER) {
        picker->graphical =
            tinyfd_selectFolderDialog("tinyfd_query", "") != NULL;

        if (picker->graphical) {
            selection = tinyfd_selectFolderDialog("Open music folder", "");
        }
    } else if (picker->mode == FILE_PICKER_SAVE_FILE) {
        const char *filters[] = {"*.m3u", "*.m3u8"};
        int filter_count = (int)(sizeof(filters) / sizeof(filters[0]));

        picker->graphical =
            tinyfd_saveFileDialog("tinyfd_query", "playlist.m3u",
                                  filter_count, filters,
                                  "Playlist files") != NULL;

        if (picker->graphical) {
            selection = tinyfd_saveFileDialog(
                "Save playlist", "playlist.m3u", filter_count, filters,
                "Playlist files");
        }
    }

    if (selection != NULL) {
        size_t length = strlen(selection) + 1;
        picker->selection = malloc(length);

        if (picker->selection != NULL) {
            memcpy(picker->selection, selection, length);
        } else {
            picker->failed = true;
        }
    }

    atomic_store_explicit(&picker->state, FILE_PICKER_COMPLETE,
                          memory_order_release);
    return 0;
}

static void file_picker_init(File_Picker *picker)
{
    *picker = (File_Picker){0};
    atomic_init(&picker->state, FILE_PICKER_IDLE);
}

static bool file_picker_start(File_Picker *picker, File_Picker_Mode mode)
{
    if (mode == FILE_PICKER_NONE ||
        atomic_load_explicit(&picker->state, memory_order_acquire) !=
            FILE_PICKER_IDLE) {
        return false;
    }

    picker->mode = mode;
    picker->selection = NULL;
    picker->graphical = false;
    picker->failed = false;
    atomic_store_explicit(&picker->state, FILE_PICKER_RUNNING,
                          memory_order_release);

    if (!worker_thread_start(&picker->thread, file_picker_run, picker)) {
        atomic_store_explicit(&picker->state, FILE_PICKER_IDLE,
                              memory_order_release);
        picker->mode = FILE_PICKER_NONE;
        return false;
    }

    return true;
}

static bool file_picker_busy(const File_Picker *picker)
{
    return atomic_load_explicit(&picker->state, memory_order_acquire) !=
           FILE_PICKER_IDLE;
}

static bool file_picker_take(File_Picker *picker, char **selection,
                             File_Picker_Mode *mode, bool *graphical,
                             bool *failed)
{
    if (atomic_load_explicit(&picker->state, memory_order_acquire) !=
        FILE_PICKER_COMPLETE) {
        return false;
    }

    worker_thread_join(&picker->thread);
    *selection = picker->selection;
    *mode = picker->mode;
    *graphical = picker->graphical;
    *failed = picker->failed;
    picker->selection = NULL;
    picker->mode = FILE_PICKER_NONE;
    atomic_store_explicit(&picker->state, FILE_PICKER_IDLE,
                          memory_order_release);
    return true;
}

static void file_picker_uninit(File_Picker *picker)
{
    if (atomic_load_explicit(&picker->state, memory_order_acquire) !=
        FILE_PICKER_IDLE) {
        worker_thread_join(&picker->thread);
    }

    free(picker->selection);
}

typedef struct {
    Texture2D back;
    Texture2D forward;
    Texture2D pause;
    Texture2D play;
    Texture2D repeat;
    Texture2D repeat_one;
    Texture2D shuffle;
    Texture2D playlist;
    Texture2D settings;
    Texture2D playlist_back;
    Texture2D playlist_forward;
} Ui_Icons;

typedef struct {
    Ui_Icons current;
    Ui_Icons next;
    System_Theme current_theme;
    System_Theme target_theme;
    float progress;
    bool active;
} Ui_Icon_Transition;

typedef struct {
    Color background;
    Color surface;
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

static const Ui_Theme *ui_theme_for(System_Theme theme)
{
    return &ui_themes[theme == SYSTEM_THEME_DARK ? SYSTEM_THEME_DARK
                                                  : SYSTEM_THEME_LIGHT];
}

typedef struct {
    Ui_Theme current;
    Ui_Theme start;
    Ui_Theme target;
    float progress;
    bool active;
} Ui_Theme_Transition;

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
#define BLEND_THEME_COLOR(field)                                             \
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

static void begin_theme_transition(Ui_Theme_Transition *transition,
                                   System_Theme target)
{
    transition->start = transition->current;
    transition->target = *ui_theme_for(target);
    transition->progress = 0.0f;
    transition->active = true;
}

static void update_theme_transition(Ui_Theme_Transition *transition,
                                    float delta_time)
{
    if (!transition->active) return;

    transition->progress += delta_time / THEME_TRANSITION_SECONDS;

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
    int playlist_header_size;
    int playlist_title_size;
    int playlist_details_size;
    int playlist_search_size;
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
    Rectangle playlist_button;
    Rectangle settings_button;
    Rectangle progress_bar;
    Rectangle progress_hitbox;
    Rectangle album_art;
    Rectangle spectrum;
    Rectangle playlist_panel;
    Rectangle playlist_viewport;
    Rectangle playlist_search;
    Rectangle playlist_open;
    Rectangle settings_panel;
    Rectangle settings_playlist_side;
    Rectangle playlist_toggle;
    Rectangle playlist_toggle_reveal;
} Ui_Layout;

typedef struct {
    Rectangle panel;
    Rectangle files;
    Rectangle folder;
} Open_Menu_Layout;

static void unload_ui_icons(Ui_Icons *icons)
{
    Texture2D *textures[] = {
        &icons->back,     &icons->forward,       &icons->pause,
        &icons->play,     &icons->repeat,        &icons->repeat_one,
        &icons->shuffle,  &icons->playlist,      &icons->settings,
        &icons->playlist_back, &icons->playlist_forward,
    };

    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        if (IsTextureValid(*textures[i])) UnloadTexture(*textures[i]);
    }

    *icons = (Ui_Icons){0};
}

static bool load_application_icon(Texture2D *texture)
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

static int font_index_for_size(int font_size)
{
    int index = 0;
    int distance = abs(font_sizes[0] - font_size);

    for (int i = 1; i < FONT_COUNT; ++i) {
        int candidate_distance = abs(font_sizes[i] - font_size);

        if (candidate_distance < distance) {
            index = i;
            distance = candidate_distance;
        }
    }

    return index;
}

static bool font_codepoints_add(Font_Codepoints *set, int codepoint)
{
    for (int i = 0; i < set->count; ++i) {
        if (set->values[i] == codepoint) return true;
    }

    if (set->count == set->capacity) {
        int capacity = set->capacity == 0 ? 64 : set->capacity * 2;

        if (capacity < set->capacity ||
            (size_t)capacity > SIZE_MAX / sizeof(*set->values)) {
            return false;
        }

        int *values = realloc(set->values,
                              (size_t)capacity * sizeof(*set->values));

        if (values == NULL) return false;

        set->values = values;
        set->capacity = capacity;
    }

    set->values[set->count++] = codepoint;
    return true;
}

static bool font_codepoints_contains(const Font_Codepoints *set,
                                     int codepoint)
{
    for (int i = 0; i < set->count; ++i) {
        if (set->values[i] == codepoint) return true;
    }

    return false;
}

static Embedded_Asset font_asset(Font_Face face)
{
    static const Asset_Id ids[FONT_FACE_COUNT] = {
        [FONT_FACE_BASE] = ASSET_NOTO_SANS_TTF,
        [FONT_FACE_JP] = ASSET_NOTO_SANS_JP_TTF,
        [FONT_FACE_KR] = ASSET_NOTO_SANS_KR_TTF,
        [FONT_FACE_TC] = ASSET_NOTO_SANS_TC_TTF,
    };

    return asset_get(ids[face]);
}

static void unload_fonts(void)
{
    for (int i = 0; i < FONT_COUNT; ++i) {
        if (IsFontValid(fonts[i])) UnloadFont(fonts[i]);
        fonts[i] = (Font){0};
        font_dirty[i] = false;

        for (int face = 0; face < FONT_FACE_COUNT; ++face) {
            free(font_codepoints[i][face].values);
            font_codepoints[i][face] = (Font_Codepoints){0};
        }

        free(font_unsupported[i].values);
        font_unsupported[i] = (Font_Codepoints){0};
    }

    font_renderer_uninit(&font_renderer);
}

static bool load_font_renderer(void)
{
    const unsigned char *data[FONT_FACE_COUNT] = {0};
    size_t sizes[FONT_FACE_COUNT] = {0};

    for (int face = 0; face < FONT_FACE_COUNT; ++face) {
        Embedded_Asset asset = font_asset((Font_Face)face);
        data[face] = asset.data;
        sizes[face] = asset.size;
    }

    return font_renderer_init(&font_renderer, data, sizes, FONT_FACE_COUNT);
}

static bool rebuild_font(int index)
{
    GlyphInfo *parts[FONT_FACE_COUNT] = {0};
    int part_counts[FONT_FACE_COUNT] = {0};
    int total = 0;
    int font_size = font_sizes[index];

    for (int face = 0; face < FONT_FACE_COUNT; ++face) {
        Font_Codepoints *codepoints = &font_codepoints[index][face];

        if (codepoints->count == 0) continue;

        Embedded_Asset asset = font_asset((Font_Face)face);

        if (asset.data == NULL || asset.size > INT_MAX) goto failure;

        parts[face] = font_renderer_load(
            &font_renderer, face, font_size, codepoints->values,
            codepoints->count);
        part_counts[face] = parts[face] == NULL ? 0 : codepoints->count;

        if (parts[face] == NULL ||
            part_counts[face] != codepoints->count ||
            part_counts[face] > INT_MAX - total) {
            goto failure;
        }

        total += part_counts[face];
    }

    Font replacement = {
        .baseSize = font_size,
        .glyphCount = total,
        .glyphPadding = 4,
    };
    replacement.glyphs =
        MemAlloc((unsigned int)((size_t)total * sizeof(*replacement.glyphs)));

    if (replacement.glyphs == NULL) goto failure;

    int offset = 0;

    for (int face = 0; face < FONT_FACE_COUNT; ++face) {
        if (part_counts[face] == 0) continue;

        memcpy(replacement.glyphs + offset, parts[face],
               (size_t)part_counts[face] * sizeof(*parts[face]));
        offset += part_counts[face];
        MemFree(parts[face]);
        parts[face] = NULL;
    }

    Image atlas = GenImageFontAtlas(
        replacement.glyphs, &replacement.recs, replacement.glyphCount,
        replacement.baseSize, replacement.glyphPadding, 0);

    if (!IsImageValid(atlas) || replacement.recs == NULL) {
        if (IsImageValid(atlas)) UnloadImage(atlas);
        UnloadFontData(replacement.glyphs, replacement.glyphCount);
        if (replacement.recs != NULL) MemFree(replacement.recs);
        return false;
    }

    replacement.texture = LoadTextureFromImage(atlas);
    UnloadImage(atlas);

    if (!IsTextureValid(replacement.texture)) {
        UnloadFontData(replacement.glyphs, replacement.glyphCount);
        MemFree(replacement.recs);
        return false;
    }

    SetTextureFilter(replacement.texture, TEXTURE_FILTER_POINT);

    if (IsFontValid(fonts[index])) UnloadFont(fonts[index]);
    fonts[index] = replacement;
    return true;

failure:
    for (int face = 0; face < FONT_FACE_COUNT; ++face) {
        if (parts[face] != NULL) {
            UnloadFontData(parts[face], part_counts[face]);
        }
    }

    return false;
}

static bool japanese_codepoint(int codepoint)
{
    return (codepoint >= 0x3040 && codepoint <= 0x30ff) ||
           (codepoint >= 0x31f0 && codepoint <= 0x31ff);
}

static bool korean_codepoint(int codepoint)
{
    return (codepoint >= 0x1100 && codepoint <= 0x11ff) ||
           (codepoint >= 0x3130 && codepoint <= 0x318f) ||
           (codepoint >= 0xa960 && codepoint <= 0xa97f) ||
           (codepoint >= 0xac00 && codepoint <= 0xd7ff);
}

static bool traditional_chinese_codepoint(int codepoint)
{
    return (codepoint >= 0x3100 && codepoint <= 0x312f) ||
           (codepoint >= 0x31a0 && codepoint <= 0x31bf);
}

static bool shared_cjk_codepoint(int codepoint)
{
    return (codepoint >= 0x3000 && codepoint <= 0x303f) ||
           (codepoint >= 0x3400 && codepoint <= 0x4dbf) ||
           (codepoint >= 0x4e00 && codepoint <= 0x9fff) ||
           (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
           (codepoint >= 0xff00 && codepoint <= 0xffef) ||
           (codepoint >= 0x20000 && codepoint <= 0x2fa1f);
}

static Font_Face text_cjk_face(const char *text)
{
    bool has_korean = false;
    bool has_traditional_chinese = false;

    for (const char *cursor = text; *cursor != '\0';) {
        int bytes = 0;
        int codepoint = GetCodepointNext(cursor, &bytes);

        if (bytes <= 0) bytes = 1;
        cursor += bytes;

        if (japanese_codepoint(codepoint)) return FONT_FACE_JP;
        if (korean_codepoint(codepoint)) has_korean = true;
        if (traditional_chinese_codepoint(codepoint)) {
            has_traditional_chinese = true;
        }
    }

    if (has_korean) return FONT_FACE_KR;
    if (has_traditional_chinese) return FONT_FACE_TC;
    return FONT_FACE_TC;
}

static Font_Face codepoint_face(int codepoint, Font_Face cjk_face)
{
    if (japanese_codepoint(codepoint)) return FONT_FACE_JP;
    if (korean_codepoint(codepoint)) return FONT_FACE_KR;
    if (traditional_chinese_codepoint(codepoint)) return FONT_FACE_TC;
    if (shared_cjk_codepoint(codepoint)) return cjk_face;
    return FONT_FACE_BASE;
}

static bool available_codepoint_face(int codepoint, Font_Face cjk_face,
                                     Font_Face *available)
{
    Font_Face preferred = codepoint_face(codepoint, cjk_face);
    Font_Face order[] = {
        preferred,
        FONT_FACE_BASE,
        FONT_FACE_JP,
        FONT_FACE_KR,
        FONT_FACE_TC,
    };

    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        bool duplicate = false;

        for (size_t previous = 0; previous < i; ++previous) {
            if (order[previous] == order[i]) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate &&
            font_renderer_has(&font_renderer, order[i], codepoint)) {
            *available = order[i];
            return true;
        }
    }

    return false;
}

static bool collect_font_text(const char *text, int font_size)
{
    if (text == NULL || text[0] == '\0') return true;

    int index = font_index_for_size(font_size);
    Font_Face cjk_face = text_cjk_face(text);

    for (const char *cursor = text; *cursor != '\0';) {
        int bytes = 0;
        int codepoint = GetCodepointNext(cursor, &bytes);

        if (bytes <= 0) bytes = 1;
        cursor += bytes;

        bool known = false;

        for (int face = 0; face < FONT_FACE_COUNT; ++face) {
            if (font_codepoints_contains(&font_codepoints[index][face],
                                         codepoint)) {
                known = true;
                break;
            }
        }

        if (known) continue;

        if (font_codepoints_contains(&font_unsupported[index], codepoint)) {
            continue;
        }

        Font_Face face;

        if (!available_codepoint_face(codepoint, cjk_face, &face)) {
            if (!font_codepoints_add(&font_unsupported[index], codepoint)) {
                return false;
            }

            continue;
        }

        if (!font_codepoints_add(&font_codepoints[index][face], codepoint)) {
            return false;
        }

        font_dirty[index] = true;
    }

    return true;
}

static bool rebuild_dirty_fonts(void)
{
    bool success = true;

    for (int i = 0; i < FONT_COUNT; ++i) {
        if (!font_dirty[i]) continue;

        if (!rebuild_font(i)) success = false;
        font_dirty[i] = false;
    }

    return success;
}

static bool load_fonts(void)
{
    const int ranges[][2] = {
        {0x0020, 0x007e},
        {0x00a0, 0x017f},
        {0x0400, 0x0486},
        {0x0488, 0x0513},
        {0x2000, 0x200b},
        {0x2013, 0x2015},
        {0x2017, 0x201e},
        {0x2020, 0x2022},
        {0x2026, 0x2026},
        {0x2030, 0x2030},
        {0x2032, 0x2033},
        {0x2039, 0x203a},
        {0x203c, 0x203c},
        {0x2044, 0x2044},
    };

    if (!load_font_renderer()) {
        unload_fonts();
        return false;
    }

    for (int i = 0; i < FONT_COUNT; ++i) {
        Font_Codepoints *base = &font_codepoints[i][FONT_FACE_BASE];

        for (size_t range = 0; range < sizeof(ranges) / sizeof(ranges[0]);
             ++range) {
            for (int codepoint = ranges[range][0];
                 codepoint <= ranges[range][1]; ++codepoint) {
                if (!font_renderer_has(&font_renderer, FONT_FACE_BASE,
                                       codepoint)) {
                    continue;
                }

                if (!font_codepoints_add(base, codepoint)) {
                    unload_fonts();
                    return false;
                }
            }
        }

        if (!rebuild_font(i)) {
            mp_log(ERROR, "failed to load Noto Sans at %d px",
                   font_sizes[i]);
            unload_fonts();
            return false;
        }
    }

    return true;
}

static Font font_for_size(int font_size)
{
    return fonts[font_index_for_size(font_size)];
}

static int measure_text(const char *text, int font_size)
{
    Font font = font_for_size(font_size);
    return (int)roundf(MeasureTextEx(font, text, (float)font_size, 0.0f).x);
}

static void draw_text(const char *text, int x, int y, int font_size,
                      Color color)
{
    DrawTextEx(font_for_size(font_size), text, (Vector2){(float)x, (float)y},
               (float)font_size, 0.0f, color);
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

static bool create_ui_icons(Ui_Icons *icons, System_Theme theme)
{
    bool dark = theme == SYSTEM_THEME_DARK;
    Asset_Id back = dark ? ASSET_BACK_WHITE_SVG : ASSET_BACK_BLACK_SVG;
    Asset_Id forward =
        dark ? ASSET_FORWARD_WHITE_SVG : ASSET_FORWARD_BLACK_SVG;
    Asset_Id pause = dark ? ASSET_PAUSE_WHITE_SVG : ASSET_PAUSE_BLACK_SVG;
    Asset_Id play = dark ? ASSET_PLAY_WHITE_SVG : ASSET_PLAY_BLACK_SVG;
    Asset_Id repeat = dark ? ASSET_REPEAT_WHITE_SVG : ASSET_REPEAT_BLACK_SVG;
    Asset_Id repeat_one =
        dark ? ASSET_REPEAT_ONE_WHITE_SVG : ASSET_REPEAT_ONE_BLACK_SVG;
    Asset_Id shuffle =
        dark ? ASSET_SHUFFLE_WHITE_SVG : ASSET_SHUFFLE_BLACK_SVG;
    Asset_Id playlist =
        dark ? ASSET_PLAYLIST_WHITE_SVG : ASSET_PLAYLIST_BLACK_SVG;
    Asset_Id settings =
        dark ? ASSET_SETTINGS_WHITE_SVG : ASSET_SETTINGS_BLACK_SVG;

    *icons = (Ui_Icons){
        .back = load_asset_texture(back, UI_BUTTON_ICON_SIZE, 0.74f),
        .forward = load_asset_texture(forward, UI_BUTTON_ICON_SIZE, 0.74f),
        .pause = load_asset_texture(pause, UI_BUTTON_ICON_SIZE, 0.72f),
        .play = load_asset_texture(play, UI_BUTTON_ICON_SIZE, 0.72f),
        .repeat = load_asset_texture(repeat, UI_BUTTON_ICON_SIZE, 0.68f),
        .repeat_one =
            load_asset_texture(repeat_one, UI_BUTTON_ICON_SIZE, 0.68f),
        .shuffle = load_asset_texture(shuffle, UI_BUTTON_ICON_SIZE, 0.68f),
        .playlist = load_asset_texture(playlist, UI_BUTTON_ICON_SIZE, 0.72f),
        .settings = load_asset_texture(settings, UI_BUTTON_ICON_SIZE, 0.68f),
        .playlist_back = load_asset_texture(back, UI_TOGGLE_ICON_SIZE, 0.88f),
        .playlist_forward =
            load_asset_texture(forward, UI_TOGGLE_ICON_SIZE, 0.88f),
    };

    Texture2D textures[] = {
        icons->back,     icons->forward,       icons->pause,
        icons->play,     icons->repeat,        icons->repeat_one,
        icons->shuffle,  icons->playlist,      icons->settings,
        icons->playlist_back, icons->playlist_forward,
    };

    for (size_t i = 0; i < sizeof(textures) / sizeof(textures[0]); ++i) {
        if (!IsTextureValid(textures[i])) {
            unload_ui_icons(icons);
            return false;
        }
    }

    return true;
}

static bool init_ui_icon_transition(Ui_Icon_Transition *transition,
                                    System_Theme theme)
{
    *transition = (Ui_Icon_Transition){
        .current_theme = theme,
        .target_theme = theme,
    };
    return create_ui_icons(&transition->current, theme);
}

static bool begin_ui_icon_transition(Ui_Icon_Transition *transition,
                                     System_Theme target)
{
    if (transition->active) {
        if (target == transition->target_theme) return true;

        if (target == transition->current_theme) {
            Ui_Icons icons = transition->current;
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

    Ui_Icons next = {0};

    if (!create_ui_icons(&next, target)) return false;

    transition->next = next;
    transition->target_theme = target;
    transition->progress = 0.0f;
    transition->active = true;
    return true;
}

static void update_ui_icon_transition(Ui_Icon_Transition *transition,
                                      float delta_time)
{
    if (!transition->active) return;

    transition->progress += delta_time / THEME_TRANSITION_SECONDS;

    if (transition->progress < 1.0f) return;

    unload_ui_icons(&transition->current);
    transition->current = transition->next;
    transition->next = (Ui_Icons){0};
    transition->current_theme = transition->target_theme;
    transition->progress = 0.0f;
    transition->active = false;
}

static float ui_icon_transition_amount(const Ui_Icon_Transition *transition)
{
    if (!transition->active) return 0.0f;

    float progress = transition->progress;
    return progress * progress * (3.0f - 2.0f * progress);
}

static void unload_ui_icon_transition(Ui_Icon_Transition *transition)
{
    unload_ui_icons(&transition->current);
    unload_ui_icons(&transition->next);
    *transition = (Ui_Icon_Transition){0};
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

static Open_Menu_Layout make_open_menu_layout(Vector2 center, float scale,
                                               int screen_width,
                                               int screen_height)
{
    float width = snap_pixel(clamp_float(164.0f * scale, 144.0f, 210.0f));
    float row_height =
        snap_pixel(clamp_float(40.0f * scale, 36.0f, 52.0f));
    float border = 1.0f;
    Rectangle panel = snap_rectangle((Rectangle){
        center.x - width / 2.0f,
        center.y - row_height,
        width,
        row_height * 2.0f + border * 2.0f,
    });

    if (panel.x < 4.0f) panel.x = 4.0f;
    if (panel.y < 4.0f) panel.y = 4.0f;
    if (panel.x + panel.width > (float)screen_width - 4.0f) {
        panel.x = (float)screen_width - panel.width - 4.0f;
    }
    if (panel.y + panel.height > (float)screen_height - 4.0f) {
        panel.y = (float)screen_height - panel.height - 4.0f;
    }

    return (Open_Menu_Layout){
        .panel = panel,
        .files = {panel.x + border, panel.y + border,
                  panel.width - border * 2.0f, row_height},
        .folder = {panel.x + border, panel.y + border + row_height,
                   panel.width - border * 2.0f, row_height},
    };
}

static void draw_panel_frame(Rectangle panel, Color color)
{
    panel.width -= 1.0f;
    panel.height -= 1.0f;

    if (panel.width > 0.0f && panel.height > 0.0f) {
        DrawRectangleLinesEx(panel, 1.0f, color);
    }
}

static int crisp_font_size(float desired, int minimum, int maximum);

static void draw_open_menu(const Open_Menu_Layout *menu, Vector2 mouse,
                           float scale, const Ui_Theme *theme)
{
    int font_size = crisp_font_size(17.0f * scale, 14, 20);
    const Rectangle rows[] = {menu->files, menu->folder};
    const char *labels[] = {"Files", "Folder"};

    DrawRectangleRec(menu->panel, theme->surface);

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        bool hovered = CheckCollisionPointRec(mouse, rows[i]);
        DrawRectangleRec(rows[i], hovered ? theme->playlist_hover
                                          : theme->playlist_item);
        int width = measure_text(labels[i], font_size);
        draw_text(labels[i],
                  (int)snap_pixel(rows[i].x +
                                  (rows[i].width - (float)width) / 2.0f),
                  (int)snap_pixel(rows[i].y +
                                  (rows[i].height - font_size) / 2.0f - 1.0f),
                  font_size, theme->text_primary);
    }

    draw_panel_frame(menu->panel, theme->surface_border);
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
        mp_log(WARNING, "failed to remember album art path");
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
        mp_log(WARNING, "failed to decode album art for \"%s\"", track_path);
        return;
    }

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);

    if (!IsTextureValid(texture)) {
        mp_log(WARNING, "failed to upload album art for \"%s\"", track_path);
        return;
    }

    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    album_art->texture = texture;
}

static void draw_album_art(const Album_Art *album_art, Rectangle bounds,
                           const Ui_Theme *theme)
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

    DrawRectangleRec(bounds, theme->surface);
    DrawTexturePro(album_art->texture, source, destination, (Vector2){0}, 0.0f,
                   WHITE);
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
                          size_t bar_count, float scale,
                          const Ui_Theme *theme)
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
    int text_width = measure_text(text, font_size);
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
    draw_text(text, (int)(bounds.x - snap_pixel(offset)), (int)bounds.y,
              font_size, color);
    EndScissorMode();
}

static int crisp_font_size(float desired, int minimum, int maximum)
{
    int requested = (int)roundf(desired);

    if (requested < minimum) requested = minimum;
    if (requested > maximum) requested = maximum;

    int index = font_index_for_size(requested);

    while (index > 0 && font_sizes[index] > maximum) --index;
    while (index + 1 < FONT_COUNT && font_sizes[index] < minimum) ++index;

    return font_sizes[index];
}

static Ui_Layout make_ui_layout(int width, int height,
                                float side_panel_open_amount,
                                bool playlist_button_on_side)
{
    side_panel_open_amount =
        clamp_float(side_panel_open_amount, 0.0f, 1.0f);

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
    float panel_width =
        snap_pixel(clamp_float((float)width * 0.36f, 280.0f, 520.0f));
    float panel_max_height = (float)height - padding * 2.0f;
    float panel_height = snap_pixel(panel_max_height);
    float toggle_width = snap_pixel(clamp_float(32.0f * scale, 24.0f, 48.0f));
    float toggle_height = snap_pixel(clamp_float(72.0f * scale, 52.0f, 108.0f));
    float settings_x = (float)width - padding - button_size;
    float playlist_x = settings_x - gap - button_size;
    float repeat_right = playlist_button_on_side ? settings_x : playlist_x;
    float repeat_x = repeat_right - gap - button_size;
    float next_x = repeat_x - gap - button_size;
    float play_x = next_x - gap - button_size;
    float previous_x = play_x - gap - button_size;
    float shuffle_x = previous_x - gap - button_size;
    float panel_x = (float)width - panel_width;
    float panel_y = padding;
    float title_y = snap_pixel(controls_y - 128.0f * scale);
    float album_art_gap = snap_pixel(20.0f * scale);
    float max_album_art_width = (float)width - padding * 2.0f;
    float max_album_art_height = title_y - album_art_gap - padding;
    float album_art_size = snap_pixel(max_album_art_width < max_album_art_height
                                          ? max_album_art_width
                                          : max_album_art_height);

    if (album_art_size < 0.0f) album_art_size = 0.0f;

    Rectangle album_art = {
        padding,
        title_y - album_art_gap - album_art_size,
        album_art_size,
        album_art_size,
    };
    float spectrum_x = album_art.x + album_art.width + padding;
    Rectangle spectrum = {
        spectrum_x,
        album_art.y,
        (float)width - padding - spectrum_x,
        album_art.height,
    };

    if (spectrum.width < 0.0f) spectrum.width = 0.0f;

    if (panel_width + toggle_width + gap + padding > (float)width) {
        panel_width = (float)width - toggle_width - gap - padding;
        panel_x = (float)width - panel_width;
    }

    float open_panel_x = panel_x;
    float open_toggle_x = open_panel_x - gap - toggle_width;
    float closed_toggle_x = (float)width - toggle_width;
    panel_x = (float)width +
              (open_panel_x - (float)width) * side_panel_open_amount;
    float toggle_x = closed_toggle_x +
                     (open_toggle_x - closed_toggle_x) *
                         side_panel_open_amount;
    float panel_scale = panel_width / 380.0f;
    float panel_height_scale = panel_height / 640.0f;

    if (panel_height_scale < panel_scale) panel_scale = panel_height_scale;
    panel_scale = clamp_float(panel_scale, 0.72f, 1.3f);

    int playlist_header_size =
        crisp_font_size(24.0f * panel_scale, 20, 30);
    int playlist_title_size =
        crisp_font_size(20.0f * panel_scale, 16, 24);
    int playlist_details_size =
        crisp_font_size(16.0f * panel_scale, 14, 20);
    int playlist_search_size =
        crisp_font_size(17.0f * panel_scale, 14, 20);
    float panel_inner = snap_pixel(clamp_float(12.0f * panel_scale, 10.0f,
                                               18.0f));
    float header_height = snap_pixel(clamp_float(
        54.0f * panel_scale, playlist_header_size + 20.0f, 70.0f));
    float search_height = snap_pixel(clamp_float(
        36.0f * panel_scale, playlist_search_size + 12.0f, 44.0f));
    float search_gap = snap_pixel(clamp_float(8.0f * panel_scale, 6.0f, 12.0f));
    float open_width = snap_pixel(clamp_float(68.0f * panel_scale, 60.0f,
                                              84.0f));
    float search_y = panel_y + panel_height - panel_inner - search_height;
    float playlist_top = panel_y + header_height;
    float playlist_bottom = search_y - panel_inner;
    float playlist_item_height = snap_pixel(clamp_float(
        (float)(playlist_title_size + playlist_details_size) +
            12.0f * panel_scale,
        44.0f, 68.0f));
    float playlist_item_gap =
        snap_pixel(clamp_float(4.0f * panel_scale, 3.0f, 7.0f));

    Ui_Layout layout = {
        .scale = scale,
        .width = width,
        .height = height,
        .title_size = crisp_font_size(38.0f * scale, 30, 60),
        .status_size = crisp_font_size(25.0f * scale, 20, 40),
        .playlist_header_size = playlist_header_size,
        .playlist_title_size = playlist_title_size,
        .playlist_details_size = playlist_details_size,
        .playlist_search_size = playlist_search_size,
        .title_x = padding,
        .title_y = title_y,
        .details_y = snap_pixel(controls_y - 82.0f * scale),
        .metadata_y = snap_pixel(controls_y - 37.0f * scale),
        .playlist_top = playlist_top,
        .playlist_item_height = playlist_item_height,
        .playlist_item_gap = playlist_item_gap,
        .previous_button = {previous_x, controls_y, button_size, button_size},
        .play_button = {play_x, controls_y, button_size, button_size},
        .next_button = {next_x, controls_y, button_size, button_size},
        .repeat_button = {repeat_x, controls_y, button_size, button_size},
        .shuffle_button = {shuffle_x, controls_y, button_size, button_size},
        .playlist_button = {playlist_x, controls_y, button_size, button_size},
        .settings_button = {settings_x, controls_y, button_size, button_size},
        .progress_bar =
            {
                padding,
                controls_y + (button_size - progress_height) / 2.0f,
                shuffle_x - gap - padding,
                progress_height,
            },
        .album_art = album_art,
        .spectrum = spectrum,
        .playlist_panel = {panel_x, panel_y, panel_width, panel_height},
        .playlist_viewport =
            {
                panel_x + panel_inner,
                playlist_top,
                panel_width - panel_inner * 2.0f,
                playlist_bottom - playlist_top,
            },
        .playlist_search =
            {
                panel_x + panel_inner,
                search_y,
                panel_width - panel_inner * 2.0f - search_gap - open_width,
                search_height,
            },
        .playlist_open =
            {
                panel_x + panel_width - panel_inner - open_width,
                search_y,
                open_width,
                search_height,
            },
        .settings_panel = {panel_x, panel_y, panel_width, panel_height},
        .settings_playlist_side =
            {
                panel_x + 16.0f * scale,
                panel_y + 72.0f * scale,
                panel_width - 32.0f * scale,
                snap_pixel(clamp_float(64.0f * scale, 52.0f, 88.0f)),
            },
        .playlist_toggle =
            {
                toggle_x,
                ((float)height - toggle_height) / 2.0f,
                toggle_width,
                toggle_height,
            },
    };

    float toggle_reveal_padding = snap_pixel(24.0f * scale);
    layout.playlist_toggle_reveal = (Rectangle){
        layout.playlist_toggle.x - toggle_reveal_padding,
        layout.playlist_toggle.y - toggle_reveal_padding,
        layout.playlist_toggle.width + toggle_reveal_padding * 2.0f,
        layout.playlist_toggle.height + toggle_reveal_padding * 2.0f,
    };

    if (layout.playlist_toggle_reveal.x < 0.0f) {
        layout.playlist_toggle_reveal.width += layout.playlist_toggle_reveal.x;
        layout.playlist_toggle_reveal.x = 0.0f;
    }

    if (layout.playlist_toggle_reveal.y < 0.0f) {
        layout.playlist_toggle_reveal.height += layout.playlist_toggle_reveal.y;
        layout.playlist_toggle_reveal.y = 0.0f;
    }

    float reveal_right =
        layout.playlist_toggle_reveal.x + layout.playlist_toggle_reveal.width;
    float reveal_bottom =
        layout.playlist_toggle_reveal.y + layout.playlist_toggle_reveal.height;

    if (reveal_right > (float)width) {
        layout.playlist_toggle_reveal.width -= reveal_right - (float)width;
    }

    if (reveal_bottom > (float)height) {
        layout.playlist_toggle_reveal.height -= reveal_bottom - (float)height;
    }

    layout.previous_button = snap_rectangle(layout.previous_button);
    layout.play_button = snap_rectangle(layout.play_button);
    layout.next_button = snap_rectangle(layout.next_button);
    layout.repeat_button = snap_rectangle(layout.repeat_button);
    layout.shuffle_button = snap_rectangle(layout.shuffle_button);
    layout.playlist_button = snap_rectangle(layout.playlist_button);
    layout.settings_button = snap_rectangle(layout.settings_button);
    layout.progress_bar = snap_rectangle(layout.progress_bar);
    layout.album_art = snap_rectangle(layout.album_art);
    layout.spectrum = snap_rectangle(layout.spectrum);
    layout.playlist_panel = snap_rectangle(layout.playlist_panel);
    layout.playlist_viewport = snap_rectangle(layout.playlist_viewport);
    layout.playlist_search = snap_rectangle(layout.playlist_search);
    layout.playlist_open = snap_rectangle(layout.playlist_open);
    layout.settings_panel = snap_rectangle(layout.settings_panel);
    layout.settings_playlist_side =
        snap_rectangle(layout.settings_playlist_side);
    layout.playlist_toggle = snap_rectangle(layout.playlist_toggle);
    layout.playlist_toggle_reveal =
        snap_rectangle(layout.playlist_toggle_reveal);

    layout.progress_hitbox = snap_rectangle((Rectangle){
        layout.progress_bar.x,
        layout.progress_bar.y - 9.0f * scale,
        layout.progress_bar.width,
        24.0f * scale,
    });

    float playlist_step =
        layout.playlist_item_height + layout.playlist_item_gap;
    layout.visible_playlist_items =
        (int)(layout.playlist_viewport.height / playlist_step);

    if (layout.visible_playlist_items < 1) {
        layout.visible_playlist_items = 1;
    }

    return layout;
}

static Rectangle playlist_item_bounds(const Ui_Layout *layout,
                                      int visible_index, float scroll_offset)
{
    return snap_rectangle((Rectangle){
        layout->playlist_viewport.x,
        layout->playlist_viewport.y - scroll_offset +
            (float)visible_index *
                (layout->playlist_item_height + layout->playlist_item_gap),
        layout->playlist_viewport.width,
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

static Texture2D button_icon_texture(const Ui_Icons *icons, Button_Icon icon)
{
    switch (icon) {
    case BUTTON_PREVIOUS:
        return icons->back;
    case BUTTON_PLAY:
        return icons->play;
    case BUTTON_PAUSE:
        return icons->pause;
    case BUTTON_NEXT:
        return icons->forward;
    case BUTTON_REPEAT:
        return icons->repeat;
    case BUTTON_REPEAT_ONE:
        return icons->repeat_one;
    case BUTTON_SHUFFLE:
        return icons->shuffle;
    case BUTTON_PLAYLIST:
        return icons->playlist;
    case BUTTON_SETTINGS:
        return icons->settings;
    }

    return (Texture2D){0};
}

static void draw_button(Rectangle bounds, Button_Icon icon,
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

    Texture2D current =
        button_icon_texture(&icon_transition->current, icon);

    if (!icon_transition->active) {
        draw_texture_icon(current, bounds, Fade(WHITE, opacity));
        return;
    }

    float amount = ui_icon_transition_amount(icon_transition);
    Texture2D next = button_icon_texture(&icon_transition->next, icon);
    draw_texture_icon(current, bounds, Fade(WHITE, opacity * (1.0f - amount)));
    draw_texture_icon(next, bounds, Fade(WHITE, opacity * amount));
}

static bool format_track_details(const Track_Metadata *metadata, char *text,
                                 size_t capacity)
{
    bool has_artist = metadata->artist[0] != '\0';
    bool has_album = metadata->album[0] != '\0';

    if (has_artist && has_album) {
        snprintf(text, capacity, "%s | %s", metadata->artist,
                 metadata->album);
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

    if (!collect_font_text(search->query, layout->playlist_search_size)) {
        return false;
    }

    for (int visible_index = 0;
         visible_index <= layout->visible_playlist_items; ++visible_index) {
        size_t result_index = first + (size_t)visible_index;

        if (result_index >= count) break;

        size_t index = playlist_search_track(search, result_index);

        const Track_Metadata *metadata = playlist_get_metadata(playlist, index);

        if (metadata == NULL ||
            !collect_font_text(metadata->title,
                               layout->playlist_title_size)) {
            return false;
        }

        char details[METADATA_TEXT_SIZE * 2 + 4];

        if (!format_track_details(metadata, details, sizeof(details))) continue;

        if (!collect_font_text(details, layout->playlist_details_size)) {
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
                                bool picker_busy,
                                const Ui_Theme *theme)
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

    DrawRectangleRec(panel, theme->surface);
    draw_panel_frame(panel, theme->surface_border);
    draw_text("Playlist", (int)layout->playlist_viewport.x,
              (int)snap_pixel(panel.y +
                              (layout->playlist_top - panel.y -
                               layout->playlist_header_size) /
                                  2.0f),
              layout->playlist_header_size,
              theme->text_primary);

    if (count == 0) {
        const char *empty = playlist_count == 0 ? "No tracks" : "No matches";
        draw_text(empty, (int)layout->playlist_viewport.x,
                  (int)snap_pixel(layout->playlist_viewport.y + 8.0f),
                  layout->playlist_details_size,
                  theme->text_muted);
    }

    for (int visible_index = 0;
         visible_index <= layout->visible_playlist_items; ++visible_index) {
        size_t result_index = first + (size_t)visible_index;

        if (result_index >= count) break;

        size_t index = playlist_search_track(search, result_index);

        Rectangle item =
            playlist_item_bounds(layout, visible_index, scroll_offset);

        if (!CheckCollisionRecs(item, layout->playlist_viewport)) continue;

        bool hovered = index == hovered_track;
        Color fill = index == current ? theme->playlist_current
                                      : theme->playlist_item;

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
        float text_padding = snap_pixel(clamp_float(
            9.0f * layout->scale, 7.0f, 12.0f));
        float text_x = snap_pixel(item.x + text_padding);
        float text_width = item.width - text_padding * 2.0f;
        Color title_color = index == current ? theme->text_primary
                                             : theme->text_secondary;

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
            Color details_color = index == current ? theme->text_secondary
                                                   : theme->text_muted;

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
                                text_started_at,
                                &layout->playlist_viewport);
        }
    }

    Rectangle field = layout->playlist_search;
    Color field_fill =
        search_focused ? theme->playlist_hover : theme->playlist_item;
    DrawRectangleRec(field, field_fill);

    const char *search_text = search->query[0] == '\0'
                                  ? search_focused ? "" : "Search playlist"
                                  : search->query;
    Color search_color = search->query[0] == '\0' ? theme->text_faint
                                                   : theme->text_secondary;
    float search_padding = snap_pixel(clamp_float(
        10.0f * layout->scale, 8.0f, 14.0f));
    char result_count[32] = {0};
    float count_width = 0.0f;

    if (playlist_search_active(search)) {
        snprintf(result_count, sizeof(result_count), "%zu", count);
        count_width =
            (float)measure_text(result_count, layout->playlist_search_size);
    }

    Rectangle text_clip = {
        field.x + search_padding,
        field.y,
        field.width - search_padding * 2.0f -
            (count_width > 0.0f ? count_width + search_padding : 0.0f),
        field.height,
    };
    float text_y = snap_pixel(field.y +
                              (field.height - layout->playlist_search_size) /
                                  2.0f -
                              1.0f);
    char query_prefix[PLAYLIST_SEARCH_QUERY_CAPACITY];
    size_t prefix_size = search->cursor;

    if (prefix_size >= sizeof(query_prefix)) prefix_size = 0;

    memcpy(query_prefix, search->query, prefix_size);
    query_prefix[prefix_size] = '\0';

    float cursor_offset =
        (float)measure_text(query_prefix, layout->playlist_search_size);
    float text_x = text_clip.x;
    float caret_width = snap_pixel(clamp_float(
        (float)layout->playlist_search_size * 0.52f, 8.0f, 12.0f));

    if (cursor_offset + caret_width > text_clip.width) {
        text_x -= cursor_offset + caret_width - text_clip.width;
    }

    Rectangle caret = {
        snap_pixel(text_x + cursor_offset),
        text_y - 1.0f,
        caret_width,
        (float)layout->playlist_search_size + 2.0f,
    };

    BeginScissorMode((int)text_clip.x, (int)text_clip.y,
                     (int)text_clip.width, (int)text_clip.height);

    if (search_focused) {
        DrawRectangleRec(caret, theme->text_primary);
    }

    draw_text(search_text, (int)snap_pixel(text_x), (int)text_y,
              layout->playlist_search_size, search_color);

    EndScissorMode();

    if (search_focused && search->query[search->cursor] != '\0') {
        Rectangle inverted_clip = GetCollisionRec(caret, text_clip);

        BeginScissorMode((int)inverted_clip.x, (int)inverted_clip.y,
                         (int)inverted_clip.width,
                         (int)inverted_clip.height);
        draw_text(search_text, (int)snap_pixel(text_x), (int)text_y,
                  layout->playlist_search_size, field_fill);
        EndScissorMode();
    }

    if (result_count[0] != '\0') {
        draw_text(result_count,
                  (int)snap_pixel(field.x + field.width - search_padding -
                                  count_width),
                  (int)text_y, layout->playlist_search_size,
                  theme->text_faint);
    }

    Rectangle open = layout->playlist_open;
    bool open_hovered = CheckCollisionPointRec(mouse, open);
    Color open_fill = picker_busy ? theme->button_active
                                  : open_hovered ? theme->button_hover
                                                 : theme->button;
    const char *open_text = picker_busy ? "..." : "Open";
    int open_width = measure_text(open_text, layout->playlist_search_size);

    DrawRectangleRec(open, open_fill);
    draw_text(open_text,
              (int)snap_pixel(open.x + (open.width - (float)open_width) / 2.0f),
              (int)snap_pixel(open.y +
                              (open.height - layout->playlist_search_size) /
                                  2.0f -
                              1.0f),
              layout->playlist_search_size,
              picker_busy ? theme->text_muted : theme->text_primary);
}

static void draw_settings_panel(const Ui_Layout *layout,
                                bool playlist_button_on_side, Vector2 mouse,
                                const Ui_Theme *theme)
{
    Rectangle panel = layout->settings_panel;
    Rectangle option = layout->settings_playlist_side;
    bool hovered = CheckCollisionPointRec(mouse, option);
    int label_size = crisp_font_size(22.0f * layout->scale, 20, 30);

    DrawRectangleRec(panel, theme->surface);
    draw_panel_frame(panel, theme->surface_border);
    draw_text("Settings", (int)snap_pixel(panel.x + 20.0f * layout->scale),
              (int)snap_pixel(panel.y + 20.0f * layout->scale),
              crisp_font_size(35.0f * layout->scale, 30, 50),
              theme->text_primary);

    DrawRectangleRec(option,
                     hovered ? theme->playlist_hover : theme->playlist_item);
    float switch_width = snap_pixel(44.0f * layout->scale);
    float switch_height = snap_pixel(24.0f * layout->scale);
    Rectangle toggle = snap_rectangle((Rectangle){
        option.x + option.width - switch_width - 14.0f * layout->scale,
        option.y + (option.height - switch_height) / 2.0f,
        switch_width,
        switch_height,
    });
    int label_x = (int)snap_pixel(option.x + 14.0f * layout->scale);
    float available_label_width = toggle.x - (float)label_x -
                                  10.0f * layout->scale;

    if ((float)measure_text("Playlist button on side", label_size) <=
        available_label_width) {
        int label_y = (int)snap_pixel(option.y +
                                      (option.height - label_size) / 2.0f);
        draw_text("Playlist button on side", label_x, label_y, label_size,
                  theme->text_secondary);
    } else {
        int line_size = crisp_font_size(18.0f * layout->scale, 20, 20);
        int first_y = (int)snap_pixel(option.y +
                                      (option.height - line_size * 2) / 2.0f);
        draw_text("Playlist button", label_x, first_y, line_size,
                  theme->text_secondary);
        draw_text("on side", label_x, first_y + line_size, line_size,
                  theme->text_secondary);
    }

    Color toggle_fill = playlist_button_on_side ? theme->progress_foreground
                                                 : theme->progress_background;
    DrawRectangleRec(toggle, toggle_fill);

    float knob_padding = snap_pixel(3.0f * layout->scale);
    float knob_size = toggle.height - knob_padding * 2.0f;
    float knob_x = playlist_button_on_side
                       ? toggle.x + toggle.width - knob_padding - knob_size
                       : toggle.x + knob_padding;
    DrawRectangleRec(snap_rectangle((Rectangle){
                         knob_x,
                         toggle.y + knob_padding,
                         knob_size,
                         knob_size,
                     }),
                     theme->surface);
}

static void draw_playlist_toggle(Rectangle bounds, bool open,
                                 const Ui_Icon_Transition *icon_transition,
                                 Vector2 mouse, float opacity,
                                 const Ui_Theme *theme)
{
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    Color fill = hovered ? theme->button_hover : theme->button;

    DrawRectangleRec(bounds, Fade(fill, opacity));
    Texture2D current = open ? icon_transition->current.playlist_back
                             : icon_transition->current.playlist_forward;

    if (!icon_transition->active) {
        draw_texture_icon(current, bounds, Fade(WHITE, opacity));
        return;
    }

    float amount = ui_icon_transition_amount(icon_transition);
    Texture2D next = open ? icon_transition->next.playlist_back
                          : icon_transition->next.playlist_forward;
    draw_texture_icon(current, bounds,
                      Fade(WHITE, opacity * (1.0f - amount)));
    draw_texture_icon(next, bounds, Fade(WHITE, opacity * amount));
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

    if (config_path_available &&
        app_config_load(config_path, &app_config) ==
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
    System_Theme system_theme =
        system_theme_monitor_get(&system_theme_monitor);
    Ui_Theme_Transition theme_transition = {
        .current = *ui_theme_for(system_theme),
        .target = *ui_theme_for(system_theme),
    };
    const Ui_Theme *theme = &theme_transition.current;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, window_title);

    Ui_Icon_Transition icon_transition = {0};
    Texture2D application_icon = {0};

    if (!load_application_icon(&application_icon)) {
        system_theme_monitor_uninit(&system_theme_monitor);
        CloseWindow();
        playlist_uninit(&playlist);
        playback_order_uninit(&playback_order);
        player_uninit(&player);
        return 1;
    }

    if (!load_fonts()) {
        system_theme_monitor_uninit(&system_theme_monitor);
        UnloadTexture(application_icon);
        CloseWindow();
        playlist_uninit(&playlist);
        playback_order_uninit(&playback_order);
        player_uninit(&player);
        return 1;
    }

    if (!init_ui_icon_transition(&icon_transition, system_theme)) {
        system_theme_monitor_uninit(&system_theme_monitor);
        unload_fonts();
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

    while (!WindowShouldClose()) {
        float ui_frame_time = GetFrameTime();

        char *file_picker_selection = NULL;
        File_Picker_Mode file_picker_mode = FILE_PICKER_NONE;
        bool file_picker_graphical = false;
        bool file_picker_failed = false;

        if (file_picker_take(&file_picker, &file_picker_selection,
                             &file_picker_mode,
                             &file_picker_graphical, &file_picker_failed)) {
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
                                              saved_path,
                                              sizeof(saved_path))) {
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

            if (begin_ui_icon_transition(&icon_transition, detected_theme)) {
                system_theme = detected_theme;
                begin_theme_transition(&theme_transition, system_theme);
                mp_log(INFO, "theme: %s",
                       system_theme == SYSTEM_THEME_DARK ? "dark" : "light");
            } else {
                mp_log(WARNING, "failed to switch application theme");
            }
        }

        update_theme_transition(&theme_transition, ui_frame_time);
        update_ui_icon_transition(&icon_transition, ui_frame_time);

        if (IsWindowResized()) {
            spectrum_resume_at =
                GetTime() + SPECTRUM_RESIZE_SETTLE_SECONDS;
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
        bool has_track = state != PLAYER_STOPPED;

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

        Ui_Layout layout = make_ui_layout(GetScreenWidth(), GetScreenHeight(),
                                          side_panel_animation,
                                          playlist_button_on_side);
        bool playlist_panel_visible =
            has_track && animated_side_panel == SIDE_PANEL_PLAYLIST &&
            side_panel_animation > 0.001f;
        bool settings_panel_visible =
            has_track && animated_side_panel == SIDE_PANEL_SETTINGS &&
            side_panel_animation > 0.001f;
        bool picker_busy = file_picker_busy(&file_picker);
        Open_Menu_Layout open_menu = make_open_menu_layout(
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
                    (playlist_panel_visible && CheckCollisionPointRec(
                                                   mouse,
                                                   layout.playlist_open)))) {
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
        open_menu = make_open_menu_layout(open_menu_center, layout.scale,
                                          layout.width, layout.height);
        bool open_menu_blocks_mouse =
            open_menu_visible || open_menu_consumed_click;
        bool playlist_toggle_target =
            has_track &&
            (side_panel != SIDE_PANEL_NONE ||
             (playlist_button_on_side &&
              CheckCollisionPointRec(mouse,
                                     layout.playlist_toggle_reveal)));
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
        bool alt_down =
            IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
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
                bool delete_pressed = IsKeyPressed(KEY_DELETE) ||
                                      IsKeyPressedRepeat(KEY_DELETE);
                bool left_pressed = IsKeyPressed(KEY_LEFT) ||
                                    IsKeyPressedRepeat(KEY_LEFT);
                bool right_pressed = IsKeyPressed(KEY_RIGHT) ||
                                     IsKeyPressedRepeat(KEY_RIGHT);

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
                        const char *utf8 = CodepointToUTF8(codepoint,
                                                          &utf8_size);

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

        playlist_scroll_target = clamp_float(playlist_scroll_target, 0.0f,
                                             max_playlist_scroll);
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

        size_t hovered_playlist_track = PLAYLIST_TRACK_NONE;

        if (mouse_over_playlist_list && mouse_wheel != 0.0f) {
            float previous_target = playlist_scroll_target;
            playlist_scroll_target -= mouse_wheel * 2.35f;
            playlist_scroll_target = clamp_float(
                playlist_scroll_target, 0.0f, max_playlist_scroll);

            if (playlist_scroll_target != previous_target) {
                playlist_text_started_at = GetTime();
            }
        }

        playlist_scroll = animate_towards(playlist_scroll,
                                          playlist_scroll_target, 15.0f,
                                          ui_frame_time);

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
            float step =
                layout.playlist_item_height + layout.playlist_item_gap;
            float scroll_offset =
                (playlist_scroll - (float)first) * step;

            for (int visible_index = 0;
                 visible_index <= layout.visible_playlist_items;
                 ++visible_index) {
                size_t result_index = first + (size_t)visible_index;

                if (result_index >= playlist_view_count) break;

                size_t index =
                    playlist_search_track(&playlist_search, result_index);

                Rectangle item = playlist_item_bounds(
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
        has_track = state != PLAYER_STOPPED;

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
            layout = make_ui_layout(GetScreenWidth(), GetScreenHeight(), 0.0f,
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

        if (repeat_pressed) {
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

        if (shuffle_pressed) {
            bool enabled = !shuffle_enabled;

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
            ((!playlist_search_focused && shift_down &&
              IsKeyPressed(KEY_RIGHT) &&
              can_next) ||
             button_pressed(layout.next_button, mouse,
                            can_next && !sidebar_blocks_mouse))) {
            play_next_track(&player, &playlist, &playback_order, repeat_all,
                            &metadata);
        }

        if (!seek_dragging &&
            ((!playlist_search_focused && shift_down &&
              IsKeyPressed(KEY_LEFT) &&
              can_previous) ||
             button_pressed(layout.previous_button, mouse,
                            can_previous && !sidebar_blocks_mouse))) {
            play_previous_track(&player, &playlist, &playback_order, repeat_all,
                                &metadata);
        }

        bool toggle_requested =
            has_track && !seek_dragging &&
            ((!playlist_search_focused &&
              (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_P))) ||
             button_pressed(layout.play_button, mouse, !sidebar_blocks_mouse));

        if (toggle_requested && !player_toggle(&player)) {
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

        if (volume_change != 0.0f) {
            player_adjust_volume(&player, volume_change);
        }

        float volume_padding = snap_pixel(4.0f * layout.scale);
        int volume_slot_width = measure_text("Volume 100%", layout.status_size);
        Rectangle volume_bounds = snap_rectangle((Rectangle){
            layout.progress_bar.x + layout.progress_bar.width -
                (float)volume_slot_width - volume_padding,
            layout.metadata_y - volume_padding,
            (float)volume_slot_width + volume_padding * 2.0f,
            (float)layout.status_size + volume_padding * 2.0f,
        });

        if ((has_track && !playlist_search_focused && IsKeyPressed(KEY_M)) ||
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
                             measure_text(volume_text, layout.status_size));
        bool volume_hovered = has_track && !sidebar_blocks_mouse &&
                              CheckCollisionPointRec(mouse, volume_bounds);

        float cursor = player_get_cursor(&player);
        float length = player_get_length(&player);
        float keyboard_seek = 0.0f;

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
        has_track = state != PLAYER_STOPPED;
        const char *album_art_path =
            !has_track
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
                            measure_text(time_text, layout.status_size)) /
                               2.0f);

        char playlist_text[64];

        snprintf(playlist_text, sizeof(playlist_text), "%zu | %zu",
                 playlist_get_current(&playlist) + 1,
                 playlist_get_count(&playlist));

        int playlist_x = status_x + measure_text(status, layout.status_size) +
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
        int drop_title_size = crisp_font_size(40.0f * layout.scale, 30, 60);
        int drop_hint_size = crisp_font_size(20.0f * layout.scale, 16, 28);
        bool fonts_ready = collect_font_text(metadata.title, layout.title_size) &&
                           collect_font_text(details_text,
                                             layout.status_size);

        if (fonts_ready && !has_track) {
            fonts_ready = collect_font_text(drop_title, drop_title_size) &&
                          collect_font_text(drop_hint, drop_hint_size);
        }

        if (fonts_ready && playlist_panel_visible) {
            fonts_ready = collect_playlist_font_text(
                &playlist, &playlist_search, &layout, playlist_scroll);
        }

        if (!fonts_ready || !rebuild_dirty_fonts()) {
            mp_log(ERROR, "failed to update Noto Sans glyphs");
            exit_code = 1;
            break;
        }

        BeginDrawing();
        ClearBackground(theme->background);

        if (!has_track) {
            float icon_size = snap_pixel(clamp_float(
                150.0f * layout.scale, 104.0f, 190.0f));
            float icon_gap = snap_pixel(24.0f * layout.scale);
            float hint_gap = snap_pixel(14.0f * layout.scale);
            float group_height = icon_size + icon_gap +
                                 (float)drop_title_size + hint_gap +
                                 (float)drop_hint_size;
            float group_y = snap_pixel(
                ((float)layout.height - group_height) / 2.0f);
            Rectangle icon_bounds = snap_rectangle((Rectangle){
                ((float)layout.width - icon_size) / 2.0f,
                group_y,
                icon_size,
                icon_size,
            });
            int drop_title_x =
                (layout.width - measure_text(drop_title, drop_title_size)) / 2;
            int drop_title_y =
                (int)snap_pixel(group_y + icon_size + icon_gap);
            int drop_hint_x =
                (layout.width - measure_text(drop_hint, drop_hint_size)) / 2;
            int drop_hint_y = (int)snap_pixel(
                (float)drop_title_y + drop_title_size + hint_gap);

            draw_texture_icon(application_icon, icon_bounds, WHITE);
            draw_text(drop_title, drop_title_x, drop_title_y, drop_title_size,
                      theme->text_primary);
            draw_text(drop_hint, drop_hint_x, drop_hint_y, drop_hint_size,
                      theme->text_muted);
        } else {
            draw_album_art(&album_art, layout.album_art, theme);
            draw_spectrum(&spectrum, layout.spectrum, bar_count, layout.scale,
                          theme);
            draw_scrolling_text(metadata.title, current_title_bounds,
                                layout.title_size, layout.scale,
                                theme->text_primary, current_title_hovered,
                                current_title_text_started_at, NULL);
            draw_text(details_text, (int)layout.title_x, (int)layout.details_y,
                      layout.status_size, theme->text_muted);
            draw_text(status, status_x, (int)layout.metadata_y,
                      layout.status_size, theme->text_secondary);
            draw_text(playlist_text, playlist_x, (int)layout.metadata_y,
                      layout.status_size, theme->text_faint);
            draw_text(time_text, time_x, (int)layout.metadata_y,
                      layout.status_size, theme->text_muted);
            draw_text(volume_text, volume_x, (int)layout.metadata_y,
                      layout.status_size,
                      volume_hovered ? theme->text_secondary
                                     : theme->text_muted);

            DrawRectangleRec(layout.progress_bar, theme->progress_background);
            DrawRectangleRec(progress_fill, progress_hovered
                                                ? theme->progress_hover
                                                : theme->progress_foreground);
            float handle_scale = progress_hovered ? 14.0f : 10.0f;
            float handle_size = snap_pixel(handle_scale * layout.scale);
            Color handle_color =
                progress_hovered ? theme->progress_hover
                                 : theme->progress_foreground;
            DrawRectangleRec(snap_rectangle((Rectangle){
                                 progress_handle.x - handle_size / 2.0f,
                                 progress_handle.y - handle_size / 2.0f,
                                 handle_size,
                                 handle_size,
                             }),
                             handle_color);

            draw_button(layout.shuffle_button, BUTTON_SHUFFLE,
                        &icon_transition, mouse, true, shuffle_enabled, theme);
            draw_button(layout.previous_button, BUTTON_PREVIOUS,
                        &icon_transition, mouse, can_previous, false, theme);
            draw_button(layout.play_button,
                        state == PLAYER_PLAYING ? BUTTON_PAUSE : BUTTON_PLAY,
                        &icon_transition, mouse, true, false, theme);
            draw_button(layout.next_button, BUTTON_NEXT, &icon_transition,
                        mouse, can_next, false, theme);
            draw_button(layout.repeat_button,
                        repeat_mode == REPEAT_ONE ? BUTTON_REPEAT_ONE
                                                  : BUTTON_REPEAT,
                        &icon_transition, mouse, true,
                        repeat_mode != REPEAT_OFF, theme);
            if (!playlist_button_on_side) {
                draw_button(layout.playlist_button, BUTTON_PLAYLIST,
                            &icon_transition, mouse, true,
                            side_panel == SIDE_PANEL_PLAYLIST, theme);
            }
            draw_button(layout.settings_button, BUTTON_SETTINGS,
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
            draw_settings_panel(&layout, playlist_button_on_side, mouse,
                                theme);
        }

        if (playlist_toggle_visible) {
            draw_playlist_toggle(playlist_toggle_bounds,
                                 side_panel != SIDE_PANEL_NONE,
                                 &icon_transition, mouse,
                                 playlist_toggle_animation, theme);
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
    unload_ui_icon_transition(&icon_transition);
    UnloadTexture(application_icon);
    unload_fonts();
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
