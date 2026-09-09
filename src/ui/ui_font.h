#ifndef SOCKET_CHAT_UI_FONT_H
#define SOCKET_CHAT_UI_FONT_H

#include <raylib.h>

#include <stdbool.h>

/* Loads the bundled UI font. The Raylib default font is used as a fallback. */
bool ui_font_load(void);
void ui_font_unload(void);
Font ui_font_get(void);
Font ui_font_get_semibold(void);

#endif
