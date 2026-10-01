#include "client/chat_screen.h"

#include "client/client_connection.h"
#include "ui/ui_animation.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <raylib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHAT_MAX_CONTACTS 24u
#define CHAT_MAX_VISIBLE_MESSAGES 7u
#define CHAT_CONTACT_LINK_CAPACITY \
    (sizeof("socketchat://contact/") - 1u + PROTOCOL_IDENTITY_ID_SIZE * 2u + 1u)

static float roundness(Rectangle bounds, float radius)
{
    const float side = bounds.width < bounds.height ? bounds.width : bounds.height;
    return side <= 0.0f ? 0.0f : radius * 2.0f / side;
}

static void draw_text(const char *value, Vector2 at, float size, Color color, bool bold)
{
    DrawTextEx(bold ? ui_font_get_semibold() : ui_font_get(), value, at, size, 1.0f, color);
}

static uint32_t draw_wrapped_text(
    const char *value,
    Font font,
    float size,
    float maximum_width,
    Vector2 at,
    Color color,
    bool draw,
    float *last_line_width)
{
    char line[CLIENT_DATABASE_MESSAGE_TEXT_CAPACITY];
    const float line_height = size + 4.0f;
    size_t line_length = 0;
    size_t index;
    uint32_t line_count = 0;

    if (value == NULL || maximum_width <= 0.0f)
    {
        return 0;
    }
    for (index = 0;; ++index)
    {
        const char character = value[index];

        if (character == '\0' || character == '\n')
        {
            line[line_length] = '\0';
            if (draw)
            {
                DrawTextEx(font, line, (Vector2){ at.x, at.y + line_count * line_height },
                           size, 1.0f, color);
            }
            ++line_count;
            line_length = 0;
            if (character == '\0')
            {
                break;
            }
            continue;
        }

        if (line_length + 1u >= sizeof(line))
        {
            line[line_length] = '\0';
            if (draw)
            {
                DrawTextEx(font, line, (Vector2){ at.x, at.y + line_count * line_height },
                           size, 1.0f, color);
            }
            ++line_count;
            line_length = 0;
        }
        line[line_length++] = character;
        line[line_length] = '\0';

        while (line_length > 1u &&
               MeasureTextEx(font, line, size, 1.0f).x > maximum_width)
        {
            size_t break_index = line_length;

            while (break_index > 0 && line[break_index - 1u] != ' ')
            {
                --break_index;
            }
            if (break_index > 0)
            {
                size_t display_length = break_index - 1u;
                size_t overflow_index = break_index;

                while (display_length > 0 && line[display_length - 1u] == ' ')
                {
                    --display_length;
                }
                while (line[overflow_index] == ' ')
                {
                    ++overflow_index;
                }
                line[display_length] = '\0';
                if (draw)
                {
                    DrawTextEx(font, line,
                               (Vector2){ at.x, at.y + line_count * line_height },
                               size, 1.0f, color);
                }
                ++line_count;
                memmove(line, line + overflow_index, line_length - overflow_index + 1u);
                line_length -= overflow_index;
            }
            else if (line_length > 1u)
            {
                const char overflow = line[line_length - 1u];

                line[line_length - 1u] = '\0';
                if (draw)
                {
                    DrawTextEx(font, line,
                               (Vector2){ at.x, at.y + line_count * line_height },
                               size, 1.0f, color);
                }
                ++line_count;
                line[0] = overflow;
                line[1] = '\0';
                line_length = 1;
            }
        }
    }
    if (last_line_width != NULL)
    {
        *last_line_width = MeasureTextEx(font, line, size, 1.0f).x;
    }
    return line_count;
}

static Rectangle scaled_rectangle(Rectangle bounds, float scale)
{
    const float width = bounds.width * scale;
    const float height = bounds.height * scale;

    return (Rectangle){
        bounds.x + (bounds.width - width) * 0.5f,
        bounds.y + (bounds.height - height) * 0.5f,
        width,
        height
    };
}

static void draw_glass_panel(Rectangle bounds, const UiTheme *theme, float opacity, float radius)
{
    const Color shine = ui_theme_is_dark_mode() ? (Color){ 255, 255, 255, 12 }
                                                : (Color){ 255, 255, 255, 100 };
    const Rectangle top_shine = { bounds.x + 2.0f, bounds.y + 2.0f,
                                  bounds.width - 4.0f, 2.0f };

    DrawRectangleRounded((Rectangle){ bounds.x, bounds.y + 3.0f, bounds.width, bounds.height },
                         roundness(bounds, radius), 14, Fade(theme->shadow, 0.34f));
    DrawRectangleRounded(bounds, roundness(bounds, radius), 14, Fade(theme->surface, opacity));
    DrawRectangleRoundedLinesEx(bounds, roundness(bounds, radius), 14, 1.0f,
                                Fade(theme->border, ui_theme_is_dark_mode() ? 0.48f : 0.42f));
    DrawRectangleRounded(top_shine, roundness(top_shine, 3.0f), 8, shine);
}

static void draw_background_glow(float width, float height, const UiTheme *theme)
{
    const float time = (float)GetTime();
    const Color primary_glow = Fade(theme->primary, ui_theme_is_dark_mode() ? 0.06f : 0.05f);
    const Color secondary_glow = Fade(theme->primary_hovered,
                                      ui_theme_is_dark_mode() ? 0.035f : 0.04f);

    DrawCircle((int)(width * 0.78f + cosf(time * 0.23f) * 42.0f),
               (int)(height * 0.16f + sinf(time * 0.19f) * 24.0f), 132.0f, primary_glow);
    DrawCircle((int)(width * 0.39f + sinf(time * 0.17f) * 35.0f),
               (int)(height * 0.88f + cosf(time * 0.22f) * 18.0f), 96.0f, secondary_glow);
}

