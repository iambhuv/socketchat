#include "ui/ui_widgets.h"

#include "ui/ui_animation.h"
#include "ui/ui_font.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define UI_CONTROL_PADDING 18.0f

static float ui_roundness(Rectangle bounds, float radius)
{
    const float smaller_side = bounds.width < bounds.height
                                   ? bounds.width
                                   : bounds.height;

    if (smaller_side <= 0.0f)
    {
        return 0.0f;
    }

    return ui_clamp((radius * 2.0f) / smaller_side, 0.0f, 1.0f);
}

static Color ui_color_lerp(Color start, Color end, float amount)
{
    Color result;

    result.r = (unsigned char)ui_lerp(start.r, end.r, amount);
    result.g = (unsigned char)ui_lerp(start.g, end.g, amount);
    result.b = (unsigned char)ui_lerp(start.b, end.b, amount);
    result.a = (unsigned char)ui_lerp(start.a, end.a, amount);
    return result;
}

static void ui_draw_centered_text(
    const char *text,
    Rectangle bounds,
    float font_size,
    Color color)
{
    const Font font = ui_font_get_semibold();
    const Vector2 size = MeasureTextEx(font, text, font_size, 1.0f);
    const Vector2 position = {
        bounds.x + (bounds.width - size.x) * 0.5f,
        bounds.y + (bounds.height - size.y) * 0.5f
    };

    DrawTextEx(font, text, position, font_size, 1.0f, color);
}

static Rectangle ui_scale_rectangle(Rectangle bounds, float scale)
{
    const float new_width = bounds.width * scale;
    const float new_height = bounds.height * scale;
    Rectangle result = {
        bounds.x + (bounds.width - new_width) * 0.5f,
        bounds.y + (bounds.height - new_height) * 0.5f,
        new_width,
        new_height
    };

    return result;
}

static size_t ui_previous_codepoint(const char *text, size_t cursor)
{
    if (cursor == 0)
    {
        return 0;
    }

    --cursor;

    while (cursor > 0 && (((unsigned char)text[cursor] & 0xc0u) == 0x80u))
    {
        --cursor;
    }

    return cursor;
}

static size_t ui_next_codepoint(const char *text, size_t cursor, size_t length)
{
    if (cursor >= length)
    {
        return length;
    }

    ++cursor;

    while (cursor < length &&
           (((unsigned char)text[cursor] & 0xc0u) == 0x80u))
    {
        ++cursor;
    }

    return cursor;
}

static bool ui_codepoint_is_allowed(int codepoint, UiTextInputMode mode)
{
    if (mode == UI_TEXT_INPUT_DIGITS)
    {
        return codepoint >= '0' && codepoint <= '9';
    }

    if (mode == UI_TEXT_INPUT_USERNAME)
    {
        return (codepoint >= 'a' && codepoint <= 'z') ||
               (codepoint >= '0' && codepoint <= '9') || codepoint == '_';
    }

    return codepoint >= 32 && codepoint != 127;
}

static bool ui_insert_codepoint(
    char *text,
    size_t capacity,
    size_t *cursor,
    int codepoint,
    UiTextInputMode mode)
{
    const char *encoded;
    int encoded_size;
    size_t length;

    if (!ui_codepoint_is_allowed(codepoint, mode))
    {
        return false;
    }

    encoded = CodepointToUTF8(codepoint, &encoded_size);
    length = strlen(text);

    if (encoded_size <= 0 || length + (size_t)encoded_size >= capacity)
    {
        return false;
    }

    memmove(
        text + *cursor + (size_t)encoded_size,
        text + *cursor,
        length - *cursor + 1);
    memcpy(text + *cursor, encoded, (size_t)encoded_size);
    *cursor += (size_t)encoded_size;
    return true;
}

static bool ui_insert_clipboard_text(
    char *text,
    size_t capacity,
    size_t *cursor,
    UiTextInputMode mode)
{
    const char *clipboard = GetClipboardText();
    bool changed = false;

    if (clipboard == NULL)
    {
        return false;
    }

    while (*clipboard != '\0')
    {
        int codepoint_size;
        const int codepoint = GetCodepointNext(clipboard, &codepoint_size);

        if (codepoint_size <= 0)
        {
            break;
        }

        if (ui_insert_codepoint(
                text,
                capacity,
                cursor,
                codepoint,
                mode))
        {
            changed = true;
        }

        clipboard += codepoint_size;
    }

    return changed;
}

