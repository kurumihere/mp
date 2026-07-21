#include "media_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#if !defined(_WIN32)
static char *copy_string(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = (char *)malloc(size);

    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}
#endif

#if defined(_WIN32) && defined(__has_include)
#if __has_include(<systemmediatransportcontrolsinterop.h>) &&                  \
    __has_include(<windows.media.h>) && __has_include(<wrl/client.h>)
#if __has_include(<wrl/event.h>) &&                                            \
    __has_include(<wrl/wrappers/corewrappers.h>)
#define MP_HAS_WINDOWS_SMTC 1
#endif
#endif
#endif

#if defined(MP_HAS_WINDOWS_SMTC)

#include <new>
#include <roapi.h>
#include <systemmediatransportcontrolsinterop.h>
#include <windows.foundation.h>
#include <windows.h>
#include <windows.media.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Foundation = ABI::Windows::Foundation;
namespace Media = ABI::Windows::Media;

typedef struct {
    ComPtr<Media::ISystemMediaTransportControls> controls;
    ComPtr<Media::ISystemMediaTransportControlsDisplayUpdater> updater;
    EventRegistrationToken button_token;
    bool button_registered;
    bool runtime_initialized;
    unsigned int pending_commands;
    char track_path[4096];
    Media_Session_Playback playback;
    ULONGLONG timeline_updated_at;
    double timeline_position;
} Media_Session_Impl;

static wchar_t *utf8_to_wide(const char *text)
{
    if (text == NULL) text = "";
    int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);

    if (size <= 0) return NULL;

    wchar_t *wide = (wchar_t *)malloc((size_t)size * sizeof(*wide));

    if (wide == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, size) == 0) {
        free(wide);
        return NULL;
    }

    return wide;
}

static Foundation::TimeSpan seconds_to_timespan(double seconds)
{
    Foundation::TimeSpan result = {};
    result.Duration = (INT64)(seconds * 10000000.0);
    return result;
}

extern "C" bool media_session_init(Media_Session *session, void *window_handle)
{
    *session = {};
    if (window_handle == NULL) return false;

    HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
    bool runtime_initialized = SUCCEEDED(result);

    if (result == RPC_E_CHANGED_MODE) result = S_OK;
    if (FAILED(result)) return false;

    Media_Session_Impl *impl = new(std::nothrow) Media_Session_Impl();

    if (impl == NULL) {
        if (runtime_initialized) RoUninitialize();
        return false;
    }

    impl->runtime_initialized = runtime_initialized;
    ComPtr<ISystemMediaTransportControlsInterop> interop;
    HStringReference class_name(
        RuntimeClass_Windows_Media_SystemMediaTransportControls);
    result = RoGetActivationFactory(
        class_name.Get(), __uuidof(ISystemMediaTransportControlsInterop),
        (void **)interop.GetAddressOf());

    if (SUCCEEDED(result)) {
        result = interop->GetForWindow(
            (HWND)window_handle, IID_PPV_ARGS(impl->controls.GetAddressOf()));
    }

    if (SUCCEEDED(result)) {
        auto handler = Callback<Foundation::ITypedEventHandler<
            Media::SystemMediaTransportControls *,
            Media::SystemMediaTransportControlsButtonPressedEventArgs *>>(
            [impl](Media::ISystemMediaTransportControls *,
                   Media::ISystemMediaTransportControlsButtonPressedEventArgs
                       *args) -> HRESULT {
                Media::SystemMediaTransportControlsButton button;
                HRESULT event_result = args->get_Button(&button);

                if (FAILED(event_result)) return event_result;

                unsigned int command = MEDIA_SESSION_COMMAND_NONE;

                switch (button) {
                case Media::SystemMediaTransportControlsButton_Play:
                    command = MEDIA_SESSION_COMMAND_PLAY;
                    break;
                case Media::SystemMediaTransportControlsButton_Pause:
                    command = MEDIA_SESSION_COMMAND_PAUSE;
                    break;
                case Media::SystemMediaTransportControlsButton_Next:
                    command = MEDIA_SESSION_COMMAND_NEXT;
                    break;
                case Media::SystemMediaTransportControlsButton_Previous:
                    command = MEDIA_SESSION_COMMAND_PREVIOUS;
                    break;
                case Media::SystemMediaTransportControlsButton_Stop:
                    command = MEDIA_SESSION_COMMAND_STOP;
                    break;
                default:
                    break;
                }

                if (command != MEDIA_SESSION_COMMAND_NONE) {
                    __atomic_fetch_or(&impl->pending_commands, command,
                                      __ATOMIC_RELEASE);
                }

                return S_OK;
            });
        result = impl->controls->add_ButtonPressed(handler.Get(),
                                                   &impl->button_token);
        impl->button_registered = SUCCEEDED(result);
    }

    if (SUCCEEDED(result))
        result =
            impl->controls->get_DisplayUpdater(impl->updater.GetAddressOf());
    if (SUCCEEDED(result))
        result = impl->updater->put_Type(Media::MediaPlaybackType_Music);
    if (SUCCEEDED(result)) result = impl->controls->put_IsEnabled(TRUE);

    if (FAILED(result)) {
        if (impl->button_registered) {
            impl->controls->remove_ButtonPressed(impl->button_token);
        }
        bool uninitialize = impl->runtime_initialized;
        delete impl;
        if (uninitialize) RoUninitialize();
        return false;
    }

    session->implementation = impl;
    return true;
}

