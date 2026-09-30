#include "client/client_connection.h"
#include "client/client_identity.h"
#include "core/dependencies.h"
#include "net/net.h"
#include "protocol/protocol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition)                                                      \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
            goto cleanup;                                                       \
        }                                                                       \
    } while (0)

static bool bytes_are_zero(const unsigned char *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index)
    {
        if (bytes[index] != 0)
        {
            return false;
        }
    }

    return true;
}

static bool build_chat_id(
    const uint8_t first[PROTOCOL_IDENTITY_ID_SIZE],
    const uint8_t second[PROTOCOL_IDENTITY_ID_SIZE],
    uint8_t output[PROTOCOL_CHAT_ID_SIZE])
{
    uint8_t material[PROTOCOL_IDENTITY_ID_SIZE * 2u];
    const uint8_t *lower = first;
    const uint8_t *higher = second;

    if (memcmp(first, second, PROTOCOL_IDENTITY_ID_SIZE) > 0)
    {
        lower = second;
        higher = first;
    }
    memcpy(material, lower, PROTOCOL_IDENTITY_ID_SIZE);
    memcpy(material + PROTOCOL_IDENTITY_ID_SIZE, higher,
           PROTOCOL_IDENTITY_ID_SIZE);
    return crypto_generichash(output, PROTOCOL_CHAT_ID_SIZE, material,
                              sizeof(material), NULL, 0) == 0;
}

static bool encrypt_record(
    const uint8_t key[crypto_kx_SESSIONKEYBYTES],
    uint64_t sequence,
    const uint8_t *frame,
    size_t frame_size,
    uint8_t **record,
    size_t *record_size)
{
    uint8_t nonce[PROTOCOL_NONCE_SIZE];
    uint8_t aad[64];
    uint32_t ciphertext_length;
    size_t prefix_size;
    size_t aad_size;
    unsigned long long ciphertext_size;

    if (frame_size > UINT32_MAX - PROTOCOL_AEAD_TAG_SIZE)
    {
        return false;
    }
    ciphertext_length = (uint32_t)frame_size + PROTOCOL_AEAD_TAG_SIZE;
    *record_size = protocol_transport_record_size(ciphertext_length);
    *record = malloc(*record_size);
    return *record != NULL &&
        protocol_encode_transport_record_prefix(*record, *record_size, sequence,
                                                ciphertext_length, &prefix_size) ==
            PROTOCOL_STATUS_OK &&
        protocol_build_transport_nonce(nonce, sequence) == PROTOCOL_STATUS_OK &&
        protocol_build_transport_authenticated_data(aad, sizeof(aad), sequence,
                                                    ciphertext_length, &aad_size) ==
            PROTOCOL_STATUS_OK &&
        crypto_aead_xchacha20poly1305_ietf_encrypt(
            *record + prefix_size, &ciphertext_size, frame,
            (unsigned long long)frame_size, aad, (unsigned long long)aad_size,
            NULL, nonce, key) == 0 && ciphertext_size == ciphertext_length;
}

