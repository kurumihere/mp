#ifndef MP_MEDIA_KEYS_H
#define MP_MEDIA_KEYS_H

#include <stdbool.h>

typedef enum {
    MEDIA_KEY_NONE = 0,
    MEDIA_KEY_PLAY_PAUSE = 1 << 0,
    MEDIA_KEY_NEXT = 1 << 1,
    MEDIA_KEY_PREVIOUS = 1 << 2,
    MEDIA_KEY_STOP = 1 << 3,
    MEDIA_KEY_VOLUME_UP = 1 << 4,
    MEDIA_KEY_VOLUME_DOWN = 1 << 5,
    MEDIA_KEY_MUTE = 1 << 6,
} Media_Key;

void media_keys_init(bool global);
void media_keys_set_global(bool global);
unsigned int media_keys_poll(bool focused, bool global);
void media_keys_uninit(void);

#endif