extern "C" void
media_session_update(Media_Session *session, const char *track_path,
                     const Track_Metadata *metadata,
                     Media_Session_Playback playback, double position,
                     double duration, double volume, bool can_previous,
                     bool can_next, bool shuffle, Media_Session_Repeat repeat)
{
    (void)volume;
    (void)shuffle;
    (void)repeat;
    Media_Session_Impl *impl = (Media_Session_Impl *)session->implementation;

    if (impl == NULL) return;

    bool track_changed =
        (track_path == NULL) != (impl->track_path[0] == '\0') ||
        (track_path != NULL && strcmp(track_path, impl->track_path) != 0);

    if (track_changed && track_path == NULL) {
        impl->updater->ClearAll();
        impl->track_path[0] = '\0';
    } else if (track_changed) {
        wchar_t *title = utf8_to_wide(metadata == NULL ? "" : metadata->title);
        wchar_t *artist =
            utf8_to_wide(metadata == NULL ? "" : metadata->artist);
        wchar_t *album = utf8_to_wide(metadata == NULL ? "" : metadata->album);
        ComPtr<Media::IMusicDisplayProperties> music;

        if (title != NULL && artist != NULL && album != NULL &&
            SUCCEEDED(impl->updater->get_MusicProperties(&music))) {
            music->put_Title(HStringReference(title).Get());
            music->put_Artist(HStringReference(artist).Get());
            ComPtr<Media::IMusicDisplayProperties2> music2;

            if (SUCCEEDED(music.As(&music2))) {
                music2->put_AlbumTitle(HStringReference(album).Get());
            }
            impl->updater->Update();
        }

        free(title);
        free(artist);
        free(album);
        snprintf(impl->track_path, sizeof(impl->track_path), "%s",
                 track_path == NULL ? "" : track_path);
    }

    impl->controls->put_IsPlayEnabled(playback != MEDIA_SESSION_EMPTY);
    impl->controls->put_IsPauseEnabled(playback != MEDIA_SESSION_EMPTY);
    impl->controls->put_IsStopEnabled(playback != MEDIA_SESSION_EMPTY);
    impl->controls->put_IsNextEnabled(can_next);
    impl->controls->put_IsPreviousEnabled(can_previous);

    Media::MediaPlaybackStatus status = Media::MediaPlaybackStatus_Closed;

    if (playback == MEDIA_SESSION_PLAYING) {
        status = Media::MediaPlaybackStatus_Playing;
    } else if (playback == MEDIA_SESSION_PAUSED) {
        status = Media::MediaPlaybackStatus_Paused;
    } else if (playback == MEDIA_SESSION_STOPPED) {
        status = Media::MediaPlaybackStatus_Stopped;
    }

    bool state_changed = playback != impl->playback;

    if (state_changed || track_changed) {
        impl->controls->put_PlaybackStatus(status);
        impl->playback = playback;
    }

    ULONGLONG now = GetTickCount64();
    bool timeline_due = track_changed || state_changed ||
                        now - impl->timeline_updated_at >= 5000 ||
                        position + 1.0 < impl->timeline_position;

    if (duration > 0.0 && timeline_due) {
        ComPtr<Media::ISystemMediaTransportControls2> controls2;

        if (SUCCEEDED(impl->controls.As(&controls2))) {
            ComPtr<IInspectable> instance;
            HStringReference timeline_class(
                RuntimeClass_Windows_Media_SystemMediaTransportControlsTimelineProperties);

            if (SUCCEEDED(
                    RoActivateInstance(timeline_class.Get(), &instance))) {
                ComPtr<Media::ISystemMediaTransportControlsTimelineProperties>
                    timeline;

                if (SUCCEEDED(instance.As(&timeline))) {
                    Foundation::TimeSpan zero = seconds_to_timespan(0.0);
                    Foundation::TimeSpan cursor = seconds_to_timespan(position);
                    Foundation::TimeSpan length = seconds_to_timespan(duration);
                    timeline->put_StartTime(zero);
                    timeline->put_MinSeekTime(zero);
                    timeline->put_Position(cursor);
                    timeline->put_MaxSeekTime(length);
                    timeline->put_EndTime(length);
                    controls2->UpdateTimelineProperties(timeline.Get());
                    controls2->put_PlaybackRate(
                        playback == MEDIA_SESSION_PLAYING ? 1.0 : 0.0);
                    impl->timeline_updated_at = now;
                    impl->timeline_position = position;
                }
            }
        }
    }
}

