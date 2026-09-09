#include "ui/ui_font.h"

#include <stddef.h>
#include <stdio.h>

#define UI_FONT_BASE_SIZE 48
#define UI_FONT_PATH_CAPACITY 1024u

static Font ui_font_regular;
static Font ui_font_semibold;
static bool ui_font_regular_is_loaded = false;
static bool ui_font_semibold_is_loaded = false;

static Font ui_font_load_file(const char *filename, bool *loaded)
{
    char font_path[UI_FONT_PATH_CAPACITY];
    const Font default_font = GetFontDefault();
    Font font;
    int path_length;

    path_length = snprintf(
        font_path,
        sizeof(font_path),
        "%s/assets/fonts/%s",
        GetApplicationDirectory(),
        filename);

    if (path_length < 0 || (size_t)path_length >= sizeof(font_path) ||
        !FileExists(font_path))
    {
        *loaded = false;
        return default_font;
    }

    font = LoadFontEx(font_path, UI_FONT_BASE_SIZE, NULL, 0);

    if (!IsFontValid(font) || font.texture.id == default_font.texture.id)
    {
        *loaded = false;
        return default_font;
    }

    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    *loaded = true;
    return font;
}

bool ui_font_load(void)
{
    ui_font_regular = ui_font_load_file(
        "Nunito-Regular.ttf",
        &ui_font_regular_is_loaded);
    ui_font_semibold = ui_font_load_file(
        "Nunito-SemiBold.ttf",
        &ui_font_semibold_is_loaded);
    return ui_font_regular_is_loaded && ui_font_semibold_is_loaded;
}

void ui_font_unload(void)
{
    if (ui_font_regular_is_loaded)
    {
        UnloadFont(ui_font_regular);
        ui_font_regular_is_loaded = false;
    }

    if (ui_font_semibold_is_loaded)
    {
        UnloadFont(ui_font_semibold);
        ui_font_semibold_is_loaded = false;
    }
}

Font ui_font_get(void)
{
    return ui_font_regular_is_loaded ? ui_font_regular : GetFontDefault();
}

Font ui_font_get_semibold(void)
{
    return ui_font_semibold_is_loaded ? ui_font_semibold : ui_font_get();
}
