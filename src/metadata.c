#include "metadata.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"

#define MAX_METADATA_BLOCK (16u * 1024u * 1024u)

static uint32_t read_u24_be(const unsigned char *data)
{
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) |
           (uint32_t)data[2];
}

static uint32_t read_u32_be(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint32_t read_u32_le(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint32_t read_synchsafe(const unsigned char *data)
{
    return ((uint32_t)(data[0] & 0x7f) << 21) |
           ((uint32_t)(data[1] & 0x7f) << 14) |
           ((uint32_t)(data[2] & 0x7f) << 7) | (uint32_t)(data[3] & 0x7f);
}

static bool bytes_match(const unsigned char *data, size_t length,
                        const char *text)
{
    size_t text_length = strlen(text);

    if (length != text_length) return false;

    for (size_t i = 0; i < length; ++i) {
        unsigned char character = data[i];
        unsigned char expected = (unsigned char)text[i];

        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
        if (expected >= 'a' && expected <= 'z') expected -= 'a' - 'A';
        if (character != expected) return false;
    }

    return true;
}

static Track_Cover_Format cover_format_from_data(const unsigned char *data,
                                                 size_t length)
{
    static const unsigned char png_signature[] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
    };

    if (length >= sizeof(png_signature) &&
        memcmp(data, png_signature, sizeof(png_signature)) == 0) {
        return TRACK_COVER_PNG;
    }

    if (length >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) {
        return TRACK_COVER_JPEG;
    }

    return TRACK_COVER_NONE;
}

static Track_Cover_Format cover_format_from_mime(const unsigned char *mime,
                                                 size_t length)
{
    if (bytes_match(mime, length, "image/jpeg") ||
        bytes_match(mime, length, "image/jpg") ||
        bytes_match(mime, length, "jpg")) {
        return TRACK_COVER_JPEG;
    }

    if (bytes_match(mime, length, "image/png") ||
        bytes_match(mime, length, "png")) {
        return TRACK_COVER_PNG;
    }

    return TRACK_COVER_NONE;
}

static bool copy_cover(Track_Cover *cover, const unsigned char *data,
                       size_t size, Track_Cover_Format format)
{
    if (size == 0 || size > MAX_METADATA_BLOCK || format == TRACK_COVER_NONE) {
        return false;
    }

    unsigned char *copy = malloc(size);

    if (copy == NULL) return false;

    memcpy(copy, data, size);
    free(cover->data);
    cover->data = copy;
    cover->size = size;
    cover->format = format;
    return true;
}

static void copy_bytes(char *destination, size_t capacity,
                       const unsigned char *source, size_t length)
{
    if (capacity == 0) return;
    if (length >= capacity) length = capacity - 1;

    memcpy(destination, source, length);
    destination[length] = '\0';

    while (length > 0 &&
           (destination[length - 1] == '\0' || destination[length - 1] == ' ' ||
            destination[length - 1] == '\r' ||
            destination[length - 1] == '\n')) {
        destination[--length] = '\0';
    }
}

static void append_utf8(char *destination, size_t capacity, size_t *length,
                        uint32_t codepoint)
{
    unsigned char encoded[4];
    size_t encoded_length;

    if (codepoint <= 0x7f) {
        encoded[0] = (unsigned char)codepoint;
        encoded_length = 1;
    } else if (codepoint <= 0x7ff) {
        encoded[0] = (unsigned char)(0xc0 | (codepoint >> 6));
        encoded[1] = (unsigned char)(0x80 | (codepoint & 0x3f));
        encoded_length = 2;
    } else if (codepoint <= 0xffff) {
        encoded[0] = (unsigned char)(0xe0 | (codepoint >> 12));
        encoded[1] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[2] = (unsigned char)(0x80 | (codepoint & 0x3f));
        encoded_length = 3;
    } else if (codepoint <= 0x10ffff) {
        encoded[0] = (unsigned char)(0xf0 | (codepoint >> 18));
        encoded[1] = (unsigned char)(0x80 | ((codepoint >> 12) & 0x3f));
        encoded[2] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[3] = (unsigned char)(0x80 | (codepoint & 0x3f));
        encoded_length = 4;
    } else {
        return;
    }

    if (*length + encoded_length >= capacity) return;

    memcpy(destination + *length, encoded, encoded_length);
    *length += encoded_length;
    destination[*length] = '\0';
}

static void decode_latin1(char *destination, size_t capacity,
                          const unsigned char *source, size_t length)
{
    size_t output_length = 0;

    if (capacity == 0) return;
    destination[0] = '\0';

    for (size_t i = 0; i < length && source[i] != 0; ++i) {
        append_utf8(destination, capacity, &output_length, source[i]);
    }
}

static void decode_utf16(char *destination, size_t capacity,
                         const unsigned char *source, size_t length,
                         bool big_endian)
{
    size_t output_length = 0;

    if (capacity == 0) return;
    destination[0] = '\0';

    for (size_t i = 0; i + 1 < length; i += 2) {
        uint16_t first =
            big_endian ? (uint16_t)(((uint16_t)source[i] << 8) | source[i + 1])
                       : (uint16_t)(((uint16_t)source[i + 1] << 8) | source[i]);

        if (first == 0) break;

        uint32_t codepoint = first;

        if (first >= 0xd800 && first <= 0xdbff && i + 3 < length) {
            uint16_t second =
                big_endian
                    ? (uint16_t)(((uint16_t)source[i + 2] << 8) | source[i + 3])
                    : (uint16_t)(((uint16_t)source[i + 3] << 8) |
                                 source[i + 2]);

            if (second >= 0xdc00 && second <= 0xdfff) {
                codepoint = 0x10000u + ((uint32_t)(first - 0xd800) << 10) +
                            (uint32_t)(second - 0xdc00);
                i += 2;
            }
        }

        append_utf8(destination, capacity, &output_length, codepoint);
    }
}

static void decode_id3_text(char *destination, size_t capacity,
                            const unsigned char *data, size_t length)
{
    if (capacity == 0) return;
    destination[0] = '\0';
    if (length < 2) return;

    unsigned char encoding = data[0];
    ++data;
    --length;

    if (encoding == 0) {
        decode_latin1(destination, capacity, data, length);
    } else if (encoding == 1) {
        bool big_endian = true;

        if (length >= 2 && data[0] == 0xff && data[1] == 0xfe) {
            big_endian = false;
            data += 2;
            length -= 2;
        } else if (length >= 2 && data[0] == 0xfe && data[1] == 0xff) {
            data += 2;
            length -= 2;
        }

        decode_utf16(destination, capacity, data, length, big_endian);
    } else if (encoding == 2) {
        decode_utf16(destination, capacity, data, length, true);
    } else if (encoding == 3) {
        size_t text_length = 0;

        while (text_length < length && data[text_length] != 0)
            ++text_length;
        copy_bytes(destination, capacity, data, text_length);
    }
}

static bool id_matches(const unsigned char *id, const char *v22,
                       const char *modern, int version)
{
    if (version == 2) return memcmp(id, v22, 3) == 0;

    return memcmp(id, modern, 4) == 0;
}

static bool parse_id3_cover(const unsigned char *data, size_t length,
                            int version, const unsigned char **image,
                            size_t *image_size, Track_Cover_Format *format,
                            unsigned char *picture_type)
{
    if (length < 2) return false;

    unsigned char encoding = data[0];
    size_t offset;

    if (version == 2) {
        if (length < 5) return false;

        *format = cover_format_from_mime(data + 1, 3);
        offset = 4;
    } else {
        const unsigned char *mime_end = memchr(data + 1, '\0', length - 1);

        if (mime_end == NULL) return false;

        size_t mime_length = (size_t)(mime_end - (data + 1));
        *format = cover_format_from_mime(data + 1, mime_length);
        offset = (size_t)(mime_end - data) + 1;
    }

    if (offset >= length) return false;

    *picture_type = data[offset++];
    bool description_ended = false;

    if (encoding == 1 || encoding == 2) {
        while (offset + 1 < length) {
            if (data[offset] == 0 && data[offset + 1] == 0) {
                offset += 2;
                description_ended = true;
                break;
            }

            offset += 2;
        }
    } else {
        const unsigned char *description_end =
            memchr(data + offset, '\0', length - offset);

        if (description_end != NULL) {
            offset = (size_t)(description_end - data) + 1;
            description_ended = true;
        }
    }

    if (!description_ended || offset >= length) return false;

    *image = data + offset;
    *image_size = length - offset;

    if (*format == TRACK_COVER_NONE) {
        *format = cover_format_from_data(*image, *image_size);
    }

    return *format != TRACK_COVER_NONE;
}

static void load_id3v2(FILE *file, Track_Metadata *metadata, Track_Cover *cover)
{
    unsigned char header[10];

    if (fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "ID3", 3) != 0) {
        return;
    }

    int version = header[3];
    uint32_t tag_size = read_synchsafe(&header[6]);

    if (version < 2 || version > 4 || tag_size > MAX_METADATA_BLOCK) return;

    unsigned char *tag = malloc((size_t)tag_size + 1);

    if (tag == NULL || fread(tag, 1, tag_size, file) != tag_size) {
        free(tag);
        return;
    }

    tag[tag_size] = 0;

    size_t offset = 0;

    if ((header[5] & 0x40) != 0 && version >= 3 && tag_size >= 4) {
        uint32_t extended_size =
            version == 4 ? read_synchsafe(tag) : read_u32_be(tag) + 4;

        if (extended_size > tag_size) {
            free(tag);
            return;
        }

        offset = extended_size;
    }

    while (offset < tag_size) {
        size_t frame_header_size = version == 2 ? 6 : 10;

        if (tag_size - offset < frame_header_size || tag[offset] == 0) break;

        const unsigned char *frame = tag + offset;
        uint32_t frame_size = version == 2   ? read_u24_be(frame + 3)
                              : version == 4 ? read_synchsafe(frame + 4)
                                             : read_u32_be(frame + 4);
        offset += frame_header_size;

        if (frame_size > tag_size - offset) break;

        if (metadata != NULL) {
            if (metadata->title[0] == '\0' &&
                id_matches(frame, "TT2", "TIT2", version)) {
                decode_id3_text(metadata->title, sizeof(metadata->title),
                                tag + offset, frame_size);
            } else if (metadata->artist[0] == '\0' &&
                       id_matches(frame, "TP1", "TPE1", version)) {
                decode_id3_text(metadata->artist, sizeof(metadata->artist),
                                tag + offset, frame_size);
            } else if (metadata->album[0] == '\0' &&
                       id_matches(frame, "TAL", "TALB", version)) {
                decode_id3_text(metadata->album, sizeof(metadata->album),
                                tag + offset, frame_size);
            }
        }

        if (cover != NULL && id_matches(frame, "PIC", "APIC", version)) {
            const unsigned char *image;
            size_t image_size;
            Track_Cover_Format format;
            unsigned char picture_type;

            if (parse_id3_cover(tag + offset, frame_size, version, &image,
                                &image_size, &format, &picture_type) &&
                (cover->data == NULL || picture_type == 3) &&
                copy_cover(cover, image, image_size, format) &&
                picture_type == 3 && metadata == NULL) {
                break;
            }
        }

        offset += frame_size;
    }

    free(tag);
}

