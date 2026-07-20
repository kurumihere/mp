#include "fs.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <unistd.h>

#include "raylib.h"
#endif

#ifndef _WIN32
static char *copy_string(const char *source)
{
    size_t length = strlen(source) + 1;
    char *copy = malloc(length);

    if (copy != NULL) memcpy(copy, source, length);

    return copy;
}
#endif

static bool path_list_append(Fs_Path_List *paths, char *path)
{
    if (paths->count == paths->capacity) {
        size_t capacity = paths->capacity == 0 ? 16 : paths->capacity * 2;

        if (capacity < paths->capacity ||
            capacity > SIZE_MAX / sizeof(*paths->items)) {
            return false;
        }

        char **items = realloc(paths->items, capacity * sizeof(*items));

        if (items == NULL) return false;

        paths->items = items;
        paths->capacity = capacity;
    }

    paths->items[paths->count++] = path;
    return true;
}

void fs_path_list_uninit(Fs_Path_List *paths)
{
    for (size_t i = 0; i < paths->count; ++i) {
        free(paths->items[i]);
    }

    free(paths->items);
    *paths = (Fs_Path_List){0};
}

#ifdef _WIN32

wchar_t *fs_utf8_to_utf16(const char *text)
{
    if (text == NULL) return NULL;

    int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);

    if (length == 0 || (size_t)length > SIZE_MAX / sizeof(wchar_t)) return NULL;

    wchar_t *wide = malloc((size_t)length * sizeof(*wide));

    if (wide == NULL) return NULL;

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide,
                            length) == 0) {
        free(wide);
        return NULL;
    }

    return wide;
}

static char *utf16_to_utf8(const wchar_t *text)
{
    if (text == NULL) return NULL;

    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                     NULL, 0, NULL, NULL);

    if (length == 0) return NULL;

    char *utf8 = malloc((size_t)length);

    if (utf8 == NULL) return NULL;

    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, utf8,
                            length, NULL, NULL) == 0) {
        free(utf8);
        return NULL;
    }

    return utf8;
}

FILE *fs_fopen(const char *path, const char *mode)
{
    wchar_t *wide_path = fs_utf8_to_utf16(path);
    wchar_t *wide_mode = fs_utf8_to_utf16(mode);
    FILE *file = NULL;

    if (wide_path != NULL && wide_mode != NULL) {
        file = _wfopen(wide_path, wide_mode);
    } else {
        errno = EINVAL;
    }

    free(wide_mode);
    free(wide_path);
    return file;
}