static void mode_toggle(Rectangle bounds, const UiTheme *theme, float *hover_amount)
{
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    const bool dark = ui_theme_is_dark_mode();
    const float delta_time = GetFrameTime();
    Rectangle animated_bounds;
    Color fill;
    Vector2 center;

    *hover_amount = ui_smooth(*hover_amount, hovered ? 1.0f : 0.0f, 13.0f, delta_time);
    animated_bounds = scaled_rectangle(bounds, 1.0f + *hover_amount * 0.045f);
    fill = Fade(theme->surface, 0.66f + *hover_amount * 0.20f);
    center = (Vector2){ animated_bounds.x + animated_bounds.width * 0.5f,
                        animated_bounds.y + animated_bounds.height * 0.5f };

    if (hovered)
    {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            ui_theme_set_dark_mode(!dark);
        }
    }

    draw_glass_panel(animated_bounds, theme, 0.78f + *hover_amount * 0.12f,
                     animated_bounds.height * 0.5f);
    DrawRectangleRounded(animated_bounds, roundness(animated_bounds, animated_bounds.height * 0.5f),
                         12, fill);

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

static bool glass_button(
    Rectangle bounds,
    const char *label,
    const UiTheme *theme,
    float *hover_amount,
    float *press_amount,
    bool secondary)
{
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    const bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const float delta_time = GetFrameTime();
    Rectangle animated_bounds;
    Color fill;
    const Color label_color = secondary ? theme->text : (Color){ 255, 255, 255, 255 };
    const Vector2 label_size = MeasureTextEx(ui_font_get_semibold(), label, 14.0f, 1.0f);

    *hover_amount = ui_smooth(*hover_amount, hovered ? 1.0f : 0.0f, 15.0f, delta_time);
    *press_amount = ui_smooth(*press_amount, pressed ? 1.0f : 0.0f, 22.0f, delta_time);
    animated_bounds = scaled_rectangle(bounds,
                                       1.0f + *hover_amount * 0.025f - *press_amount * 0.045f);
    fill = secondary ? Fade(theme->surface, 0.76f + *hover_amount * 0.16f)
                     : Fade(theme->primary, 0.86f + *hover_amount * 0.14f);

    if (hovered)
    {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }
    DrawRectangleRounded((Rectangle){ animated_bounds.x, animated_bounds.y + 4.0f,
                                      animated_bounds.width, animated_bounds.height },
                         roundness(animated_bounds, 12.0f), 12, Fade(theme->shadow, 0.46f));
    DrawRectangleRounded(animated_bounds, roundness(animated_bounds, 12.0f), 12, fill);
    DrawRectangleRoundedLinesEx(animated_bounds, roundness(animated_bounds, 12.0f), 12, 1.0f,
                                secondary ? Fade(theme->border, 0.76f)
                                          : Fade((Color){ 255, 255, 255, 255 }, 0.26f));
    draw_text(label, (Vector2){ animated_bounds.x + (animated_bounds.width - label_size.x) * 0.5f,
                                animated_bounds.y + (animated_bounds.height - label_size.y) * 0.5f - 1.0f },
              14.0f, label_color, true);
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void show_toast(char toast[96], float *expires_at, const char *message)
{
    snprintf(toast, 96, "%s", message);
    *expires_at = (float)GetTime() + 2.6f;
}

static void draw_toast(const char *toast, float expires_at, float width, const UiTheme *theme)
{
    const float remaining = expires_at - (float)GetTime();
    const float opacity = ui_clamp(fminf(remaining * 2.8f, 1.0f), 0.0f, 1.0f);
    const Rectangle bounds = { width - 282.0f, 84.0f, 254.0f, 44.0f };

    if (toast[0] == '\0' || remaining <= 0.0f)
    {
        return;
    }
    draw_glass_panel(bounds, theme, 0.92f * opacity, 14.0f);
    DrawCircleV((Vector2){ bounds.x + 19.0f, bounds.y + 22.0f }, 5.0f,
                Fade(theme->connected, opacity));
    draw_text(toast, (Vector2){ bounds.x + 32.0f, bounds.y + 14.0f }, 13.0f,
              Fade(theme->text, opacity), true);
}

static void delete_selected_text(char *value, size_t *cursor, size_t *selection_anchor)
{
    const size_t first = *cursor < *selection_anchor ? *cursor : *selection_anchor;
    const size_t last = *cursor < *selection_anchor ? *selection_anchor : *cursor;

    if (first != last)
    {
        memmove(value + first, value + last, strlen(value + last) + 1u);
    }
    *cursor = first;
    *selection_anchor = first;
}

static void insert_text_character(
    char *value,
    size_t capacity,
    size_t *cursor,
    size_t *selection_anchor,
    char character)
{
    size_t length;

    delete_selected_text(value, cursor, selection_anchor);
    length = strlen(value);
    if (length + 1u >= capacity)
    {
        return;
    }
    memmove(value + *cursor + 1u, value + *cursor, length - *cursor + 1u);
    value[*cursor] = character;
    ++*cursor;
    *selection_anchor = *cursor;
}

static void edit_text(
    char *value,
    size_t capacity,
    size_t *cursor,
    size_t *selection_anchor,
    bool allow_newlines)
{
    int character;
    size_t length = strlen(value);
    const bool shift_down = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool control_down = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool paste_requested =
        control_down && IsKeyPressed(KEY_V);

    if (*cursor > length)
    {
        *cursor = length;
    }
    if (*selection_anchor > length)
    {
        *selection_anchor = length;
    }
    if (control_down && IsKeyPressed(KEY_A))
    {
        *selection_anchor = 0;
        *cursor = length;
    }
    if (control_down && (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_X)) &&
        *cursor != *selection_anchor)
    {
        const size_t first = *cursor < *selection_anchor ? *cursor : *selection_anchor;
        const size_t last = *cursor < *selection_anchor ? *selection_anchor : *cursor;
        char selected[512];

        if (last - first < sizeof(selected))
        {
            memcpy(selected, value + first, last - first);
            selected[last - first] = '\0';
            SetClipboardText(selected);
        }
        if (IsKeyPressed(KEY_X))
        {
            delete_selected_text(value, cursor, selection_anchor);
            length = strlen(value);
        }
    }
    if (IsKeyPressed(KEY_LEFT))
    {
        if (!shift_down && *cursor != *selection_anchor)
        {
            *cursor = *cursor < *selection_anchor ? *cursor : *selection_anchor;
        }
        else if (*cursor > 0)
        {
            --*cursor;
        }
        if (!shift_down)
        {
            *selection_anchor = *cursor;
        }
    }
    if (IsKeyPressed(KEY_RIGHT))
    {
        if (!shift_down && *cursor != *selection_anchor)
        {
            *cursor = *cursor > *selection_anchor ? *cursor : *selection_anchor;
        }
        else if (*cursor < length)
        {
            ++*cursor;
        }
        if (!shift_down)
        {
            *selection_anchor = *cursor;
        }
    }
    if (IsKeyPressed(KEY_HOME))
    {
        *cursor = 0;
        if (!shift_down)
        {
            *selection_anchor = *cursor;
        }
    }
    if (IsKeyPressed(KEY_END))
    {
        *cursor = length;
        if (!shift_down)
        {
            *selection_anchor = *cursor;
        }
    }

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
    {
        if (*cursor != *selection_anchor)
        {
            delete_selected_text(value, cursor, selection_anchor);
        }
        else if (*cursor > 0)
        {
            --*cursor;
            *selection_anchor = *cursor;
            memmove(value + *cursor, value + *cursor + 1u,
                    strlen(value + *cursor + 1u) + 1u);
        }
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE))
    {
        if (*cursor != *selection_anchor)
        {
            delete_selected_text(value, cursor, selection_anchor);
        }
        else if (*cursor < strlen(value))
        {
            memmove(value + *cursor, value + *cursor + 1u,
                    strlen(value + *cursor + 1u) + 1u);
        }
    }
    if (allow_newlines && shift_down && IsKeyPressed(KEY_ENTER))
    {
        insert_text_character(value, capacity, cursor, selection_anchor, '\n');
    }
    while ((character = GetCharPressed()) > 0)
    {
        if (character >= 32 && character <= 126)
        {
            insert_text_character(value, capacity, cursor, selection_anchor, (char)character);
        }
    }
    if (paste_requested)
    {
        const char *clipboard = GetClipboardText();
        size_t index;

        if (clipboard == NULL)
        {
            return;
        }
        for (index = 0; clipboard[index] != '\0'; ++index)
        {
            if (clipboard[index] >= 32 && clipboard[index] <= 126)
            {
                insert_text_character(value, capacity, cursor, selection_anchor, clipboard[index]);
            }
            else if (allow_newlines && clipboard[index] == '\n')
            {
                insert_text_character(value, capacity, cursor, selection_anchor, '\n');
            }
        }
    }
}