extern "C" Media_Session_Event media_session_poll(Media_Session *session)
{
    Media_Session_Event event = {};
    Media_Session_Impl *impl = (Media_Session_Impl *)session->implementation;

    if (impl != NULL) {
        event.commands =
            __atomic_exchange_n(&impl->pending_commands, 0, __ATOMIC_ACQUIRE);
    }

    return event;
}

extern "C" void media_session_uninit(Media_Session *session)
{
    Media_Session_Impl *impl = (Media_Session_Impl *)session->implementation;

    if (impl == NULL) return;
    if (impl->button_registered) {
        impl->controls->remove_ButtonPressed(impl->button_token);
    }
    if (impl->updater) impl->updater->ClearAll();
    if (impl->controls) impl->controls->put_IsEnabled(FALSE);
    bool uninitialize = impl->runtime_initialized;
    delete impl;
    if (uninitialize) RoUninitialize();
    *session = {};
}

#elif defined(_WIN32)

extern "C" bool media_session_init(Media_Session *session, void *window_handle)
{
    (void)window_handle;
    *session = {};
    return false;
}

extern "C" void
media_session_update(Media_Session *session, const char *track_path,
                     const Track_Metadata *metadata,
                     Media_Session_Playback playback, double position,
                     double duration, double volume, bool can_previous,
                     bool can_next, bool shuffle, Media_Session_Repeat repeat)
{
    (void)session;
    (void)track_path;
    (void)metadata;
    (void)playback;
    (void)position;
    (void)duration;
    (void)volume;
    (void)can_previous;
    (void)can_next;
    (void)shuffle;
    (void)repeat;
}

extern "C" Media_Session_Event media_session_poll(Media_Session *session)
{
    (void)session;
    return {};
}

extern "C" void media_session_uninit(Media_Session *session)
{
    *session = {};
}

#elif defined(__APPLE__)

#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

typedef struct {
    unsigned int pending_commands;
    char *track_path;
    Media_Session_Playback playback;
    double published_position;
    double published_at;
    id command_tokens[6];
} Media_Session_Impl;

static NSString *string_from_utf8(const char *text)
{
    if (text == NULL || text[0] == '\0') return @"";
    NSString *result = [NSString stringWithUTF8String:text];
    return result == nil ? @"" : result;
}

static void queue_command(Media_Session_Impl *impl, unsigned int command)
{
    __atomic_fetch_or(&impl->pending_commands, command, __ATOMIC_RELEASE);
}

static id add_command_handler(MPRemoteCommand *command,
                              Media_Session_Impl *impl,
                              unsigned int media_command)
{
    command.enabled = YES;
    return [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                        MPRemoteCommandEvent *event) {
      (void)event;
      queue_command(impl, media_command);
      return MPRemoteCommandHandlerStatusSuccess;
    }];
}

bool media_session_init(Media_Session *session, void *window_handle)
{
    (void)window_handle;
    *session = (Media_Session){0};

    if (@available(macOS 10.12.2, *)) {
        Media_Session_Impl *impl = calloc(1, sizeof(*impl));

        if (impl == NULL) return false;

        MPRemoteCommandCenter *commands =
            [MPRemoteCommandCenter sharedCommandCenter];
        impl->command_tokens[0] = add_command_handler(
            commands.playCommand, impl, MEDIA_SESSION_COMMAND_PLAY);
        impl->command_tokens[1] = add_command_handler(
            commands.pauseCommand, impl, MEDIA_SESSION_COMMAND_PAUSE);
        impl->command_tokens[2] =
            add_command_handler(commands.togglePlayPauseCommand, impl,
                                MEDIA_SESSION_COMMAND_TOGGLE);
        impl->command_tokens[3] = add_command_handler(
            commands.nextTrackCommand, impl, MEDIA_SESSION_COMMAND_NEXT);
        impl->command_tokens[4] =
            add_command_handler(commands.previousTrackCommand, impl,
                                MEDIA_SESSION_COMMAND_PREVIOUS);
        impl->command_tokens[5] = add_command_handler(
            commands.stopCommand, impl, MEDIA_SESSION_COMMAND_STOP);
        session->implementation = impl;
        return true;
    }

    return false;
}

