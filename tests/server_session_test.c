#include "server/server_identity.h"
#include "server/server_session.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; } } while (0)

static ProtocolFrame make_frame(
    ProtocolFrameType type, uint32_t request_id,
    const uint8_t *payload, uint32_t payload_length)
{
    ProtocolFrame frame = { type, 0, request_id, payload, payload_length };
    return frame;
}

int main(void)
{
    const char *database_path = "server_identity_test.db";
    ServerIdentity identity;
    ServerIdentity reloaded_identity;
    ServerSession session;
    ProtocolHello hello = { 0 };
    ProtocolChallenge challenge;
    ProtocolFrame frame;
    ProtocolFrame decoded_response;
    uint8_t hello_payload[PROTOCOL_HELLO_SIZE];
    uint8_t response[SERVER_SESSION_MAX_RESPONSE_SIZE];
    uint8_t signature_data[256];
    size_t hello_size;
    size_t response_size;
    size_t consumed;
    size_t signature_data_size;

    CHECK(sodium_init() >= 0);
    remove(database_path);
    CHECK(server_identity_load_or_create(database_path, &identity));
    CHECK(server_identity_load_or_create(database_path, &reloaded_identity));
    CHECK(memcmp(&identity, &reloaded_identity, sizeof(identity)) == 0);
    server_identity_clear(&reloaded_identity);
    remove(database_path);

    memset(hello.identity_public_key, 0x11, sizeof(hello.identity_public_key));
    memset(hello.client_nonce, 0x22, sizeof(hello.client_nonce));
    memset(hello.client_ephemeral_public_key, 0x33,
           sizeof(hello.client_ephemeral_public_key));
    CHECK(protocol_encode_hello(
              hello_payload, sizeof(hello_payload),
              &hello, &hello_size) == PROTOCOL_STATUS_OK);

    server_session_init(&session);
    frame = make_frame(PROTOCOL_FRAME_PING, 1, NULL, 0);
    CHECK(server_session_handle_frame(
              &session, &identity, &frame, response, sizeof(response),
              &response_size) == SERVER_SESSION_UNEXPECTED_FRAME);

    frame = make_frame(PROTOCOL_FRAME_HELLO, 10, hello_payload, 1);
    CHECK(server_session_handle_frame(
              &session, &identity, &frame, response, sizeof(response),
              &response_size) == SERVER_SESSION_INVALID_PAYLOAD);

    frame = make_frame(PROTOCOL_FRAME_HELLO, 10, hello_payload,
                       (uint32_t)hello_size);
    CHECK(server_session_handle_frame(
              &session, &identity, &frame, response, 1,
              &response_size) == SERVER_SESSION_OUTPUT_TOO_SMALL);
    CHECK(!session.hello_received);

    CHECK(server_session_handle_frame(
              &session, &identity, &frame, response, sizeof(response),
              &response_size) == SERVER_SESSION_REPLY_READY);
    CHECK(session.hello_received);
    CHECK(session.hello_request_id == 10);
    CHECK(protocol_decode_frame(
              response, response_size, &decoded_response,
              &consumed) == PROTOCOL_STATUS_OK);
    CHECK(consumed == response_size);
    CHECK(decoded_response.type == PROTOCOL_FRAME_CHALLENGE);
    CHECK(decoded_response.request_id == 10);
    CHECK(protocol_decode_challenge(
              decoded_response.payload, decoded_response.payload_length,
              &challenge) == PROTOCOL_STATUS_OK);
    CHECK(memcmp(challenge.server_identity_public_key, identity.public_key,
                 sizeof(identity.public_key)) == 0);
    CHECK(protocol_build_server_auth_signature_data(
              signature_data, sizeof(signature_data), &hello, &challenge,
              &signature_data_size) == PROTOCOL_STATUS_OK);
    CHECK(crypto_sign_verify_detached(
              challenge.server_signature, signature_data,
              (unsigned long long)signature_data_size,
              identity.public_key) == 0);

    /* A different HELLO must not validate against this signed challenge. */
    hello.client_nonce[0] ^= 1;
    CHECK(protocol_build_server_auth_signature_data(
              signature_data, sizeof(signature_data), &hello, &challenge,
              &signature_data_size) == PROTOCOL_STATUS_OK);
    CHECK(crypto_sign_verify_detached(
              challenge.server_signature, signature_data,
              (unsigned long long)signature_data_size,
              identity.public_key) != 0);

    CHECK(server_session_handle_frame(
              &session, &identity, &frame, response, sizeof(response),
              &response_size) == SERVER_SESSION_UNEXPECTED_FRAME);
    frame = make_frame(PROTOCOL_FRAME_PING, 20, NULL, 0);
    CHECK(server_session_handle_frame(
              &session, &identity, &frame, response, sizeof(response),
              &response_size) == SERVER_SESSION_UNEXPECTED_FRAME);

    server_session_init(&session);
    server_identity_clear(&identity);
    return 0;
}
