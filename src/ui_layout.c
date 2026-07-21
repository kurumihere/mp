#include "ui_layout.h"

#include "ui_font.h"

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

Open_Menu_Layout ui_open_menu_layout_make(Vector2 center, float scale,
                                          int screen_width, int screen_height)
{
    float width = snap_pixel(clamp_float(164.0f * scale, 144.0f, 210.0f));
    float row_height = snap_pixel(clamp_float(40.0f * scale, 36.0f, 52.0f));
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

Ui_Layout ui_layout_make(int width, int height, float side_panel_open_amount,
                         bool playlist_button_on_side)
{
    side_panel_open_amount = clamp_float(side_panel_open_amount, 0.0f, 1.0f);

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

    Rectangle album_art = {padding, title_y - album_art_gap - album_art_size,
                           album_art_size, album_art_size};
    float spectrum_x = album_art.x + album_art.width + padding;
    Rectangle spectrum = {spectrum_x, album_art.y,
                          (float)width - padding - spectrum_x,
                          album_art.height};

    if (spectrum.width < 0.0f) spectrum.width = 0.0f;

    if (panel_width + toggle_width + gap + padding > (float)width) {
        panel_width = (float)width - toggle_width - gap - padding;
        panel_x = (float)width - panel_width;
    }

    float open_panel_x = panel_x;
    float open_toggle_x = open_panel_x - gap - toggle_width;
    float closed_toggle_x = (float)width - toggle_width;
    panel_x =
        (float)width + (open_panel_x - (float)width) * side_panel_open_amount;
    float toggle_x = closed_toggle_x +
                     (open_toggle_x - closed_toggle_x) * side_panel_open_amount;
    float panel_scale = panel_width / 380.0f;
    float panel_height_scale = panel_height / 640.0f;

    if (panel_height_scale < panel_scale) panel_scale = panel_height_scale;
    panel_scale = clamp_float(panel_scale, 0.72f, 1.3f);

    int playlist_header_size = ui_font_crisp_size(24.0f * panel_scale, 20, 30);
    int playlist_title_size = ui_font_crisp_size(20.0f * panel_scale, 16, 24);
    int playlist_details_size = ui_font_crisp_size(16.0f * panel_scale, 14, 20);
    int playlist_search_size = ui_font_crisp_size(17.0f * panel_scale, 14, 20);
    float panel_inner =
        snap_pixel(clamp_float(12.0f * panel_scale, 10.0f, 18.0f));
    float header_height = snap_pixel(
        clamp_float(54.0f * panel_scale, playlist_header_size + 20.0f, 70.0f));
    float search_height = snap_pixel(
        clamp_float(36.0f * panel_scale, playlist_search_size + 12.0f, 44.0f));
    float search_gap = snap_pixel(clamp_float(8.0f * panel_scale, 6.0f, 12.0f));
    float open_width =
        snap_pixel(clamp_float(68.0f * panel_scale, 60.0f, 84.0f));
    float search_y = panel_y + panel_height - panel_inner - search_height;
    float playlist_top = panel_y + header_height;
    float playlist_bottom = search_y - panel_inner;
    float playlist_item_height = snap_pixel(
        clamp_float((float)(playlist_title_size + playlist_details_size) +
                        12.0f * panel_scale,
                    44.0f, 68.0f));
    float playlist_item_gap =
        snap_pixel(clamp_float(4.0f * panel_scale, 3.0f, 7.0f));
    float tab_x = panel_x + panel_inner;
    float tab_width = (panel_width - panel_inner * 2.0f) / 2.0f;
    float settings_option_height =
        snap_pixel(clamp_float(64.0f * scale, 52.0f, 88.0f));
    float settings_option_gap = snap_pixel(10.0f * scale);
    float settings_option_x = panel_x + 16.0f * scale;
    float settings_option_y = panel_y + 72.0f * scale;
    float settings_option_width = panel_width - 32.0f * scale;

    Ui_Layout layout = {
        .scale = scale,
        .width = width,
        .height = height,
        .title_size = ui_font_crisp_size(38.0f * scale, 30, 60),
        .status_size = ui_font_crisp_size(25.0f * scale, 20, 40),
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
        .progress_bar = {padding,
                         controls_y + (button_size - progress_height) / 2.0f,
                         shuffle_x - gap - padding, progress_height},
        .album_art = album_art,
        .spectrum = spectrum,
        .playlist_panel = {panel_x, panel_y, panel_width, panel_height},
        .playlist_tab = {tab_x, panel_y, tab_width, header_height},
        .folders_tab = {tab_x + tab_width, panel_y, tab_width, header_height},
        .playlist_viewport = {panel_x + panel_inner, playlist_top,
                              panel_width - panel_inner * 2.0f,
                              playlist_bottom - playlist_top},
        .playlist_search = {panel_x + panel_inner, search_y,
                            panel_width - panel_inner * 2.0f - search_gap -
                                open_width,
                            search_height},
        .playlist_open = {panel_x + panel_width - panel_inner - open_width,
                          search_y, open_width, search_height},
        .settings_panel = {panel_x, panel_y, panel_width, panel_height},
        .settings_playlist_side = {settings_option_x, settings_option_y,
                                   settings_option_width,
                                   settings_option_height},
        .settings_global_media_keys =
            {
                settings_option_x,
                settings_option_y + settings_option_height +
                    settings_option_gap,
                settings_option_width,
                settings_option_height,
            },
        .playlist_toggle = {toggle_x, ((float)height - toggle_height) / 2.0f,
                            toggle_width, toggle_height},
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
    layout.playlist_tab = snap_rectangle(layout.playlist_tab);
    layout.folders_tab = snap_rectangle(layout.folders_tab);
    layout.playlist_viewport = snap_rectangle(layout.playlist_viewport);
    layout.playlist_search = snap_rectangle(layout.playlist_search);
    layout.playlist_open = snap_rectangle(layout.playlist_open);
    layout.settings_panel = snap_rectangle(layout.settings_panel);
    layout.settings_playlist_side =
        snap_rectangle(layout.settings_playlist_side);
    layout.settings_global_media_keys =
        snap_rectangle(layout.settings_global_media_keys);
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
    if (layout.visible_playlist_items < 1) layout.visible_playlist_items = 1;
    return layout;
}

Rectangle ui_layout_playlist_item_bounds(const Ui_Layout *layout,
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