static bool keys_match(
    const uint8_t first[PROTOCOL_IDENTITY_ID_SIZE],
    const uint8_t second[PROTOCOL_IDENTITY_ID_SIZE])
{
    return memcmp(first, second, PROTOCOL_IDENTITY_ID_SIZE) == 0;
}

static void contact_label(
    const uint8_t identity[PROTOCOL_IDENTITY_ID_SIZE],
    const uint8_t own_identity[PROTOCOL_IDENTITY_ID_SIZE],
    char output[40])
{
    if (keys_match(identity, own_identity))
    {
        memcpy(output, "You", sizeof("You"));
        return;
    }

    snprintf(output, 40, "Contact %02x%02x%02x%02x",
             (unsigned int)identity[0], (unsigned int)identity[1],
             (unsigned int)identity[2], (unsigned int)identity[3]);
}

static void build_contact_link(
    const uint8_t identity[PROTOCOL_IDENTITY_ID_SIZE],
    char output[CHAT_CONTACT_LINK_CAPACITY])
{
    static const char prefix[] = "socketchat://contact/";
    static const char hex[] = "0123456789abcdef";
    size_t index;

    memcpy(output, prefix, sizeof(prefix) - 1u);
    for (index = 0; index < PROTOCOL_IDENTITY_ID_SIZE; ++index)
    {
        output[sizeof(prefix) - 1u + index * 2u] = hex[identity[index] >> 4];
        output[sizeof(prefix) + index * 2u] = hex[identity[index] & 0x0fu];
    }
    output[CHAT_CONTACT_LINK_CAPACITY - 1u] = '\0';
}

