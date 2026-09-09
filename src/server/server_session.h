#ifndef SOCKET_CHAT_SERVER_SESSION_H
#define SOCKET_CHAT_SERVER_SESSION_H

#include "protocol/protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SERVER_SESSION_MAX_RESPONSE_SIZE (PROTOCOL_FRAME_HEADER_SIZE + 8u)

typedef struct ServerSession
{
    bool hello_received;
    uint32_t hello_request_id;
    ProtocolHello hello;
} ServerSession;

typedef enum ServerSessionResult
{
    SERVER_SESSION_ACCEPTED = 0,
    SERVER_SESSION_REPLY_READY,
    SERVER_SESSION_INVALID_PAYLOAD,
    SERVER_SESSION_UNEXPECTED_FRAME,
    SERVER_SESSION_OUTPUT_TOO_SMALL
} ServerSessionResult;

void server_session_init(ServerSession *session);

ServerSessionResult server_session_handle_frame(
    ServerSession *session,
    const ProtocolFrame *frame,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size);

const char *server_session_result_string(ServerSessionResult result);

#endif
