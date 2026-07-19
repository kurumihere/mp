#ifndef MP_LOG_H
#define MP_LOG_H

typedef enum {
    INFO,
    WARNING,
    ERROR,
} Log_Level;

void mp_log_set_level(Log_Level level);
void mp_log(Log_Level level, const char *format, ...);

#endif