static void load_id3v1(FILE *file, Track_Metadata *metadata)
{
    unsigned char tag[128];

    if (fseek(file, -128, SEEK_END) != 0 ||
        fread(tag, 1, sizeof(tag), file) != sizeof(tag) ||
        memcmp(tag, "TAG", 3) != 0) {
        return;
    }

    if (metadata->title[0] == '\0') {
        decode_latin1(metadata->title, sizeof(metadata->title), tag + 3, 30);
    }

    if (metadata->artist[0] == '\0') {
        decode_latin1(metadata->artist, sizeof(metadata->artist), tag + 33, 30);
    }

    if (metadata->album[0] == '\0') {
        decode_latin1(metadata->album, sizeof(metadata->album), tag + 63, 30);
    }
}

static bool key_matches(const unsigned char *comment, size_t key_length,
                        const char *key)
{
    return bytes_match(comment, key_length, key);
}

static void parse_vorbis_comments(const unsigned char *data, size_t length,
                                  Track_Metadata *metadata)
{
    if (length < 8) return;

    uint32_t vendor_length = read_u32_le(data);
    size_t offset = 4;

    if (vendor_length > length - offset) return;
    offset += vendor_length;
    if (length - offset < 4) return;

    uint32_t comment_count = read_u32_le(data + offset);
    offset += 4;

    for (uint32_t i = 0; i < comment_count; ++i) {
        if (length - offset < 4) return;

        uint32_t comment_length = read_u32_le(data + offset);
        offset += 4;

        if (comment_length > length - offset) return;

        const unsigned char *comment = data + offset;
        const unsigned char *separator = memchr(comment, '=', comment_length);

        if (separator != NULL) {
            size_t key_length = (size_t)(separator - comment);
            const unsigned char *value = separator + 1;
            size_t value_length = comment_length - key_length - 1;

            if (metadata->title[0] == '\0' &&
                key_matches(comment, key_length, "TITLE")) {
                copy_bytes(metadata->title, sizeof(metadata->title), value,
                           value_length);
            } else if (metadata->artist[0] == '\0' &&
                       key_matches(comment, key_length, "ARTIST")) {
                copy_bytes(metadata->artist, sizeof(metadata->artist), value,
                           value_length);
            } else if (metadata->album[0] == '\0' &&
                       key_matches(comment, key_length, "ALBUM")) {
                copy_bytes(metadata->album, sizeof(metadata->album), value,
                           value_length);
            }
        }

        offset += comment_length;
    }
}

