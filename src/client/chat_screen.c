#include "client/chat_screen.h"

#include "client/client_connection.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <raylib.h>
#include <math.h>
#include <string.h>

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

static bool button(Rectangle bounds, const char *label, const UiTheme *theme)
{
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    DrawRectangleRounded(bounds, roundness(bounds, 11.0f), 10,
                         hovered ? Fade(theme->primary, 0.92f) : theme->primary);
    draw_text(label, (Vector2){ bounds.x + 15.0f, bounds.y + 10.0f }, 14.0f,
              (Color){ 255, 255, 255, 255 }, true);
    return hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static void edit_text(char *value, size_t capacity)
{
    int character;

    if (IsKeyPressed(KEY_BACKSPACE))
    {
        size_t length = strlen(value);
        if (length > 0)
        {
            value[length - 1] = '\0';
        }
    }
    while ((character = GetCharPressed()) > 0)
    {
        const size_t length = strlen(value);
        if (character >= 32 && character <= 126 && length + 1 < capacity)
        {
            value[length] = (char)character;
            value[length + 1] = '\0';
        }
    }
}

void chat_screen_draw(ClientConnection *connection)
{
    static char draft[512] = "";
    static char echo[512] = "";
    static char contact_link[256] = "";
    static bool adding_contact = false;
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float sidebar_width = width < 780.0f ? 245.0f : 310.0f;
    const Rectangle sidebar = { 0.0f, 0.0f, sidebar_width, height };
    const Rectangle header = { sidebar_width, 0.0f, width - sidebar_width, 88.0f };
    const Rectangle theme_button = { width - 70.0f, 22.0f, 42.0f, 42.0f };
    const UiTheme *theme = ui_theme_default();
    const char *empty_title = "Start a private chat";
    const char *empty_detail = "Messages are protected by your verified connection.";
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
    if (button((Rectangle){ 24.0f, 116.0f, sidebar_width - 48.0f, 40.0f }, "+ Add contact", theme))
    {
        adding_contact = true;
    }
    draw_text("No conversations", (Vector2){ 26.0f, 180.0f }, 15.0f, theme->text_muted, false);

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

    if (echo[0] != '\0')
    {
        draw_text("Secure server reply", (Vector2){ sidebar_width + 34.0f, height - 168.0f },
                  13.0f, theme->text_muted, true);
        draw_text(echo, (Vector2){ sidebar_width + 34.0f, height - 140.0f },
                  16.0f, theme->text, false);
    }

    {
        const Rectangle composer = { sidebar_width + 28.0f, height - 82.0f,
                                     width - sidebar_width - 150.0f, 52.0f };
        const Rectangle send = { width - 112.0f, height - 82.0f, 84.0f, 52.0f };
        DrawRectangleRounded(composer, roundness(composer, 13.0f), 12, Fade(theme->surface, 0.76f));
        DrawRectangleRoundedLinesEx(composer, roundness(composer, 13.0f), 12, 1.0f,
                                    Fade(theme->border, 0.85f));
        if (CheckCollisionPointRec(GetMousePosition(), composer))
        {
            edit_text(draft, sizeof(draft));
        }
        draw_text(draft[0] == '\0' ? "Write a message..." : draft,
                  (Vector2){ composer.x + 16.0f, composer.y + 16.0f }, 16.0f,
                  draft[0] == '\0' ? theme->text_muted : theme->text, false);
        if ((button(send, "Send", theme) || (IsKeyPressed(KEY_ENTER) && draft[0] != '\0')) &&
            draft[0] != '\0')
        {
            if (connection != NULL && client_connection_send_text(connection, draft, echo, sizeof(echo)) == CLIENT_CONNECTION_OK)
            {
                draft[0] = '\0';
            }
            else
            {
                memcpy(echo, "Message could not be delivered.",
                       sizeof("Message could not be delivered."));
            }
        }
    }

    if (adding_contact)
    {
        const Rectangle panel = { sidebar_width + 70.0f, height * 0.22f,
                                  width - sidebar_width - 140.0f, 220.0f };
        const Rectangle input = { panel.x + 24.0f, panel.y + 88.0f, panel.width - 48.0f, 46.0f };
        DrawRectangleRounded(panel, roundness(panel, 20.0f), 14, Fade(theme->surface, 0.96f));
        DrawRectangleRoundedLinesEx(panel, roundness(panel, 20.0f), 14, 1.0f, theme->border);
        draw_text("Add a contact", (Vector2){ panel.x + 24.0f, panel.y + 24.0f }, 20.0f, theme->text, true);
        draw_text("Paste their Socket Chat link or scan its QR into this field.",
                  (Vector2){ panel.x + 24.0f, panel.y + 54.0f }, 13.0f, theme->text_muted, false);
        DrawRectangleRounded(input, roundness(input, 10.0f), 10, Fade(theme->background, 0.72f));
        if (CheckCollisionPointRec(GetMousePosition(), input))
        {
            edit_text(contact_link, sizeof(contact_link));
        }
        draw_text(contact_link[0] == '\0' ? "socketchat://contact/..." : contact_link,
                  (Vector2){ input.x + 12.0f, input.y + 14.0f }, 14.0f,
                  contact_link[0] == '\0' ? theme->text_muted : theme->text, false);
        if (button((Rectangle){ panel.x + panel.width - 110.0f, panel.y + 154.0f, 86.0f, 40.0f }, "Done", theme))
        {
            adding_contact = false;
        }
    }
}