void media_session_update(Media_Session *session, const char *track_path,
                          const Track_Metadata *metadata,
                          Media_Session_Playback playback, double position,
                          double duration, double volume, bool can_previous,
                          bool can_next, bool shuffle,
                          Media_Session_Repeat repeat)
{
    (void)volume;
    (void)shuffle;
    (void)repeat;
    Media_Session_Impl *impl = session->implementation;

    if (impl == NULL) return;

    MPRemoteCommandCenter *commands =
        [MPRemoteCommandCenter sharedCommandCenter];
    commands.nextTrackCommand.enabled = can_next;
    commands.previousTrackCommand.enabled = can_previous;
    commands.playCommand.enabled = playback != MEDIA_SESSION_EMPTY;
    commands.pauseCommand.enabled = playback != MEDIA_SESSION_EMPTY;
    commands.togglePlayPauseCommand.enabled = playback != MEDIA_SESSION_EMPTY;
    commands.stopCommand.enabled = playback != MEDIA_SESSION_EMPTY;

    bool track_changed =
        (track_path == NULL && impl->track_path != NULL) ||
        (track_path != NULL && (impl->track_path == NULL ||
                                strcmp(track_path, impl->track_path) != 0));
    MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];

    if (track_path == NULL) {
        if (impl->track_path != NULL) {
            center.nowPlayingInfo = nil;
            center.playbackState = MPNowPlayingPlaybackStateStopped;
            free(impl->track_path);
            impl->track_path = NULL;
        }
        impl->playback = playback;
        return;
    }

    bool state_changed = playback != impl->playback;
    double now = CFAbsoluteTimeGetCurrent();
    bool timeline_due = state_changed || track_changed ||
                        now - impl->published_at >= 5.0 ||
                        position + 1.0 < impl->published_position;

    if (track_changed) {
        NSMutableDictionary *info = [NSMutableDictionary dictionary];
        info[MPMediaItemPropertyTitle] =
            string_from_utf8(metadata == NULL ? "" : metadata->title);
        info[MPMediaItemPropertyArtist] =
            string_from_utf8(metadata == NULL ? "" : metadata->artist);
        info[MPMediaItemPropertyAlbumTitle] =
            string_from_utf8(metadata == NULL ? "" : metadata->album);
        info[MPMediaItemPropertyPlaybackDuration] = @(duration);
        info[MPNowPlayingInfoPropertyDefaultPlaybackRate] = @1.0;
        info[MPNowPlayingInfoPropertyMediaType] =
            @(MPNowPlayingInfoMediaTypeAudio);

        Track_Cover cover;

        if (metadata_cover_load(track_path, &cover)) {
            NSData *data = [NSData dataWithBytes:cover.data length:cover.size];
            NSImage *image = [[NSImage alloc] initWithData:data];

            if (image != nil && image.size.width > 0.0 &&
                image.size.height > 0.0) {
                MPMediaItemArtwork *artwork = [[MPMediaItemArtwork alloc]
                    initWithBoundsSize:image.size
                        requestHandler:^NSImage *(CGSize requested_size) {
                          NSImage *result = [image copy];
                          result.size = NSMakeSize(requested_size.width,
                                                   requested_size.height);
                          return [result autorelease];
                        }];
                info[MPMediaItemPropertyArtwork] = artwork;
                [artwork release];
            }

            [image release];
            metadata_cover_unload(&cover);
        }

        center.nowPlayingInfo = info;
        free(impl->track_path);
        impl->track_path = copy_string(track_path);
    }

    if (timeline_due) {
        NSMutableDictionary *info = [center.nowPlayingInfo mutableCopy];

        if (info != nil) {
            info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(position);
            info[MPNowPlayingInfoPropertyPlaybackRate] =
                playback == MEDIA_SESSION_PLAYING ? @1.0 : @0.0;
            center.nowPlayingInfo = info;
            [info release];
        }

        impl->published_position = position;
        impl->published_at = now;
    }

    if (playback == MEDIA_SESSION_PLAYING) {
        center.playbackState = MPNowPlayingPlaybackStatePlaying;
    } else if (playback == MEDIA_SESSION_PAUSED) {
        center.playbackState = MPNowPlayingPlaybackStatePaused;
    } else {
        center.playbackState = MPNowPlayingPlaybackStateStopped;
    }

    impl->playback = playback;
}

Media_Session_Event media_session_poll(Media_Session *session)
{
    Media_Session_Event event = {0};
    Media_Session_Impl *impl = session->implementation;

    if (impl != NULL) {
        event.commands =
            __atomic_exchange_n(&impl->pending_commands, 0, __ATOMIC_ACQUIRE);
    }

    return event;
}

void media_session_uninit(Media_Session *session)
{
    Media_Session_Impl *impl = session->implementation;

    if (impl == NULL) return;

    if (@available(macOS 10.12.2, *)) {
        MPRemoteCommandCenter *commands =
            [MPRemoteCommandCenter sharedCommandCenter];
        MPRemoteCommand *registered_commands[] = {
            commands.playCommand,
            commands.pauseCommand,
            commands.togglePlayPauseCommand,
            commands.nextTrackCommand,
            commands.previousTrackCommand,
            commands.stopCommand,
        };

        for (size_t i = 0; i < 6; ++i) {
            if (impl->command_tokens[i] != nil) {
                [registered_commands[i] removeTarget:impl->command_tokens[i]];
            }
        }

        MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
        center.playbackState = MPNowPlayingPlaybackStateStopped;
        center.nowPlayingInfo = nil;
    }

    free(impl->track_path);
    free(impl);
    *session = (Media_Session){0};
}

