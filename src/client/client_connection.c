#include "client/client_connection.h"

#include <sodium.h>

#include <stdlib.h>
#include <string.h>

#define CLIENT_HELLO_REQUEST_ID 1u
#define CLIENT_AUTHENTICATE_REQUEST_ID 2u
#define CLIENT_TEXT_REQUEST_ID 3u

static ClientConnectionStatus client_connection_send_encrypted_frame(
    ClientConnection *connection, const uint8_t *frame, size_t frame_size)
{
    uint8_t nonce[PROTOCOL_NONCE_SIZE];
    uint8_t aad[64];
    uint8_t *record;
    size_t prefix_size;
    size_t aad_size;
    unsigned long long ciphertext_size;
    const uint32_t expected_ciphertext_size = (uint32_t)(frame_size + PROTOCOL_AEAD_TAG_SIZE);

    if (connection == NULL || frame == NULL || !connection->authenticated ||
        connection->send_sequence == UINT64_MAX ||
        protocol_transport_record_size(expected_ciphertext_size) == 0)
    {
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    record = malloc(protocol_transport_record_size(expected_ciphertext_size));
    if (record == NULL ||
        protocol_encode_transport_record_prefix(record,
                                                protocol_transport_record_size(expected_ciphertext_size),
                                                connection->send_sequence,
                                                expected_ciphertext_size,
                                                &prefix_size) != PROTOCOL_STATUS_OK ||
        protocol_build_transport_nonce(nonce, connection->send_sequence) != PROTOCOL_STATUS_OK ||
        protocol_build_transport_authenticated_data(aad, sizeof(aad),
                                                    connection->send_sequence,
                                                    expected_ciphertext_size,
                                                    &aad_size) != PROTOCOL_STATUS_OK ||
        crypto_aead_xchacha20poly1305_ietf_encrypt(
            record + prefix_size, &ciphertext_size, frame, (unsigned long long)frame_size,
            aad, (unsigned long long)aad_size, NULL, nonce, connection->send_key) != 0 ||
        ciphertext_size != expected_ciphertext_size)
    {
        free(record);
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    if (net_send_all(connection->socket, record,
                     protocol_transport_record_size(expected_ciphertext_size)) != NET_STATUS_OK)
    {
        free(record);
        client_connection_close(connection);
        return CLIENT_CONNECTION_SEND_FAILED;
    }

    free(record);
    ++connection->send_sequence;
    return CLIENT_CONNECTION_OK;
}

static ClientConnectionStatus client_connection_receive_encrypted_frame(
    ClientConnection *connection, uint8_t *plaintext, size_t plaintext_capacity,
    size_t *plaintext_size)
{
    uint8_t prefix[PROTOCOL_TRANSPORT_HEADER_SIZE];
    uint8_t nonce[PROTOCOL_NONCE_SIZE];
    uint8_t aad[64];
    uint8_t *record;
    ProtocolTransportRecord transport_record;
    uint32_t ciphertext_length;
    size_t consumed;
    size_t aad_size;
    unsigned long long decrypted_size;

    if (connection == NULL || plaintext == NULL || plaintext_size == NULL ||
        connection->receive_sequence == UINT64_MAX)
    {
        return CLIENT_CONNECTION_INVALID_ARGUMENT;
    }

    if (net_receive_exact(connection->socket, prefix, sizeof(prefix)) != NET_STATUS_OK)
    {
        client_connection_close(connection);
        return CLIENT_CONNECTION_RECEIVE_FAILED;
    }

    record = malloc(PROTOCOL_TRANSPORT_HEADER_SIZE + PROTOCOL_MAX_TRANSPORT_CIPHERTEXT);
    if (record == NULL)
    {
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }
    memcpy(record, prefix, sizeof(prefix));
    ciphertext_length = ((uint32_t)prefix[0] << 24) | ((uint32_t)prefix[1] << 16) |
                        ((uint32_t)prefix[2] << 8) | (uint32_t)prefix[3];
    if (protocol_transport_record_size(ciphertext_length) == 0 ||
        ciphertext_length - PROTOCOL_AEAD_TAG_SIZE > plaintext_capacity ||
        net_receive_exact(connection->socket, record + sizeof(prefix), ciphertext_length) != NET_STATUS_OK ||
        protocol_decode_transport_record(record, sizeof(prefix) + ciphertext_length,
                                         &transport_record, &consumed) != PROTOCOL_STATUS_OK ||
        transport_record.sequence != connection->receive_sequence ||
        protocol_build_transport_nonce(nonce, transport_record.sequence) != PROTOCOL_STATUS_OK ||
        protocol_build_transport_authenticated_data(aad, sizeof(aad), transport_record.sequence,
                                                    transport_record.ciphertext_length,
                                                    &aad_size) != PROTOCOL_STATUS_OK ||
        crypto_aead_xchacha20poly1305_ietf_decrypt(
            plaintext, &decrypted_size, NULL, transport_record.ciphertext,
            transport_record.ciphertext_length, aad, (unsigned long long)aad_size,
            nonce, connection->receive_key) != 0)
    {
        free(record);
        client_connection_close(connection);
        return CLIENT_CONNECTION_SERVER_AUTH_FAILED;
    }

    free(record);
    *plaintext_size = (size_t)decrypted_size;
    ++connection->receive_sequence;
    return CLIENT_CONNECTION_OK;
}

static void client_connection_clear_handshake(ClientConnection *connection)
{
    sodium_memzero(&connection->hello, sizeof(connection->hello));
    sodium_memzero(connection->ephemeral_secret_key, sizeof(connection->ephemeral_secret_key));
    sodium_memzero(connection->receive_key, sizeof(connection->receive_key));
    sodium_memzero(connection->send_key, sizeof(connection->send_key));
    connection->hello_request_id = 0;
    connection->authenticated = false;
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

ClientConnectionStatus client_connection_finish_handshake(
    ClientConnection *connection,
    ClientDatabase *database,
    const char *host,
    uint16_t port,
    const ClientIdentity *identity)
{
    uint8_t bytes[PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_CHALLENGE_PAYLOAD_SIZE];
    uint8_t signature_data[256];
    uint8_t authenticate_payload[PROTOCOL_SIGNATURE_SIZE];
    uint8_t authenticate_frame[PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_SIGNATURE_SIZE];
    ProtocolFrame frame;
    ProtocolChallenge challenge;
    uint32_t payload_size;
    size_t consumed;
    size_t signature_data_size;
    size_t authenticate_frame_size;
    ClientDatabaseStatus database_status;

    if (connection == NULL || database == NULL || host == NULL || port == 0 ||
        identity == NULL || !client_connection_is_open(connection) || connection->authenticated)
    {
        return CLIENT_CONNECTION_INVALID_ARGUMENT;
    }

    if (net_receive_exact(connection->socket, bytes, PROTOCOL_FRAME_HEADER_SIZE) != NET_STATUS_OK)
    {
        client_connection_close(connection);
        return CLIENT_CONNECTION_RECEIVE_FAILED;
    }
    payload_size = ((uint32_t)bytes[12] << 24) | ((uint32_t)bytes[13] << 16) |
                   ((uint32_t)bytes[14] << 8) | (uint32_t)bytes[15];
    if (payload_size != PROTOCOL_CHALLENGE_PAYLOAD_SIZE ||
        net_receive_exact(connection->socket, bytes + PROTOCOL_FRAME_HEADER_SIZE, payload_size) != NET_STATUS_OK ||
        protocol_decode_frame(bytes, PROTOCOL_FRAME_HEADER_SIZE + payload_size, &frame, &consumed) != PROTOCOL_STATUS_OK ||
        frame.type != PROTOCOL_FRAME_CHALLENGE || frame.request_id != connection->hello_request_id ||
        protocol_decode_challenge(frame.payload, frame.payload_length, &challenge) != PROTOCOL_STATUS_OK ||
        protocol_build_server_auth_signature_data(signature_data, sizeof(signature_data),
                                                  &connection->hello, &challenge,
                                                  &signature_data_size) != PROTOCOL_STATUS_OK ||
        crypto_sign_verify_detached(challenge.server_signature, signature_data,
                                    (unsigned long long)signature_data_size,
                                    challenge.server_identity_public_key) != 0)
    {
        sodium_memzero(signature_data, sizeof(signature_data));
        client_connection_close(connection);
        return CLIENT_CONNECTION_SERVER_AUTH_FAILED;
    }
    sodium_memzero(signature_data, sizeof(signature_data));

    database_status = client_database_trust_server(
        database, host, port, challenge.server_identity_public_key);
    if (database_status != CLIENT_DATABASE_OK)
    {
        client_connection_close(connection);
        return CLIENT_CONNECTION_SERVER_UNTRUSTED;
    }

    if (protocol_build_client_auth_signature_data(signature_data, sizeof(signature_data),
                                                   &connection->hello, &challenge,
                                                   &signature_data_size) != PROTOCOL_STATUS_OK ||
        crypto_sign_detached(authenticate_payload, NULL, signature_data,
                             (unsigned long long)signature_data_size,
                             identity->secret_key) != 0 ||
        protocol_encode_frame(authenticate_frame, sizeof(authenticate_frame),
                              PROTOCOL_FRAME_AUTHENTICATE, CLIENT_AUTHENTICATE_REQUEST_ID,
                              authenticate_payload, sizeof(authenticate_payload),
                              &authenticate_frame_size) != PROTOCOL_STATUS_OK ||
        crypto_kx_client_session_keys(connection->receive_key, connection->send_key,
                                      connection->hello.client_ephemeral_public_key,
                                      connection->ephemeral_secret_key,
                                      challenge.server_ephemeral_public_key) != 0 ||
        net_send_all(connection->socket, authenticate_frame, authenticate_frame_size) != NET_STATUS_OK)
    {
        sodium_memzero(signature_data, sizeof(signature_data));
        client_connection_close(connection);
        return CLIENT_CONNECTION_CLIENT_AUTH_FAILED;
    }

    sodium_memzero(signature_data, sizeof(signature_data));
    {
        uint8_t authenticated_frame[PROTOCOL_FRAME_HEADER_SIZE + 64u];
        ProtocolFrame authenticated;
        ProtocolAuthenticated payload;
        size_t authenticated_frame_size;
        size_t authenticated_consumed;

        connection->authenticated = true;
        if (client_connection_receive_encrypted_frame(connection, authenticated_frame,
                                                      sizeof(authenticated_frame),
                                                      &authenticated_frame_size) != CLIENT_CONNECTION_OK ||
            protocol_decode_frame(authenticated_frame, authenticated_frame_size,
                                  &authenticated, &authenticated_consumed) != PROTOCOL_STATUS_OK ||
            authenticated_consumed != authenticated_frame_size ||
            authenticated.type != PROTOCOL_FRAME_AUTHENTICATED ||
            authenticated.request_id != CLIENT_AUTHENTICATE_REQUEST_ID ||
            protocol_decode_authenticated(authenticated.payload, authenticated.payload_length,
                                          &payload) != PROTOCOL_STATUS_OK ||
            sodium_memcmp(payload.identity_id, identity->public_key,
                          sizeof(payload.identity_id)) != 0)
        {
            client_connection_close(connection);
            return CLIENT_CONNECTION_CLIENT_AUTH_FAILED;
        }
    }

    return CLIENT_CONNECTION_OK;
}

ClientConnectionStatus client_connection_send_text(
    ClientConnection *connection, const char *text, char *reply, size_t reply_capacity)
{
    uint8_t frame_bytes[PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_MAX_TEXT_SIZE];
    uint8_t response_bytes[PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_MAX_TEXT_SIZE];
    ProtocolFrame response;
    size_t text_size;
    size_t frame_size;
    size_t response_size;
    size_t consumed;
    ClientConnectionStatus status;

    if (connection == NULL || text == NULL || reply == NULL || reply_capacity == 0 ||
        !connection->authenticated)
    {
        return CLIENT_CONNECTION_INVALID_ARGUMENT;
    }

    text_size = strlen(text);
    if (text_size == 0 || text_size > PROTOCOL_MAX_TEXT_SIZE || text_size + 1u > reply_capacity ||
        protocol_encode_frame(frame_bytes, sizeof(frame_bytes), PROTOCOL_FRAME_PING,
                              CLIENT_TEXT_REQUEST_ID, (const uint8_t *)text,
                              (uint32_t)text_size, &frame_size) != PROTOCOL_STATUS_OK)
    {
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    status = client_connection_send_encrypted_frame(connection, frame_bytes, frame_size);
    if (status != CLIENT_CONNECTION_OK)
    {
        return status;
    }
    status = client_connection_receive_encrypted_frame(connection, response_bytes,
                                                       sizeof(response_bytes), &response_size);
    if (status != CLIENT_CONNECTION_OK ||
        protocol_decode_frame(response_bytes, response_size, &response, &consumed) != PROTOCOL_STATUS_OK ||
        consumed != response_size || response.type != PROTOCOL_FRAME_PONG ||
        response.request_id != CLIENT_TEXT_REQUEST_ID ||
        response.payload_length + 1u > reply_capacity)
    {
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    memcpy(reply, response.payload, response.payload_length);
    reply[response.payload_length] = '\0';
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
        case CLIENT_CONNECTION_RECEIVE_FAILED:
            return "could not receive server challenge";
        case CLIENT_CONNECTION_SERVER_AUTH_FAILED:
            return "server challenge could not be verified";
        case CLIENT_CONNECTION_SERVER_UNTRUSTED:
            return "server identity was not trusted";
        case CLIENT_CONNECTION_CLIENT_AUTH_FAILED:
            return "could not authenticate this client";
        default:
            return "unknown connection status";
    }
}
