#ifndef SOCKET_CHAT_UI_WIDGETS_H
#define SOCKET_CHAT_UI_WIDGETS_H

#include "ui/ui_theme.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct UiButtonState
{
    float hover_amount;
    float press_amount;
} UiButtonState;

typedef enum UiTextInputMode
{
    UI_TEXT_INPUT_NORMAL = 0,
    UI_TEXT_INPUT_DIGITS,
    UI_TEXT_INPUT_USERNAME
} UiTextInputMode;

typedef struct UiTextInputState
{
    bool initialized;
    bool focused;
    size_t cursor;
    float hover_amount;
    float focus_amount;
    float scroll_x;
    float caret_time;
} UiTextInputState;

typedef enum UiStatusKind
{
    UI_STATUS_OFFLINE = 0,
    UI_STATUS_CONNECTING,
    UI_STATUS_CONNECTED,
    UI_STATUS_ERROR
} UiStatusKind;

void ui_draw_panel(Rectangle bounds, const UiTheme *theme);

bool ui_button(
    Rectangle bounds,
    const char *label,
    bool enabled,
    UiButtonState *state,
    const UiTheme *theme);

bool ui_text_input(
    Rectangle bounds,
    const char *label,
    const char *placeholder,
    char *text,
    size_t text_capacity,
    UiTextInputMode mode,
    bool has_error,
    UiTextInputState *state,
    const UiTheme *theme);

void ui_status_badge(
    Rectangle bounds,
    const char *label,
    UiStatusKind kind,
    const UiTheme *theme);

#endif
