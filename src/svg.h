#ifndef MP_SVG_H
#define MP_SVG_H

#include <stddef.h>

#include "raylib.h"

Texture2D svg_load_texture(const char *name, const unsigned char *data,
                           size_t data_size, int size, float content_scale);

#endif
