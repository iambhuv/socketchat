#ifndef SOCKET_CHAT_RELAY_SESSION_H
#define SOCKET_CHAT_RELAY_SESSION_H

#include "protocol/protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RELAY_SESSION_MAX_RESPONSE_SIZE (PROTOCOL_FRAME_HEADER_SIZE + 8u)

typedef struct RelaySession
{
    bool hello_received;
    uint32_t hello_request_id;
    ProtocolHello hello;
} RelaySession;

typedef enum RelaySessionResult
{
    RELAY_SESSION_ACCEPTED = 0,
    RELAY_SESSION_REPLY_READY,
    RELAY_SESSION_INVALID_PAYLOAD,
    RELAY_SESSION_UNEXPECTED_FRAME,
    RELAY_SESSION_OUTPUT_TOO_SMALL
} RelaySessionResult;

void relay_session_init(RelaySession *session);

RelaySessionResult relay_session_handle_frame(
    RelaySession *session,
    const ProtocolFrame *frame,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size);

const char *relay_session_result_string(RelaySessionResult result);

#endif