#else

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <math.h>

#define MPRIS_BUS_NAME "org.mpris.MediaPlayer2.mp"
#define MPRIS_OBJECT_PATH "/org/mpris/MediaPlayer2"
#define MPRIS_PLAYER_INTERFACE "org.mpris.MediaPlayer2.Player"

static const char mpris_introspection_xml[] =
    "<node>"
    " <interface name='org.mpris.MediaPlayer2'>"
    "  <method name='Raise'/><method name='Quit'/>"
    "  <property name='CanQuit' type='b' access='read'/>"
    "  <property name='CanRaise' type='b' access='read'/>"
    "  <property name='HasTrackList' type='b' access='read'/>"
    "  <property name='Identity' type='s' access='read'/>"
    "  <property name='DesktopEntry' type='s' access='read'/>"
    "  <property name='SupportedUriSchemes' type='as' access='read'/>"
    "  <property name='SupportedMimeTypes' type='as' access='read'/>"
    " </interface>"
    " <interface name='org.mpris.MediaPlayer2.Player'>"
    "  <method name='Next'/><method name='Previous'/>"
    "  <method name='Pause'/><method name='PlayPause'/>"
    "  <method name='Stop'/><method name='Play'/>"
    "  <method name='Seek'><arg direction='in' type='x' "
    "name='Offset'/></method>"
    "  <method name='SetPosition'>"
    "   <arg direction='in' type='o' name='TrackId'/>"
    "   <arg direction='in' type='x' name='Position'/>"
    "  </method>"
    "  <method name='OpenUri'><arg direction='in' type='s' "
    "name='Uri'/></method>"
    "  <signal name='Seeked'><arg type='x' name='Position'/></signal>"
    "  <property name='PlaybackStatus' type='s' access='read'/>"
    "  <property name='LoopStatus' type='s' access='readwrite'/>"
    "  <property name='Rate' type='d' access='readwrite'/>"
    "  <property name='Shuffle' type='b' access='readwrite'/>"
    "  <property name='Metadata' type='a{sv}' access='read'/>"
    "  <property name='Volume' type='d' access='readwrite'/>"
    "  <property name='Position' type='x' access='read'/>"
    "  <property name='MinimumRate' type='d' access='read'/>"
    "  <property name='MaximumRate' type='d' access='read'/>"
    "  <property name='CanGoNext' type='b' access='read'/>"
    "  <property name='CanGoPrevious' type='b' access='read'/>"
    "  <property name='CanPlay' type='b' access='read'/>"
    "  <property name='CanPause' type='b' access='read'/>"
    "  <property name='CanSeek' type='b' access='read'/>"
    "  <property name='CanControl' type='b' access='read'/>"
    " </interface>"
    "</node>";

typedef struct {
    GDBusConnection *connection;
    GDBusNodeInfo *node_info;
    guint root_registration;
    guint player_registration;
    guint name_owner;
    Media_Session_Event pending;
    char *track_path;
    char *art_path;
    Track_Metadata metadata;
    char track_id[96];
    unsigned long track_serial;
    Media_Session_Playback playback;
    double position;
    double duration;
    double volume;
    bool can_previous;
    bool can_next;
    bool shuffle;
    Media_Session_Repeat repeat;
    bool published;
    bool seek_signal_pending;
} Media_Session_Impl;

static gint64 seconds_to_microseconds(double seconds)
{
    if (!isfinite(seconds) || seconds <= 0.0) return 0;
    return (gint64)(seconds * 1000000.0 + 0.5);
}

static const char *playback_status(Media_Session_Playback playback)
{
    if (playback == MEDIA_SESSION_PLAYING) return "Playing";
    if (playback == MEDIA_SESSION_PAUSED) return "Paused";
    return "Stopped";
}

static const char *loop_status(Media_Session_Repeat repeat)
{
    if (repeat == MEDIA_SESSION_REPEAT_TRACK) return "Track";
    if (repeat == MEDIA_SESSION_REPEAT_PLAYLIST) return "Playlist";
    return "None";
}

