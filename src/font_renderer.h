#ifndef FONT_RENDERER_H
#define FONT_RENDERER_H

#include <stdbool.h>
#include <stddef.h>

#include "raylib.h"

typedef struct {
    void *implementation;
} Font_Renderer;

bool font_renderer_init(Font_Renderer *renderer,
                        const unsigned char *const *font_data,
                        const size_t *font_sizes, int font_count);
void font_renderer_uninit(Font_Renderer *renderer);
bool font_renderer_has(const Font_Renderer *renderer, int face_index,
                       int codepoint);
GlyphInfo *font_renderer_load(const Font_Renderer *renderer, int face_index,
                              int pixel_size, const int *codepoints,
                              int codepoint_count);

#endif
