#include "album_art.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "metadata.h"

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

void album_art_clear(Album_Art *album_art)
{
    if (IsTextureValid(album_art->texture)) {
        UnloadTexture(album_art->texture);
    }

    free(album_art->track_path);
    *album_art = (Album_Art){0};
}

void album_art_update(Album_Art *album_art, const char *track_path)
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

void album_art_draw(const Album_Art *album_art, Rectangle bounds, Color surface)
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

    DrawRectangleRec(bounds, surface);
    DrawTexturePro(album_art->texture, source, destination, (Vector2){0}, 0.0f,
                   WHITE);
}
