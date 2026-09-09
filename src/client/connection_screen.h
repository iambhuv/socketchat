#ifndef SOCKET_CHAT_CONNECTION_SCREEN_H
#define SOCKET_CHAT_CONNECTION_SCREEN_H

#include "ui/ui_widgets.h"

#include <stdbool.h>
#include <stdint.h>

#define CONNECTION_HOST_CAPACITY 256u
#define CONNECTION_PORT_CAPACITY 6u
#define CONNECTION_STATUS_CAPACITY 64u

typedef struct ConnectionScreen
{
    char host[CONNECTION_HOST_CAPACITY];
    char port[CONNECTION_PORT_CAPACITY];
    char status_text[CONNECTION_STATUS_CAPACITY];

    UiTextInputState host_input;
    UiTextInputState port_input;
    UiButtonState connect_button;
    UiStatusKind status_kind;

    bool host_has_error;
    bool port_has_error;
    float entrance_amount;
} ConnectionScreen;

void connection_screen_init(ConnectionScreen *screen);

/* Returns true when valid connection details are submitted. */
bool connection_screen_draw(ConnectionScreen *screen);

const char *connection_screen_host(const ConnectionScreen *screen);
uint16_t connection_screen_port(const ConnectionScreen *screen);

void connection_screen_set_status(
    ConnectionScreen *screen,
    UiStatusKind kind,
    const char *text);

#endif