static GVariant *metadata_variant(const Media_Session_Impl *impl)
{
    GVariantBuilder metadata;
    g_variant_builder_init(&metadata, G_VARIANT_TYPE("a{sv}"));

    if (impl->track_path == NULL) return g_variant_builder_end(&metadata);

    g_variant_builder_add(&metadata, "{sv}", "mpris:trackid",
                          g_variant_new_object_path(impl->track_id));
    g_variant_builder_add(
        &metadata, "{sv}", "mpris:length",
        g_variant_new_int64(seconds_to_microseconds(impl->duration)));
    g_variant_builder_add(&metadata, "{sv}", "xesam:title",
                          g_variant_new_string(impl->metadata.title));

    if (impl->metadata.artist[0] != '\0') {
        const char *artists[] = {impl->metadata.artist};
        g_variant_builder_add(&metadata, "{sv}", "xesam:artist",
                              g_variant_new_strv(artists, 1));
    }

    if (impl->metadata.album[0] != '\0') {
        g_variant_builder_add(&metadata, "{sv}", "xesam:album",
                              g_variant_new_string(impl->metadata.album));
    }

    if (impl->art_path != NULL) {
        char *art_uri = g_filename_to_uri(impl->art_path, NULL, NULL);

        if (art_uri != NULL) {
            g_variant_builder_add(&metadata, "{sv}", "mpris:artUrl",
                                  g_variant_new_string(art_uri));
            g_free(art_uri);
        }
    }

    GError *error = NULL;
    char *uri = g_filename_to_uri(impl->track_path, NULL, &error);

    if (uri != NULL) {
        g_variant_builder_add(&metadata, "{sv}", "xesam:url",
                              g_variant_new_string(uri));
        g_free(uri);
    }
    if (error != NULL) g_error_free(error);

    return g_variant_builder_end(&metadata);
}

static GVariant *mpris_get_property(GDBusConnection *connection,
                                    const char *sender, const char *object_path,
                                    const char *interface_name,
                                    const char *property_name, GError **error,
                                    void *user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)error;
    Media_Session_Impl *impl = user_data;

    if (strcmp(interface_name, "org.mpris.MediaPlayer2") == 0) {
        if (strcmp(property_name, "CanQuit") == 0 ||
            strcmp(property_name, "CanRaise") == 0 ||
            strcmp(property_name, "HasTrackList") == 0) {
            return g_variant_new_boolean(false);
        }
        if (strcmp(property_name, "Identity") == 0) {
            return g_variant_new_string("mp");
        }
        if (strcmp(property_name, "DesktopEntry") == 0) {
            return g_variant_new_string("io.github.kurumihere.mp");
        }
        if (strcmp(property_name, "SupportedUriSchemes") == 0 ||
            strcmp(property_name, "SupportedMimeTypes") == 0) {
            return g_variant_new_strv(NULL, 0);
        }
    }

    if (strcmp(property_name, "PlaybackStatus") == 0) {
        return g_variant_new_string(playback_status(impl->playback));
    }
    if (strcmp(property_name, "LoopStatus") == 0) {
        return g_variant_new_string(loop_status(impl->repeat));
    }
    if (strcmp(property_name, "Rate") == 0 ||
        strcmp(property_name, "MinimumRate") == 0 ||
        strcmp(property_name, "MaximumRate") == 0) {
        return g_variant_new_double(1.0);
    }
    if (strcmp(property_name, "Shuffle") == 0) {
        return g_variant_new_boolean(impl->shuffle);
    }
    if (strcmp(property_name, "Metadata") == 0) return metadata_variant(impl);
    if (strcmp(property_name, "Volume") == 0) {
        return g_variant_new_double(impl->volume);
    }
    if (strcmp(property_name, "Position") == 0) {
        return g_variant_new_int64(seconds_to_microseconds(impl->position));
    }
    if (strcmp(property_name, "CanGoNext") == 0) {
        return g_variant_new_boolean(impl->can_next);
    }
    if (strcmp(property_name, "CanGoPrevious") == 0) {
        return g_variant_new_boolean(impl->can_previous);
    }
    if (strcmp(property_name, "CanPlay") == 0 ||
        strcmp(property_name, "CanPause") == 0 ||
        strcmp(property_name, "CanSeek") == 0) {
        return g_variant_new_boolean(impl->track_path != NULL);
    }
    if (strcmp(property_name, "CanControl") == 0) {
        return g_variant_new_boolean(true);
    }

    return NULL;
}

static gboolean mpris_set_property(GDBusConnection *connection,
                                   const char *sender, const char *object_path,
                                   const char *interface_name,
                                   const char *property_name, GVariant *value,
                                   GError **error, void *user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    Media_Session_Impl *impl = user_data;

    if (strcmp(property_name, "Volume") == 0) {
        impl->pending.volume_requested = true;
        impl->pending.volume = g_variant_get_double(value);
        return true;
    }
    if (strcmp(property_name, "Shuffle") == 0) {
        impl->pending.shuffle_requested = true;
        impl->pending.shuffle = g_variant_get_boolean(value);
        return true;
    }
    if (strcmp(property_name, "LoopStatus") == 0) {
        const char *status = g_variant_get_string(value, NULL);
        impl->pending.repeat_requested = true;

        if (strcmp(status, "Track") == 0) {
            impl->pending.repeat = MEDIA_SESSION_REPEAT_TRACK;
        } else if (strcmp(status, "Playlist") == 0) {
            impl->pending.repeat = MEDIA_SESSION_REPEAT_PLAYLIST;
        } else if (strcmp(status, "None") == 0) {
            impl->pending.repeat = MEDIA_SESSION_REPEAT_NONE;
        } else {
            impl->pending.repeat_requested = false;
            g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                        "Invalid loop status");
            return false;
        }
        return true;
    }
    if (strcmp(property_name, "Rate") == 0 &&
        g_variant_get_double(value) == 1.0) {
        return true;
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
                "Property is not writable");
    return false;
}

