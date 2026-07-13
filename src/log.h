#ifndef MP_LOG_H
#define MP_LOG_H

typedef enum {
    INFO,
    WARNING,
    ERROR,
} Log_Level;

void mp_log(Log_Level level, const char *format, ...);

#endif
