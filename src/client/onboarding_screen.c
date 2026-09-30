#include "client/onboarding_screen.h"

#include "ui/ui_animation.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void onboarding_draw_background(const UiTheme *theme)
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

static void onboarding_draw_chat_icon(Vector2 center, const UiTheme *theme)
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

static void onboarding_draw_heading(Rectangle card, const UiTheme *theme)
{
    const Font title_font = ui_font_get_semibold();
    const Font body_font = ui_font_get();
    const char *title = "Choose your username";
    const char *subtitle = "Friends will use it to find you";
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

void onboarding_screen_init(OnboardingScreen *screen)
{
    if (screen == NULL)
    {
        return;
    }

    memset(screen, 0, sizeof(*screen));
    screen->username_input.focused = true;
    onboarding_screen_set_status(screen, UI_STATUS_OFFLINE, "Offline");
}

bool onboarding_screen_draw(OnboardingScreen *screen)
{
    const UiTheme *theme = ui_theme_default();
    const float screen_width = (float)GetScreenWidth();
    const float screen_height = (float)GetScreenHeight();
    const float card_width = ui_clamp(screen_width - 56.0f, 420.0f, 520.0f);
    const float card_height = 430.0f;
    Rectangle card;
    Rectangle username_input;
    Rectangle continue_button;
    bool continue_requested;
    bool can_submit;

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
    username_input = (Rectangle){
        card.x + 42.0f,
        card.y + 225.0f,
        card.width - 84.0f,
        theme->control_height
    };
    continue_button = (Rectangle){
        card.x + 42.0f,
        card.y + 321.0f,
        card.width - 84.0f,
        theme->control_height
    };

    onboarding_draw_background(theme);
    ui_draw_panel(card, theme);
    onboarding_draw_chat_icon(
        (Vector2){ card.x + card.width * 0.5f, card.y + 67.0f },
        theme);
    onboarding_draw_heading(card, theme);

    if (ui_text_input(
            username_input,
            "Username",
            "bhuvnesh_01",
            screen->username,
            sizeof(screen->username),
            UI_TEXT_INPUT_NORMAL,
            screen->username_has_error,
            &screen->username_input,
            theme))
    {
        screen->username_has_error = false;

        if (screen->status_kind == UI_STATUS_ERROR)
        {
            onboarding_screen_set_status(screen, UI_STATUS_OFFLINE, "Offline");
        }
    }

    can_submit = screen->status_kind != UI_STATUS_CONNECTING &&
                 screen->status_kind != UI_STATUS_CONNECTED;
    continue_requested = ui_button(
        continue_button,
        screen->status_kind == UI_STATUS_CONNECTED
            ? "Server reached"
            : "Create my account",
        can_submit,
        &screen->continue_button,
        theme);

    if (can_submit && IsKeyPressed(KEY_ENTER))
    {
        continue_requested = true;
    }

    if (continue_requested)
    {
        screen->username_has_error =
            !protocol_username_is_valid(
                screen->username,
                strlen(screen->username));

        onboarding_screen_set_status(
            screen,
            screen->username_has_error ? UI_STATUS_ERROR : UI_STATUS_CONNECTING,
            screen->username_has_error ? "Check username" : "Connecting");
    }

    /* Draw this last so a button click updates the badge in the same frame. */
    ui_status_badge(
        (Rectangle){ card.x + card.width - 156.0f, card.y + 22.0f, 132.0f, 32.0f },
        screen->status_text,
        screen->status_kind,
        theme);

    return continue_requested && !screen->username_has_error;
}

const char *onboarding_screen_username(const OnboardingScreen *screen)
{
    return screen == NULL ? "" : screen->username;
}

void onboarding_screen_set_username(OnboardingScreen *screen, const char *username)
{
    if (screen == NULL || username == NULL)
    {
        return;
    }
    snprintf(screen->username, sizeof(screen->username), "%s", username);
}

void onboarding_screen_set_status(
    OnboardingScreen *screen,
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
