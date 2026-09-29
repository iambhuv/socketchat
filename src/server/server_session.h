#ifndef SOCKET_CHAT_SERVER_SESSION_H
#define SOCKET_CHAT_SERVER_SESSION_H

#include "protocol/protocol.h"
#include "server/server_identity.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SERVER_SESSION_MAX_RESPONSE_SIZE \
    (PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_CHALLENGE_PAYLOAD_SIZE)

typedef struct ServerSession
{
    bool hello_received;
    uint32_t hello_request_id;
    ProtocolHello hello;
    ProtocolChallenge challenge;
    uint8_t server_ephemeral_secret_key[crypto_kx_SECRETKEYBYTES];
} ServerSession;

typedef enum ServerSessionResult
{
    SERVER_SESSION_ACCEPTED = 0,
    SERVER_SESSION_REPLY_READY,
    SERVER_SESSION_INVALID_PAYLOAD,
    SERVER_SESSION_UNEXPECTED_FRAME,
    SERVER_SESSION_OUTPUT_TOO_SMALL,
    SERVER_SESSION_INTERNAL_ERROR
} ServerSessionResult;

void server_session_init(ServerSession *session);

ServerSessionResult server_session_handle_frame(
    ServerSession *session,
    const ServerIdentity *identity,
    const ProtocolFrame *frame,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size);

const char *server_session_result_string(ServerSessionResult result);

#endif