int fs_rename(const char *source, const char *destination)
{
    wchar_t *wide_source = fs_utf8_to_utf16(source);
    wchar_t *wide_destination = fs_utf8_to_utf16(destination);
    int result = -1;

    if (wide_source != NULL && wide_destination != NULL) {
        if (MoveFileExW(wide_source, wide_destination,
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            result = 0;
        } else {
            errno = EIO;
        }
    } else {
        errno = EINVAL;
    }

    free(wide_destination);
    free(wide_source);
    return result;
}

int fs_remove(const char *path)
{
    wchar_t *wide = fs_utf8_to_utf16(path);

    if (wide == NULL) {
        errno = EINVAL;
        return -1;
    }

    int result = _wremove(wide);
    free(wide);
    return result;
}

bool fs_make_parent_directories(const char *path)
{
    wchar_t *wide = fs_utf8_to_utf16(path);

    if (wide == NULL) return false;

    wchar_t *slash = wcsrchr(wide, L'/');
    wchar_t *backslash = wcsrchr(wide, L'\\');

    if (slash == NULL || (backslash != NULL && backslash > slash)) {
        slash = backslash;
    }

    if (slash == NULL) {
        free(wide);
        return true;
    }

    if (slash == wide || (slash == wide + 2 && wide[1] == L':')) {
        free(wide);
        return true;
    }

    *slash = L'\0';
    int result = SHCreateDirectoryExW(NULL, wide, NULL);
    free(wide);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
           result == ERROR_FILE_EXISTS;
}

bool fs_is_directory(const char *path)
{
    wchar_t *wide = fs_utf8_to_utf16(path);

    if (wide == NULL) return false;

    DWORD attributes = GetFileAttributesW(wide);
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool fs_path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;

    if (path[0] == '/' || path[0] == '\\') return true;

    unsigned char drive = (unsigned char)path[0];
    return ((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z')) &&
           path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}

char *fs_absolute_path(const char *path)
{
    wchar_t *wide = fs_utf8_to_utf16(path);

    if (wide == NULL) return NULL;

    DWORD length = GetFullPathNameW(wide, 0, NULL, NULL);

    if (length == 0 || (size_t)length + 1 > SIZE_MAX / sizeof(wchar_t)) {
        free(wide);
        return NULL;
    }

    wchar_t *absolute = malloc(((size_t)length + 1) * sizeof(*absolute));

    if (absolute == NULL) {
        free(wide);
        return NULL;
    }

    DWORD written = GetFullPathNameW(wide, length + 1, absolute, NULL);
    free(wide);

    if (written == 0 || written > length) {
        free(absolute);
        return NULL;
    }

    char *utf8 = utf16_to_utf8(absolute);
    free(absolute);
    return utf8;
}

char *fs_environment(const char *name)
{
    wchar_t *wide_name = fs_utf8_to_utf16(name);

    if (wide_name == NULL) return NULL;

    const wchar_t *value = _wgetenv(wide_name);
    char *utf8 =
        value == NULL || value[0] == L'\0' ? NULL : utf16_to_utf8(value);
    free(wide_name);
    return utf8;
}

static wchar_t *join_wide_path(const wchar_t *directory, const wchar_t *name)
{
    size_t directory_length = wcslen(directory);
    size_t name_length = wcslen(name);
    bool needs_separator = directory_length > 0 &&
                           directory[directory_length - 1] != L'/' &&
                           directory[directory_length - 1] != L'\\';

    if (directory_length > SIZE_MAX - name_length - 2 ||
        directory_length + name_length + 2 > SIZE_MAX / sizeof(wchar_t)) {
        return NULL;
    }

    size_t length =
        directory_length + name_length + (needs_separator ? 2u : 1u);
    wchar_t *joined = malloc(length * sizeof(*joined));

    if (joined == NULL) return NULL;

    memcpy(joined, directory, directory_length * sizeof(*joined));
    size_t offset = directory_length;

    if (needs_separator) joined[offset++] = L'\\';

    memcpy(joined + offset, name, (name_length + 1) * sizeof(*joined));
    return joined;
}

static bool is_audio_file(const wchar_t *path)
{
    const wchar_t *extension = wcsrchr(path, L'.');

    if (extension == NULL) return false;

    return _wcsicmp(extension, L".flac") == 0 ||
           _wcsicmp(extension, L".mp3") == 0 ||
           _wcsicmp(extension, L".wav") == 0;
}

static bool scan_audio_directory(const wchar_t *directory, Fs_Path_List *paths)
{
    wchar_t *pattern = join_wide_path(directory, L"*");

    if (pattern == NULL) return false;

    WIN32_FIND_DATAW entry;
    HANDLE search = FindFirstFileW(pattern, &entry);
    free(pattern);

    if (search == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    bool success = true;

    do {
        if (wcscmp(entry.cFileName, L".") == 0 ||
            wcscmp(entry.cFileName, L"..") == 0) {
            continue;
        }

        wchar_t *path = join_wide_path(directory, entry.cFileName);

        if (path == NULL) {
            success = false;
            break;
        }

        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                success = scan_audio_directory(path, paths);
            }
        } else if (is_audio_file(path)) {
            char *utf8 = utf16_to_utf8(path);
            success = utf8 != NULL && path_list_append(paths, utf8);

            if (!success) free(utf8);
        }

        free(path);
    } while (success && FindNextFileW(search, &entry));

    if (success && GetLastError() != ERROR_NO_MORE_FILES) success = false;

    FindClose(search);
    return success;
}

bool fs_list_audio_files(const char *directory, Fs_Path_List *paths)
{
    wchar_t *wide = fs_utf8_to_utf16(directory);

    if (wide == NULL) return false;

    bool success = scan_audio_directory(wide, paths);
    free(wide);

    if (!success) fs_path_list_uninit(paths);

    return success;
}

bool fs_windows_command_line(int *argc, char ***argv)
{
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);

    if (wide == NULL || count < 1 ||
        (size_t)count + 1 > SIZE_MAX / sizeof(char *)) {
        if (wide != NULL) LocalFree(wide);
        return false;
    }

    char **utf8 = calloc((size_t)count + 1, sizeof(*utf8));

    if (utf8 == NULL) {
        LocalFree(wide);
        return false;
    }

    int converted = 0;

    for (; converted < count; ++converted) {
        utf8[converted] = utf16_to_utf8(wide[converted]);

        if (utf8[converted] == NULL) break;
    }

    LocalFree(wide);

    if (converted != count) {
        fs_windows_command_line_free(converted, utf8);
        return false;
    }

    *argc = count;
    *argv = utf8;
    return true;
}

void fs_windows_command_line_free(int argc, char **argv)
{
    if (argv == NULL) return;

    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }

    free(argv);
}