static void mpris_method_call(GDBusConnection *connection, const char *sender,
                              const char *object_path,
                              const char *interface_name,
                              const char *method_name, GVariant *parameters,
                              GDBusMethodInvocation *invocation,
                              void *user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    Media_Session_Impl *impl = user_data;
    unsigned int command = MEDIA_SESSION_COMMAND_NONE;

    if (strcmp(interface_name, "org.mpris.MediaPlayer2") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (strcmp(method_name, "Next") == 0) {
        command = MEDIA_SESSION_COMMAND_NEXT;
    } else if (strcmp(method_name, "Previous") == 0) {
        command = MEDIA_SESSION_COMMAND_PREVIOUS;
    } else if (strcmp(method_name, "Pause") == 0) {
        command = MEDIA_SESSION_COMMAND_PAUSE;
    } else if (strcmp(method_name, "PlayPause") == 0) {
        command = MEDIA_SESSION_COMMAND_TOGGLE;
    } else if (strcmp(method_name, "Stop") == 0) {
        command = MEDIA_SESSION_COMMAND_STOP;
    } else if (strcmp(method_name, "Play") == 0) {
        command = MEDIA_SESSION_COMMAND_PLAY;
    } else if (strcmp(method_name, "Seek") == 0) {
        gint64 offset;
        g_variant_get(parameters, "(x)", &offset);
        impl->pending.seek_requested = true;
        impl->pending.seek_position =
            impl->position + (double)offset / 1000000.0;
    } else if (strcmp(method_name, "SetPosition") == 0) {
        const char *track_id;
        gint64 position;
        g_variant_get(parameters, "(&ox)", &track_id, &position);

        if (strcmp(track_id, impl->track_id) == 0 && position >= 0 &&
            position <= seconds_to_microseconds(impl->duration)) {
            impl->pending.seek_requested = true;
            impl->pending.seek_position = (double)position / 1000000.0;
        }
    } else if (strcmp(method_name, "OpenUri") == 0) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                              G_DBUS_ERROR_NOT_SUPPORTED,
                                              "Opening URIs is not supported");
        return;
    }

    impl->pending.commands |= command;
    g_dbus_method_invocation_return_value(invocation, NULL);
}

static const GDBusInterfaceVTable mpris_vtable = {
    .method_call = mpris_method_call,
    .get_property = mpris_get_property,
    .set_property = mpris_set_property,
};

bool media_session_init(Media_Session *session, void *window_handle)
{
    (void)window_handle;
    *session = (Media_Session){0};
    Media_Session_Impl *impl = calloc(1, sizeof(*impl));

    if (impl == NULL) return false;

    GError *error = NULL;
    impl->node_info =
        g_dbus_node_info_new_for_xml(mpris_introspection_xml, &error);
    impl->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

    if (impl->node_info != NULL && impl->connection != NULL) {
        impl->root_registration = g_dbus_connection_register_object(
            impl->connection, MPRIS_OBJECT_PATH, impl->node_info->interfaces[0],
            &mpris_vtable, impl, NULL, &error);
        impl->player_registration = g_dbus_connection_register_object(
            impl->connection, MPRIS_OBJECT_PATH, impl->node_info->interfaces[1],
            &mpris_vtable, impl, NULL, &error);
    }

    if (impl->root_registration == 0 || impl->player_registration == 0) {
        if (error != NULL) {
            mp_log(WARNING, "failed to initialize MPRIS: %s", error->message);
            g_error_free(error);
        }
        if (impl->root_registration != 0) {
            g_dbus_connection_unregister_object(impl->connection,
                                                impl->root_registration);
        }
        if (impl->player_registration != 0) {
            g_dbus_connection_unregister_object(impl->connection,
                                                impl->player_registration);
        }
        if (impl->connection != NULL) g_object_unref(impl->connection);
        if (impl->node_info != NULL) g_dbus_node_info_unref(impl->node_info);
        free(impl);
        return false;
    }

    impl->name_owner = g_bus_own_name_on_connection(
        impl->connection, MPRIS_BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
        NULL, NULL, NULL);
    session->implementation = impl;
    return true;
}

