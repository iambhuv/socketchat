#include "client/chat_screen.h"

#include "client/client_connection.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <raylib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHAT_MAX_CONTACTS 24u
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
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
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
    static char echo[512] = "";
    static char delivery_status[96] = "";
    static char contact_link[256] = "";
    static bool adding_contact = false;
    static bool saved_messages_selected = false;
    static bool composer_focused = false;
    static char contact_status[96] = "";
    static uint8_t selected_contact[PROTOCOL_IDENTITY_ID_SIZE] = { 0 };
    static char selected_title[40] = "Saved messages";
    static bool last_message_was_incoming = false;
    ClientContact contacts[CHAT_MAX_CONTACTS];
    uint32_t contact_count = 0;
    uint32_t contact_index;
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float sidebar_width = width < 780.0f ? 245.0f : 310.0f;
    const Rectangle sidebar = { 0.0f, 0.0f, sidebar_width, height };
    const Rectangle header = { sidebar_width, 0.0f, width - sidebar_width, 72.0f };
    const Rectangle theme_button = { width - 62.0f, 15.0f, 42.0f, 42.0f };
    const UiTheme *theme = ui_theme_default();
    const char *empty_title = "Your private space";
    const char *empty_detail = "Start with saved messages, or add someone you trust.";
    const Font title_font = ui_font_get_semibold();
    const Font body_font = ui_font_get();
    const float main_center = sidebar_width + (width - sidebar_width) * 0.5f;
    const Vector2 title_size = MeasureTextEx(title_font, empty_title, 24.0f, 1.0f);
    const Vector2 detail_size = MeasureTextEx(body_font, empty_detail, 16.0f, 1.0f);

    ClearBackground(theme->background);
    DrawRectangleRec(sidebar, theme->surface);
    DrawLine((int)sidebar_width, 0, (int)sidebar_width, (int)height, Fade(theme->border, 0.92f));

    draw_text("Socket Chat", (Vector2){ 26.0f, 28.0f }, 23.0f, theme->text, true);
    draw_text("Messages", (Vector2){ 26.0f, 88.0f }, 14.0f, theme->text_muted, true);
    if (database != NULL)
    {
        (void)client_database_list_contacts(database, contacts, CHAT_MAX_CONTACTS,
                                            &contact_count);
    }
    if (connection != NULL && identity != NULL && connection->authenticated)
    {
        char received_text[sizeof(echo)];
        uint8_t sender[PROTOCOL_IDENTITY_ID_SIZE];
        uint64_t received_at;
        const ClientConnectionStatus receive_status =
            client_connection_poll_private_message(
                connection, identity, sender, received_text, sizeof(received_text),
                &received_at);

        (void)received_at;
        if (receive_status == CLIENT_CONNECTION_OK)
        {
            if (saved_messages_selected && keys_match(sender, selected_contact))
            {
                memcpy(echo, received_text, sizeof(echo));
                last_message_was_incoming = true;
                memcpy(delivery_status, "Message received and acknowledged",
                       sizeof("Message received and acknowledged"));
            }
            else
            {
                memcpy(delivery_status, "New private message received",
                       sizeof("New private message received"));
            }
        }
        else if (receive_status != CLIENT_CONNECTION_NO_MESSAGE)
        {
            snprintf(delivery_status, sizeof(delivery_status), "Connection: %s",
                     client_connection_status_string(receive_status));
        }
    }
    {
        const Rectangle saved_messages = { 18.0f, 164.0f, sidebar_width - 36.0f, 56.0f };
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
            echo[0] = '\0';
        }
        DrawRectangleRounded(saved_messages, roundness(saved_messages, 12.0f), 10,
                             saved_messages_selected ? Fade(theme->primary, 0.18f)
                                                    : (hovered ? Fade(theme->primary, 0.10f)
                                                               : Fade(theme->surface, 0.15f)));
        draw_text("You", (Vector2){ 26.0f, 178.0f }, 16.0f, theme->text, true);
    }
    draw_text("Saved messages", (Vector2){ 26.0f, 201.0f }, 13.0f, theme->text_muted, false);
    for (contact_index = 0; contact_index < contact_count; ++contact_index)
    {
        const Rectangle contact_bounds = { 18.0f, 232.0f + (float)contact_index * 58.0f,
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
        DrawRectangleRounded(contact_bounds, roundness(contact_bounds, 12.0f), 10,
                             selected ? Fade(theme->primary, 0.18f)
                                      : (hovered ? Fade(theme->primary, 0.10f)
                                                 : Fade(theme->surface, 0.15f)));
        draw_text(label, (Vector2){ contact_bounds.x + 10.0f, contact_bounds.y + 10.0f },
                  14.0f, theme->text, true);
        draw_text("Private contact", (Vector2){ contact_bounds.x + 10.0f,
                                                 contact_bounds.y + 30.0f },
                  12.0f, theme->text_muted, false);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            memcpy(selected_contact, contacts[contact_index].public_key,
                   sizeof(selected_contact));
            memcpy(selected_title, label, sizeof(selected_title));
            saved_messages_selected = true;
            composer_focused = false;
            echo[0] = '\0';
            last_message_was_incoming = false;
        }
    }

    DrawRectangleRec(header, theme->surface);
    DrawLine((int)sidebar_width, (int)header.height, (int)width, (int)header.height, Fade(theme->border, 0.92f));
    if (saved_messages_selected)
    {
        const Rectangle back = { sidebar_width + 20.0f, 16.0f, 112.0f, 40.0f };
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), back);
        DrawRectangleRounded(back, roundness(back, 10.0f), 10,
                             hovered ? Fade(theme->primary, 0.12f) : Fade(theme->surface, 0.01f));
        draw_text("<  Chats", (Vector2){ back.x + 10.0f, back.y + 11.0f },
                  15.0f, hovered ? theme->primary : theme->text_muted, true);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
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
    mode_toggle(theme_button, theme);

    if (!saved_messages_selected)
    {
        const Rectangle saved_card = { main_center - 218.0f, height * 0.46f, 436.0f, 88.0f };
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), saved_card);
        const Rectangle add_contact = { width - 178.0f, height - 84.0f, 150.0f, 48.0f };
        const Rectangle copy_link = { sidebar_width + 32.0f, height - 84.0f, 138.0f, 48.0f };

        draw_text(empty_title,
                  (Vector2){ main_center - title_size.x * 0.5f, height * 0.28f },
                  26.0f, theme->text, true);
        draw_text(empty_detail,
                  (Vector2){ main_center - detail_size.x * 0.5f, height * 0.28f + 40.0f },
                  16.0f, theme->text_muted, false);
        DrawRectangleRounded(saved_card, roundness(saved_card, 18.0f), 12,
                             hovered ? Fade(theme->primary, 0.15f) : Fade(theme->surface, 0.72f));
        DrawRectangleRoundedLinesEx(saved_card, roundness(saved_card, 18.0f), 12, 1.0f,
                                    Fade(theme->border, 0.76f));
        DrawCircleV((Vector2){ saved_card.x + 44.0f, saved_card.y + 44.0f }, 20.0f,
                    Fade(theme->primary, 0.74f));
        draw_text("You", (Vector2){ saved_card.x + 82.0f, saved_card.y + 21.0f },
                  18.0f, theme->text, true);
        draw_text("Saved messages", (Vector2){ saved_card.x + 82.0f, saved_card.y + 49.0f },
                  14.0f, theme->text_muted, false);
        draw_text(">", (Vector2){ saved_card.x + saved_card.width - 34.0f, saved_card.y + 31.0f },
                  20.0f, theme->text_muted, true);
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (identity != NULL)
            {
                memcpy(selected_contact, identity->public_key,
                       sizeof(selected_contact));
                memcpy(selected_title, "Saved messages", sizeof("Saved messages"));
            }
            saved_messages_selected = true;
            echo[0] = '\0';
            last_message_was_incoming = false;
        }
        if (button(copy_link, "Copy my link", theme) && identity != NULL)
        {
            char own_link[CHAT_CONTACT_LINK_CAPACITY];
            build_contact_link(identity->public_key, own_link);
            SetClipboardText(own_link);
            memcpy(delivery_status, "Your contact link was copied",
                   sizeof("Your contact link was copied"));
        }
        if (button(add_contact, "+ Add contact", theme))
        {
            adding_contact = true;
        }
    }

    if (saved_messages_selected)
    {
        draw_text(keys_match(selected_contact, identity->public_key) ? "Saved messages"
                                                                      : "Authenticated direct message",
                  (Vector2){ sidebar_width + 28.0f, 57.0f }, 13.0f,
                  theme->text_muted, false);
    }

    if (saved_messages_selected && echo[0] != '\0')
    {
        const float message_width = MeasureTextEx(body_font, echo, 16.0f, 1.0f).x;
        const float bubble_width = message_width + 42.0f;
        const float bubble_x = last_message_was_incoming ? sidebar_width + 30.0f
                                                          : width - 72.0f - message_width;
        draw_text(last_message_was_incoming ? selected_title : "You",
                  (Vector2){ bubble_x + 10.0f, height - 168.0f },
                  13.0f, theme->text_muted, true);
        DrawRectangleRounded((Rectangle){ bubble_x, height - 146.0f,
                                          bubble_width, 42.0f }, 0.28f, 10,
                             last_message_was_incoming ? Fade(theme->surface, 0.90f)
                                                      : Fade(theme->primary, 0.18f));
        draw_text(echo, (Vector2){ bubble_x + 20.0f,
                                   height - 133.0f }, 16.0f, theme->text, false);
    }

    if (saved_messages_selected)
    {
        const Rectangle composer = { sidebar_width + 28.0f, height - 82.0f,
                                     width - sidebar_width - 186.0f, 52.0f };
        const Rectangle send = { width - 142.0f, height - 82.0f, 114.0f, 52.0f };
        DrawRectangleRounded(composer, roundness(composer, 13.0f), 12, Fade(theme->surface, 0.76f));
        DrawRectangleRoundedLinesEx(composer, roundness(composer, 13.0f), 12, 1.0f,
                                    Fade(theme->border, 0.85f));
        if (CheckCollisionPointRec(GetMousePosition(), composer) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            composer_focused = true;
        }
        if (composer_focused)
        {
            edit_text(draft, sizeof(draft));
        }
        draw_text(draft[0] == '\0' ? "Write a message..." : draft,
                  (Vector2){ composer.x + 16.0f, composer.y + 16.0f }, 16.0f,
                  draft[0] == '\0' ? theme->text_muted : theme->text, false);
        if (composer_focused && fmodf((float)GetTime(), 1.0f) < 0.55f)
        {
            const char *caret_text = draft[0] == '\0' ? "" : draft;
            const float caret_x = composer.x + 16.0f +
                MeasureTextEx(body_font, caret_text, 16.0f, 1.0f).x;
            DrawRectangle((int)caret_x, (int)composer.y + 14, 2, 23, theme->primary);
        }
        if ((button(send, "Send", theme) || (IsKeyPressed(KEY_ENTER) && draft[0] != '\0')) &&
            draft[0] != '\0')
        {
            uint64_t delivery_id;
            if (connection != NULL && identity != NULL &&
                client_connection_send_private_text(connection, identity,
                                                    selected_contact, draft,
                                                    &delivery_id) == CLIENT_CONNECTION_OK)
            {
                (void)delivery_id;
                memcpy(echo, draft, sizeof(echo));
                draft[0] = '\0';
                composer_focused = false;
                last_message_was_incoming = false;
                memcpy(delivery_status, "Message accepted by the server",
                       sizeof("Message accepted by the server"));
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
                                  width - sidebar_width - 140.0f, 220.0f };
        const Rectangle input = { panel.x + 24.0f, panel.y + 88.0f, panel.width - 48.0f, 46.0f };
        DrawRectangleRounded(panel, roundness(panel, 20.0f), 14, Fade(theme->surface, 0.96f));
        DrawRectangleRoundedLinesEx(panel, roundness(panel, 20.0f), 14, 1.0f, theme->border);
        draw_text("Add a contact", (Vector2){ panel.x + 24.0f, panel.y + 24.0f }, 20.0f, theme->text, true);
        draw_text("Paste their Socket Chat link (a QR scanner can paste the same link here).",
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
            const ClientDatabaseStatus status = client_database_add_contact_link(database, contact_link);
            if (status == CLIENT_DATABASE_OK)
            {
                memcpy(contact_status, "Contact saved", sizeof("Contact saved"));
                contact_link[0] = '\0';
                adding_contact = false;
            }
            else
            {
                memcpy(contact_status, "Use a full Socket Chat contact link", sizeof("Use a full Socket Chat contact link"));
            }
        }
        if (contact_status[0] != '\0')
        {
            draw_text(contact_status, (Vector2){ panel.x + 24.0f, panel.y + 168.0f },
                      13.0f, theme->text_muted, false);
        }
    }
}
