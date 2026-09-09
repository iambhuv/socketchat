#ifndef SOCKET_CHAT_UI_THEME_H
#define SOCKET_CHAT_UI_THEME_H

#include <raylib.h>

typedef struct UiTheme
{
    Color background;
    Color surface;
    Color surface_hovered;
    Color input;
    Color border;
    Color primary;
    Color primary_hovered;
    Color text;
    Color text_muted;
    Color shadow;
    Color offline;
    Color connecting;
    Color connected;
    Color error;

    float panel_radius;
    float control_radius;
    float control_height;
    float body_text_size;
    float small_text_size;
    float animation_speed;
} UiTheme;

const UiTheme *ui_theme_default(void);

#endif