static void emit_player_properties(Media_Session_Impl *impl)
{
    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(
        &changed, "{sv}", "PlaybackStatus",
        g_variant_new_string(playback_status(impl->playback)));
    g_variant_builder_add(&changed, "{sv}", "LoopStatus",
                          g_variant_new_string(loop_status(impl->repeat)));
    g_variant_builder_add(&changed, "{sv}", "Shuffle",
                          g_variant_new_boolean(impl->shuffle));
    g_variant_builder_add(&changed, "{sv}", "Metadata", metadata_variant(impl));
    g_variant_builder_add(&changed, "{sv}", "Volume",
                          g_variant_new_double(impl->volume));
    g_variant_builder_add(&changed, "{sv}", "CanGoNext",
                          g_variant_new_boolean(impl->can_next));
    g_variant_builder_add(&changed, "{sv}", "CanGoPrevious",
                          g_variant_new_boolean(impl->can_previous));
    bool has_track = impl->track_path != NULL;
    g_variant_builder_add(&changed, "{sv}", "CanPlay",
                          g_variant_new_boolean(has_track));
    g_variant_builder_add(&changed, "{sv}", "CanPause",
                          g_variant_new_boolean(has_track));
    g_variant_builder_add(&changed, "{sv}", "CanSeek",
                          g_variant_new_boolean(has_track));
    g_dbus_connection_emit_signal(
        impl->connection, NULL, MPRIS_OBJECT_PATH,
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(s@a{sv}@as)", MPRIS_PLAYER_INTERFACE,
                      g_variant_builder_end(&changed),
                      g_variant_new_strv(NULL, 0)),
        NULL);
}

static void update_cached_art(Media_Session_Impl *impl, const char *track_path)
{
    if (impl->art_path != NULL) {
        g_remove(impl->art_path);
        g_free(impl->art_path);
        impl->art_path = NULL;
    }

    if (track_path == NULL) return;

    Track_Cover cover;

    if (!metadata_cover_load(track_path, &cover)) return;

    char *directory = g_build_filename(g_get_user_cache_dir(), "mp", NULL);

    if (g_mkdir_with_parents(directory, 0700) == 0) {
        const char *extension = cover.format == TRACK_COVER_PNG ? "png" : "jpg";
        impl->art_path =
            g_strdup_printf("%s/now-playing.%s", directory, extension);

        if (!g_file_set_contents(impl->art_path, (const char *)cover.data,
                                 (gssize)cover.size, NULL)) {
            g_free(impl->art_path);
            impl->art_path = NULL;
        }
    }

    g_free(directory);
    metadata_cover_unload(&cover);
}

void media_session_update(Media_Session *session, const char *track_path,
                          const Track_Metadata *metadata,
                          Media_Session_Playback playback, double position,
                          double duration, double volume, bool can_previous,
                          bool can_next, bool shuffle,
                          Media_Session_Repeat repeat)
{
    Media_Session_Impl *impl = session->implementation;

    if (impl == NULL) return;

    bool track_changed =
        (track_path == NULL && impl->track_path != NULL) ||
        (track_path != NULL && (impl->track_path == NULL ||
                                strcmp(track_path, impl->track_path) != 0));
    bool changed =
        !impl->published || track_changed || playback != impl->playback ||
        duration != impl->duration || volume != impl->volume ||
        can_previous != impl->can_previous || can_next != impl->can_next ||
        shuffle != impl->shuffle || repeat != impl->repeat;

    if (track_changed) {
        free(impl->track_path);
        impl->track_path = track_path == NULL ? NULL : copy_string(track_path);
        impl->metadata = metadata == NULL ? (Track_Metadata){0} : *metadata;
        update_cached_art(impl, track_path);
        ++impl->track_serial;
        snprintf(impl->track_id, sizeof(impl->track_id),
                 "/io/github/kurumihere/mp/Track/%lu", impl->track_serial);
    }

    impl->playback = playback;
    impl->position = position;
    impl->duration = duration;
    impl->volume = volume;
    impl->can_previous = can_previous;
    impl->can_next = can_next;
    impl->shuffle = shuffle;
    impl->repeat = repeat;

    if (changed) emit_player_properties(impl);

    if (impl->seek_signal_pending) {
        g_dbus_connection_emit_signal(
            impl->connection, NULL, MPRIS_OBJECT_PATH, MPRIS_PLAYER_INTERFACE,
            "Seeked", g_variant_new("(x)", seconds_to_microseconds(position)),
            NULL);
        impl->seek_signal_pending = false;
    }
    impl->published = true;
}

Media_Session_Event media_session_poll(Media_Session *session)
{
    Media_Session_Event event = {0};
    Media_Session_Impl *impl = session->implementation;

    if (impl != NULL) {
        event = impl->pending;
        impl->seek_signal_pending = event.seek_requested;
        impl->pending = (Media_Session_Event){0};
    }

    return event;
}

void media_session_uninit(Media_Session *session)
{
    Media_Session_Impl *impl = session->implementation;

    if (impl == NULL) return;
    if (impl->name_owner != 0) g_bus_unown_name(impl->name_owner);
    g_dbus_connection_unregister_object(impl->connection,
                                        impl->root_registration);
    g_dbus_connection_unregister_object(impl->connection,
                                        impl->player_registration);
    g_dbus_node_info_unref(impl->node_info);
    g_object_unref(impl->connection);
    if (impl->art_path != NULL) {
        g_remove(impl->art_path);
        g_free(impl->art_path);
    }
    free(impl->track_path);
    free(impl);
    *session = (Media_Session){0};
}

#endif
