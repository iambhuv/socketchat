#include "client/connection_screen.h"

#include "ui/ui_animation.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t connection_screen_read_port(const char *text)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);

    if (errno != 0 || text[0] == '\0' || *end != '\0' ||
        value == 0 || value > UINT16_MAX)
    {
        return 0;
    }

    return (uint16_t)value;
}

static void connection_screen_draw_background(const UiTheme *theme)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float time = (float)GetTime();
    const Color lavender = { 206, 195, 245, 255 };
    const Color peach = { 247, 210, 201, 255 };
    const Color mint = { 199, 235, 221, 255 };

    ClearBackground(theme->background);

    DrawCircleV(
        (Vector2){
            width * 0.13f + sinf(time * 0.45f) * 8.0f,
            height * 0.22f + cosf(time * 0.38f) * 7.0f
        },
        118.0f,
        Fade(lavender, 0.23f));
    DrawCircleV(
        (Vector2){
            width * 0.88f + cosf(time * 0.35f) * 10.0f,
            height * 0.78f + sinf(time * 0.42f) * 8.0f
        },
        145.0f,
        Fade(peach, 0.22f));
    DrawCircleV(
        (Vector2){
            width * 0.83f + sinf(time * 0.28f) * 6.0f,
            height * 0.14f + cosf(time * 0.31f) * 5.0f
        },
        58.0f,
        Fade(mint, 0.24f));
}

static void connection_screen_draw_chat_icon(
    Vector2 center,
    const UiTheme *theme)
{
    const Rectangle shadow = {
        center.x - 30.0f,
        center.y - 26.0f,
        60.0f,
        60.0f
    };
    const Rectangle bubble = {
        center.x - 30.0f,
        center.y - 30.0f,
        60.0f,
        60.0f
    };
    const Color dot = Fade(WHITE, 0.92f);

    DrawRectangleRounded(shadow, 0.42f, 14, Fade(theme->primary, 0.18f));
    DrawRectangleRounded(bubble, 0.42f, 14, theme->primary);
    DrawTriangle(
        (Vector2){ center.x + 10.0f, center.y + 27.0f },
        (Vector2){ center.x + 23.0f, center.y + 38.0f },
        (Vector2){ center.x + 22.0f, center.y + 20.0f },
        theme->primary);

    DrawCircleV((Vector2){ center.x - 13.0f, center.y }, 4.0f, dot);
    DrawCircleV((Vector2){ center.x, center.y }, 4.0f, dot);
    DrawCircleV((Vector2){ center.x + 13.0f, center.y }, 4.0f, dot);
}

static void connection_screen_draw_heading(
    Rectangle card,
    const UiTheme *theme)
{
    const Font title_font = ui_font_get_semibold();
    const Font body_font = ui_font_get();
    const char *title = "Connect to your relay";
    const char *subtitle = "Enter the server used by your group";
    const float title_size = 30.0f;
    const Vector2 title_measure = MeasureTextEx(
        title_font,
        title,
        title_size,
        1.0f);
    const Vector2 subtitle_measure = MeasureTextEx(
        body_font,
        subtitle,
        theme->small_text_size,
        1.0f);

    DrawTextEx(
        title_font,
        title,
        (Vector2){
            card.x + (card.width - title_measure.x) * 0.5f,
            card.y + 111.0f
        },
        title_size,
        1.0f,
        theme->text);
    DrawTextEx(
        body_font,
        subtitle,
        (Vector2){
            card.x + (card.width - subtitle_measure.x) * 0.5f,
            card.y + 151.0f
        },
        theme->small_text_size,
        1.0f,
        theme->text_muted);
}

static void connection_screen_cycle_focus(ConnectionScreen *screen)
{
    if (!IsKeyPressed(KEY_TAB))
    {
        return;
    }

    if (screen->host_input.focused)
    {
        screen->host_input.focused = false;
        screen->port_input.focused = true;
        screen->port_input.caret_time = 0.0f;
    }
    else
    {
        screen->port_input.focused = false;
        screen->host_input.focused = true;
        screen->host_input.caret_time = 0.0f;
    }
}

