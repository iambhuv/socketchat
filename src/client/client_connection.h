#ifndef SOCKET_CHAT_CLIENT_CONNECTION_H
#define SOCKET_CHAT_CLIENT_CONNECTION_H

#include "client/client_identity.h"
#include "client/client_database.h"
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
    CLIENT_CONNECTION_SEND_FAILED,
    CLIENT_CONNECTION_RECEIVE_FAILED,
    CLIENT_CONNECTION_SERVER_AUTH_FAILED,
    CLIENT_CONNECTION_SERVER_UNTRUSTED,
    CLIENT_CONNECTION_CLIENT_AUTH_FAILED,
    CLIENT_CONNECTION_MESSAGE_REJECTED,
    CLIENT_CONNECTION_NO_MESSAGE
} ClientConnectionStatus;

typedef struct ClientConnection
{
    NetSocket socket;
    ProtocolHello hello;
    unsigned char ephemeral_secret_key[crypto_kx_SECRETKEYBYTES];
    unsigned char receive_key[crypto_kx_SESSIONKEYBYTES];
    unsigned char send_key[crypto_kx_SESSIONKEYBYTES];
    uint64_t receive_sequence;
    uint64_t send_sequence;
    uint32_t hello_request_id;
    bool authenticated;
} ClientConnection;

void client_connection_init(ClientConnection *connection);

/* net_init() must be called before connecting. */
ClientConnectionStatus client_connection_connect(
    ClientConnection *connection,
    const char *host,
    uint16_t port,
    const ClientIdentity *identity);

ClientConnectionStatus client_connection_finish_handshake(
    ClientConnection *connection,
    ClientDatabase *database,
    const char *host,
    uint16_t port,
    const ClientIdentity *identity);

/* Sends a signed UTF-8 message to one authenticated contact. The server only
 * accepts the send after the authenticated transport handshake has completed. */
ClientConnectionStatus client_connection_send_private_text(
    ClientConnection *connection,
    const ClientIdentity *identity,
    const uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE],
    const char *text,
    uint64_t *delivery_id);

/* Reads one delivered private message when the socket is ready. The function
 * never waits for a new message; CLIENT_CONNECTION_NO_MESSAGE means there was
 * nothing to read in this frame. */
ClientConnectionStatus client_connection_poll_private_message(
    ClientConnection *connection,
    const ClientIdentity *identity,
    uint8_t sender_id[PROTOCOL_IDENTITY_ID_SIZE],
    char *text,
    size_t text_capacity,
    uint64_t *sender_timestamp_unix_ms);

bool client_connection_is_open(const ClientConnection *connection);
void client_connection_close(ClientConnection *connection);
const char *client_connection_status_string(ClientConnectionStatus status);

#endif