static void load_flac(FILE *file, Track_Metadata *metadata)
{
    if (fseek(file, 4, SEEK_SET) != 0) return;

    bool last_block = false;

    while (!last_block) {
        unsigned char header[4];

        if (fread(header, 1, sizeof(header), file) != sizeof(header)) return;

        last_block = (header[0] & 0x80) != 0;
        unsigned int type = header[0] & 0x7f;
        uint32_t length = read_u24_be(header + 1);

        if (length > MAX_METADATA_BLOCK) return;

        if (type != 4) {
            if (fseek(file, (long)length, SEEK_CUR) != 0) return;
            continue;
        }

        unsigned char *block = malloc(length);

        if (block == NULL || fread(block, 1, length, file) != length) {
            free(block);
            return;
        }

        parse_vorbis_comments(block, length, metadata);
        free(block);
        return;
    }
}

static bool parse_flac_picture(const unsigned char *data, size_t length,
                               const unsigned char **image, size_t *image_size,
                               Track_Cover_Format *format,
                               uint32_t *picture_type)
{
    if (length < 8) return false;

    size_t offset = 0;
    *picture_type = read_u32_be(data + offset);
    offset += 4;

    uint32_t mime_length = read_u32_be(data + offset);
    offset += 4;

    if (mime_length > length - offset) return false;

    const unsigned char *mime = data + offset;
    offset += mime_length;

    if (length - offset < 4) return false;

    uint32_t description_length = read_u32_be(data + offset);
    offset += 4;

    if (description_length > length - offset) return false;

    offset += description_length;

    if (length - offset < 20) return false;

    offset += 16;
    uint32_t data_length = read_u32_be(data + offset);
    offset += 4;

    if (data_length == 0 || data_length > length - offset) return false;

    *image = data + offset;
    *image_size = data_length;
    *format = cover_format_from_mime(mime, mime_length);

    if (*format == TRACK_COVER_NONE) {
        *format = cover_format_from_data(*image, *image_size);
    }

    return *format != TRACK_COVER_NONE;
}

