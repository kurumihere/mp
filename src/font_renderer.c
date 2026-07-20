#include "font_renderer.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct {
    FT_Library library;
    FT_Face *faces;
    int face_count;
} Font_Renderer_Impl;

static void unload_glyphs(GlyphInfo *glyphs, int count)
{
    if (glyphs == NULL) return;

    for (int i = 0; i < count; ++i) UnloadImage(glyphs[i].image);
    MemFree(glyphs);
}

static bool copy_bitmap(const FT_Bitmap *bitmap, Image *image)
{
    if (bitmap->width == 0 || bitmap->rows == 0) return true;
    if (bitmap->width > INT_MAX || bitmap->rows > INT_MAX ||
        bitmap->width > SIZE_MAX / bitmap->rows ||
        bitmap->width * bitmap->rows > UINT_MAX) {
        return false;
    }

    size_t size = (size_t)bitmap->width * bitmap->rows;
    unsigned char *pixels = MemAlloc((unsigned int)size);

    if (pixels == NULL) return false;

    int pitch = bitmap->pitch;
    size_t stride = (size_t)(pitch < 0 ? -pitch : pitch);

    for (unsigned int y = 0; y < bitmap->rows; ++y) {
        unsigned int source_y = pitch < 0 ? bitmap->rows - 1 - y : y;
        const unsigned char *source = bitmap->buffer + source_y * stride;
        unsigned char *destination = pixels + (size_t)y * bitmap->width;

        if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
            if (bitmap->num_grays == 256) {
                memcpy(destination, source, bitmap->width);
            } else {
                unsigned int denominator =
                    bitmap->num_grays > 1 ? bitmap->num_grays - 1 : 1;

                for (unsigned int x = 0; x < bitmap->width; ++x) {
                    destination[x] =
                        (unsigned char)((unsigned int)source[x] * 255u /
                                        denominator);
                }
            }
        } else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
            for (unsigned int x = 0; x < bitmap->width; ++x) {
                destination[x] =
                    (source[x / 8] & (0x80u >> (x % 8))) ? 255 : 0;
            }
        } else {
            MemFree(pixels);
            return false;
        }
    }

    *image = (Image){
        .data = pixels,
        .width = (int)bitmap->width,
        .height = (int)bitmap->rows,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE,
    };
    return true;
}

static bool make_space_image(int width, int height, Image *image)
{
    if (width <= 0 || height <= 0 || width > INT_MAX / height) return false;

    size_t size = (size_t)width * (size_t)height;

    if (size > UINT_MAX) return false;

    void *pixels = MemAlloc((unsigned int)size);

    if (pixels == NULL) return false;

    memset(pixels, 0, size);
    *image = (Image){
        .data = pixels,
        .width = width,
        .height = height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE,
    };
    return true;
}

bool font_renderer_init(Font_Renderer *renderer,
                        const unsigned char *const *font_data,
                        const size_t *font_sizes, int font_count)
{
    if (renderer == NULL || font_data == NULL || font_sizes == NULL ||
        font_count <= 0) {
        return false;
    }

    Font_Renderer_Impl *implementation = calloc(1, sizeof(*implementation));

    if (implementation == NULL) return false;

    implementation->faces =
        calloc((size_t)font_count, sizeof(*implementation->faces));

    if (implementation->faces == NULL ||
        FT_Init_FreeType(&implementation->library) != 0) {
        free(implementation->faces);
        free(implementation);
        return false;
    }

    for (int i = 0; i < font_count; ++i) {
        if (font_data[i] == NULL || font_sizes[i] == 0 ||
            font_sizes[i] > LONG_MAX ||
            FT_New_Memory_Face(implementation->library, font_data[i],
                               (FT_Long)font_sizes[i], 0,
                               &implementation->faces[i]) != 0 ||
            FT_Select_Charmap(implementation->faces[i],
                              FT_ENCODING_UNICODE) != 0) {
            implementation->face_count = i + 1;
            Font_Renderer failed = {.implementation = implementation};
            font_renderer_uninit(&failed);
            return false;
        }

        implementation->face_count = i + 1;
    }

    font_renderer_uninit(renderer);
    renderer->implementation = implementation;
    return true;
}

