#ifndef MP_METADATA_H
#define MP_METADATA_H

#define METADATA_TEXT_SIZE 256

typedef struct {
    char title[METADATA_TEXT_SIZE];
    char artist[METADATA_TEXT_SIZE];
    char album[METADATA_TEXT_SIZE];
} Track_Metadata;

void metadata_load(const char *path, Track_Metadata *metadata);

#endif
