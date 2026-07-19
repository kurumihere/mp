#ifndef MP_THEME_H
#define MP_THEME_H

#include <stdbool.h>

typedef enum {
    SYSTEM_THEME_LIGHT,
    SYSTEM_THEME_DARK,
} System_Theme;

typedef struct {
    void *settings;
    System_Theme current;
    bool changed;
    double next_poll;
} System_Theme_Monitor;

void system_theme_monitor_init(System_Theme_Monitor *monitor);
void system_theme_monitor_uninit(System_Theme_Monitor *monitor);
System_Theme system_theme_monitor_get(const System_Theme_Monitor *monitor);
bool system_theme_monitor_update(System_Theme_Monitor *monitor, double now);

#endif
