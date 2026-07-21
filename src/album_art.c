#include "album_art.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "metadata.h"

#define ALBUM_ART_TRANSITION_SECONDS 0.45f

static float snap_pixel(float value)
{
    return (float)((int)(value + 0.5f));
}

void album_art_clear(Album_Art *album_art)
{
    if (IsTextureValid(album_art->texture)) {
        UnloadTexture(album_art->texture);
    }
    if (IsTextureValid(album_art->previous_texture)) {
        UnloadTexture(album_art->previous_texture);
    }

    free(album_art->track_path);
    *album_art = (Album_Art){0};
}

static Texture2D album_art_load_texture(const char *track_path)
{
    Track_Cover cover;

    if (!metadata_cover_load(track_path, &cover)) return (Texture2D){0};

    const char *file_type = cover.format == TRACK_COVER_JPEG ? ".jpg" : ".png";
    Image image = {0};

    if (cover.size <= INT_MAX) {
        image = LoadImageFromMemory(file_type, cover.data, (int)cover.size);
    }

    metadata_cover_unload(&cover);

    if (!IsImageValid(image)) {
        mp_log(WARNING, "failed to decode album art for \"%s\"", track_path);
        return (Texture2D){0};
    }

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);

    if (!IsTextureValid(texture)) {
        mp_log(WARNING, "failed to upload album art for \"%s\"", track_path);
        return (Texture2D){0};
    }

    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

void album_art_update(Album_Art *album_art, const char *track_path)
{
    if ((track_path == NULL && album_art->track_path == NULL) ||
        (track_path != NULL && album_art->track_path != NULL &&
         strcmp(track_path, album_art->track_path) == 0)) {
        return;
    }

    char *new_path = NULL;
    Texture2D new_texture = {0};

    if (track_path != NULL) {
        size_t path_size = strlen(track_path) + 1;
        new_path = malloc(path_size);

        if (new_path == NULL) {
            mp_log(WARNING, "failed to remember album art path");
            return;
        }

        memcpy(new_path, track_path, path_size);
        new_texture = album_art_load_texture(track_path);
    }

    bool had_track = album_art->track_path != NULL;

    if (IsTextureValid(album_art->previous_texture)) {
        UnloadTexture(album_art->previous_texture);
    }

    album_art->previous_texture = album_art->texture;
    album_art->texture = new_texture;
    free(album_art->track_path);
    album_art->track_path = new_path;
    album_art->transition_progress = 0.0f;
    album_art->transitioning =
        had_track && (IsTextureValid(album_art->previous_texture) ||
                      IsTextureValid(album_art->texture));

    if (!album_art->transitioning &&
        IsTextureValid(album_art->previous_texture)) {
        UnloadTexture(album_art->previous_texture);
        album_art->previous_texture = (Texture2D){0};
    }
}

void album_art_advance(Album_Art *album_art, float frame_time)
{
    if (!album_art->transitioning) return;

    album_art->transition_progress +=
        frame_time / ALBUM_ART_TRANSITION_SECONDS;

    if (album_art->transition_progress < 1.0f) return;

    album_art->transition_progress = 1.0f;
    album_art->transitioning = false;

    if (IsTextureValid(album_art->previous_texture)) {
        UnloadTexture(album_art->previous_texture);
        album_art->previous_texture = (Texture2D){0};
    }
}

static void album_art_draw_texture(Texture2D texture, Rectangle bounds,
                                   float size, float offset_y, float opacity)
{
    if (!IsTextureValid(texture)) return;

    float horizontal_scale = bounds.width / texture.width;
    float vertical_scale = bounds.height / texture.height;
    float scale =
        (horizontal_scale < vertical_scale ? horizontal_scale : vertical_scale) *
        size;
    float width = (float)texture.width * scale;
    float height = (float)texture.height * scale;
    Rectangle destination = {
        bounds.x + (bounds.width - width) / 2.0f,
        bounds.y + (bounds.height - height) / 2.0f + offset_y,
        width,
        height,
    };
    Rectangle source = {
        0.0f,
        0.0f,
        (float)texture.width,
        (float)texture.height,
    };
    Color tint = {255, 255, 255, (unsigned char)(255.0f * opacity)};

    DrawTexturePro(texture, source, destination, (Vector2){0}, 0.0f, tint);
}

void album_art_draw(const Album_Art *album_art, Rectangle bounds, Color surface)
{
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    DrawRectangleRec(bounds, surface);

    if (!album_art->transitioning) {
        album_art_draw_texture(album_art->texture, bounds, 1.0f, 0.0f, 1.0f);
        return;
    }

    float progress = album_art->transition_progress;
    float eased = progress * progress * (3.0f - 2.0f * progress);

    BeginScissorMode((int)snap_pixel(bounds.x), (int)snap_pixel(bounds.y),
                     (int)snap_pixel(bounds.width),
                     (int)snap_pixel(bounds.height));
    album_art_draw_texture(album_art->previous_texture, bounds,
                           1.0f - 0.28f * eased,
                           -bounds.height * 0.92f * eased, 1.0f - eased);
    album_art_draw_texture(album_art->texture, bounds,
                           0.72f + 0.28f * eased,
                           bounds.height * 0.92f * (1.0f - eased), eased);
    EndScissorMode();
}
