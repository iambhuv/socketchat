#ifndef SOCKET_CHAT_CLIENT_CONNECTION_H
#define SOCKET_CHAT_CLIENT_CONNECTION_H

#include "client/client_identity.h"
#include "net/net.h"
#include "protocol/protocol.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum ClientConnectionStatus
{
    CLIENT_CONNECTION_OK = 0,
    CLIENT_CONNECTION_INVALID_ARGUMENT,
    CLIENT_CONNECTION_ALREADY_OPEN,
    CLIENT_CONNECTION_CONNECT_FAILED,
    CLIENT_CONNECTION_KEY_GENERATION_FAILED,
    CLIENT_CONNECTION_PROTOCOL_FAILED,
    CLIENT_CONNECTION_SEND_FAILED
} ClientConnectionStatus;

typedef struct ClientConnection
{
    NetSocket socket;
    ProtocolHello hello;
    unsigned char ephemeral_secret_key[crypto_kx_SECRETKEYBYTES];
    uint32_t hello_request_id;
} ClientConnection;

void client_connection_init(ClientConnection *connection);

/* net_init() must be called before connecting. */
ClientConnectionStatus client_connection_connect(
    ClientConnection *connection,
    const char *host,
    uint16_t port,
    const ClientIdentity *identity);

bool client_connection_is_open(const ClientConnection *connection);
void client_connection_close(ClientConnection *connection);
const char *client_connection_status_string(ClientConnectionStatus status);

#endif