void font_renderer_uninit(Font_Renderer *renderer)
{
    if (renderer == NULL || renderer->implementation == NULL) return;

    Font_Renderer_Impl *implementation = renderer->implementation;

    for (int i = 0; i < implementation->face_count; ++i) {
        if (implementation->faces[i] != NULL) {
            FT_Done_Face(implementation->faces[i]);
        }
    }

    free(implementation->faces);
    FT_Done_FreeType(implementation->library);
    free(implementation);
    *renderer = (Font_Renderer){0};
}

bool font_renderer_has(const Font_Renderer *renderer, int face_index,
                       int codepoint)
{
    if (renderer == NULL || renderer->implementation == NULL ||
        codepoint < 0 || codepoint > 0x10ffff) {
        return false;
    }

    Font_Renderer_Impl *implementation = renderer->implementation;

    if (face_index < 0 || face_index >= implementation->face_count) {
        return false;
    }

    return FT_Get_Char_Index(implementation->faces[face_index],
                             (FT_ULong)codepoint) != 0;
}

GlyphInfo *font_renderer_load(const Font_Renderer *renderer, int face_index,
                              int pixel_size, const int *codepoints,
                              int codepoint_count)
{
    if (renderer == NULL || renderer->implementation == NULL ||
        pixel_size <= 0 || codepoints == NULL || codepoint_count <= 0) {
        return NULL;
    }

    Font_Renderer_Impl *implementation = renderer->implementation;

    if (face_index < 0 || face_index >= implementation->face_count) {
        return NULL;
    }

    FT_Face face = implementation->faces[face_index];
    FT_Long line_units = face->ascender - face->descender;

    if (line_units <= 0 || face->units_per_EM == 0) return NULL;

    int64_t scaled_size =
        (int64_t)pixel_size * 64 * face->units_per_EM;
    FT_F26Dot6 character_height =
        (FT_F26Dot6)((scaled_size + line_units / 2) / line_units);

    if (FT_Set_Char_Size(face, 0, character_height, 72, 72) != 0 ||
        (size_t)codepoint_count > UINT_MAX / sizeof(GlyphInfo)) {
        return NULL;
    }

    GlyphInfo *glyphs =
        MemAlloc((unsigned int)((size_t)codepoint_count * sizeof(*glyphs)));

    if (glyphs == NULL) return NULL;

    memset(glyphs, 0, (size_t)codepoint_count * sizeof(*glyphs));
    int ascent = (int)(((int64_t)pixel_size * face->ascender +
                        line_units / 2) /
                       line_units);
    FT_Int32 load_flags = FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT |
                          FT_LOAD_TARGET_LIGHT;

    for (int i = 0; i < codepoint_count; ++i) {
        FT_UInt glyph_index =
            FT_Get_Char_Index(face, (FT_ULong)codepoints[i]);

        if (glyph_index == 0 ||
            FT_Load_Glyph(face, glyph_index, load_flags) != 0) {
            unload_glyphs(glyphs, i);
            return NULL;
        }

        FT_GlyphSlot slot = face->glyph;
        glyphs[i].value = codepoints[i];
        glyphs[i].offsetX = slot->bitmap_left;
        glyphs[i].offsetY = ascent - slot->bitmap_top;
        glyphs[i].advanceX = (int)((slot->advance.x + 32) >> 6);

        if ((codepoints[i] == 0x20 || codepoints[i] == 0x3000) &&
            slot->bitmap.width == 0) {
            if (!make_space_image(glyphs[i].advanceX, pixel_size,
                                  &glyphs[i].image)) {
                unload_glyphs(glyphs, i + 1);
                return NULL;
            }
        } else if (!copy_bitmap(&slot->bitmap, &glyphs[i].image)) {
            unload_glyphs(glyphs, i + 1);
            return NULL;
        }
    }

    return glyphs;
}
