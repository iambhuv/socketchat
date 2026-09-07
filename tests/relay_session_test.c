#include "relay/relay_session.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static ProtocolFrame make_frame(
    ProtocolFrameType type,
    uint32_t request_id,
    const uint8_t *payload,
    uint32_t payload_length)
{
    ProtocolFrame frame;
    frame.type = type;
    frame.flags = 0;
    frame.request_id = request_id;
    frame.payload = payload;
    frame.payload_length = payload_length;
    return frame;
}

int main(void)
{
    RelaySession session;
    ProtocolHello hello = { 0 };
    ProtocolFrame frame;
    ProtocolFrame decoded_response;
    uint8_t hello_payload[PROTOCOL_HELLO_SIZE];
    uint8_t ping_payload[8];
    uint8_t response[RELAY_SESSION_MAX_RESPONSE_SIZE];
    size_t hello_size;
    size_t ping_size;
    size_t response_size;
    size_t consumed;
    uint64_t decoded_token;

    memset(hello.identity_public_key, 0x11, sizeof(hello.identity_public_key));
    memset(hello.client_nonce, 0x22, sizeof(hello.client_nonce));
    memset(
        hello.client_ephemeral_public_key,
        0x33,
        sizeof(hello.client_ephemeral_public_key));

    CHECK(protocol_encode_hello(
              hello_payload,
              sizeof(hello_payload),
              &hello,
              &hello_size) == PROTOCOL_STATUS_OK);
    CHECK(protocol_encode_ping(
              ping_payload,
              sizeof(ping_payload),
              UINT64_C(0x0102030405060708),
              &ping_size) == PROTOCOL_STATUS_OK);

    relay_session_init(&session);

    frame = make_frame(
        PROTOCOL_FRAME_PING,
        1,
        ping_payload,
        (uint32_t)ping_size);
    CHECK(relay_session_handle_frame(
              &session,
              &frame,
              response,
              sizeof(response),
              &response_size) == RELAY_SESSION_UNEXPECTED_FRAME);

    frame = make_frame(
        PROTOCOL_FRAME_HELLO,
        10,
        hello_payload,
        (uint32_t)hello_size);
    CHECK(relay_session_handle_frame(
              &session,
              &frame,
              response,
              sizeof(response),
              &response_size) == RELAY_SESSION_ACCEPTED);
    CHECK(session.hello_received);
    CHECK(session.hello_request_id == 10);
    CHECK(memcmp(
              session.hello.identity_public_key,
              hello.identity_public_key,
              sizeof(hello.identity_public_key)) == 0);

    CHECK(relay_session_handle_frame(
              &session,
              &frame,
              response,
              sizeof(response),
              &response_size) == RELAY_SESSION_UNEXPECTED_FRAME);

    frame = make_frame(
        PROTOCOL_FRAME_PING,
        20,
        ping_payload,
        (uint32_t)ping_size);
    CHECK(relay_session_handle_frame(
              &session,
              &frame,
              response,
              sizeof(response),
              &response_size) == RELAY_SESSION_REPLY_READY);
    CHECK(protocol_decode_frame(
              response,
              response_size,
              &decoded_response,
              &consumed) == PROTOCOL_STATUS_OK);
    CHECK(decoded_response.type == PROTOCOL_FRAME_PONG);
    CHECK(decoded_response.request_id == 20);
    CHECK(protocol_decode_ping(
              decoded_response.payload,
              decoded_response.payload_length,
              &decoded_token) == PROTOCOL_STATUS_OK);
    CHECK(decoded_token == UINT64_C(0x0102030405060708));

    frame = make_frame(
        PROTOCOL_FRAME_PONG,
        21,
        ping_payload,
        (uint32_t)ping_size);
    CHECK(relay_session_handle_frame(
              &session,
              &frame,
              response,
              sizeof(response),
              &response_size) == RELAY_SESSION_ACCEPTED);
    CHECK(response_size == 0);

    frame = make_frame(PROTOCOL_FRAME_PONG, 21, ping_payload, 7);
    CHECK(relay_session_handle_frame(
              &session,
              &frame,
              response,
              sizeof(response),
              &response_size) == RELAY_SESSION_INVALID_PAYLOAD);

    frame = make_frame(PROTOCOL_FRAME_SEND, 22, NULL, 0);
    CHECK(relay_session_handle_frame(
              &session,
              &frame,
              response,
              sizeof(response),
              &response_size) == RELAY_SESSION_UNEXPECTED_FRAME);

    return 0;
}