static bool ui_update_text(
    char *text,
    size_t capacity,
    UiTextInputMode mode,
    UiTextInputState *state)
{
    bool changed = false;
    size_t length = strlen(text);
    const bool shortcut_key =
        IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
        IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

    if (IsKeyPressed(KEY_HOME))
    {
        state->cursor = 0;
        state->caret_time = 0.0f;
    }
    else if (IsKeyPressed(KEY_END))
    {
        state->cursor = length;
        state->caret_time = 0.0f;
    }
    else if (IsKeyPressed(KEY_LEFT))
    {
        state->cursor = ui_previous_codepoint(text, state->cursor);
        state->caret_time = 0.0f;
    }
    else if (IsKeyPressed(KEY_RIGHT))
    {
        state->cursor = ui_next_codepoint(text, state->cursor, length);
        state->caret_time = 0.0f;
    }

    if (IsKeyPressed(KEY_BACKSPACE) && state->cursor > 0)
    {
        const size_t previous = ui_previous_codepoint(text, state->cursor);
        memmove(
            text + previous,
            text + state->cursor,
            length - state->cursor + 1);
        state->cursor = previous;
        state->caret_time = 0.0f;
        changed = true;
        length = strlen(text);
    }

    if (IsKeyPressed(KEY_DELETE) && state->cursor < length)
    {
        const size_t next = ui_next_codepoint(text, state->cursor, length);
        memmove(
            text + state->cursor,
            text + next,
            length - next + 1);
        state->caret_time = 0.0f;
        changed = true;
    }

    if (shortcut_key && IsKeyPressed(KEY_V))
    {
        if (ui_insert_clipboard_text(
                text,
                capacity,
                &state->cursor,
                mode))
        {
            state->caret_time = 0.0f;
            changed = true;
        }
    }
    else
    {
        int codepoint = GetCharPressed();

        while (codepoint > 0)
        {
            if (ui_insert_codepoint(
                    text,
                    capacity,
                    &state->cursor,
                    codepoint,
                    mode))
            {
                state->caret_time = 0.0f;
                changed = true;
            }

            codepoint = GetCharPressed();
        }
    }

    return changed;
}

static float ui_text_width_before_cursor(
    char *text,
    size_t cursor,
    float font_size)
{
    const char saved = text[cursor];
    float width;

    text[cursor] = '\0';
    width = MeasureTextEx(ui_font_get(), text, font_size, 1.0f).x;
    text[cursor] = saved;
    return width;
}

void ui_draw_panel(Rectangle bounds, const UiTheme *theme)
{
    const float roundness = ui_roundness(bounds, theme->panel_radius);
    Rectangle shadow = bounds;

    shadow.y += 10.0f;
    DrawRectangleRounded(shadow, roundness, 16, theme->shadow);

    shadow.y -= 5.0f;
    shadow.x += 4.0f;
    shadow.width -= 8.0f;
    DrawRectangleRounded(shadow, roundness, 16, Fade(theme->shadow, 0.45f));

    DrawRectangleRounded(bounds, roundness, 16, theme->surface);
    DrawRectangleRoundedLinesEx(
        bounds,
        roundness,
        16,
        1.0f,
        Fade(theme->border, 0.65f));
}

bool ui_button(
    Rectangle bounds,
    const char *label,
    bool enabled,
    UiButtonState *state,
    const UiTheme *theme)
{
    const float delta_time = GetFrameTime();
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    const bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    Rectangle animated_bounds;
    Rectangle shadow;
    Color fill;

    state->hover_amount = ui_smooth(
        state->hover_amount,
        hovered ? 1.0f : 0.0f,
        theme->animation_speed,
        delta_time);
    state->press_amount = ui_smooth(
        state->press_amount,
        pressed ? 1.0f : 0.0f,
        theme->animation_speed * 1.5f,
        delta_time);

    if (hovered)
    {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }

    animated_bounds = ui_scale_rectangle(
        bounds,
        1.0f - state->press_amount * 0.025f);
    shadow = animated_bounds;
    shadow.y += ui_lerp(5.0f, 2.0f, state->press_amount);
    fill = ui_color_lerp(
        theme->primary,
        theme->primary_hovered,
        state->hover_amount);

    if (!enabled)
    {
        fill = ui_color_lerp(fill, theme->border, 0.65f);
    }

    DrawRectangleRounded(
        shadow,
        ui_roundness(shadow, theme->control_radius),
        14,
        Fade(theme->primary, enabled ? 0.20f : 0.08f));
    DrawRectangleRounded(
        animated_bounds,
        ui_roundness(animated_bounds, theme->control_radius),
        14,
        fill);
    ui_draw_centered_text(
        label,
        animated_bounds,
        theme->body_text_size,
        enabled ? WHITE : theme->text_muted);

    return clicked;
}

