#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define COLOR_GREEN "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_RED "\x1b[31m"
#define COLOR_RESET "\x1b[0m"

static Log_Level minimum_level = WARNING;

void mp_log_set_level(Log_Level level)
{
    minimum_level = level;
}

static const char *level_name(Log_Level level)
{
    switch (level) {
    case INFO:
        return "INFO";
    case WARNING:
        return "WARNING";
    case ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static const char *level_color(Log_Level level)
{
    switch (level) {
    case INFO:
        return COLOR_GREEN;
    case WARNING:
        return COLOR_YELLOW;
    case ERROR:
        return COLOR_RED;
    default:
        return "";
    }
}

void mp_log(Log_Level level, const char *format, ...)
{
    if (level < minimum_level) return;

    FILE *stream = level == INFO ? stdout : stderr;
    int descriptor = level == INFO ? STDOUT_FILENO : STDERR_FILENO;
    bool use_color = isatty(descriptor);

    if (use_color) {
        fprintf(stream, "%s%s:%s ", level_color(level), level_name(level),
                COLOR_RESET);
    } else {
        fprintf(stream, "%s: ", level_name(level));
    }

    va_list arguments;
    va_start(arguments, format);
    vfprintf(stream, format, arguments);
    va_end(arguments);

    fputc('\n', stream);
    fflush(stream);
}
