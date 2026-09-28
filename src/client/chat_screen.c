#include "client/chat_screen.h"

#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <raylib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct ChatPreview
{
    const char *name;
    const char *message;
    const char *time;
    Color avatar;
    bool online;
    bool unread;
} ChatPreview;

static const ChatPreview conversations[] = {
    { "Maya Chen", "That sounds perfect!", "10:42", { 239, 151, 144, 255 }, true, true },
    { "Jordan Lee", "See you tomorrow", "09:18", { 117, 178, 234, 255 }, true, false },
    { "The Weekend Club", "Ava: I found a new place", "Yesterday", { 155, 205, 167, 255 }, false, false },
    { "Noah Williams", "Photo", "Sun", { 226, 177, 107, 255 }, false, false }
};

static float roundness(Rectangle bounds, float radius)
{
    const float side = bounds.width < bounds.height ? bounds.width : bounds.height;
    return side <= 0.0f ? 0.0f : radius * 2.0f / side;
}

static void text(const char *value, Vector2 at, float size, Color color, bool bold)
{
    DrawTextEx(bold ? ui_font_get_semibold() : ui_font_get(), value, at, size, 1.0f, color);
}

static void avatar(Vector2 center, float radius, Color color, const char *initials, bool online)
{
    const Vector2 measured = MeasureTextEx(ui_font_get_semibold(), initials, radius * 0.68f, 1.0f);

    DrawCircleV(center, radius, color);
    text(initials, (Vector2){ center.x - measured.x * 0.5f, center.y - measured.y * 0.5f - 1.0f }, radius * 0.68f, WHITE, true);
    if (online)
    {
        DrawCircleV((Vector2){ center.x + radius * 0.68f, center.y + radius * 0.68f }, radius * 0.26f, WHITE);
        DrawCircleV((Vector2){ center.x + radius * 0.68f, center.y + radius * 0.68f }, radius * 0.17f, (Color){ 76, 185, 137, 255 });
    }
}

static void bubble(Rectangle bounds, const char *value, bool own, const UiTheme *theme)
{
    const Color fill = own
                           ? theme->primary
                           : (ui_theme_is_dark_mode()
                                  ? (Color){ 57, 53, 72, 255 }
                                  : (Color){ 242, 239, 248, 255 });
    const Color ink = own ? WHITE : theme->text;

    DrawRectangleRounded(bounds, roundness(bounds, 18.0f), 12, fill);
    text(value, (Vector2){ bounds.x + 16.0f, bounds.y + 13.0f }, 16.0f, ink, false);
}