void chat_screen_draw(
    ClientConnection *connection,
    ClientDatabase *database,
    const ClientIdentity *identity)
{
    static char draft[512] = "";
    static char pending_text[512] = "";
    static char delivery_status[96] = "";
    static char toast[96] = "";
    static char contact_link[256] = "";
    static size_t contact_cursor = 0;
    static size_t contact_selection_anchor = 0;
    static bool adding_contact = false;
    static bool contact_input_focused = false;
    static bool saved_messages_selected = false;
    static bool composer_focused = false;
    static size_t draft_cursor = 0;
    static size_t draft_selection_anchor = 0;
    static float draft_scroll_y = 0.0f;
    static char contact_status[96] = "";
    static uint8_t selected_contact[PROTOCOL_IDENTITY_ID_SIZE] = { 0 };
    static char selected_title[40] = "Saved messages";
    static uint8_t pending_contact[PROTOCOL_IDENTITY_ID_SIZE] = { 0 };
    static uint8_t pending_packet_id[PROTOCOL_PACKET_ID_SIZE] = { 0 };
    static uint32_t pending_request_id = 0;
    static uint64_t pending_timestamp_unix_ms = 0;
    static bool send_pending = false;
    static float toast_expires_at = 0.0f;
    static float theme_hover = 0.0f;
    static float back_hover = 0.0f;
    static float back_press = 0.0f;
    static float saved_card_hover = 0.0f;
    static float saved_card_press = 0.0f;
    static float copy_hover = 0.0f;
    static float copy_press = 0.0f;
    static float add_hover = 0.0f;
    static float add_press = 0.0f;
    static float send_hover = 0.0f;
    static float send_press = 0.0f;
    static float done_hover = 0.0f;
    static float done_press = 0.0f;
    static float cancel_hover = 0.0f;
    static float cancel_press = 0.0f;
    ClientContact contacts[CHAT_MAX_CONTACTS];
    ClientChatMessage messages[CHAT_MAX_VISIBLE_MESSAGES];
    float message_bubble_y[CHAT_MAX_VISIBLE_MESSAGES];
    float message_bubble_height[CHAT_MAX_VISIBLE_MESSAGES];
    float message_bubble_width[CHAT_MAX_VISIBLE_MESSAGES];
    uint32_t message_line_count[CHAT_MAX_VISIBLE_MESSAGES];
    uint32_t contact_count = 0;
    uint32_t message_count = 0;
    uint32_t contact_index;
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float sidebar_width = width < 780.0f ? 245.0f : 310.0f;
    const Rectangle sidebar = { 0.0f, 0.0f, sidebar_width, height };
    const Rectangle header = { sidebar_width, 0.0f, width - sidebar_width, 72.0f };
    const Rectangle theme_button = { width - 62.0f, 15.0f, 42.0f, 42.0f };
    const UiTheme *theme = ui_theme_default();
    const char *empty_title = "Private conversations";
    const char *empty_detail = "Keep notes for yourself or connect with someone you trust.";
    const Font title_font = ui_font_get_semibold();
    const Font body_font = ui_font_get();
    const float main_center = sidebar_width + (width - sidebar_width) * 0.5f;
    const float composer_width = width - sidebar_width - 186.0f;
    const float composer_text_width = composer_width - 32.0f;
    const uint32_t draft_line_count = draw_wrapped_text(
        draft, body_font, 16.0f, composer_text_width, (Vector2){ 0.0f, 0.0f },
        theme->text, false, NULL);
    const float composer_height = fminf((float)draft_line_count * 20.0f + 20.0f,
                                        height * 0.45f);
    const float composer_y = height - 30.0f - composer_height;
    const Vector2 title_size = MeasureTextEx(title_font, empty_title, 24.0f, 1.0f);
    const Vector2 detail_size = MeasureTextEx(body_font, empty_detail, 16.0f, 1.0f);

    ClearBackground(theme->background);
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    draw_background_glow(width, height, theme);
    DrawRectangleRec(sidebar, Fade(theme->surface, 0.86f));
    DrawLine((int)sidebar_width, 0, (int)sidebar_width, (int)height, Fade(theme->border, 0.64f));

    DrawCircleV((Vector2){ 38.0f, 40.0f }, 13.0f, Fade(theme->primary, 0.88f));
    draw_text("S", (Vector2){ 33.0f, 30.0f }, 16.0f, (Color){ 255, 255, 255, 255 }, true);
    draw_text("Socket Chat", (Vector2){ 59.0f, 29.0f }, 22.0f, theme->text, true);
    draw_text("PRIVATE MESSAGES", (Vector2){ 26.0f, 96.0f }, 10.0f, theme->text_muted, true);
    if (database != NULL)
    {
        (void)client_database_list_contacts(database, contacts, CHAT_MAX_CONTACTS,
                                            &contact_count);
    }
    if (connection != NULL && identity != NULL && connection->authenticated)
    {
        const ClientConnectionStatus receive_status =
            client_connection_pump(connection, database, identity);

        if (receive_status != CLIENT_CONNECTION_OK &&
            receive_status != CLIENT_CONNECTION_NO_MESSAGE)
        {
            snprintf(delivery_status, sizeof(delivery_status), "Connection: %s",
                     client_connection_status_string(receive_status));
        }
        if (send_pending)
        {
            uint64_t delivery_id;
            const ClientConnectionStatus send_status = client_connection_take_send_result(
                connection, pending_request_id, pending_packet_id, &delivery_id);

            if (send_status == CLIENT_CONNECTION_OK)
            {
                bool was_inserted;
                const ClientDatabaseStatus database_status = client_database_store_message(
                    database, pending_contact, pending_packet_id, CLIENT_MESSAGE_OUTGOING,
                    pending_timestamp_unix_ms, (const uint8_t *)pending_text,
                    (uint32_t)strlen(pending_text), &was_inserted);
                (void)delivery_id;
                if (database_status == CLIENT_DATABASE_OK)
                {
                    memcpy(delivery_status, "Message delivered", sizeof("Message delivered"));
                }
                else
                {
                    memcpy(delivery_status, "Message delivered but could not be saved",
                           sizeof("Message delivered but could not be saved"));
                }
                send_pending = false;
            }
            else if (send_status != CLIENT_CONNECTION_NO_MESSAGE)
            {
                snprintf(delivery_status, sizeof(delivery_status), "Message: %s",
                         client_connection_status_string(send_status));
                send_pending = false;
            }
        }
    }
    {
        const Rectangle saved_messages = { 18.0f, 156.0f, sidebar_width - 36.0f, 56.0f };
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), saved_messages);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (identity != NULL)
            {
                memcpy(selected_contact, identity->public_key,
                       sizeof(selected_contact));
                memcpy(selected_title, "Saved messages", sizeof("Saved messages"));
            }
            saved_messages_selected = true;
        }
        if (hovered)
        {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        }
        draw_glass_panel(saved_messages, theme,
                         saved_messages_selected ? 0.96f : (hovered ? 0.88f : 0.64f), 12.0f);
        DrawCircleV((Vector2){ 42.0f, 184.0f }, 13.0f,
                    Fade(theme->primary, saved_messages_selected ? 0.92f : 0.62f));
        draw_text("You", (Vector2){ 64.0f, 168.0f }, 16.0f, theme->text, true);
    }
    draw_text("Saved messages", (Vector2){ 64.0f, 191.0f }, 13.0f, theme->text_muted, false);
    for (contact_index = 0; contact_index < contact_count; ++contact_index)
    {
        const Rectangle contact_bounds = { 18.0f, 224.0f + (float)contact_index * 58.0f,
                                           sidebar_width - 36.0f, 54.0f };
        const bool is_self = identity != NULL &&
            keys_match(contacts[contact_index].public_key, identity->public_key);
        const bool selected = saved_messages_selected &&
            keys_match(contacts[contact_index].public_key, selected_contact);
        const bool hovered = !is_self &&
            CheckCollisionPointRec(GetMousePosition(), contact_bounds);
        char label[40];

        if (is_self || contact_bounds.y + contact_bounds.height > height - 12.0f)
        {
            continue;
        }
        contact_label(contacts[contact_index].public_key, identity->public_key, label);
        draw_glass_panel(contact_bounds, theme, selected ? 0.96f : (hovered ? 0.84f : 0.58f), 12.0f);
        DrawCircleV((Vector2){ contact_bounds.x + 23.0f, contact_bounds.y + 26.0f }, 12.0f,
                    Fade(theme->primary, selected ? 0.90f : 0.54f));
        draw_text(label, (Vector2){ contact_bounds.x + 43.0f, contact_bounds.y + 9.0f },
                  14.0f, theme->text, true);
        draw_text("Private contact", (Vector2){ contact_bounds.x + 43.0f,
                                                 contact_bounds.y + 30.0f },
                  12.0f, theme->text_muted, false);
        if (hovered)
        {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        }
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            memcpy(selected_contact, contacts[contact_index].public_key,
                   sizeof(selected_contact));
            memcpy(selected_title, label, sizeof(selected_title));
            saved_messages_selected = true;
            composer_focused = false;
        }
    }

    DrawRectangleRec(header, Fade(theme->surface, 0.74f));
    DrawLine((int)sidebar_width, (int)header.height, (int)width, (int)header.height,
             Fade(theme->border, 0.60f));
    if (saved_messages_selected)
    {
        const Rectangle back = { sidebar_width + 20.0f, 16.0f, 112.0f, 40.0f };
        if (glass_button(back, "<  Chats", theme, &back_hover, &back_press, true))
        {
            saved_messages_selected = false;
            composer_focused = false;
        }
        draw_text(selected_title, (Vector2){ sidebar_width + 150.0f, 24.0f },
                  18.0f, theme->text, true);
    }
    else
    {
        draw_text("Chats", (Vector2){ sidebar_width + 28.0f, 24.0f }, 18.0f, theme->text, true);
    }
    mode_toggle(theme_button, theme, &theme_hover);

    if (!saved_messages_selected)
    {
        const Rectangle saved_card = { main_center - 210.0f, height * 0.46f, 420.0f, 82.0f };
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), saved_card);
        const bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        Rectangle animated_saved_card;
        const Rectangle add_contact = { width - 178.0f, height - 84.0f, 150.0f, 48.0f };
        const Rectangle copy_link = { sidebar_width + 32.0f, height - 84.0f, 138.0f, 48.0f };

        saved_card_hover = ui_smooth(saved_card_hover, hovered ? 1.0f : 0.0f, 13.0f, GetFrameTime());
        saved_card_press = ui_smooth(saved_card_press, pressed ? 1.0f : 0.0f, 22.0f, GetFrameTime());
        animated_saved_card = scaled_rectangle(saved_card,
                                               1.0f + saved_card_hover * 0.018f - saved_card_press * 0.032f);

        draw_text(empty_title,
                  (Vector2){ main_center - title_size.x * 0.5f, height * 0.28f },
                  26.0f, theme->text, true);
        draw_text(empty_detail,
                  (Vector2){ main_center - detail_size.x * 0.5f, height * 0.28f + 40.0f },
                  16.0f, theme->text_muted, false);
        draw_glass_panel(animated_saved_card, theme, hovered ? 0.94f : 0.82f, 18.0f);
        DrawCircleV((Vector2){ animated_saved_card.x + 43.0f, animated_saved_card.y + 41.0f }, 19.0f,
                    Fade(theme->primary, 0.78f + saved_card_hover * 0.16f));
        draw_text("You", (Vector2){ animated_saved_card.x + 78.0f, animated_saved_card.y + 17.0f },
                  18.0f, theme->text, true);
        draw_text("Saved messages", (Vector2){ animated_saved_card.x + 78.0f, animated_saved_card.y + 45.0f },
                  14.0f, theme->text_muted, false);
        draw_text("Open  >", (Vector2){ animated_saved_card.x + animated_saved_card.width - 76.0f,
                                         animated_saved_card.y + 31.0f },
                  13.0f, hovered ? theme->primary : theme->text_muted, true);
        if (hovered)
        {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        }
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (identity != NULL)
            {
                memcpy(selected_contact, identity->public_key,
                       sizeof(selected_contact));
                memcpy(selected_title, "Saved messages", sizeof("Saved messages"));
            }
            saved_messages_selected = true;
        }
        if (glass_button(copy_link, "Copy my link", theme, &copy_hover, &copy_press, true) && identity != NULL)
        {
            char own_link[CHAT_CONTACT_LINK_CAPACITY];
            build_contact_link(identity->public_key, own_link);
            SetClipboardText(own_link);
            show_toast(toast, &toast_expires_at, "Contact link copied");
        }
        (void)glass_button(add_contact, "+ Add contact", theme, &add_hover, &add_press, false);
        if (CheckCollisionPointRec(GetMousePosition(), add_contact) &&
            (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonReleased(MOUSE_BUTTON_LEFT)))
        {
            adding_contact = true;
            contact_input_focused = true;
            contact_link[0] = '\0';
            contact_cursor = 0;
            contact_selection_anchor = 0;
            contact_status[0] = '\0';
        }
    }

    if (saved_messages_selected)
    {
        draw_text(keys_match(selected_contact, identity->public_key) ? "Saved messages"
                                                                      : "Authenticated direct message",
                  (Vector2){ sidebar_width + 28.0f, 57.0f }, 13.0f,
                  theme->text_muted, false);
    }

    if (saved_messages_selected && database != NULL && identity != NULL)
    {
        if (client_database_load_messages(database, selected_contact, messages,
                                          CHAT_MAX_VISIBLE_MESSAGES,
                                          &message_count) != CLIENT_DATABASE_OK)
        {
            message_count = 0;
        }
        if (message_count == 0)
        {
            const char *empty_chat_title = keys_match(selected_contact, identity->public_key)
                                               ? "A quiet place for your thoughts"
                                               : "No messages yet";
            const char *empty_chat_detail = keys_match(selected_contact, identity->public_key)
                                                ? "Write something to keep it close."
                                                : "Send the first message when they are online.";
            const Vector2 empty_chat_title_size = MeasureTextEx(
                title_font, empty_chat_title, 20.0f, 1.0f);
            const Vector2 empty_chat_detail_size = MeasureTextEx(
                body_font, empty_chat_detail, 14.0f, 1.0f);
            const float empty_chat_center = sidebar_width + (width - sidebar_width) * 0.5f;

            DrawCircleV((Vector2){ empty_chat_center, height * 0.39f }, 26.0f,
                        Fade(theme->primary, 0.13f));
            DrawCircleV((Vector2){ empty_chat_center, height * 0.39f }, 9.0f,
                        Fade(theme->primary, 0.75f));
            draw_text(empty_chat_title,
                      (Vector2){ empty_chat_center - empty_chat_title_size.x * 0.5f,
                                 height * 0.47f },
                      20.0f, theme->text, true);
            draw_text(empty_chat_detail,
                      (Vector2){ empty_chat_center - empty_chat_detail_size.x * 0.5f,
                                 height * 0.47f + 30.0f },
                      14.0f, theme->text_muted, false);
        }
        {
            float next_message_bottom = composer_y - 32.0f;

            for (contact_index = message_count; contact_index > 0; --contact_index)
            {
                const uint32_t message_index = contact_index - 1u;
                const float message_width = MeasureTextEx(
                    body_font, messages[message_index].text, 16.0f, 1.0f).x;

                message_bubble_width[message_index] = fminf(
                    message_width + 42.0f, width - sidebar_width - 80.0f);
                message_line_count[message_index] = draw_wrapped_text(
                    messages[message_index].text,
                    body_font,
                    16.0f,
                    message_bubble_width[message_index] - 40.0f,
                    (Vector2){ 0.0f, 0.0f },
                    theme->text,
                    false,
                    NULL);
                message_bubble_height[message_index] =
                    (float)message_line_count[message_index] * 20.0f + 18.0f;
                next_message_bottom -= message_bubble_height[message_index];
                message_bubble_y[message_index] = next_message_bottom;
                next_message_bottom -= 28.0f;
            }
        }
        for (contact_index = 0; contact_index < message_count; ++contact_index)
        {
            const ClientChatMessage *message = &messages[contact_index];
            const bool incoming = message->direction == CLIENT_MESSAGE_INCOMING;
            const float bubble_width = message_bubble_width[contact_index];
            const float bubble_x = incoming ? sidebar_width + 30.0f
                                            : width - 30.0f - bubble_width;
            const float bubble_y = message_bubble_y[contact_index];

            if (bubble_y + message_bubble_height[contact_index] < header.height + 14.0f)
            {
                continue;
            }

            draw_text(incoming ? selected_title : "You",
                      (Vector2){ bubble_x + 10.0f, bubble_y - 16.0f }, 13.0f,
                      theme->text_muted, true);
            draw_glass_panel((Rectangle){ bubble_x, bubble_y, bubble_width,
                                          message_bubble_height[contact_index] },
                             theme, incoming ? 0.88f : 0.82f, 12.0f);
            if (!incoming)
            {
                DrawRectangleRounded((Rectangle){ bubble_x, bubble_y,
                                                  bubble_width, message_bubble_height[contact_index] }, 0.28f, 10,
                                     Fade(theme->primary, 0.15f));
            }
            (void)draw_wrapped_text(message->text, body_font, 16.0f, bubble_width - 40.0f,
                                    (Vector2){ bubble_x + 20.0f, bubble_y + 10.0f },
                                    theme->text, true, NULL);
        }
    }

    if (saved_messages_selected)
    {
        const Rectangle composer = { sidebar_width + 28.0f, composer_y,
                                     composer_width, composer_height };
        const Rectangle send = { width - 142.0f, height - 82.0f, 114.0f, 52.0f };
        const bool send_pressed = glass_button(send, send_pending ? "Sending" : "Send  >", theme,
                                               &send_hover, &send_press, false);
        const bool shift_down = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        uint32_t caret_line_count = 1;
        uint32_t current_draft_line_count;
        float caret_top;
        float caret_line_width = 0.0f;
        float maximum_draft_scroll;
        char saved_character;
        draw_glass_panel(composer, theme, composer_focused ? 0.96f : 0.82f, 13.0f);
        if (CheckCollisionPointRec(GetMousePosition(), composer) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            composer_focused = true;
            draft_cursor = strlen(draft);
            draft_selection_anchor = draft_cursor;
        }
        /* A selected conversation owns keyboard input, even if the mouse click that
         * opened it landed on the transparent edge of the glass composer. */
        if (!adding_contact)
        {
            composer_focused = true;
            edit_text(draft, sizeof(draft), &draft_cursor, &draft_selection_anchor, true);
        }
        saved_character = draft[draft_cursor];
        draft[draft_cursor] = '\0';
        caret_line_count = draw_wrapped_text(
            draft, body_font, 16.0f, composer_text_width,
            (Vector2){ 0.0f, 0.0f }, theme->text, false, &caret_line_width);
        draft[draft_cursor] = saved_character;
        current_draft_line_count = draw_wrapped_text(
            draft, body_font, 16.0f, composer_text_width,
            (Vector2){ 0.0f, 0.0f }, theme->text, false, NULL);
        caret_top = (float)(caret_line_count - 1u) * 20.0f;
        maximum_draft_scroll = fmaxf(
            0.0f, (float)current_draft_line_count * 20.0f - (composer.height - 20.0f));
        if (caret_top < draft_scroll_y)
        {
            draft_scroll_y = caret_top;
        }
        else if (caret_top + 20.0f > draft_scroll_y + composer.height - 20.0f)
        {
            draft_scroll_y = caret_top + 20.0f - (composer.height - 20.0f);
        }
        draft_scroll_y = ui_clamp(draft_scroll_y, 0.0f, maximum_draft_scroll);
        BeginScissorMode((int)composer.x + 12, (int)composer.y + 8,
                         (int)composer.width - 24, (int)composer.height - 16);
        if (draft_cursor != draft_selection_anchor)
        {
            DrawRectangle((int)composer.x + 14, (int)composer.y + 10,
                          (int)composer.width - 28, (int)composer.height - 20,
                          Fade(theme->primary, 0.18f));
        }
        if (draft[0] == '\0')
        {
            draw_text("Write a message...", (Vector2){ composer.x + 16.0f, composer.y + 10.0f },
                      16.0f, theme->text_muted, false);
        }
        else
        {
            (void)draw_wrapped_text(draft, body_font, 16.0f, composer_text_width,
                                    (Vector2){ composer.x + 16.0f,
                                                composer.y + 10.0f - draft_scroll_y },
                                    theme->text, true, &caret_line_width);
        }
        EndScissorMode();
        if (composer_focused && fmodf((float)GetTime(), 1.0f) < 0.55f)
        {
            float caret_y;
            float caret_x;

            caret_x = composer.x + 16.0f + caret_line_width;
            caret_y = composer.y + 10.0f + caret_top - draft_scroll_y;

            DrawRectangle((int)caret_x, (int)caret_y, 2, 19, theme->primary);
        }
        if (!send_pending &&
            (send_pressed || (IsKeyPressed(KEY_ENTER) && !shift_down && draft[0] != '\0')) &&
            draft[0] != '\0')
        {
            uint32_t request_id;
            uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE];
            uint64_t sender_timestamp_unix_ms;
            if (connection != NULL && identity != NULL &&
                client_connection_send_private_text(connection, identity,
                                                    selected_contact, draft,
                                                    &request_id, packet_id,
                                                    &sender_timestamp_unix_ms) == CLIENT_CONNECTION_OK)
            {
                memcpy(pending_text, draft, sizeof(pending_text));
                memcpy(pending_contact, selected_contact, sizeof(pending_contact));
                memcpy(pending_packet_id, packet_id, sizeof(pending_packet_id));
                pending_request_id = request_id;
                pending_timestamp_unix_ms = sender_timestamp_unix_ms;
                send_pending = true;
                draft[0] = '\0';
                draft_cursor = 0;
                draft_selection_anchor = 0;
                draft_scroll_y = 0.0f;
                composer_focused = false;
                memcpy(delivery_status, "Sending message...", sizeof("Sending message..."));
            }
            else
            {
                memcpy(delivery_status, "Message could not be delivered. Is the contact online?",
                       sizeof("Message could not be delivered. Is the contact online?"));
            }
        }
        if (delivery_status[0] != '\0')
        {
            draw_text(delivery_status, (Vector2){ composer.x + 4.0f, composer.y - 24.0f },
                      13.0f, theme->text_muted, false);
        }
    }

    if (adding_contact)
    {
        const Rectangle panel = { sidebar_width + 70.0f, height * 0.22f,
                                  width - sidebar_width - 140.0f, 228.0f };
        const Rectangle input = { panel.x + 24.0f, panel.y + 88.0f, panel.width - 48.0f, 46.0f };
        const Rectangle cancel = { panel.x + 24.0f, panel.y + 158.0f, 94.0f, 40.0f };
        const Rectangle done = { panel.x + panel.width - 110.0f, panel.y + 158.0f, 86.0f, 40.0f };
        const Vector2 mouse = GetMousePosition();

        if (IsKeyPressed(KEY_ESCAPE) ||
            (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, panel)))
        {
            adding_contact = false;
            contact_input_focused = false;
            contact_link[0] = '\0';
            contact_cursor = 0;
            contact_selection_anchor = 0;
            contact_status[0] = '\0';
        }
        if (!adding_contact)
        {
            draw_toast(toast, toast_expires_at, width, theme);
            return;
        }
        DrawRectangleRec((Rectangle){ sidebar_width, 72.0f, width - sidebar_width, height - 72.0f },
                         Fade(theme->background, 0.32f));
        draw_glass_panel(panel, theme, 0.97f, 20.0f);
        draw_text("Add a contact", (Vector2){ panel.x + 24.0f, panel.y + 24.0f }, 20.0f, theme->text, true);
        draw_text("Paste their Socket Chat link (a QR scanner can paste the same link here).",
                  (Vector2){ panel.x + 24.0f, panel.y + 54.0f }, 13.0f, theme->text_muted, false);
        draw_glass_panel(input, theme, 0.76f, 10.0f);
        if (CheckCollisionPointRec(mouse, input) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            contact_input_focused = true;
            contact_cursor = strlen(contact_link);
            contact_selection_anchor = contact_cursor;
        }
        if (contact_input_focused)
        {
            edit_text(contact_link, sizeof(contact_link), &contact_cursor,
                      &contact_selection_anchor, false);
        }
        draw_text(contact_link[0] == '\0' ? "socketchat://contact/..." : contact_link,
                  (Vector2){ input.x + 12.0f, input.y + 14.0f }, 14.0f,
                  contact_link[0] == '\0' ? theme->text_muted : theme->text, false);
        if (contact_input_focused && fmodf((float)GetTime(), 1.0f) < 0.55f)
        {
            const char saved_character = contact_link[contact_cursor];
            float caret_x;

            contact_link[contact_cursor] = '\0';
            caret_x = input.x + 12.0f +
                MeasureTextEx(body_font, contact_link, 14.0f, 1.0f).x;
            contact_link[contact_cursor] = saved_character;
            DrawRectangle((int)caret_x, (int)input.y + 12, 2, 21, theme->primary);
        }
        if (glass_button(cancel, "Cancel", theme, &cancel_hover, &cancel_press, true))
        {
            adding_contact = false;
            contact_input_focused = false;
            contact_link[0] = '\0';
            contact_status[0] = '\0';
        }
        if (glass_button(done, "Add", theme, &done_hover, &done_press, false))
        {
            const ClientDatabaseStatus status = client_database_add_contact_link(database, contact_link);
            if (status == CLIENT_DATABASE_OK)
            {
                contact_link[0] = '\0';
                contact_cursor = 0;
                contact_selection_anchor = 0;
                adding_contact = false;
                contact_input_focused = false;
                contact_status[0] = '\0';
                show_toast(toast, &toast_expires_at, "Contact added");
            }
            else
            {
                memcpy(contact_status, "Use a full Socket Chat contact link", sizeof("Use a full Socket Chat contact link"));
            }
        }
        if (contact_status[0] != '\0')
        {
            draw_text(contact_status, (Vector2){ panel.x + 132.0f, panel.y + 171.0f },
                      13.0f, theme->text_muted, false);
        }
    }

    draw_toast(toast, toast_expires_at, width, theme);
}
