#include "file_picker.h"

#include <stdlib.h>
#include <string.h>

#include "tinyfiledialogs/tinyfiledialogs.h"

typedef enum {
    FILE_PICKER_IDLE,
    FILE_PICKER_RUNNING,
    FILE_PICKER_COMPLETE,
} File_Picker_State;

static int file_picker_run(void *context)
{
    File_Picker *picker = context;
    char *selection = NULL;

    if (picker->mode == FILE_PICKER_FILES) {
        const char *filters[] = {
            "*.flac", "*.FLAC", "*.mp3", "*.MP3",  "*.wav",
            "*.WAV",  "*.m3u",  "*.M3U", "*.m3u8", "*.M3U8",
        };
        int filter_count = (int)(sizeof(filters) / sizeof(filters[0]));

        picker->graphical =
            tinyfd_openFileDialog("tinyfd_query", "", filter_count, filters,
                                  "Audio and playlist files", 1) != NULL;

        if (picker->graphical) {
            selection =
                tinyfd_openFileDialog("Open audio files", "", filter_count,
                                      filters, "Audio and playlist files", 1);
        }
    } else if (picker->mode == FILE_PICKER_FOLDER) {
        picker->graphical =
            tinyfd_selectFolderDialog("tinyfd_query", "") != NULL;

        if (picker->graphical) {
            selection = tinyfd_selectFolderDialog("Open music folder", "");
        }
    } else if (picker->mode == FILE_PICKER_SAVE_FILE) {
        const char *filters[] = {"*.m3u", "*.m3u8"};
        int filter_count = (int)(sizeof(filters) / sizeof(filters[0]));

        picker->graphical =
            tinyfd_saveFileDialog("tinyfd_query", "playlist.m3u", filter_count,
                                  filters, "Playlist files") != NULL;

        if (picker->graphical) {
            selection =
                tinyfd_saveFileDialog("Save playlist", "playlist.m3u",
                                      filter_count, filters, "Playlist files");
        }
    }

    if (selection != NULL) {
        size_t length = strlen(selection) + 1;
        picker->selection = malloc(length);

        if (picker->selection != NULL) {
            memcpy(picker->selection, selection, length);
        } else {
            picker->failed = true;
        }
    }

    atomic_store_explicit(&picker->state, FILE_PICKER_COMPLETE,
                          memory_order_release);
    return 0;
}

void file_picker_init(File_Picker *picker)
{
    *picker = (File_Picker){0};
    atomic_init(&picker->state, FILE_PICKER_IDLE);
}

bool file_picker_start(File_Picker *picker, File_Picker_Mode mode)
{
    if (mode == FILE_PICKER_NONE ||
        atomic_load_explicit(&picker->state, memory_order_acquire) !=
            FILE_PICKER_IDLE) {
        return false;
    }

    picker->mode = mode;
    picker->selection = NULL;
    picker->graphical = false;
    picker->failed = false;
    atomic_store_explicit(&picker->state, FILE_PICKER_RUNNING,
                          memory_order_release);

    if (!worker_thread_start(&picker->thread, file_picker_run, picker)) {
        atomic_store_explicit(&picker->state, FILE_PICKER_IDLE,
                              memory_order_release);
        picker->mode = FILE_PICKER_NONE;
        return false;
    }

    return true;
}

bool file_picker_busy(const File_Picker *picker)
{
    return atomic_load_explicit(&picker->state, memory_order_acquire) !=
           FILE_PICKER_IDLE;
}

bool file_picker_take(File_Picker *picker, char **selection,
                      File_Picker_Mode *mode, bool *graphical, bool *failed)
{
    if (atomic_load_explicit(&picker->state, memory_order_acquire) !=
        FILE_PICKER_COMPLETE) {
        return false;
    }

    worker_thread_join(&picker->thread);
    *selection = picker->selection;
    *mode = picker->mode;
    *graphical = picker->graphical;
    *failed = picker->failed;
    picker->selection = NULL;
    picker->mode = FILE_PICKER_NONE;
    atomic_store_explicit(&picker->state, FILE_PICKER_IDLE,
                          memory_order_release);
    return true;
}

void file_picker_uninit(File_Picker *picker)
{
    if (atomic_load_explicit(&picker->state, memory_order_acquire) !=
        FILE_PICKER_IDLE) {
        worker_thread_join(&picker->thread);
    }

    free(picker->selection);
}
