#include "media_keys.h"

#if defined(_WIN32)

#include <windows.h>

typedef struct {
    int virtual_key;
    Media_Key key;
    bool down;
} Media_Key_State;

static Media_Key_State media_key_states[] = {
    {VK_MEDIA_PLAY_PAUSE, MEDIA_KEY_PLAY_PAUSE, false},
    {VK_MEDIA_NEXT_TRACK, MEDIA_KEY_NEXT, false},
    {VK_MEDIA_PREV_TRACK, MEDIA_KEY_PREVIOUS, false},
    {VK_MEDIA_STOP, MEDIA_KEY_STOP, false},
    {VK_VOLUME_UP, MEDIA_KEY_VOLUME_UP, false},
    {VK_VOLUME_DOWN, MEDIA_KEY_VOLUME_DOWN, false},
    {VK_VOLUME_MUTE, MEDIA_KEY_MUTE, false},
};

void media_keys_init(bool global)
{
    (void)global;
}

void media_keys_set_global(bool global)
{
    (void)global;
}

unsigned int media_keys_poll(bool focused, bool global)
{
    unsigned int pressed_keys = MEDIA_KEY_NONE;
    bool enabled = focused || global;

    for (unsigned int i = 0;
         i < sizeof(media_key_states) / sizeof(media_key_states[0]); ++i) {
        Media_Key_State *state = &media_key_states[i];
        bool down = (GetAsyncKeyState(state->virtual_key) & 0x8000) != 0;

        if (enabled && down && !state->down) pressed_keys |= state->key;
        state->down = down;
    }

    return pressed_keys;
}

void media_keys_uninit(void)
{
    for (unsigned int i = 0;
         i < sizeof(media_key_states) / sizeof(media_key_states[0]); ++i) {
        media_key_states[i].down = false;
    }
}

#elif defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <IOKit/hidsystem/ev_keymap.h>

static id local_media_key_monitor;
static id global_media_key_monitor;
static unsigned int pending_keys;

static unsigned int media_key_for_type(int type)
{
    switch (type) {
    case NX_KEYTYPE_PLAY:
        return MEDIA_KEY_PLAY_PAUSE;
    case NX_KEYTYPE_NEXT:
        return MEDIA_KEY_NEXT;
    case NX_KEYTYPE_PREVIOUS:
        return MEDIA_KEY_PREVIOUS;
    case NX_KEYTYPE_SOUND_UP:
        return MEDIA_KEY_VOLUME_UP;
    case NX_KEYTYPE_SOUND_DOWN:
        return MEDIA_KEY_VOLUME_DOWN;
    case NX_KEYTYPE_MUTE:
        return MEDIA_KEY_MUTE;
    default:
        return MEDIA_KEY_NONE;
    }
}

static void record_media_key(NSEvent *event)
{
    if ([event subtype] != 8) return;

    int data = (int)[event data1];
    int state = (data & 0x0000ff00) >> 8;
    bool repeated = (data & 1) != 0;

    if (state == 0x0a && !repeated) {
        int type = (data & 0xffff0000) >> 16;
        pending_keys |= media_key_for_type(type);
    }
}

void media_keys_set_global(bool global)
{
    if (global && global_media_key_monitor == nil) {
        global_media_key_monitor = [NSEvent
            addGlobalMonitorForEventsMatchingMask:NSEventMaskSystemDefined
                                          handler:^(NSEvent *event) {
                                            record_media_key(event);
                                          }];
    } else if (!global && global_media_key_monitor != nil) {
        [NSEvent removeMonitor:global_media_key_monitor];
        global_media_key_monitor = nil;
    }
}

void media_keys_init(bool global)
{
    local_media_key_monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskSystemDefined
                                     handler:^NSEvent *(NSEvent *event) {
                                       record_media_key(event);
                                       return event;
                                     }];
    media_keys_set_global(global);
}

unsigned int media_keys_poll(bool focused, bool global)
{
    unsigned int keys = pending_keys;
    pending_keys = MEDIA_KEY_NONE;
    return focused || global ? keys : MEDIA_KEY_NONE;
}

void media_keys_uninit(void)
{
    if (local_media_key_monitor != nil) {
        [NSEvent removeMonitor:local_media_key_monitor];
        local_media_key_monitor = nil;
    }

    media_keys_set_global(false);
    pending_keys = MEDIA_KEY_NONE;
}

#else

#define Font X11Font
#include <X11/XF86keysym.h>
#include <X11/Xlib.h>
#undef Font

typedef struct {
    KeyCode code;
    Media_Key key;
    bool down;
} Media_Key_State;

static Display *media_display;
static Media_Key_State media_key_states[] = {
    {0, MEDIA_KEY_PLAY_PAUSE, false}, {0, MEDIA_KEY_NEXT, false},
    {0, MEDIA_KEY_PREVIOUS, false},   {0, MEDIA_KEY_STOP, false},
    {0, MEDIA_KEY_VOLUME_UP, false},  {0, MEDIA_KEY_VOLUME_DOWN, false},
    {0, MEDIA_KEY_MUTE, false},
};

void media_keys_init(bool global)
{
    (void)global;
    static const KeySym symbols[] = {
        XF86XK_AudioPlay, XF86XK_AudioNext,        XF86XK_AudioPrev,
        XF86XK_AudioStop, XF86XK_AudioRaiseVolume, XF86XK_AudioLowerVolume,
        XF86XK_AudioMute,
    };

    media_display = XOpenDisplay(NULL);
    if (media_display == NULL) return;

    for (unsigned int i = 0;
         i < sizeof(media_key_states) / sizeof(media_key_states[0]); ++i) {
        media_key_states[i].code = XKeysymToKeycode(media_display, symbols[i]);
    }
}

void media_keys_set_global(bool global)
{
    (void)global;
}

unsigned int media_keys_poll(bool focused, bool global)
{
    if (media_display == NULL) return MEDIA_KEY_NONE;

    char keys[32];
    XQueryKeymap(media_display, keys);
    unsigned int pressed_keys = MEDIA_KEY_NONE;
    bool enabled = focused || global;

    for (unsigned int i = 0;
         i < sizeof(media_key_states) / sizeof(media_key_states[0]); ++i) {
        Media_Key_State *state = &media_key_states[i];
        bool down = state->code != 0 &&
                    (keys[state->code / 8] & (1 << (state->code % 8))) != 0;

        if (enabled && down && !state->down) pressed_keys |= state->key;
        state->down = down;
    }

    return pressed_keys;
}

void media_keys_uninit(void)
{
    if (media_display != NULL) XCloseDisplay(media_display);
    media_display = NULL;

    for (unsigned int i = 0;
         i < sizeof(media_key_states) / sizeof(media_key_states[0]); ++i) {
        media_key_states[i].code = 0;
        media_key_states[i].down = false;
    }
}

#endif
