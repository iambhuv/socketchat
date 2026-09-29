#include "client/chat_screen.h"

#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <raylib.h>
#include <math.h>

static float roundness(Rectangle bounds, float radius)
{
    const float side = bounds.width < bounds.height ? bounds.width : bounds.height;
    return side <= 0.0f ? 0.0f : radius * 2.0f / side;
}

static void draw_text(const char *value, Vector2 at, float size, Color color, bool bold)
{
    DrawTextEx(bold ? ui_font_get_semibold() : ui_font_get(), value, at, size, 1.0f, color);
}

static void mode_toggle(Rectangle bounds, const UiTheme *theme)
{
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    const bool dark = ui_theme_is_dark_mode();
    const Color fill = hovered ? Fade(theme->primary, 0.16f) : Fade(theme->surface, 0.58f);
    const Vector2 center = { bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f };

    if (hovered)
    {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            ui_theme_set_dark_mode(!dark);
        }
    }

    DrawRectangleRounded(bounds, roundness(bounds, bounds.height * 0.5f), 12, fill);
    DrawRectangleRoundedLinesEx(bounds, roundness(bounds, bounds.height * 0.5f), 12, 1.0f, Fade(theme->border, 0.72f));

    if (dark)
    {
        DrawCircleV(center, 8.0f, theme->primary);
        DrawCircleV((Vector2){ center.x + 4.0f, center.y - 4.0f }, 8.0f, fill);
    }
    else
    {
        int ray;

        DrawCircleV(center, 6.0f, (Color){ 230, 173, 71, 255 });
        for (ray = 0; ray < 8; ++ray)
        {
            const float angle = (float)ray * PI / 4.0f;
            const Vector2 start = { center.x + cosf(angle) * 10.0f, center.y + sinf(angle) * 10.0f };
            const Vector2 end = { center.x + cosf(angle) * 13.0f, center.y + sinf(angle) * 13.0f };
            DrawLineEx(start, end, 1.5f, (Color){ 230, 173, 71, 255 });
        }
    }
}

void chat_screen_draw(void)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float sidebar_width = width < 780.0f ? 245.0f : 310.0f;
    const Rectangle sidebar = { 0.0f, 0.0f, sidebar_width, height };
    const Rectangle header = { sidebar_width, 0.0f, width - sidebar_width, 88.0f };
    const Rectangle theme_button = { width - 70.0f, 22.0f, 42.0f, 42.0f };
    const UiTheme *theme = ui_theme_default();
    const char *empty_title = "No chats yet";
    const char *empty_detail = "Your private conversations will appear here.";
    const Font title_font = ui_font_get_semibold();
    const Font body_font = ui_font_get();
    const float main_center = sidebar_width + (width - sidebar_width) * 0.5f;
    const Vector2 title_size = MeasureTextEx(title_font, empty_title, 24.0f, 1.0f);
    const Vector2 detail_size = MeasureTextEx(body_font, empty_detail, 16.0f, 1.0f);

    ClearBackground(theme->background);
    DrawCircleV((Vector2){ width * 0.82f, height * 0.08f }, 260.0f, Fade((Color){ 207, 193, 255, 255 }, 0.19f));
    DrawCircleV((Vector2){ width * 0.40f, height * 0.86f }, 240.0f, Fade((Color){ 180, 230, 222, 255 }, 0.16f));
    DrawRectangleRec(sidebar, Fade(theme->surface, 0.66f));
    DrawLine((int)sidebar_width, 0, (int)sidebar_width, (int)height, Fade(theme->border, 0.62f));

    draw_text("Socket Chat", (Vector2){ 26.0f, 28.0f }, 23.0f, theme->text, true);
    draw_text("Messages", (Vector2){ 26.0f, 88.0f }, 14.0f, theme->text_muted, true);
    draw_text("No conversations", (Vector2){ 26.0f, 132.0f }, 15.0f, theme->text_muted, false);

    DrawRectangleRec(header, Fade(theme->surface, 0.62f));
    DrawLine((int)sidebar_width, (int)header.height, (int)width, (int)header.height, Fade(theme->border, 0.65f));
    draw_text("Private chats", (Vector2){ sidebar_width + 28.0f, 29.0f }, 19.0f, theme->text, true);
    mode_toggle(theme_button, theme);

    draw_text(empty_title,
              (Vector2){ main_center - title_size.x * 0.5f, height * 0.47f },
              24.0f, theme->text, true);
    draw_text(empty_detail,
              (Vector2){ main_center - detail_size.x * 0.5f, height * 0.47f + 40.0f },
              16.0f, theme->text_muted, false);
}