static bool build_delivery_frame(
    const ClientIdentity *sender,
    const ClientIdentity *recipient,
    const char *text,
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE],
    uint8_t **frame,
    size_t *frame_size)
{
    ProtocolText message;
    ProtocolPacket packet;
    ProtocolDeliver deliver;
    uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE];
    uint8_t signature[PROTOCOL_SIGNATURE_SIZE];
    uint8_t *text_data = NULL;
    uint8_t *content_bytes = NULL;
    uint8_t *packet_bytes = NULL;
    uint8_t *delivery_payload = NULL;
    size_t text_data_size;
    size_t content_size;
    size_t packet_size;
    size_t unsigned_packet_size;
    size_t delivery_payload_size;
    bool built = false;

    message.sender_timestamp_unix_ms = UINT64_C(42);
    message.text = (const uint8_t *)text;
    message.text_length = (uint32_t)strlen(text);
    text_data_size = protocol_text_data_size(message.text_length);
    content_size = protocol_content_size((uint32_t)text_data_size);
    packet_size = protocol_packet_size((uint32_t)content_size);
    if (text_data_size == 0 || content_size == 0 || packet_size == 0 ||
        !build_chat_id(sender->public_key, recipient->public_key, chat_id))
    {
        goto cleanup;
    }

    text_data = malloc(text_data_size);
    content_bytes = malloc(content_size);
    packet_bytes = malloc(packet_size);
    if (text_data == NULL || content_bytes == NULL || packet_bytes == NULL)
    {
        goto cleanup;
    }
    randombytes_buf(packet_id, PROTOCOL_PACKET_ID_SIZE);
    if (protocol_encode_text_data(text_data, text_data_size, &message,
                                  &text_data_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_content(content_bytes, content_size, PROTOCOL_CONTENT_TEXT,
                                text_data, (uint32_t)text_data_size,
                                &content_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_unsigned_packet(packet_bytes, packet_size,
                                        PROTOCOL_PACKET_TEXT_MESSAGE, packet_id,
                                        sender->public_key, recipient->public_key,
                                        chat_id, content_bytes, (uint32_t)content_size,
                                        &unsigned_packet_size) != PROTOCOL_STATUS_OK ||
        crypto_sign_detached(signature, NULL, packet_bytes,
                             (unsigned long long)unsigned_packet_size,
                             sender->secret_key) != 0 ||
        protocol_append_packet_signature(packet_bytes, packet_size,
                                         unsigned_packet_size, signature,
                                         &packet_size) != PROTOCOL_STATUS_OK ||
        protocol_decode_packet(packet_bytes, packet_size, &packet) != PROTOCOL_STATUS_OK)
    {
        goto cleanup;
    }

    deliver.delivery_id = 17;
    memcpy(deliver.packet_id, packet_id, sizeof(deliver.packet_id));
    memcpy(deliver.sender_id, sender->public_key, sizeof(deliver.sender_id));
    deliver.server_received_at_unix_ms = 99;
    deliver.packet = packet_bytes;
    deliver.packet_length = (uint32_t)packet_size;
    delivery_payload_size = protocol_deliver_payload_size(deliver.packet_length);
    delivery_payload = malloc(delivery_payload_size);
    *frame = malloc(PROTOCOL_FRAME_HEADER_SIZE + delivery_payload_size);
    if (delivery_payload == NULL || *frame == NULL ||
        protocol_encode_deliver(delivery_payload, delivery_payload_size, &deliver,
                                &delivery_payload_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_frame(*frame,
                              PROTOCOL_FRAME_HEADER_SIZE + delivery_payload_size,
                              PROTOCOL_FRAME_DELIVER, 0, delivery_payload,
                              (uint32_t)delivery_payload_size,
                              frame_size) != PROTOCOL_STATUS_OK)
    {
        free(*frame);
        *frame = NULL;
        goto cleanup;
    }
    built = true;

cleanup:
    sodium_memzero(signature, sizeof(signature));
    free(text_data);
    free(content_bytes);
    free(packet_bytes);
    free(delivery_payload);
    return built;
}

int main(void)
{
    uint8_t frame_bytes[PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_HELLO_SIZE];
    ClientConnection connection;
    ClientIdentity identity;
    ClientIdentity sender_identity;
    ClientDatabase database = { 0 };
    NetSocket listening_socket = net_socket_invalid();
    NetSocket accepted_socket = net_socket_invalid();
    NetSocket buffered_client_socket = net_socket_invalid();
    NetSocket buffered_server_socket = net_socket_invalid();
    ProtocolFrame frame;
    ProtocolHello hello;
    size_t consumed;
    uint16_t port;
    bool dependencies_ready = false;
    bool networking_ready = false;
    uint8_t session_key[crypto_kx_SESSIONKEYBYTES];
    uint8_t sent_packet_id[PROTOCOL_PACKET_ID_SIZE];
    uint8_t delivered_packet_id[PROTOCOL_PACKET_ID_SIZE];
    uint8_t *delivery_frame = NULL;
    uint8_t *accepted_frame = NULL;
    uint8_t *delivery_record = NULL;
    uint8_t *accepted_record = NULL;
    size_t delivery_frame_size;
    size_t accepted_frame_size;
    size_t delivery_record_size;
    size_t accepted_record_size;
    uint32_t send_request_id;
    uint64_t sender_timestamp;
    uint64_t delivery_id;
    ClientChatMessage messages[2];
    uint32_t message_count;
    int exit_code = 1;

    client_connection_init(&connection);

    REQUIRE(dependencies_initialize());
    dependencies_ready = true;
    REQUIRE(net_init() == NET_STATUS_OK);
    networking_ready = true;
    REQUIRE(client_identity_generate(&identity));
    REQUIRE(net_listen(0, 1, &listening_socket) == NET_STATUS_OK);
    REQUIRE(net_get_local_port(listening_socket, &port) == NET_STATUS_OK);

    REQUIRE(client_connection_connect(
                &connection,
                "127.0.0.1",
                port,
                &identity) == CLIENT_CONNECTION_OK);
    REQUIRE(client_connection_is_open(&connection));
    REQUIRE(net_accept(listening_socket, &accepted_socket) == NET_STATUS_OK);
    REQUIRE(net_receive_exact(
                accepted_socket,
                frame_bytes,
                sizeof(frame_bytes)) == NET_STATUS_OK);
    REQUIRE(protocol_decode_frame(
                frame_bytes,
                sizeof(frame_bytes),
                &frame,
                &consumed) == PROTOCOL_STATUS_OK);
    REQUIRE(frame.type == PROTOCOL_FRAME_HELLO);
    REQUIRE(frame.request_id == connection.hello_request_id);
    REQUIRE(protocol_decode_hello(
                frame.payload,
                frame.payload_length,
                &hello) == PROTOCOL_STATUS_OK);
    REQUIRE(memcmp(
                hello.identity_public_key,
                identity.public_key,
                sizeof(hello.identity_public_key)) == 0);

    client_connection_close(&connection);
    REQUIRE(!client_connection_is_open(&connection));
    REQUIRE(bytes_are_zero(
        connection.ephemeral_secret_key,
        sizeof(connection.ephemeral_secret_key)));

    /* A partial TCP record must return immediately.  Once completed, a
     * delivery that arrives before SEND_ACCEPTED must still be persisted and
     * the accepted reply must remain available to the original send request. */
    net_close(&accepted_socket);
    net_close(&listening_socket);
    REQUIRE(net_listen(0, 1, &listening_socket) == NET_STATUS_OK);
    REQUIRE(net_get_local_port(listening_socket, &port) == NET_STATUS_OK);
    REQUIRE(net_connect("127.0.0.1", port, &buffered_client_socket) == NET_STATUS_OK);
    REQUIRE(net_accept(listening_socket, &buffered_server_socket) == NET_STATUS_OK);
    remove("client_connection_messages_test.db");
    REQUIRE(client_database_open(&database, "client_connection_messages_test.db") ==
            CLIENT_DATABASE_OK);
    REQUIRE(client_identity_generate(&sender_identity));

    client_connection_init(&connection);
    connection.socket = buffered_client_socket;
    buffered_client_socket = net_socket_invalid();
    connection.authenticated = true;
    connection.receive_buffer = malloc(CLIENT_CONNECTION_RECEIVE_BUFFER_CAPACITY);
    connection.receive_buffer_capacity = CLIENT_CONNECTION_RECEIVE_BUFFER_CAPACITY;
    connection.next_request_id = 3;
    randombytes_buf(session_key, sizeof(session_key));
    memcpy(connection.receive_key, session_key, sizeof(connection.receive_key));
    randombytes_buf(connection.send_key, sizeof(connection.send_key));
    REQUIRE(connection.receive_buffer != NULL);

    REQUIRE(client_connection_send_private_text(
                &connection, &identity, sender_identity.public_key, "outgoing",
                &send_request_id, sent_packet_id, &sender_timestamp) ==
            CLIENT_CONNECTION_OK);
    REQUIRE(sender_timestamp > 0);
    REQUIRE(build_delivery_frame(&sender_identity, &identity, "incoming",
                                 delivered_packet_id, &delivery_frame,
                                 &delivery_frame_size));
    REQUIRE(encrypt_record(session_key, 0, delivery_frame, delivery_frame_size,
                           &delivery_record, &delivery_record_size));
    REQUIRE(net_send_all(buffered_server_socket, delivery_record, 5) == NET_STATUS_OK);
    REQUIRE(client_connection_pump(&connection, &database, &identity) ==
            CLIENT_CONNECTION_NO_MESSAGE);

    {
        ProtocolDeliveryReference accepted;
        uint8_t accepted_payload[8u + PROTOCOL_PACKET_ID_SIZE];
        size_t accepted_payload_size;

        accepted.delivery_id = 23;
        memcpy(accepted.packet_id, sent_packet_id, sizeof(accepted.packet_id));
        REQUIRE(protocol_encode_delivery_reference(accepted_payload,
                                                   sizeof(accepted_payload),
                                                   &accepted,
                                                   &accepted_payload_size) ==
                PROTOCOL_STATUS_OK);
        accepted_frame = malloc(PROTOCOL_FRAME_HEADER_SIZE + accepted_payload_size);
        REQUIRE(accepted_frame != NULL);
        REQUIRE(protocol_encode_frame(accepted_frame,
                                      PROTOCOL_FRAME_HEADER_SIZE + accepted_payload_size,
                                      PROTOCOL_FRAME_SEND_ACCEPTED, send_request_id,
                                      accepted_payload, (uint32_t)accepted_payload_size,
                                      &accepted_frame_size) == PROTOCOL_STATUS_OK);
    }
    REQUIRE(encrypt_record(session_key, 1, accepted_frame, accepted_frame_size,
                           &accepted_record, &accepted_record_size));
    REQUIRE(net_send_all(buffered_server_socket, delivery_record + 5,
                         delivery_record_size - 5) == NET_STATUS_OK);
    REQUIRE(net_send_all(buffered_server_socket, accepted_record,
                         accepted_record_size) == NET_STATUS_OK);
    {
        int attempt;
        ClientConnectionStatus send_status = CLIENT_CONNECTION_NO_MESSAGE;

        for (attempt = 0; attempt < 20 &&
                          send_status == CLIENT_CONNECTION_NO_MESSAGE; ++attempt)
        {
            bool ready = false;
            ClientConnectionStatus pump_status;

            REQUIRE(net_wait_readable(&connection.socket, 1, 50, &ready) ==
                    NET_STATUS_OK);
            if (!ready)
            {
                continue;
            }

            pump_status = client_connection_pump(&connection, &database, &identity);
            REQUIRE(pump_status == CLIENT_CONNECTION_OK ||
                    pump_status == CLIENT_CONNECTION_NO_MESSAGE);
            send_status = client_connection_take_send_result(
                &connection, send_request_id, sent_packet_id, &delivery_id);
        }
        REQUIRE(send_status == CLIENT_CONNECTION_OK);
    }
    REQUIRE(delivery_id == 23);
    REQUIRE(client_database_load_messages(&database, sender_identity.public_key,
                                          messages, 2, &message_count) ==
            CLIENT_DATABASE_OK);
    REQUIRE(message_count == 1);
    REQUIRE(messages[0].direction == CLIENT_MESSAGE_INCOMING);
    REQUIRE(strcmp(messages[0].text, "incoming") == 0);
    exit_code = 0;

cleanup:
    client_connection_close(&connection);
    client_identity_clear(&identity);
    client_identity_clear(&sender_identity);
    client_database_close(&database);
    net_close(&accepted_socket);
    net_close(&listening_socket);
    net_close(&buffered_client_socket);
    net_close(&buffered_server_socket);
    free(delivery_frame);
    free(accepted_frame);
    free(delivery_record);
    free(accepted_record);
    remove("client_connection_messages_test.db");
    if (networking_ready)
    {
        net_shutdown();
    }
    if (dependencies_ready)
    {
        dependencies_shutdown();
    }
    return exit_code;
}
