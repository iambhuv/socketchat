#ifndef SOCKET_CHAT_CLIENT_CONNECTION_H
#define SOCKET_CHAT_CLIENT_CONNECTION_H

#include "client/client_identity.h"
#include "client/client_database.h"
#include "net/net.h"
#include "protocol/protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLIENT_CONNECTION_RECEIVE_BUFFER_CAPACITY \
    (PROTOCOL_TRANSPORT_HEADER_SIZE + PROTOCOL_MAX_TRANSPORT_CIPHERTEXT)
#define CLIENT_CONNECTION_MAX_PENDING_RESPONSES 16u
#define CLIENT_CONNECTION_MAX_PENDING_ACKNOWLEDGEMENTS 64u
#define CLIENT_CONNECTION_MAX_RESPONSE_PAYLOAD \
    (4u + PROTOCOL_MAX_ERROR_MESSAGE_SIZE)

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
    CLIENT_CONNECTION_STORAGE_FAILED,
    CLIENT_CONNECTION_NO_MESSAGE
} ClientConnectionStatus;

typedef struct ClientConnectionPendingResponse
{
    bool in_use;
    ProtocolFrameType type;
    uint32_t request_id;
    uint32_t payload_length;
    uint8_t payload[CLIENT_CONNECTION_MAX_RESPONSE_PAYLOAD];
} ClientConnectionPendingResponse;

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
    uint32_t next_request_id;
    uint8_t *receive_buffer;
    size_t receive_buffer_size;
    size_t receive_buffer_capacity;
    ClientConnectionPendingResponse
        pending_responses[CLIENT_CONNECTION_MAX_PENDING_RESPONSES];
    uint32_t pending_acknowledgement_request_ids[
        CLIENT_CONNECTION_MAX_PENDING_ACKNOWLEDGEMENTS];
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

/* Queues a signed UTF-8 message for the server.  Delivery replies are later
 * collected by client_connection_take_send_result() after the caller pumps.
 * This deliberately never assumes the next inbound frame is its response. */
ClientConnectionStatus client_connection_send_private_text(
    ClientConnection *connection,
    const ClientIdentity *identity,
    const uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE],
    const char *text,
    uint32_t *request_id,
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE],
    uint64_t *sender_timestamp_unix_ms);

/* Reads only currently-available socket bytes, assembles complete encrypted
 * records, persists every verified delivery, then sends its acknowledgement.
 * It never blocks waiting for a partial TCP record. */
ClientConnectionStatus client_connection_pump(
    ClientConnection *connection,
    ClientDatabase *database,
    const ClientIdentity *identity);

/* Retrieves the stored response that matches one earlier send request. */
ClientConnectionStatus client_connection_take_send_result(
    ClientConnection *connection,
    uint32_t request_id,
    const uint8_t expected_packet_id[PROTOCOL_PACKET_ID_SIZE],
    uint64_t *delivery_id);

bool client_connection_is_open(const ClientConnection *connection);
void client_connection_close(ClientConnection *connection);
const char *client_connection_status_string(ClientConnectionStatus status);

#endif
