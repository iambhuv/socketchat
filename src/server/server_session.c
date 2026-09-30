#include "server/server_session.h"

#include <sodium.h>
#include <string.h>

void server_session_init(ServerSession *session)
{
    if (session != NULL)
    {
        sodium_memzero(session, sizeof(*session));
    }
}

ServerSessionResult server_session_handle_frame(
    ServerSession *session,
    const ServerIdentity *identity,
    const ProtocolFrame *frame,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_size)
{
    if (session == NULL || identity == NULL || frame == NULL || response == NULL ||
        response_size == NULL)
    {
        return SERVER_SESSION_INVALID_PAYLOAD;
    }

    *response_size = 0;

    if (frame->type == PROTOCOL_FRAME_HELLO)
    {
        uint8_t signature_data[256];
        uint8_t challenge_payload[PROTOCOL_CHALLENGE_PAYLOAD_SIZE];
        size_t signature_data_size;
        size_t challenge_payload_size;
        ProtocolStatus status;

        if (session->hello_received)
        {
            return SERVER_SESSION_UNEXPECTED_FRAME;
        }

        if (protocol_decode_hello(
                frame->payload,
                frame->payload_length,
                &session->hello) != PROTOCOL_STATUS_OK)
        {
            return SERVER_SESSION_INVALID_PAYLOAD;
        }

        memcpy(session->challenge.server_identity_public_key,
               identity->public_key,
               sizeof(identity->public_key));
        randombytes_buf(session->challenge.server_nonce,
                        sizeof(session->challenge.server_nonce));

        if (crypto_kx_keypair(
                session->challenge.server_ephemeral_public_key,
                session->server_ephemeral_secret_key) != 0 ||
            protocol_build_server_auth_signature_data(
                signature_data, sizeof(signature_data),
                &session->hello, &session->challenge,
                &signature_data_size) != PROTOCOL_STATUS_OK ||
            crypto_sign_detached(
                session->challenge.server_signature, NULL,
                signature_data, (unsigned long long)signature_data_size,
                identity->secret_key) != 0)
        {
            sodium_memzero(signature_data, sizeof(signature_data));
            return SERVER_SESSION_INTERNAL_ERROR;
        }

        sodium_memzero(signature_data, sizeof(signature_data));
        status = protocol_encode_challenge(
            challenge_payload, sizeof(challenge_payload),
            &session->challenge, &challenge_payload_size);
        if (status != PROTOCOL_STATUS_OK)
        {
            return SERVER_SESSION_INTERNAL_ERROR;
        }

        status = protocol_encode_frame(
            response, response_capacity, PROTOCOL_FRAME_CHALLENGE,
            frame->request_id, challenge_payload,
            (uint32_t)challenge_payload_size, response_size);
        if (status == PROTOCOL_STATUS_BUFFER_TOO_SMALL)
        {
            return SERVER_SESSION_OUTPUT_TOO_SMALL;
        }
        if (status != PROTOCOL_STATUS_OK)
        {
            return SERVER_SESSION_INTERNAL_ERROR;
        }

        session->hello_received = true;
        session->hello_request_id = frame->request_id;
        return SERVER_SESSION_REPLY_READY;
    }

    if (!session->hello_received)
    {
        return SERVER_SESSION_UNEXPECTED_FRAME;
    }

    if (frame->type == PROTOCOL_FRAME_AUTHENTICATE && !session->authenticated)
    {
        uint8_t signature[PROTOCOL_SIGNATURE_SIZE];
        uint8_t signature_data[256];
        size_t signature_data_size;

        if (protocol_decode_authenticate(frame->payload, frame->payload_length, signature) != PROTOCOL_STATUS_OK ||
            protocol_build_client_auth_signature_data(signature_data, sizeof(signature_data),
                                                      &session->hello, &session->challenge,
                                                      &signature_data_size) != PROTOCOL_STATUS_OK ||
            crypto_sign_verify_detached(signature, signature_data,
                                        (unsigned long long)signature_data_size,
                                        session->hello.identity_public_key) != 0 ||
            crypto_kx_server_session_keys(session->receive_key, session->send_key,
                                          session->challenge.server_ephemeral_public_key,
                                          session->server_ephemeral_secret_key,
                                          session->hello.client_ephemeral_public_key) != 0)
        {
            sodium_memzero(signature_data, sizeof(signature_data));
            return SERVER_SESSION_INVALID_PAYLOAD;
        }

        sodium_memzero(signature_data, sizeof(signature_data));
        ProtocolAuthenticated authenticated;
        uint8_t payload[PROTOCOL_IDENTITY_ID_SIZE + 4u];
        size_t payload_size;

        memcpy(authenticated.identity_id, session->hello.identity_public_key,
               sizeof(authenticated.identity_id));
        authenticated.queued_packet_count = 0;
        if (protocol_encode_authenticated(payload, sizeof(payload), &authenticated,
                                         &payload_size) != PROTOCOL_STATUS_OK ||
            protocol_encode_frame(response, response_capacity,
                                  PROTOCOL_FRAME_AUTHENTICATED,
                                  frame->request_id, payload,
                                  (uint32_t)payload_size,
                                  response_size) != PROTOCOL_STATUS_OK)
        {
            return SERVER_SESSION_OUTPUT_TOO_SMALL;
        }

        session->authenticated = true;
        return SERVER_SESSION_REPLY_READY;
    }

    if (session->authenticated && frame->type == PROTOCOL_FRAME_PING)
    {
        if (protocol_encode_frame(response, response_capacity, PROTOCOL_FRAME_PONG,
                                  frame->request_id, frame->payload,
                                  frame->payload_length, response_size) != PROTOCOL_STATUS_OK)
        {
            return SERVER_SESSION_OUTPUT_TOO_SMALL;
        }
        return SERVER_SESSION_REPLY_READY;
    }

    return SERVER_SESSION_UNEXPECTED_FRAME;
}

const char *server_session_result_string(ServerSessionResult result)
{
    switch (result)
    {
        case SERVER_SESSION_ACCEPTED:
            return "accepted";
        case SERVER_SESSION_REPLY_READY:
            return "reply ready";
        case SERVER_SESSION_INVALID_PAYLOAD:
            return "invalid frame payload";
        case SERVER_SESSION_UNEXPECTED_FRAME:
            return "unexpected frame";
        case SERVER_SESSION_OUTPUT_TOO_SMALL:
            return "response buffer too small";
        case SERVER_SESSION_INTERNAL_ERROR:
            return "internal server error";
        default:
            return "unknown server session result";
    }
}
