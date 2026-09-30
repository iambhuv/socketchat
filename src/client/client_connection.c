#include "client/client_connection.h"

#include <sodium.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CLIENT_HELLO_REQUEST_ID 1u
#define CLIENT_AUTHENTICATE_REQUEST_ID 2u
#define CLIENT_SEND_REQUEST_ID 3u
#define CLIENT_ACKNOWLEDGE_REQUEST_ID 4u

static uint64_t client_current_unix_ms(void)
{
    const time_t now = time(NULL);

    return now > 0 ? (uint64_t)now * UINT64_C(1000) : 0;
}

static bool client_build_direct_chat_id(
    const uint8_t first_identity[PROTOCOL_IDENTITY_ID_SIZE],
    const uint8_t second_identity[PROTOCOL_IDENTITY_ID_SIZE],
    uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE])
{
    uint8_t material[PROTOCOL_IDENTITY_ID_SIZE * 2u];
    const uint8_t *lower = first_identity;
    const uint8_t *higher = second_identity;
    const bool reversed = memcmp(first_identity, second_identity,
                                 PROTOCOL_IDENTITY_ID_SIZE) > 0;

    if (reversed)
    {
        lower = second_identity;
        higher = first_identity;
    }

    memcpy(material, lower, PROTOCOL_IDENTITY_ID_SIZE);
    memcpy(material + PROTOCOL_IDENTITY_ID_SIZE, higher,
           PROTOCOL_IDENTITY_ID_SIZE);
    if (crypto_generichash(chat_id, PROTOCOL_CHAT_ID_SIZE, material,
                           sizeof(material), NULL, 0) != 0)
    {
        sodium_memzero(material, sizeof(material));
        return false;
    }
    sodium_memzero(material, sizeof(material));
    return true;
}

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
    free(connection->receive_buffer);
    connection->receive_buffer = NULL;
    connection->receive_buffer_size = 0;
    connection->receive_buffer_capacity = 0;
    sodium_memzero(&connection->hello, sizeof(connection->hello));
    sodium_memzero(connection->ephemeral_secret_key, sizeof(connection->ephemeral_secret_key));
    sodium_memzero(connection->receive_key, sizeof(connection->receive_key));
    sodium_memzero(connection->send_key, sizeof(connection->send_key));
    sodium_memzero(connection->pending_responses,
                   sizeof(connection->pending_responses));
    sodium_memzero(connection->pending_acknowledgement_request_ids,
                   sizeof(connection->pending_acknowledgement_request_ids));
    connection->hello_request_id = 0;
    connection->next_request_id = 0;
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

    connection->receive_buffer = malloc(CLIENT_CONNECTION_RECEIVE_BUFFER_CAPACITY);
    if (connection->receive_buffer == NULL)
    {
        client_connection_close(connection);
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }
    connection->receive_buffer_capacity = CLIENT_CONNECTION_RECEIVE_BUFFER_CAPACITY;
    connection->next_request_id = CLIENT_SEND_REQUEST_ID;

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

static uint32_t client_connection_next_request_id(ClientConnection *connection)
{
    const uint32_t request_id = connection->next_request_id == 0
        ? CLIENT_SEND_REQUEST_ID : connection->next_request_id;

    connection->next_request_id = request_id == UINT32_MAX
        ? CLIENT_SEND_REQUEST_ID : request_id + 1u;
    return request_id;
}

static bool client_connection_track_acknowledgement(
    ClientConnection *connection, uint32_t request_id)
{
    size_t index;

    for (index = 0; index < CLIENT_CONNECTION_MAX_PENDING_ACKNOWLEDGEMENTS; ++index)
    {
        if (connection->pending_acknowledgement_request_ids[index] == 0)
        {
            connection->pending_acknowledgement_request_ids[index] = request_id;
            return true;
        }
    }
    return false;
}

static bool client_connection_consume_acknowledgement(
    ClientConnection *connection, uint32_t request_id)
{
    size_t index;

    for (index = 0; index < CLIENT_CONNECTION_MAX_PENDING_ACKNOWLEDGEMENTS; ++index)
    {
        if (connection->pending_acknowledgement_request_ids[index] == request_id)
        {
            connection->pending_acknowledgement_request_ids[index] = 0;
            return true;
        }
    }
    return false;
}

