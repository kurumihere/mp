#ifndef MP_FILE_PICKER_H
#define MP_FILE_PICKER_H

#include <stdatomic.h>
#include <stdbool.h>

#include "worker_thread.h"

typedef enum {
    FILE_PICKER_NONE,
    FILE_PICKER_FILES,
    FILE_PICKER_FOLDER,
    FILE_PICKER_SAVE_FILE,
} File_Picker_Mode;

typedef struct {
    Worker_Thread thread;
    atomic_int state;
    File_Picker_Mode mode;
    char *selection;
    bool graphical;
    bool failed;
} File_Picker;

void file_picker_init(File_Picker *picker);
bool file_picker_start(File_Picker *picker, File_Picker_Mode mode);
bool file_picker_busy(const File_Picker *picker);
bool file_picker_take(File_Picker *picker, char **selection,
                      File_Picker_Mode *mode, bool *graphical, bool *failed);
void file_picker_uninit(File_Picker *picker);

#endif