static bool load_flac_cover(FILE *file, Track_Cover *cover)
{
    if (fseek(file, 4, SEEK_SET) != 0) return false;

    bool last_block = false;

    while (!last_block) {
        unsigned char header[4];

        if (fread(header, 1, sizeof(header), file) != sizeof(header)) break;

        last_block = (header[0] & 0x80) != 0;
        unsigned int type = header[0] & 0x7f;
        uint32_t length = read_u24_be(header + 1);

        if (length > MAX_METADATA_BLOCK) break;

        if (type != 6) {
            if (fseek(file, (long)length, SEEK_CUR) != 0) break;
            continue;
        }

        unsigned char *block = malloc(length);

        if (block == NULL || fread(block, 1, length, file) != length) {
            free(block);
            break;
        }

        const unsigned char *image;
        size_t image_size;
        Track_Cover_Format format;
        uint32_t picture_type;
        bool parsed = parse_flac_picture(block, length, &image, &image_size,
                                         &format, &picture_type);

        if (parsed && (cover->data == NULL || picture_type == 3)) {
            copy_cover(cover, image, image_size, format);
        }

        free(block);

        if (parsed && picture_type == 3 && cover->data != NULL) break;
    }

    return cover->data != NULL;
}

static void load_wav(FILE *file, Track_Metadata *metadata)
{
    if (fseek(file, 12, SEEK_SET) != 0) return;

    for (;;) {
        unsigned char chunk_header[8];

        if (fread(chunk_header, 1, sizeof(chunk_header), file) !=
            sizeof(chunk_header)) {
            return;
        }

        uint32_t chunk_size = read_u32_le(chunk_header + 4);

        if (memcmp(chunk_header, "LIST", 4) != 0 || chunk_size < 4) {
            long skip = (long)chunk_size + (long)(chunk_size & 1u);

            if (fseek(file, skip, SEEK_CUR) != 0) return;
            continue;
        }

        unsigned char list_type[4];

        if (fread(list_type, 1, sizeof(list_type), file) != sizeof(list_type)) {
            return;
        }

        uint32_t remaining = chunk_size - 4;

        if (memcmp(list_type, "INFO", 4) != 0) {
            if (fseek(file, (long)remaining + (long)(chunk_size & 1u),
                      SEEK_CUR) != 0) {
                return;
            }

            continue;
        }

        while (remaining >= 8) {
            unsigned char info_header[8];

            if (fread(info_header, 1, sizeof(info_header), file) !=
                sizeof(info_header)) {
                return;
            }

            remaining -= 8;
            uint32_t value_size = read_u32_le(info_header + 4);

            if (value_size > remaining || value_size > MAX_METADATA_BLOCK) {
                return;
            }

            unsigned char *value = malloc(value_size);

            if (value == NULL ||
                fread(value, 1, value_size, file) != value_size) {
                free(value);
                return;
            }

            if (memcmp(info_header, "INAM", 4) == 0) {
                decode_latin1(metadata->title, sizeof(metadata->title), value,
                              value_size);
            } else if (memcmp(info_header, "IART", 4) == 0) {
                decode_latin1(metadata->artist, sizeof(metadata->artist), value,
                              value_size);
            } else if (memcmp(info_header, "IPRD", 4) == 0) {
                decode_latin1(metadata->album, sizeof(metadata->album), value,
                              value_size);
            }

            free(value);
            uint32_t padded_size = value_size + (value_size & 1u);

            if (padded_size > remaining) return;

            if ((value_size & 1u) != 0 && fseek(file, 1, SEEK_CUR) != 0) return;
            remaining -= padded_size;
        }

        return;
    }
}

