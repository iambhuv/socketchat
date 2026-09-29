#ifndef SOCKET_CHAT_CHAT_SCREEN_H
#define SOCKET_CHAT_CHAT_SCREEN_H

/* Draws the empty chat shell while contacts and messages are being built. */
#include "client/client_connection.h"

void chat_screen_draw(ClientConnection *connection);

#endif
