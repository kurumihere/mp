#ifndef MP_METADATA_H
#define MP_METADATA_H

#include <stdbool.h>
#include <stddef.h>

#define METADATA_TEXT_SIZE 256

typedef struct {
    char title[METADATA_TEXT_SIZE];
    char artist[METADATA_TEXT_SIZE];
    char album[METADATA_TEXT_SIZE];
} Track_Metadata;

typedef enum {
    TRACK_COVER_NONE,
    TRACK_COVER_JPEG,
    TRACK_COVER_PNG,
} Track_Cover_Format;

typedef struct {
    unsigned char *data;
    size_t size;
    Track_Cover_Format format;
} Track_Cover;

void metadata_load(const char *path, Track_Metadata *metadata);
bool metadata_cover_load(const char *path, Track_Cover *cover);
void metadata_cover_unload(Track_Cover *cover);

#endif