#else

FILE *fs_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int fs_rename(const char *source, const char *destination)
{
    return rename(source, destination);
}

int fs_remove(const char *path)
{
    return remove(path);
}

bool fs_make_parent_directories(const char *path)
{
    char *copy = copy_string(path);

    if (copy == NULL) return false;

    for (char *character = copy + 1; *character != '\0'; ++character) {
        if (*character != '/') continue;

        *character = '\0';
        int result = mkdir(copy, 0700);

        if (result != 0 && errno != EEXIST) {
            free(copy);
            return false;
        }

        *character = '/';
    }

    free(copy);
    return true;
}

bool fs_is_directory(const char *path)
{
    return DirectoryExists(path);
}

bool fs_path_is_absolute(const char *path)
{
    return path != NULL && path[0] == '/';
}

char *fs_absolute_path(const char *path)
{
    if (fs_path_is_absolute(path)) return copy_string(path);

    size_t capacity = 256;
    char *directory = NULL;

    while (capacity <= 1024 * 1024) {
        directory = malloc(capacity);

        if (directory == NULL) return NULL;
        if (getcwd(directory, capacity) != NULL) break;

        int error = errno;
        free(directory);
        directory = NULL;

        if (error != ERANGE) return NULL;

        capacity *= 2;
    }

    if (directory == NULL) return NULL;

    size_t directory_length = strlen(directory);
    size_t path_length = strlen(path);
    bool needs_separator =
        directory_length > 0 && directory[directory_length - 1] != '/';

    if (directory_length > SIZE_MAX - path_length - 2) {
        free(directory);
        return NULL;
    }

    size_t length =
        directory_length + path_length + (needs_separator ? 2u : 1u);
    char *absolute = malloc(length);

    if (absolute != NULL) {
        int written = snprintf(absolute, length, "%s%s%s", directory,
                               needs_separator ? "/" : "", path);

        if (written < 0 || (size_t)written >= length) {
            free(absolute);
            absolute = NULL;
        }
    }

    free(directory);
    return absolute;
}

char *fs_environment(const char *name)
{
    const char *value = getenv(name);
    return value == NULL || value[0] == '\0' ? NULL : copy_string(value);
}

bool fs_list_audio_files(const char *directory, Fs_Path_List *paths)
{
    FilePathList files =
        LoadDirectoryFilesEx(directory, ".flac;.mp3;.wav", true);
    bool success = files.paths != NULL || files.count == 0;

    for (unsigned int i = 0; success && i < files.count; ++i) {
        char *copy = copy_string(files.paths[i]);
        success = copy != NULL && path_list_append(paths, copy);

        if (!success) free(copy);
    }

    UnloadDirectoryFiles(files);

    if (!success) fs_path_list_uninit(paths);

    return success;
}

#endif
