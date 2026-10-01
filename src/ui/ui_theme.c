#include "ui/ui_theme.h"

#include <stdbool.h>

static bool ui_dark_mode = true;

const UiTheme *ui_theme_default(void)
{
    static const UiTheme light_theme = {
        { 246, 246, 249, 255 },
        { 255, 255, 255, 255 },
        { 248, 248, 251, 255 },
        { 250, 250, 252, 255 },
        { 222, 221, 230, 255 },
        { 109, 90, 203, 255 },
        { 128, 109, 222, 255 },
        { 43, 40, 54, 255 },
        { 122, 117, 137, 255 },
        { 44, 36, 68, 18 },
        { 151, 145, 164, 255 },
        { 224, 166, 81, 255 },
        { 74, 166, 126, 255 },
        { 217, 89, 105, 255 },
        20.0f,
        12.0f,
        56.0f,
        20.0f,
        16.0f,
        13.0f
    };
    static const UiTheme dark_theme = {
        { 14, 15, 19, 255 },
        { 24, 25, 31, 255 },
        { 31, 32, 40, 255 },
        { 29, 30, 37, 255 },
        { 54, 55, 66, 255 },
        { 143, 124, 239, 255 },
        { 161, 143, 249, 255 },
        { 244, 242, 248, 255 },
        { 167, 163, 181, 255 },
        { 0, 0, 0, 54 },
        { 135, 132, 149, 255 },
        { 237, 183, 94, 255 },
        { 94, 198, 147, 255 },
        { 238, 108, 124, 255 },
        20.0f,
        12.0f,
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