static const char *file_name_from_path(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');

    if (slash == NULL || (backslash != NULL && backslash > slash)) {
        slash = backslash;
    }

    return slash == NULL ? path : slash + 1;
}

void metadata_load(const char *path, Track_Metadata *metadata)
{
    *metadata = (Track_Metadata){0};

    FILE *file = fs_fopen(path, "rb");

    if (file != NULL) {
        unsigned char signature[12] = {0};
        size_t signature_size = fread(signature, 1, sizeof(signature), file);

        if (signature_size >= 4 && memcmp(signature, "fLaC", 4) == 0) {
            load_flac(file, metadata);
        } else if (signature_size >= 12 && memcmp(signature, "RIFF", 4) == 0 &&
                   memcmp(signature + 8, "WAVE", 4) == 0) {
            load_wav(file, metadata);
        } else {
            load_id3v2(file, metadata, NULL);
            load_id3v1(file, metadata);
        }

        fclose(file);
    }

    if (metadata->title[0] == '\0') {
        snprintf(metadata->title, sizeof(metadata->title), "%s",
                 file_name_from_path(path));
    }
}

bool metadata_cover_load(const char *path, Track_Cover *cover)
{
    *cover = (Track_Cover){0};

    FILE *file = fs_fopen(path, "rb");

    if (file == NULL) return false;

    unsigned char signature[12] = {0};
    size_t signature_size = fread(signature, 1, sizeof(signature), file);

    if (signature_size >= 4 && memcmp(signature, "fLaC", 4) == 0) {
        load_flac_cover(file, cover);
    } else if (!(signature_size >= 12 && memcmp(signature, "RIFF", 4) == 0 &&
                 memcmp(signature + 8, "WAVE", 4) == 0)) {
        load_id3v2(file, NULL, cover);
    }

    fclose(file);
    return cover->data != NULL;
}

void metadata_cover_unload(Track_Cover *cover)
{
    free(cover->data);
    *cover = (Track_Cover){0};
}
