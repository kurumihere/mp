#ifndef MP_ALBUM_ART_H
#define MP_ALBUM_ART_H

#include "raylib.h"

typedef struct {
    Texture2D texture;
    Texture2D previous_texture;
    char *track_path;
    float transition_progress;
    bool transitioning;
} Album_Art;

void album_art_clear(Album_Art *album_art);
void album_art_update(Album_Art *album_art, const char *track_path);
void album_art_advance(Album_Art *album_art, float frame_time);
void album_art_draw(const Album_Art *album_art, Rectangle bounds,
                    Color surface);

#endif
