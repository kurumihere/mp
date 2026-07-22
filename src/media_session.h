#ifndef MP_MEDIA_SESSION_H
#define MP_MEDIA_SESSION_H

#include <stdbool.h>

#include "metadata.h"

typedef enum {
    MEDIA_SESSION_EMPTY,
    MEDIA_SESSION_STOPPED,
    MEDIA_SESSION_PLAYING,
    MEDIA_SESSION_PAUSED,
} Media_Session_Playback;

typedef enum {
    MEDIA_SESSION_REPEAT_NONE,
    MEDIA_SESSION_REPEAT_TRACK,
    MEDIA_SESSION_REPEAT_PLAYLIST,
} Media_Session_Repeat;

typedef enum {
    MEDIA_SESSION_COMMAND_NONE = 0,
    MEDIA_SESSION_COMMAND_PLAY = 1 << 0,
    MEDIA_SESSION_COMMAND_PAUSE = 1 << 1,
    MEDIA_SESSION_COMMAND_TOGGLE = 1 << 2,
    MEDIA_SESSION_COMMAND_NEXT = 1 << 3,
    MEDIA_SESSION_COMMAND_PREVIOUS = 1 << 4,
    MEDIA_SESSION_COMMAND_STOP = 1 << 5,
} Media_Session_Command;

typedef struct {
    unsigned int commands;
    bool seek_requested;
    double seek_position;
    bool volume_requested;
    double volume;
    bool shuffle_requested;
    bool shuffle;
    bool repeat_requested;
    Media_Session_Repeat repeat;
} Media_Session_Event;

typedef struct {
    void *implementation;
} Media_Session;

#ifdef __cplusplus
extern "C" {
#endif

bool media_session_init(Media_Session *session, void *window_handle);
void media_session_update(Media_Session *session, const char *track_path,
                          const Track_Metadata *metadata,
                          Media_Session_Playback playback, double position,
                          double duration, double volume, bool can_previous,
                          bool can_next, bool shuffle,
                          Media_Session_Repeat repeat);
Media_Session_Event media_session_poll(Media_Session *session);
void media_session_uninit(Media_Session *session);

#ifdef __cplusplus
}
#endif

#endif