void connection_screen_init(ConnectionScreen *screen)
{
    if (screen == NULL)
    {
        return;
    }

    memset(screen, 0, sizeof(*screen));
    snprintf(screen->host, sizeof(screen->host), "%s", "127.0.0.1");
    snprintf(screen->port, sizeof(screen->port), "%s", "5050");
    connection_screen_set_status(screen, UI_STATUS_OFFLINE, "Offline");
}

bool connection_screen_draw(ConnectionScreen *screen)
{
    const UiTheme *theme = ui_theme_default();
    const float screen_width = (float)GetScreenWidth();
    const float screen_height = (float)GetScreenHeight();
    const float card_width = ui_clamp(screen_width - 56.0f, 420.0f, 520.0f);
    const float card_height = 500.0f;
    Rectangle card;
    Rectangle host_input;
    Rectangle port_input;
    Rectangle connect_button;
    bool connect_requested;
    bool host_changed;
    bool port_changed;

    if (screen == NULL)
    {
        return false;
    }

    screen->entrance_amount = ui_smooth(
        screen->entrance_amount,
        1.0f,
        7.0f,
        GetFrameTime());

    card = (Rectangle){
        (screen_width - card_width) * 0.5f,
        (screen_height - card_height) * 0.5f +
            (1.0f - screen->entrance_amount) * 22.0f,
        card_width,
        card_height
    };
    host_input = (Rectangle){
        card.x + 42.0f,
        card.y + 218.0f,
        card.width - 84.0f,
        theme->control_height
    };
    port_input = (Rectangle){
        card.x + 42.0f,
        card.y + 312.0f,
        card.width - 84.0f,
        theme->control_height
    };
    connect_button = (Rectangle){
        card.x + 42.0f,
        card.y + 397.0f,
        card.width - 84.0f,
        theme->control_height
    };

    connection_screen_draw_background(theme);
    ui_draw_panel(card, theme);
    ui_status_badge(
        (Rectangle){ card.x + card.width - 156.0f, card.y + 22.0f, 132.0f, 32.0f },
        screen->status_text,
        screen->status_kind,
        theme);
    connection_screen_draw_chat_icon(
        (Vector2){ card.x + card.width * 0.5f, card.y + 67.0f },
        theme);
    connection_screen_draw_heading(card, theme);

    connection_screen_cycle_focus(screen);

    host_changed = ui_text_input(
        host_input,
        "Relay address",
        "relay.example.com",
        screen->host,
        sizeof(screen->host),
        UI_TEXT_INPUT_NORMAL,
        screen->host_has_error,
        &screen->host_input,
        theme);
    port_changed = ui_text_input(
        port_input,
        "Port",
        "5050",
        screen->port,
        sizeof(screen->port),
        UI_TEXT_INPUT_DIGITS,
        screen->port_has_error,
        &screen->port_input,
        theme);

    if (host_changed)
    {
        screen->host_has_error = false;
    }

    if (port_changed)
    {
        screen->port_has_error = false;
    }

    if ((host_changed || port_changed) &&
        !screen->host_has_error &&
        !screen->port_has_error &&
        screen->status_kind == UI_STATUS_ERROR)
    {
        connection_screen_set_status(screen, UI_STATUS_OFFLINE, "Offline");
    }

    connect_requested = ui_button(
        connect_button,
        "Connect to relay",
        true,
        &screen->connect_button,
        theme);

    if (IsKeyPressed(KEY_ENTER))
    {
        connect_requested = true;
    }

    if (connect_requested)
    {
        screen->host_has_error = screen->host[0] == '\0';
        screen->port_has_error = connection_screen_read_port(screen->port) == 0;

        if (screen->host_has_error || screen->port_has_error)
        {
            connection_screen_set_status(
                screen,
                UI_STATUS_ERROR,
                "Check details");
            return false;
        }
    }

    return connect_requested;
}

const char *connection_screen_host(const ConnectionScreen *screen)
{
    return screen == NULL ? "" : screen->host;
}

uint16_t connection_screen_port(const ConnectionScreen *screen)
{
    return screen == NULL ? 0 : connection_screen_read_port(screen->port);
}

void connection_screen_set_status(
    ConnectionScreen *screen,
    UiStatusKind kind,
    const char *text)
{
    if (screen == NULL || text == NULL)
    {
        return;
    }

    screen->status_kind = kind;
    snprintf(screen->status_text, sizeof(screen->status_text), "%s", text);
}
