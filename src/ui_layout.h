#ifndef MP_UI_LAYOUT_H
#define MP_UI_LAYOUT_H

#include <stdbool.h>

#include "raylib.h"

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
    Rectangle settings_global_media_keys;
    Rectangle playlist_toggle;
    Rectangle playlist_toggle_reveal;
} Ui_Layout;

typedef struct {
    Rectangle panel;
    Rectangle files;
    Rectangle folder;
} Open_Menu_Layout;

Ui_Layout ui_layout_make(int width, int height, float side_panel_open_amount,
                         bool playlist_button_on_side);
Rectangle ui_layout_playlist_item_bounds(const Ui_Layout *layout,
                                         int visible_index,
                                         float scroll_offset);
Open_Menu_Layout ui_open_menu_layout_make(Vector2 center, float scale,
                                          int screen_width, int screen_height);

#endif
