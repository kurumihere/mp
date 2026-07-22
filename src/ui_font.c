#include "ui_font.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "font_renderer.h"
#include "log.h"

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

static const int font_sizes[] = {14, 16, 18, 20, 22, 24, 30, 40, 50, 60};
#define FONT_COUNT ((int)(sizeof(font_sizes) / sizeof(font_sizes[0])))

static Font fonts[FONT_COUNT];
static Font_Renderer font_renderer;
static Font_Codepoints font_codepoints[FONT_COUNT][FONT_FACE_COUNT];
static Font_Codepoints font_unsupported[FONT_COUNT];
static bool font_dirty[FONT_COUNT];

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

        int *values =
            realloc(set->values, (size_t)capacity * sizeof(*set->values));

        if (values == NULL) return false;

        set->values = values;
        set->capacity = capacity;
    }

    set->values[set->count++] = codepoint;
    return true;
}

static bool font_codepoints_contains(const Font_Codepoints *set, int codepoint)
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

void ui_font_uninit(void)
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

        parts[face] = font_renderer_load(&font_renderer, face, font_size,
                                         codepoints->values, codepoints->count);
        part_counts[face] = parts[face] == NULL ? 0 : codepoints->count;

        if (parts[face] == NULL || part_counts[face] != codepoints->count ||
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
        preferred, FONT_FACE_BASE, FONT_FACE_JP, FONT_FACE_KR, FONT_FACE_TC,
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

bool ui_font_collect(const char *text, int font_size)
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

bool ui_font_rebuild(void)
{
    bool success = true;

    for (int i = 0; i < FONT_COUNT; ++i) {
        if (!font_dirty[i]) continue;

        if (!rebuild_font(i)) success = false;
        font_dirty[i] = false;
    }

    return success;
}

bool ui_font_init(void)
{
    const int ranges[][2] = {
        {0x0020, 0x007e}, {0x00a0, 0x017f}, {0x0400, 0x0486}, {0x0488, 0x0513},
        {0x2000, 0x200b}, {0x2013, 0x2015}, {0x2017, 0x201e}, {0x2020, 0x2022},
        {0x2026, 0x2026}, {0x2030, 0x2030}, {0x2032, 0x2033}, {0x2039, 0x203a},
        {0x203c, 0x203c}, {0x2044, 0x2044},
    };

    if (!load_font_renderer()) {
        ui_font_uninit();
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
                    ui_font_uninit();
                    return false;
                }
            }
        }

        if (!rebuild_font(i)) {
            mp_log(ERROR, "failed to load Noto Sans at %d px", font_sizes[i]);
            ui_font_uninit();
            return false;
        }
    }

    return true;
}

static Font font_for_size(int font_size)
{
    return fonts[font_index_for_size(font_size)];
}

int ui_font_crisp_size(float desired, int minimum, int maximum)
{
    int requested = (int)roundf(desired);

    if (requested < minimum) requested = minimum;
    if (requested > maximum) requested = maximum;

    int index = font_index_for_size(requested);

    while (index > 0 && font_sizes[index] > maximum)
        --index;
    while (index + 1 < FONT_COUNT && font_sizes[index] < minimum)
        ++index;

    return font_sizes[index];
}

int ui_font_measure(const char *text, int font_size)
{
    Font font = font_for_size(font_size);
    return (int)roundf(MeasureTextEx(font, text, (float)font_size, 0.0f).x);
}

void ui_font_draw(const char *text, int x, int y, int font_size, Color color)
{
    DrawTextEx(font_for_size(font_size), text, (Vector2){(float)x, (float)y},
               (float)font_size, 0.0f, color);
}
