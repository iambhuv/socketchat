#include "client/client_connection.h"

#include <sodium.h>

#include <string.h>

#define CLIENT_HELLO_REQUEST_ID 1u

static void client_connection_clear_handshake(ClientConnection *connection)
{
    sodium_memzero(
        connection->ephemeral_secret_key,
        sizeof(connection->ephemeral_secret_key));
    memset(&connection->hello, 0, sizeof(connection->hello));
    connection->hello_request_id = 0;
}

void client_connection_init(ClientConnection *connection)
{
    if (connection == NULL)
    {
        return;
    }

    memset(connection, 0, sizeof(*connection));
    connection->socket = net_socket_invalid();
}

ClientConnectionStatus client_connection_connect(
    ClientConnection *connection,
    const char *host,
    uint16_t port,
    const ClientIdentity *identity)
{
    uint8_t hello_payload[PROTOCOL_HELLO_SIZE];
    uint8_t hello_frame[PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_HELLO_SIZE];
    size_t payload_size;
    size_t frame_size;

    if (connection == NULL || host == NULL || host[0] == '\0' || port == 0 ||
        identity == NULL)
    {
        return CLIENT_CONNECTION_INVALID_ARGUMENT;
    }

    if (client_connection_is_open(connection))
    {
        return CLIENT_CONNECTION_ALREADY_OPEN;
    }

    if (net_connect(host, port, &connection->socket) != NET_STATUS_OK)
    {
        return CLIENT_CONNECTION_CONNECT_FAILED;
    }

    memcpy(
        connection->hello.identity_public_key,
        identity->public_key,
        sizeof(connection->hello.identity_public_key));
    randombytes_buf(
        connection->hello.client_nonce,
        sizeof(connection->hello.client_nonce));

    if (crypto_kx_keypair(
            connection->hello.client_ephemeral_public_key,
            connection->ephemeral_secret_key) != 0)
    {
        client_connection_close(connection);
        return CLIENT_CONNECTION_KEY_GENERATION_FAILED;
    }

    connection->hello_request_id = CLIENT_HELLO_REQUEST_ID;
    if (protocol_encode_hello(
            hello_payload,
            sizeof(hello_payload),
            &connection->hello,
            &payload_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_frame(
            hello_frame,
            sizeof(hello_frame),
            PROTOCOL_FRAME_HELLO,
            connection->hello_request_id,
            hello_payload,
            (uint32_t)payload_size,
            &frame_size) != PROTOCOL_STATUS_OK)
    {
        client_connection_close(connection);
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    if (net_send_all(connection->socket, hello_frame, frame_size) != NET_STATUS_OK)
    {
        client_connection_close(connection);
        return CLIENT_CONNECTION_SEND_FAILED;
    }

    return CLIENT_CONNECTION_OK;
}

bool client_connection_is_open(const ClientConnection *connection)
{
    return connection != NULL && net_socket_is_valid(connection->socket);
}

void client_connection_close(ClientConnection *connection)
{
    if (connection == NULL)
    {
        return;
    }

    net_close(&connection->socket);
    client_connection_clear_handshake(connection);
}

const char *client_connection_status_string(ClientConnectionStatus status)
{
    switch (status)
    {
        case CLIENT_CONNECTION_OK:
            return "connected";
        case CLIENT_CONNECTION_INVALID_ARGUMENT:
            return "invalid connection details";
        case CLIENT_CONNECTION_ALREADY_OPEN:
            return "connection is already open";
        case CLIENT_CONNECTION_CONNECT_FAILED:
            return "server could not be reached";
        case CLIENT_CONNECTION_KEY_GENERATION_FAILED:
            return "could not generate session keys";
        case CLIENT_CONNECTION_PROTOCOL_FAILED:
            return "could not build HELLO frame";
        case CLIENT_CONNECTION_SEND_FAILED:
            return "could not send HELLO frame";
        default:
            return "unknown connection status";
    }
}
