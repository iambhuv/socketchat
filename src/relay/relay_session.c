#include "relay/relay_session.h"

#include <string.h>

void relay_session_init(RelaySession *session)
{
    if (session != NULL)
    {
        memset(session, 0, sizeof(*session));
    }
}

RelaySessionResult relay_session_handle_frame(
    RelaySession *session,
    const ProtocolFrame *frame,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size)
{
    uint64_t token;

    if (session == NULL || frame == NULL || response == NULL ||
        response_size == NULL)
    {
        return RELAY_SESSION_INVALID_PAYLOAD;
    }

    *response_size = 0;

    if (frame->type == PROTOCOL_FRAME_HELLO)
    {
        if (session->hello_received)
        {
            return RELAY_SESSION_UNEXPECTED_FRAME;
        }

        if (protocol_decode_hello(
                frame->payload,
                frame->payload_length,
                &session->hello) != PROTOCOL_STATUS_OK)
        {
            return RELAY_SESSION_INVALID_PAYLOAD;
        }

        session->hello_received = true;
        session->hello_request_id = frame->request_id;
        return RELAY_SESSION_ACCEPTED;
    }

    if (!session->hello_received)
    {
        return RELAY_SESSION_UNEXPECTED_FRAME;
    }

    if (frame->type != PROTOCOL_FRAME_PING &&
        frame->type != PROTOCOL_FRAME_PONG)
    {
        return RELAY_SESSION_UNEXPECTED_FRAME;
    }

    if (protocol_decode_ping(
            frame->payload,
            frame->payload_length,
            &token) != PROTOCOL_STATUS_OK)
    {
        return RELAY_SESSION_INVALID_PAYLOAD;
    }

    if (frame->type == PROTOCOL_FRAME_PONG)
    {
        return RELAY_SESSION_ACCEPTED;
    }

    {
        uint8_t pong_payload[8];
        size_t pong_payload_size;
        ProtocolStatus encode_status;

        if (protocol_encode_ping(
                pong_payload,
                sizeof(pong_payload),
                token,
                &pong_payload_size) != PROTOCOL_STATUS_OK)
        {
            return RELAY_SESSION_INVALID_PAYLOAD;
        }

        encode_status = protocol_encode_frame(
            response,
            response_capacity,
            PROTOCOL_FRAME_PONG,
            frame->request_id,
            pong_payload,
            (uint32_t)pong_payload_size,
            response_size);

        if (encode_status == PROTOCOL_STATUS_BUFFER_TOO_SMALL)
        {
            return RELAY_SESSION_OUTPUT_TOO_SMALL;
        }

        if (encode_status != PROTOCOL_STATUS_OK)
        {
            return RELAY_SESSION_INVALID_PAYLOAD;
        }
    }

    return RELAY_SESSION_REPLY_READY;
}

const char *relay_session_result_string(RelaySessionResult result)
{
    switch (result)
    {
        case RELAY_SESSION_ACCEPTED:
            return "accepted";
        case RELAY_SESSION_REPLY_READY:
            return "reply ready";
        case RELAY_SESSION_INVALID_PAYLOAD:
            return "invalid frame payload";
        case RELAY_SESSION_UNEXPECTED_FRAME:
            return "unexpected frame";
        case RELAY_SESSION_OUTPUT_TOO_SMALL:
            return "response buffer too small";
        default:
            return "unknown relay session result";
    }
}
