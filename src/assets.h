#ifndef MP_ASSETS_H
#define MP_ASSETS_H

#include <stddef.h>

typedef enum {
    ASSET_BACK_SVG,
    ASSET_FORWARD_SVG,
    ASSET_PAUSE_SVG,
    ASSET_PLAY_SVG,
    ASSET_REPEAT_SVG,
    ASSET_REPEAT_ONE_SVG,
    ASSET_SHUFFLE_SVG,
    ASSET_ICON_PNG,
    ASSET_OPEN_SANS_REGULAR_TTF,
    ASSET_COUNT,
} Asset_Id;

typedef struct {
    const unsigned char *data;
    size_t size;
    const char *name;
} Embedded_Asset;

Embedded_Asset asset_get(Asset_Id id);

#endif
