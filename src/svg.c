#include "svg.h"

#include <stdlib.h>

#include "log.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "nanosvg.h"
#include "nanosvgrast.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

Texture2D svg_load_texture(const char *path, int size, float content_scale)
{
    if (size <= 0 || content_scale <= 0.0f) return (Texture2D){0};

    NSVGimage *svg = nsvgParseFromFile(path, "px", 96.0f);

    if (svg == NULL || svg->width <= 0.0f || svg->height <= 0.0f) {
        mp_log(ERROR, "failed to parse SVG: %s", path);
        nsvgDelete(svg);
        return (Texture2D){0};
    }

    for (NSVGshape *shape = svg->shapes; shape != NULL; shape = shape->next) {
        if (shape->fill.type == NSVG_PAINT_COLOR) {
            shape->fill.color = 0xffffffffu;
        }

        if (shape->stroke.type == NSVG_PAINT_COLOR) {
            shape->stroke.color = 0xffffffffu;
        }
    }

    NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
    size_t pixel_count = (size_t)size * (size_t)size;
    unsigned char *pixels = calloc(pixel_count, 4);

    if (rasterizer == NULL || pixels == NULL) {
        mp_log(ERROR, "failed to allocate SVG rasterizer: %s", path);
        free(pixels);
        nsvgDeleteRasterizer(rasterizer);
        nsvgDelete(svg);
        return (Texture2D){0};
    }

    float largest_side = svg->width > svg->height ? svg->width : svg->height;
    float scale = (float)size * content_scale / largest_side;
    float offset_x = ((float)size - svg->width * scale) / 2.0f;
    float offset_y = ((float)size - svg->height * scale) / 2.0f;

    nsvgRasterize(rasterizer, svg, offset_x, offset_y, scale, pixels, size,
                  size, size * 4);

    Image image = {
        .data = pixels,
        .width = size,
        .height = size,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D texture = LoadTextureFromImage(image);

    free(pixels);
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(svg);

    if (!IsTextureValid(texture)) {
        mp_log(ERROR, "failed to create SVG texture: %s", path);
        return (Texture2D){0};
    }

    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}
