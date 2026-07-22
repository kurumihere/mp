#ifndef MP_UI_FONT_H
#define MP_UI_FONT_H

#include <stdbool.h>

#include "raylib.h"

bool ui_font_init(void);
void ui_font_uninit(void);

bool ui_font_collect(const char *text, int font_size);
bool ui_font_rebuild(void);

int ui_font_crisp_size(float desired, int minimum, int maximum);
int ui_font_measure(const char *text, int font_size);
void ui_font_draw(const char *text, int x, int y, int font_size, Color color);

#endif