static ClientConnectionStatus client_connection_acknowledge_delivery(
    ClientConnection *connection,
    const ProtocolDeliver *deliver,
    ProtocolAcknowledgeResult result)
{
    uint8_t acknowledgement_payload[8u + PROTOCOL_PACKET_ID_SIZE + 1u];
    uint8_t acknowledgement_frame[PROTOCOL_FRAME_HEADER_SIZE +
                                  sizeof(acknowledgement_payload)];
    ProtocolAcknowledge acknowledgement;
    size_t acknowledgement_payload_size;
    size_t acknowledgement_frame_size;
    const uint32_t request_id = client_connection_next_request_id(connection);

    acknowledgement.delivery_id = deliver->delivery_id;
    memcpy(acknowledgement.packet_id, deliver->packet_id,
           sizeof(acknowledgement.packet_id));
    acknowledgement.result = result;

    if (!client_connection_track_acknowledgement(connection, request_id) ||
        protocol_encode_acknowledge(acknowledgement_payload,
                                    sizeof(acknowledgement_payload),
                                    &acknowledgement,
                                    &acknowledgement_payload_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_frame(acknowledgement_frame, sizeof(acknowledgement_frame),
                              PROTOCOL_FRAME_ACKNOWLEDGE, request_id,
                              acknowledgement_payload,
                              (uint32_t)acknowledgement_payload_size,
                              &acknowledgement_frame_size) != PROTOCOL_STATUS_OK)
    {
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    return client_connection_send_encrypted_frame(
        connection, acknowledgement_frame, acknowledgement_frame_size);
}

static ClientConnectionStatus client_connection_decrypt_record(
    ClientConnection *connection,
    const ProtocolTransportRecord *transport_record,
    uint8_t *plaintext,
    size_t plaintext_capacity,
    size_t *plaintext_size)
{
    uint8_t nonce[PROTOCOL_NONCE_SIZE];
    uint8_t aad[64];
    size_t aad_size;
    unsigned long long decrypted_size;

    if (connection == NULL || transport_record == NULL || plaintext == NULL ||
        plaintext_size == NULL || connection->receive_sequence == UINT64_MAX ||
        transport_record->sequence != connection->receive_sequence ||
        transport_record->ciphertext_length < PROTOCOL_AEAD_TAG_SIZE ||
        transport_record->ciphertext_length - PROTOCOL_AEAD_TAG_SIZE > plaintext_capacity ||
        protocol_build_transport_nonce(nonce, transport_record->sequence) != PROTOCOL_STATUS_OK ||
        protocol_build_transport_authenticated_data(aad, sizeof(aad),
                                                    transport_record->sequence,
                                                    transport_record->ciphertext_length,
                                                    &aad_size) != PROTOCOL_STATUS_OK ||
        crypto_aead_xchacha20poly1305_ietf_decrypt(
            plaintext, &decrypted_size, NULL, transport_record->ciphertext,
            transport_record->ciphertext_length, aad, (unsigned long long)aad_size,
            nonce, connection->receive_key) != 0)
    {
        return CLIENT_CONNECTION_SERVER_AUTH_FAILED;
    }

    ++connection->receive_sequence;
    *plaintext_size = (size_t)decrypted_size;
    return CLIENT_CONNECTION_OK;
}

static ClientConnectionStatus client_connection_store_response(
    ClientConnection *connection, const ProtocolFrame *frame)
{
    size_t index;

    if (frame->payload_length > CLIENT_CONNECTION_MAX_RESPONSE_PAYLOAD)
    {
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    for (index = 0; index < CLIENT_CONNECTION_MAX_PENDING_RESPONSES; ++index)
    {
        ClientConnectionPendingResponse *response =
            &connection->pending_responses[index];
        if (!response->in_use)
        {
            response->in_use = true;
            response->type = frame->type;
            response->request_id = frame->request_id;
            response->payload_length = frame->payload_length;
            memcpy(response->payload, frame->payload, frame->payload_length);
            return CLIENT_CONNECTION_OK;
        }
    }
    return CLIENT_CONNECTION_PROTOCOL_FAILED;
}

static ClientConnectionStatus client_connection_handle_delivery(
    ClientConnection *connection,
    ClientDatabase *database,
    const ClientIdentity *identity,
    const ProtocolFrame *frame)
{
    ProtocolDeliver deliver;
    ProtocolPacket packet;
    ProtocolContent content;
    ProtocolText decoded_text;
    ClientDatabaseStatus database_status;
    bool was_inserted;

    if (protocol_decode_deliver(frame->payload, frame->payload_length, &deliver) !=
            PROTOCOL_STATUS_OK ||
        protocol_decode_packet(deliver.packet, deliver.packet_length, &packet) !=
            PROTOCOL_STATUS_OK ||
        protocol_validate_delivered_packet(&deliver, &packet, identity->public_key) !=
            PROTOCOL_STATUS_OK ||
        packet.kind != PROTOCOL_PACKET_TEXT_MESSAGE ||
        crypto_sign_verify_detached(packet.signature, packet.signed_bytes,
                                    (unsigned long long)packet.signed_length,
                                    packet.sender_public_key) != 0 ||
        protocol_decode_content(packet.payload, packet.payload_length, &content) !=
            PROTOCOL_STATUS_OK ||
        content.type != PROTOCOL_CONTENT_TEXT ||
        protocol_decode_text_data(content.data, content.data_length, &decoded_text) !=
            PROTOCOL_STATUS_OK)
    {
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    database_status = client_database_store_message(
        database, deliver.sender_id, deliver.packet_id,
        sodium_memcmp(deliver.sender_id, identity->public_key,
                       PROTOCOL_IDENTITY_ID_SIZE) == 0
            ? CLIENT_MESSAGE_OUTGOING : CLIENT_MESSAGE_INCOMING,
        decoded_text.sender_timestamp_unix_ms, decoded_text.text,
        decoded_text.text_length, &was_inserted);
    if (database_status != CLIENT_DATABASE_OK)
    {
        (void)client_connection_acknowledge_delivery(
            connection, &deliver, PROTOCOL_ACK_REJECTED);
        return CLIENT_CONNECTION_STORAGE_FAILED;
    }

    return client_connection_acknowledge_delivery(
        connection, &deliver,
        was_inserted ? PROTOCOL_ACK_STORED : PROTOCOL_ACK_DUPLICATE);
}

static ClientConnectionStatus client_connection_dispatch_frame(
    ClientConnection *connection,
    ClientDatabase *database,
    const ClientIdentity *identity,
    const ProtocolFrame *frame)
{
    ProtocolDeliveryReference reference;

    if (frame->type == PROTOCOL_FRAME_DELIVER)
    {
        return client_connection_handle_delivery(connection, database, identity, frame);
    }
    if (frame->type == PROTOCOL_FRAME_ACKNOWLEDGED)
    {
        if (!client_connection_consume_acknowledgement(connection, frame->request_id) ||
            protocol_decode_delivery_reference(frame->payload, frame->payload_length,
                                               &reference) != PROTOCOL_STATUS_OK)
        {
            return CLIENT_CONNECTION_PROTOCOL_FAILED;
        }
        return CLIENT_CONNECTION_OK;
    }
    if (frame->type == PROTOCOL_FRAME_ERROR &&
        client_connection_consume_acknowledgement(connection, frame->request_id))
    {
        return CLIENT_CONNECTION_MESSAGE_REJECTED;
    }
    if (frame->type == PROTOCOL_FRAME_SEND_ACCEPTED ||
        frame->type == PROTOCOL_FRAME_ERROR)
    {
        return client_connection_store_response(connection, frame);
    }
    return CLIENT_CONNECTION_PROTOCOL_FAILED;
}

ClientConnectionStatus client_connection_pump(
    ClientConnection *connection,
    ClientDatabase *database,
    const ClientIdentity *identity)
{
    bool processed_frame = false;
    size_t reads;

    if (connection == NULL || database == NULL || identity == NULL ||
        !connection->authenticated || connection->receive_buffer == NULL ||
        connection->receive_buffer_capacity == 0 ||
        !client_identity_is_valid(identity))
    {
        return CLIENT_CONNECTION_INVALID_ARGUMENT;
    }

    for (reads = 0; reads < 8u; ++reads)
    {
        bool readable;
        size_t received;

        if (net_wait_readable(&connection->socket, 1, 0, &readable) != NET_STATUS_OK)
        {
            return CLIENT_CONNECTION_RECEIVE_FAILED;
        }
        if (!readable)
        {
            break;
        }
        if (connection->receive_buffer_size == connection->receive_buffer_capacity ||
            net_receive_some(connection->socket,
                             connection->receive_buffer + connection->receive_buffer_size,
                             connection->receive_buffer_capacity - connection->receive_buffer_size,
                             &received) != NET_STATUS_OK || received == 0)
        {
            client_connection_close(connection);
            return CLIENT_CONNECTION_RECEIVE_FAILED;
        }
        connection->receive_buffer_size += received;

        for (;;)
        {
            ProtocolTransportRecord transport_record;
            ProtocolFrame frame;
            ProtocolStatus protocol_status;
            uint8_t *plaintext;
            size_t record_size;
            size_t plaintext_size;
            size_t frame_size;
            ClientConnectionStatus status;

            protocol_status = protocol_decode_transport_record(
                connection->receive_buffer, connection->receive_buffer_size,
                &transport_record, &record_size);
            if (protocol_status == PROTOCOL_STATUS_NEED_MORE_DATA)
            {
                break;
            }
            if (protocol_status != PROTOCOL_STATUS_OK ||
                transport_record.ciphertext_length < PROTOCOL_AEAD_TAG_SIZE)
            {
                client_connection_close(connection);
                return CLIENT_CONNECTION_PROTOCOL_FAILED;
            }

            plaintext = malloc(transport_record.ciphertext_length - PROTOCOL_AEAD_TAG_SIZE);
            if (plaintext == NULL)
            {
                return CLIENT_CONNECTION_PROTOCOL_FAILED;
            }
            status = client_connection_decrypt_record(
                connection, &transport_record, plaintext,
                transport_record.ciphertext_length - PROTOCOL_AEAD_TAG_SIZE,
                &plaintext_size);
            if (status == CLIENT_CONNECTION_OK &&
                (protocol_decode_frame(plaintext, plaintext_size, &frame, &frame_size) !=
                    PROTOCOL_STATUS_OK || frame_size != plaintext_size))
            {
                status = CLIENT_CONNECTION_PROTOCOL_FAILED;
            }
            if (status == CLIENT_CONNECTION_OK)
            {
                status = client_connection_dispatch_frame(connection, database, identity,
                                                          &frame);
            }
            sodium_memzero(plaintext,
                           transport_record.ciphertext_length - PROTOCOL_AEAD_TAG_SIZE);
            free(plaintext);

            memmove(connection->receive_buffer,
                    connection->receive_buffer + record_size,
                    connection->receive_buffer_size - record_size);
            connection->receive_buffer_size -= record_size;
            processed_frame = true;
            if (status != CLIENT_CONNECTION_OK)
            {
                if (status == CLIENT_CONNECTION_PROTOCOL_FAILED ||
                    status == CLIENT_CONNECTION_SERVER_AUTH_FAILED)
                {
                    client_connection_close(connection);
                }
                return status;
            }
        }
    }

    return processed_frame ? CLIENT_CONNECTION_OK : CLIENT_CONNECTION_NO_MESSAGE;
}

ClientConnectionStatus client_connection_send_private_text(
    ClientConnection *connection,
    const ClientIdentity *identity,
    const uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE],
    const char *text,
    uint32_t *request_id,
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE],
    uint64_t *sender_timestamp_unix_ms)
{
    ProtocolText text_message;
    ProtocolSend send;
    uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE];
    uint8_t signature[PROTOCOL_SIGNATURE_SIZE];
    uint8_t *text_data = NULL;
    uint8_t *content = NULL;
    uint8_t *packet = NULL;
    uint8_t *send_payload = NULL;
    uint8_t *frame_bytes = NULL;
    size_t text_size;
    size_t text_data_size;
    size_t content_size;
    size_t packet_unsigned_size;
    size_t packet_size;
    size_t send_payload_size;
    size_t frame_size;
    ClientConnectionStatus status = CLIENT_CONNECTION_PROTOCOL_FAILED;
    uint32_t generated_request_id;

    if (connection == NULL || identity == NULL || recipient_id == NULL || text == NULL ||
        request_id == NULL || packet_id == NULL || sender_timestamp_unix_ms == NULL ||
        !connection->authenticated || !client_identity_is_valid(identity))
    {
        return CLIENT_CONNECTION_INVALID_ARGUMENT;
    }

    text_size = strlen(text);
    if (text_size == 0 || text_size > PROTOCOL_MAX_TEXT_SIZE ||
        !client_build_direct_chat_id(identity->public_key, recipient_id, chat_id))
    {
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    text_data_size = protocol_text_data_size((uint32_t)text_size);
    content_size = protocol_content_size((uint32_t)text_data_size);
    packet_size = protocol_packet_size((uint32_t)content_size);
    send_payload_size = protocol_send_payload_size((uint32_t)packet_size);
    if (text_data_size == 0 || content_size == 0 || packet_size == 0 ||
        send_payload_size == 0)
    {
        return CLIENT_CONNECTION_PROTOCOL_FAILED;
    }

    text_data = malloc(text_data_size);
    content = malloc(content_size);
    packet = malloc(packet_size);
    send_payload = malloc(send_payload_size);
    frame_bytes = malloc(PROTOCOL_FRAME_HEADER_SIZE + send_payload_size);
    if (text_data == NULL || content == NULL || packet == NULL || send_payload == NULL ||
        frame_bytes == NULL)
    {
        goto cleanup;
    }

    text_message.sender_timestamp_unix_ms = client_current_unix_ms();
    text_message.text = (const uint8_t *)text;
    text_message.text_length = (uint32_t)text_size;
    randombytes_buf(packet_id, PROTOCOL_PACKET_ID_SIZE);
    if (protocol_encode_text_data(text_data, text_data_size, &text_message,
                                  &text_data_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_content(content, content_size, PROTOCOL_CONTENT_TEXT,
                                text_data, (uint32_t)text_data_size,
                                &content_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_unsigned_packet(packet, packet_size,
                                        PROTOCOL_PACKET_TEXT_MESSAGE, packet_id,
                                        identity->public_key, recipient_id, chat_id,
                                        content, (uint32_t)content_size,
                                        &packet_unsigned_size) != PROTOCOL_STATUS_OK ||
        crypto_sign_detached(signature, NULL, packet,
                             (unsigned long long)packet_unsigned_size,
                             identity->secret_key) != 0 ||
        protocol_append_packet_signature(packet, packet_size, packet_unsigned_size,
                                         signature, &packet_size) != PROTOCOL_STATUS_OK)
    {
        goto cleanup;
    }

    memcpy(send.packet_id, packet_id, sizeof(send.packet_id));
    memcpy(send.recipient_id, recipient_id, sizeof(send.recipient_id));
    send.expires_in_seconds = 0;
    send.packet = packet;
    send.packet_length = (uint32_t)packet_size;
    generated_request_id = client_connection_next_request_id(connection);
    if (protocol_encode_send(send_payload, send_payload_size, &send,
                             &send_payload_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_frame(frame_bytes, PROTOCOL_FRAME_HEADER_SIZE + send_payload_size,
                              PROTOCOL_FRAME_SEND, generated_request_id, send_payload,
                              (uint32_t)send_payload_size, &frame_size) != PROTOCOL_STATUS_OK)
    {
        goto cleanup;
    }

    status = client_connection_send_encrypted_frame(connection, frame_bytes, frame_size);
    if (status == CLIENT_CONNECTION_OK)
    {
        *request_id = generated_request_id;
        *sender_timestamp_unix_ms = text_message.sender_timestamp_unix_ms;
    }

cleanup:
    sodium_memzero(signature, sizeof(signature));
    sodium_memzero(chat_id, sizeof(chat_id));
    free(text_data);
    free(content);
    free(packet);
    free(send_payload);
    free(frame_bytes);
    return status;
}

ClientConnectionStatus client_connection_take_send_result(
    ClientConnection *connection,
    uint32_t request_id,
    const uint8_t expected_packet_id[PROTOCOL_PACKET_ID_SIZE],
    uint64_t *delivery_id)
{
    size_t index;

    if (connection == NULL || request_id == 0 || expected_packet_id == NULL ||
        delivery_id == NULL)
    {
        return CLIENT_CONNECTION_INVALID_ARGUMENT;
    }

    for (index = 0; index < CLIENT_CONNECTION_MAX_PENDING_RESPONSES; ++index)
    {
        ClientConnectionPendingResponse *response = &connection->pending_responses[index];
        ProtocolDeliveryReference reference;

        if (!response->in_use || response->request_id != request_id)
        {
            continue;
        }
        if (response->type == PROTOCOL_FRAME_ERROR)
        {
            sodium_memzero(response, sizeof(*response));
            return CLIENT_CONNECTION_MESSAGE_REJECTED;
        }
        if (response->type != PROTOCOL_FRAME_SEND_ACCEPTED ||
            protocol_decode_delivery_reference(response->payload,
                                               response->payload_length,
                                               &reference) != PROTOCOL_STATUS_OK ||
            memcmp(reference.packet_id, expected_packet_id,
                   sizeof(reference.packet_id)) != 0)
        {
            sodium_memzero(response, sizeof(*response));
            return CLIENT_CONNECTION_PROTOCOL_FAILED;
        }

        *delivery_id = reference.delivery_id;
        sodium_memzero(response, sizeof(*response));
        return CLIENT_CONNECTION_OK;
    }

    return CLIENT_CONNECTION_NO_MESSAGE;
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
        case CLIENT_CONNECTION_MESSAGE_REJECTED:
            return "message was rejected by the server";
        case CLIENT_CONNECTION_STORAGE_FAILED:
            return "could not save the received message";
        case CLIENT_CONNECTION_NO_MESSAGE:
            return "no message available";
        default:
            return "unknown connection status";
    }
}
