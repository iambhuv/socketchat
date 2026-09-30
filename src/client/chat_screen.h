#ifndef SOCKET_CHAT_CHAT_SCREEN_H
#define SOCKET_CHAT_CHAT_SCREEN_H

#include "client/client_connection.h"

void chat_screen_draw(
    ClientConnection *connection,
    ClientDatabase *database,
    const ClientIdentity *identity);

#endif
