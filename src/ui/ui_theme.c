#include "ui/ui_theme.h"

#include <stdbool.h>

static bool ui_dark_mode = false;

const UiTheme *ui_theme_default(void)
{
    static const UiTheme light_theme = {
        { 248, 248, 250, 255 },
        { 255, 255, 255, 255 },
        { 245, 245, 248, 255 },
        { 252, 252, 253, 255 },
        { 198, 197, 206, 255 },
        { 126, 105, 218, 255 },
        { 143, 120, 232, 255 },
        { 52, 47, 68, 255 },
        { 126, 119, 145, 255 },
        { 69, 54, 104, 24 },
        { 150, 143, 166, 255 },
        { 231, 169, 84, 255 },
        { 86, 184, 145, 255 },
        { 225, 103, 118, 255 },
        24.0f,
        14.0f,
        56.0f,
        20.0f,
        16.0f,
        13.0f
    };
    static const UiTheme dark_theme = {
        { 21, 21, 24, 255 },
        { 30, 30, 35, 255 },
        { 39, 39, 46, 255 },
        { 34, 34, 40, 255 },
        { 70, 70, 80, 255 },
        { 148, 126, 245, 255 },
        { 166, 146, 255, 255 },
        { 244, 241, 250, 255 },
        { 182, 175, 198, 255 },
        { 0, 0, 0, 68 },
        { 158, 151, 171, 255 },
        { 241, 185, 94, 255 },
        { 100, 205, 157, 255 },
        { 241, 116, 132, 255 },
        24.0f,
        14.0f,
        56.0f,
        20.0f,
        16.0f,
        13.0f
    };

    return ui_dark_mode ? &dark_theme : &light_theme;
}

void ui_theme_set_dark_mode(bool enabled)
{
    ui_dark_mode = enabled;
}

bool ui_theme_is_dark_mode(void)
{
    return ui_dark_mode;
}