static bool send_button(Rectangle bounds, bool enabled, ChatScreen *screen, const UiTheme *theme)
{
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    const bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    const Vector2 center = { bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f };
    const float scale = pressed ? 0.92f : 1.0f;
    const float radius = bounds.width * 0.5f * scale;
    const Color fill = enabled
                           ? (hovered ? theme->primary_hovered : theme->primary)
                           : Fade(theme->border, 0.55f);
    const Color icon = enabled ? WHITE : theme->text_muted;

    if (hovered)
    {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }

    DrawCircleV((Vector2){ center.x, center.y + 3.0f }, radius, Fade(theme->primary, 0.17f));
    DrawCircleV(center, radius, fill);
    DrawTriangle(
        (Vector2){ center.x - 10.0f, center.y - 7.0f },
        (Vector2){ center.x + 12.0f, center.y - 12.0f },
        (Vector2){ center.x + 4.0f, center.y + 12.0f },
        icon);
    DrawTriangle(
        (Vector2){ center.x - 3.0f, center.y - 2.0f },
        (Vector2){ center.x + 4.0f, center.y + 12.0f },
        (Vector2){ center.x + 1.0f, center.y + 2.0f },
        fill);

    if (clicked)
    {
        screen->draft[0] = '\0';
        screen->draft_input.cursor = 0;
    }

    return clicked;
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

void chat_screen_init(ChatScreen *screen)
{
    if (screen != NULL)
    {
        memset(screen, 0, sizeof(*screen));
    }
}

void chat_screen_draw(ChatScreen *screen)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float sidebar_width = width < 780.0f ? 245.0f : 310.0f;
    const Rectangle sidebar = { 0.0f, 0.0f, sidebar_width, height };
    const Rectangle header = { sidebar_width, 0.0f, width - sidebar_width, 88.0f };
    const Rectangle composer = { sidebar_width + 28.0f, height - 88.0f, width - sidebar_width - 120.0f, 52.0f };
    const Rectangle send = { width - 80.0f, height - 88.0f, 52.0f, 52.0f };
    const Rectangle theme_button = { width - 118.0f, 22.0f, 42.0f, 42.0f };
    const UiTheme *theme;
    size_t index;

    if (CheckCollisionPointRec(GetMousePosition(), theme_button) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        ui_theme_set_dark_mode(!ui_theme_is_dark_mode());
    }

    theme = ui_theme_default();

    ClearBackground(theme->background);
    DrawCircleV((Vector2){ width * 0.82f, height * 0.08f }, 260.0f, Fade((Color){ 207, 193, 255, 255 }, 0.19f));
    DrawCircleV((Vector2){ width * 0.40f, height * 0.86f }, 240.0f, Fade((Color){ 180, 230, 222, 255 }, 0.16f));
    DrawRectangleRec(sidebar, Fade(theme->surface, 0.66f));
    DrawLine((int)sidebar_width, 0, (int)sidebar_width, (int)height, Fade(theme->border, 0.62f));

    text("Socket Chat", (Vector2){ 26.0f, 28.0f }, 23.0f, theme->text, true);
    DrawCircleV((Vector2){ sidebar_width - 30.0f, 40.0f }, 17.0f, Fade(theme->primary, 0.12f));
    text("+", (Vector2){ sidebar_width - 36.0f, 26.0f }, 25.0f, theme->primary, false);
    text("Messages", (Vector2){ 26.0f, 88.0f }, 14.0f, theme->text_muted, true);

    for (index = 0; index < sizeof(conversations) / sizeof(conversations[0]); ++index)
    {
        const ChatPreview *preview = &conversations[index];
        const float y = 116.0f + (float)index * 78.0f;
        const Rectangle row = { 12.0f, y, sidebar_width - 24.0f, 68.0f };
        const bool selected = (int)index == screen->selected_conversation;
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
        const char initials[3] = { preview->name[0], preview->name[strchr(preview->name, ' ') - preview->name + 1], '\0' };

        if (selected || hovered)
        {
            DrawRectangleRounded(row, roundness(row, 14.0f), 12, selected ? Fade(theme->primary, 0.13f) : Fade(theme->surface, 0.53f));
            DrawRectangleRoundedLinesEx(row, roundness(row, 14.0f), 12, 1.0f, Fade(theme->border, 0.45f));
        }
        if (hovered)
        {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        }
        if (hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            screen->selected_conversation = (int)index;
        }

        avatar((Vector2){ 46.0f, y + 34.0f }, 19.0f, preview->avatar, initials, preview->online);
        text(preview->name, (Vector2){ 76.0f, y + 15.0f }, 16.0f, theme->text, true);
        text(preview->message, (Vector2){ 76.0f, y + 38.0f }, 13.0f, theme->text_muted, false);
        text(preview->time, (Vector2){ sidebar_width - 60.0f, y + 16.0f }, 11.0f, theme->text_muted, false);
        if (preview->unread)
        {
            DrawCircleV((Vector2){ sidebar_width - 27.0f, y + 48.0f }, 4.0f, theme->primary);
        }
    }

    DrawRectangleRec(header, Fade(theme->surface, 0.62f));
    DrawLine((int)sidebar_width, (int)header.height, (int)width, (int)header.height, Fade(theme->border, 0.65f));
    avatar((Vector2){ sidebar_width + 58.0f, 44.0f }, 21.0f, conversations[screen->selected_conversation].avatar, "MC", true);
    text(conversations[screen->selected_conversation].name, (Vector2){ sidebar_width + 92.0f, 23.0f }, 19.0f, theme->text, true);
    text("Active now", (Vector2){ sidebar_width + 92.0f, 49.0f }, 13.0f, (Color){ 76, 185, 137, 255 }, false);
    mode_toggle(theme_button, theme);
    text("•••", (Vector2){ width - 58.0f, 29.0f }, 21.0f, theme->text_muted, true);

    text("Today", (Vector2){ sidebar_width + (width - sidebar_width) * 0.5f - 18.0f, 119.0f }, 12.0f, theme->text_muted, true);
    bubble((Rectangle){ sidebar_width + 30.0f, 151.0f, 225.0f, 48.0f }, "Hey! How's the project going?", false, theme);
    text("10:36", (Vector2){ sidebar_width + 31.0f, 205.0f }, 11.0f, theme->text_muted, false);
    bubble((Rectangle){ width - 282.0f, 237.0f, 252.0f, 48.0f }, "Really well — the new UI is live!", true, theme);
    text("10:39  ✓✓", (Vector2){ width - 105.0f, 291.0f }, 11.0f, theme->text_muted, false);
    bubble((Rectangle){ sidebar_width + 30.0f, 323.0f, 197.0f, 48.0f }, "That sounds perfect!", false, theme);
    text("10:42", (Vector2){ sidebar_width + 31.0f, 377.0f }, 11.0f, theme->text_muted, false);

    DrawRectangleRec((Rectangle){ sidebar_width, height - 112.0f, width - sidebar_width, 112.0f }, Fade(theme->surface, 0.59f));
    (void)ui_text_input(composer, "", "Write a message...", screen->draft, sizeof(screen->draft), UI_TEXT_INPUT_NORMAL, false, &screen->draft_input, theme);
    (void)send_button(send, screen->draft[0] != '\0', screen, theme);
}