bool ui_text_input(
    Rectangle bounds,
    const char *label,
    const char *placeholder,
    char *text,
    size_t text_capacity,
    UiTextInputMode mode,
    bool has_error,
    UiTextInputState *state,
    const UiTheme *theme)
{
    const float delta_time = GetFrameTime();
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    bool changed = false;
    Color border_color;
    Color fill_color;
    const Font font = ui_font_get();
    const float available_width = bounds.width - UI_CONTROL_PADDING * 2.0f;
    float text_width;
    float cursor_width;
    float maximum_scroll;
    Vector2 text_position;

    if (!state->initialized)
    {
        state->cursor = strlen(text);
        state->initialized = true;
    }

    if (state->cursor > strlen(text))
    {
        state->cursor = strlen(text);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        state->focused = hovered;

        if (hovered)
        {
            state->cursor = strlen(text);
            state->caret_time = 0.0f;
        }
    }

    if (hovered)
    {
        SetMouseCursor(MOUSE_CURSOR_IBEAM);
    }

    if (state->focused)
    {
        changed = ui_update_text(text, text_capacity, mode, state);
    }

    state->hover_amount = ui_smooth(
        state->hover_amount,
        hovered ? 1.0f : 0.0f,
        theme->animation_speed,
        delta_time);
    state->focus_amount = ui_smooth(
        state->focus_amount,
        state->focused ? 1.0f : 0.0f,
        theme->animation_speed,
        delta_time);
    state->caret_time += delta_time;

    border_color = ui_color_lerp(
        theme->border,
        theme->primary,
        state->focus_amount);

    if (has_error)
    {
        border_color = theme->error;
    }

    fill_color = ui_color_lerp(
        theme->input,
        theme->surface_hovered,
        state->hover_amount * 0.7f);

    DrawTextEx(
        ui_font_get_semibold(),
        label,
        (Vector2){ bounds.x + 2.0f, bounds.y - 25.0f },
        theme->small_text_size,
        1.0f,
        has_error ? theme->error : theme->text_muted);
    DrawRectangleRounded(
        bounds,
        ui_roundness(bounds, theme->control_radius),
        14,
        fill_color);
    DrawRectangleRoundedLinesEx(
        bounds,
        ui_roundness(bounds, theme->control_radius),
        14,
        ui_lerp(1.0f, 2.0f, state->focus_amount),
        border_color);

    text_width = MeasureTextEx(font, text, theme->body_text_size, 1.0f).x;
    cursor_width = ui_text_width_before_cursor(
        text,
        state->cursor,
        theme->body_text_size);
    maximum_scroll = text_width > available_width
                         ? text_width - available_width
                         : 0.0f;

    if (cursor_width - state->scroll_x > available_width)
    {
        state->scroll_x = cursor_width - available_width;
    }
    else if (cursor_width < state->scroll_x)
    {
        state->scroll_x = cursor_width;
    }

    state->scroll_x = ui_clamp(state->scroll_x, 0.0f, maximum_scroll);
    text_position = (Vector2){
        bounds.x + UI_CONTROL_PADDING - state->scroll_x,
        bounds.y + (bounds.height - theme->body_text_size) * 0.5f - 1.0f
    };

    BeginScissorMode(
        (int)(bounds.x + UI_CONTROL_PADDING),
        (int)bounds.y,
        (int)available_width,
        (int)bounds.height);

    if (text[0] == '\0' && !state->focused)
    {
        DrawTextEx(
            font,
            placeholder,
            text_position,
            theme->body_text_size,
            1.0f,
            Fade(theme->text_muted, 0.70f));
    }
    else
    {
        DrawTextEx(
            font,
            text,
            text_position,
            theme->body_text_size,
            1.0f,
            theme->text);
    }

    if (state->focused && fmodf(state->caret_time, 1.0f) < 0.55f)
    {
        const float caret_x =
            bounds.x + UI_CONTROL_PADDING + cursor_width - state->scroll_x;
        DrawRectangle(
            (int)caret_x,
            (int)(bounds.y + 15.0f),
            2,
            (int)(bounds.height - 30.0f),
            theme->primary);
    }

    EndScissorMode();
    return changed;
}

void ui_status_badge(
    Rectangle bounds,
    const char *label,
    UiStatusKind kind,
    const UiTheme *theme)
{
    Color status_color = theme->offline;
    float pulse = 0.0f;
    const float dot_radius = 4.0f;
    const Vector2 dot = {
        bounds.x + 16.0f,
        bounds.y + bounds.height * 0.5f
    };

    if (kind == UI_STATUS_CONNECTING)
    {
        status_color = theme->connecting;
        pulse = (sinf((float)GetTime() * 5.0f) + 1.0f) * 0.5f;
    }
    else if (kind == UI_STATUS_CONNECTED)
    {
        status_color = theme->connected;
    }
    else if (kind == UI_STATUS_ERROR)
    {
        status_color = theme->error;
    }

    DrawRectangleRounded(
        bounds,
        ui_roundness(bounds, bounds.height * 0.5f),
        12,
        Fade(status_color, 0.11f));

    if (pulse > 0.0f)
    {
        DrawCircleV(dot, dot_radius + pulse * 4.0f, Fade(status_color, 0.15f));
    }

    DrawCircleV(dot, dot_radius, status_color);
    DrawTextEx(
        ui_font_get_semibold(),
        label,
        (Vector2){ bounds.x + 27.0f, bounds.y + 8.0f },
        theme->small_text_size,
        1.0f,
        theme->text_muted);
}
