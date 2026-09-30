#include "server/server.h"

#include "core/app_config.h"
#include "core/dependencies.h"
#include "net/net.h"
#include "protocol/protocol.h"
#include "server/server_identity.h"
#include "server/server_session.h"

#include <sodium.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SERVER_MAX_CLIENTS 32u
#define SERVER_LISTEN_BACKLOG 16
#define SERVER_WAIT_MILLISECONDS 250
#define SERVER_CLIENT_BUFFER_SIZE \
    (PROTOCOL_TRANSPORT_HEADER_SIZE + PROTOCOL_MAX_TRANSPORT_CIPHERTEXT)

typedef struct ServerClient
{
    NetSocket socket;
    ServerSession session;
    uint8_t *read_buffer;
    size_t buffered_size;
} ServerClient;

static volatile sig_atomic_t server_stop_requested = 0;

static uint64_t server_current_unix_ms(void)
{
    const time_t now = time(NULL);

    return now > 0 ? (uint64_t)now * UINT64_C(1000) : 0;
}

static void server_handle_stop_signal(int signal_number)
{
    (void)signal_number;
    server_stop_requested = 1;
}

static void server_initialize_clients(ServerClient clients[SERVER_MAX_CLIENTS])
{
    size_t index;

    for (index = 0; index < SERVER_MAX_CLIENTS; ++index)
    {
        clients[index].socket = net_socket_invalid();
        clients[index].read_buffer = NULL;
        clients[index].buffered_size = 0;
        server_session_init(&clients[index].session);
    }
}

static void server_disconnect_client(
    ServerClient clients[SERVER_MAX_CLIENTS],
    size_t client_index,
    const char *reason)
{
    ServerClient *client = &clients[client_index];

    if (!net_socket_is_valid(client->socket))
    {
        return;
    }

    printf("Client %zu disconnected: %s\n", client_index + 1, reason);
    net_close(&client->socket);
    free(client->read_buffer);
    client->read_buffer = NULL;
    client->buffered_size = 0;
    server_session_init(&client->session);
}

static bool server_add_client(
    ServerClient clients[SERVER_MAX_CLIENTS],
    NetSocket socket)
{
    size_t index;

    for (index = 0; index < SERVER_MAX_CLIENTS; ++index)
    {
        ServerClient *client = &clients[index];

        if (net_socket_is_valid(client->socket))
        {
            continue;
        }

        client->read_buffer = malloc(SERVER_CLIENT_BUFFER_SIZE);
        if (client->read_buffer == NULL)
        {
            return false;
        }

        client->socket = socket;
        client->buffered_size = 0;
        server_session_init(&client->session);
        printf("Client %zu connected.\n", index + 1);
        return true;
    }

    return false;
}

static bool server_send_error(
    NetSocket socket,
    uint32_t request_id,
    ProtocolErrorCode code,
    const char *text)
{
    uint8_t payload[4u + PROTOCOL_MAX_ERROR_MESSAGE_SIZE];
    uint8_t frame_bytes[
        PROTOCOL_FRAME_HEADER_SIZE + 4u + PROTOCOL_MAX_ERROR_MESSAGE_SIZE];
    ProtocolError message;
    size_t payload_size;
    size_t frame_size;

    message.code = code;
    message.message = (const uint8_t *)text;
    message.message_length = (uint16_t)strlen(text);

    if (protocol_encode_error(
            payload,
            sizeof(payload),
            &message,
            &payload_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_frame(
            frame_bytes,
            sizeof(frame_bytes),
            PROTOCOL_FRAME_ERROR,
            request_id,
            payload,
            (uint32_t)payload_size,
            &frame_size) != PROTOCOL_STATUS_OK)
    {
        return false;
    }

    return net_send_all(socket, frame_bytes, frame_size) == NET_STATUS_OK;
}

static bool server_send_response(ServerClient *client, const uint8_t *frame, size_t frame_size)
{
    uint8_t nonce[PROTOCOL_NONCE_SIZE];
    uint8_t aad[64];
    uint8_t *record;
    uint32_t ciphertext_length;
    size_t prefix_size;
    size_t aad_size;
    unsigned long long ciphertext_size;

    if (!client->session.authenticated)
    {
        return net_send_all(client->socket, frame, frame_size) == NET_STATUS_OK;
    }
    if (client->session.send_sequence == UINT64_MAX ||
        frame_size > UINT32_MAX - PROTOCOL_AEAD_TAG_SIZE)
    {
        return false;
    }
    ciphertext_length = (uint32_t)frame_size + PROTOCOL_AEAD_TAG_SIZE;
    if (protocol_transport_record_size(ciphertext_length) == 0)
    {
        return false;
    }
    record = malloc(protocol_transport_record_size(ciphertext_length));
    if (record == NULL ||
        protocol_encode_transport_record_prefix(record,
                                                protocol_transport_record_size(ciphertext_length),
                                                client->session.send_sequence, ciphertext_length,
                                                &prefix_size) != PROTOCOL_STATUS_OK ||
        protocol_build_transport_nonce(nonce, client->session.send_sequence) != PROTOCOL_STATUS_OK ||
        protocol_build_transport_authenticated_data(aad, sizeof(aad),
                                                    client->session.send_sequence, ciphertext_length,
                                                    &aad_size) != PROTOCOL_STATUS_OK ||
        crypto_aead_xchacha20poly1305_ietf_encrypt(
            record + prefix_size, &ciphertext_size, frame, (unsigned long long)frame_size,
            aad, (unsigned long long)aad_size, NULL, nonce, client->session.send_key) != 0 ||
        ciphertext_size != ciphertext_length)
    {
        free(record);
        return false;
    }
    if (net_send_all(client->socket, record,
                     protocol_transport_record_size(ciphertext_length)) != NET_STATUS_OK)
    {
        free(record);
        return false;
    }
    free(record);
    ++client->session.send_sequence;
    return true;
}

static bool server_send_client_error(
    ServerClient *client,
    uint32_t request_id,
    ProtocolErrorCode code,
    const char *text)
{
    uint8_t payload[4u + PROTOCOL_MAX_ERROR_MESSAGE_SIZE];
    uint8_t frame[PROTOCOL_FRAME_HEADER_SIZE + 4u + PROTOCOL_MAX_ERROR_MESSAGE_SIZE];
    ProtocolError message;
    size_t payload_size;
    size_t frame_size;

    message.code = code;
    message.message = (const uint8_t *)text;
    message.message_length = (uint16_t)strlen(text);
    if (message.message_length > PROTOCOL_MAX_ERROR_MESSAGE_SIZE ||
        protocol_encode_error(payload, sizeof(payload), &message, &payload_size) !=
            PROTOCOL_STATUS_OK ||
        protocol_encode_frame(frame, sizeof(frame), PROTOCOL_FRAME_ERROR, request_id,
                              payload, (uint32_t)payload_size, &frame_size) !=
            PROTOCOL_STATUS_OK)
    {
        return false;
    }

    return server_send_response(client, frame, frame_size);
}

static ServerClient *server_find_authenticated_client(
    ServerClient clients[SERVER_MAX_CLIENTS],
    const uint8_t identity[PROTOCOL_IDENTITY_ID_SIZE])
{
    size_t index;

    for (index = 0; index < SERVER_MAX_CLIENTS; ++index)
    {
        ServerClient *candidate = &clients[index];

        if (net_socket_is_valid(candidate->socket) &&
            candidate->session.authenticated &&
            sodium_memcmp(candidate->session.hello.identity_public_key, identity,
                          PROTOCOL_IDENTITY_ID_SIZE) == 0)
        {
            return candidate;
        }
    }

    return NULL;
}

static bool server_handle_send(
    ServerClient clients[SERVER_MAX_CLIENTS],
    size_t sender_index,
    const ProtocolFrame *frame,
    uint64_t *next_delivery_id)
{
    ServerClient *sender = &clients[sender_index];
    ServerClient *recipient;
    size_t recipient_index;
    ProtocolSend send;
    ProtocolPacket packet;
    ProtocolDeliveryReference accepted;
    ProtocolDeliver deliver;
    uint8_t accepted_payload[8u + PROTOCOL_PACKET_ID_SIZE];
    uint8_t accepted_frame[PROTOCOL_FRAME_HEADER_SIZE + sizeof(accepted_payload)];
    uint8_t *deliver_payload = NULL;
    uint8_t *deliver_frame = NULL;
    size_t accepted_payload_size;
    size_t accepted_frame_size;
    size_t deliver_payload_size;
    size_t deliver_frame_size;
    bool delivered = false;

    if (next_delivery_id == NULL ||
        protocol_decode_send(frame->payload, frame->payload_length, &send) !=
            PROTOCOL_STATUS_OK ||
        protocol_decode_packet(send.packet, send.packet_length, &packet) !=
            PROTOCOL_STATUS_OK ||
        protocol_validate_send_packet(&send, &packet,
                                      sender->session.hello.identity_public_key) !=
            PROTOCOL_STATUS_OK ||
        packet.kind != PROTOCOL_PACKET_TEXT_MESSAGE ||
        crypto_sign_verify_detached(packet.signature, packet.signed_bytes,
                                    (unsigned long long)packet.signed_length,
                                    packet.sender_public_key) != 0)
    {
        return server_send_client_error(sender, frame->request_id,
                                        PROTOCOL_ERROR_INVALID_REQUEST,
                                        "invalid private message");
    }

    recipient = server_find_authenticated_client(clients, send.recipient_id);
    if (recipient == NULL)
    {
        return server_send_client_error(sender, frame->request_id,
                                        PROTOCOL_ERROR_INVALID_REQUEST,
                                        "recipient is offline");
    }
    recipient_index = (size_t)(recipient - clients);
    if (*next_delivery_id == 0 || *next_delivery_id == UINT64_MAX)
    {
        return server_send_client_error(sender, frame->request_id,
                                        PROTOCOL_ERROR_QUEUE_FULL,
                                        "delivery counter exhausted");
    }

    accepted.delivery_id = *next_delivery_id;
    memcpy(accepted.packet_id, send.packet_id, sizeof(accepted.packet_id));
    if (protocol_encode_delivery_reference(accepted_payload, sizeof(accepted_payload),
                                           &accepted, &accepted_payload_size) !=
            PROTOCOL_STATUS_OK ||
        protocol_encode_frame(accepted_frame, sizeof(accepted_frame),
                              PROTOCOL_FRAME_SEND_ACCEPTED, frame->request_id,
                              accepted_payload, (uint32_t)accepted_payload_size,
                              &accepted_frame_size) != PROTOCOL_STATUS_OK)
    {
        return false;
    }

    deliver.delivery_id = *next_delivery_id;
    memcpy(deliver.packet_id, send.packet_id, sizeof(deliver.packet_id));
    memcpy(deliver.sender_id, sender->session.hello.identity_public_key,
           sizeof(deliver.sender_id));
    deliver.server_received_at_unix_ms = server_current_unix_ms();
    deliver.packet = send.packet;
    deliver.packet_length = send.packet_length;
    deliver_payload_size = protocol_deliver_payload_size(deliver.packet_length);
    if (deliver_payload_size == 0)
    {
        return false;
    }
    deliver_payload = malloc(deliver_payload_size);
    deliver_frame = malloc(PROTOCOL_FRAME_HEADER_SIZE + deliver_payload_size);
    if (deliver_payload == NULL || deliver_frame == NULL ||
        protocol_encode_deliver(deliver_payload, deliver_payload_size, &deliver,
                                &deliver_payload_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_frame(deliver_frame,
                              PROTOCOL_FRAME_HEADER_SIZE + deliver_payload_size,
                              PROTOCOL_FRAME_DELIVER, 0, deliver_payload,
                              (uint32_t)deliver_payload_size,
                              &deliver_frame_size) != PROTOCOL_STATUS_OK)
    {
        free(deliver_payload);
        free(deliver_frame);
        return false;
    }

    /* The acceptance must arrive first for the self-chat case, where sender
     * and recipient are the same socket. */
    if (server_send_response(sender, accepted_frame, accepted_frame_size))
    {
        delivered = server_send_response(recipient, deliver_frame, deliver_frame_size);
    }
    free(deliver_payload);
    free(deliver_frame);
    if (!delivered)
    {
        server_disconnect_client(clients, recipient_index, "could not receive delivery");
        return true;
    }

    ++*next_delivery_id;
    printf("Delivered private message from client %zu.\n", sender_index + 1u);
    return true;
}

static bool server_handle_acknowledge(
    ServerClient *client,
    const ProtocolFrame *frame)
{
    ProtocolAcknowledge acknowledge;
    ProtocolDeliveryReference reference;
    uint8_t payload[8u + PROTOCOL_PACKET_ID_SIZE];
    uint8_t response[PROTOCOL_FRAME_HEADER_SIZE + sizeof(payload)];
    size_t payload_size;
    size_t response_size;

    if (protocol_decode_acknowledge(frame->payload, frame->payload_length,
                                    &acknowledge) != PROTOCOL_STATUS_OK)
    {
        return server_send_client_error(client, frame->request_id,
                                        PROTOCOL_ERROR_INVALID_REQUEST,
                                        "invalid delivery acknowledgement");
    }

    reference.delivery_id = acknowledge.delivery_id;
    memcpy(reference.packet_id, acknowledge.packet_id, sizeof(reference.packet_id));
    if (protocol_encode_delivery_reference(payload, sizeof(payload), &reference,
                                           &payload_size) != PROTOCOL_STATUS_OK ||
        protocol_encode_frame(response, sizeof(response), PROTOCOL_FRAME_ACKNOWLEDGED,
                              frame->request_id, payload, (uint32_t)payload_size,
                              &response_size) != PROTOCOL_STATUS_OK)
    {
        return false;
    }

    return server_send_response(client, response, response_size);
}

static void server_print_claimed_identity(
    size_t client_index,
    const uint8_t identity[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE])
{
    size_t index;

    printf("Client %zu sent HELLO (claimed identity ", client_index + 1);

    for (index = 0; index < 8; ++index)
    {
        printf("%02x", (unsigned int)identity[index]);
    }

    printf("...).\n");
}

static bool server_handle_frame(
    ServerClient clients[SERVER_MAX_CLIENTS],
    size_t client_index,
    const ServerIdentity *identity,
    uint64_t *next_delivery_id,
    const ProtocolFrame *frame)
{
    ServerClient *client = &clients[client_index];
    uint8_t response[SERVER_SESSION_MAX_RESPONSE_SIZE];
    size_t response_size;

    if (client->session.authenticated && frame->type == PROTOCOL_FRAME_SEND)
    {
        return server_handle_send(clients, client_index, frame, next_delivery_id);
    }
    if (client->session.authenticated && frame->type == PROTOCOL_FRAME_ACKNOWLEDGE)
    {
        return server_handle_acknowledge(client, frame);
    }

    ServerSessionResult result = server_session_handle_frame(
        &client->session,
        identity,
        frame,
        response,
        sizeof(response),
        &response_size);

    if (result == SERVER_SESSION_REPLY_READY)
    {
        if (!server_send_response(client, response, response_size))
        {
            return false;
        }

        if (frame->type == PROTOCOL_FRAME_HELLO)
        {
            server_print_claimed_identity(
                client_index,
                client->session.hello.identity_public_key);
            printf("Client %zu sent a signed CHALLENGE.\n", client_index + 1);
        }
        return true;
    }

    if (result != SERVER_SESSION_ACCEPTED)
    {
        if (client->session.authenticated)
        {
            (void)server_send_client_error(
                client, frame->request_id, PROTOCOL_ERROR_INVALID_REQUEST,
                server_session_result_string(result));
        }
        else
        {
            (void)server_send_error(
                client->socket,
                frame->request_id,
                PROTOCOL_ERROR_INVALID_REQUEST,
                server_session_result_string(result));
        }
        return false;
    }

    return true;
}

static void server_read_from_client(
    ServerClient clients[SERVER_MAX_CLIENTS],
    size_t client_index,
    const ServerIdentity *identity,
    uint64_t *next_delivery_id)
{
    ServerClient *client = &clients[client_index];
    size_t received_size;
    NetStatus receive_status;

    if (client->buffered_size == SERVER_CLIENT_BUFFER_SIZE)
    {
        server_send_error(
            client->socket,
            0,
            PROTOCOL_ERROR_FRAME_TOO_LARGE,
            "frame is too large");
        server_disconnect_client(clients, client_index, "frame is too large");
        return;
    }

    receive_status = net_receive_some(
        client->socket,
        client->read_buffer + client->buffered_size,
        SERVER_CLIENT_BUFFER_SIZE - client->buffered_size,
        &received_size);

    if (receive_status != NET_STATUS_OK)
    {
        server_disconnect_client(
            clients,
            client_index,
            net_status_string(receive_status));
        return;
    }

    client->buffered_size += received_size;

    while (client->buffered_size > 0)
    {
        ProtocolFrame frame;
        size_t consumed_size = 0;
        ProtocolStatus protocol_status;
        uint8_t plaintext[PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_MAX_SERVER_PAYLOAD];

        if (!client->session.authenticated)
        {
            protocol_status = protocol_decode_frame(
                client->read_buffer,
                client->buffered_size,
                &frame,
                &consumed_size);
        }
        else
        {
            ProtocolTransportRecord record;
            uint8_t nonce[PROTOCOL_NONCE_SIZE];
            uint8_t aad[64];
            size_t record_size;
            size_t aad_size;
            size_t frame_consumed;
            unsigned long long plaintext_size;

            protocol_status = protocol_decode_transport_record(
                client->read_buffer, client->buffered_size, &record, &record_size);
            if (protocol_status == PROTOCOL_STATUS_OK)
            {
                if (record.sequence != client->session.receive_sequence ||
                    client->session.receive_sequence == UINT64_MAX ||
                    protocol_build_transport_nonce(nonce, record.sequence) != PROTOCOL_STATUS_OK ||
                    protocol_build_transport_authenticated_data(aad, sizeof(aad), record.sequence,
                                                                record.ciphertext_length,
                                                                &aad_size) != PROTOCOL_STATUS_OK ||
                    crypto_aead_xchacha20poly1305_ietf_decrypt(
                        plaintext, &plaintext_size, NULL, record.ciphertext,
                        record.ciphertext_length, aad, (unsigned long long)aad_size,
                        nonce, client->session.receive_key) != 0 ||
                    plaintext_size > sizeof(plaintext) ||
                    protocol_decode_frame(plaintext, (size_t)plaintext_size, &frame,
                                          &frame_consumed) != PROTOCOL_STATUS_OK ||
                    frame_consumed != (size_t)plaintext_size)
                {
                    protocol_status = PROTOCOL_STATUS_INVALID_VALUE;
                }
                else
                {
                    consumed_size = record_size;
                    ++client->session.receive_sequence;
                }
            }
        }

        if (protocol_status == PROTOCOL_STATUS_NEED_MORE_DATA)
        {
            return;
        }

        if (protocol_status != PROTOCOL_STATUS_OK)
        {
            if (!client->session.authenticated)
            {
                server_send_error(
                    client->socket,
                    0,
                    PROTOCOL_ERROR_MALFORMED_FRAME,
                    protocol_status_string(protocol_status));
            }
            server_disconnect_client(
                clients,
                client_index,
                protocol_status_string(protocol_status));
            return;
        }

        if (!server_handle_frame(clients, client_index, identity,
                                 next_delivery_id, &frame))
        {
            server_disconnect_client(clients, client_index, "invalid request");
            return;
        }

        client->buffered_size -= consumed_size;
        memmove(
            client->read_buffer,
            client->read_buffer + consumed_size,
            client->buffered_size);
    }
}

static void server_accept_client(
    NetSocket listening_socket,
    ServerClient clients[SERVER_MAX_CLIENTS])
{
    NetSocket client_socket = net_socket_invalid();
    NetStatus status = net_accept(listening_socket, &client_socket);

    if (status != NET_STATUS_OK)
    {
        fprintf(stderr, "Could not accept client: %s\n", net_status_string(status));
        return;
    }

    if (!server_add_client(clients, client_socket))
    {
        fprintf(stderr, "Client rejected: server is full or out of memory.\n");
        net_close(&client_socket);
    }
}

static size_t server_build_socket_list(
    NetSocket listening_socket,
    const ServerClient clients[SERVER_MAX_CLIENTS],
    NetSocket sockets[SERVER_MAX_CLIENTS + 1u],
    size_t client_indexes[SERVER_MAX_CLIENTS + 1u])
{
    size_t socket_count = 1;
    size_t client_index;

    sockets[0] = listening_socket;
    client_indexes[0] = 0;

    for (client_index = 0; client_index < SERVER_MAX_CLIENTS; ++client_index)
    {
        if (net_socket_is_valid(clients[client_index].socket))
        {
            sockets[socket_count] = clients[client_index].socket;
            client_indexes[socket_count] = client_index;
            ++socket_count;
        }
    }

    return socket_count;
}

int server_run(uint16_t port)
{
    ServerClient clients[SERVER_MAX_CLIENTS];
    ServerIdentity identity;
    NetSocket listening_socket = net_socket_invalid();
    uint64_t next_delivery_id = 1;
    int exit_code = 0;

    server_stop_requested = 0;
    server_initialize_clients(clients);

    if (!dependencies_initialize())
    {
        fprintf(stderr, "Could not initialize libsodium and SQLite.\n");
        return 1;
    }

    if (!server_identity_load_or_create(APP_SERVER_DATABASE_PATH, &identity))
    {
        dependencies_shutdown();
        return 1;
    }

    if (net_init() != NET_STATUS_OK)
    {
        fprintf(stderr, "Could not initialize networking.\n");
        server_identity_clear(&identity);
        dependencies_shutdown();
        return 1;
    }

    {
        const NetStatus listen_status = net_listen(
            port,
            SERVER_LISTEN_BACKLOG,
            &listening_socket);

        if (listen_status != NET_STATUS_OK)
        {
            fprintf(
                stderr,
                "Could not listen on port %u: %s\n",
                (unsigned int)port,
                net_status_string(listen_status));
            net_shutdown();
            server_identity_clear(&identity);
            dependencies_shutdown();
            return 1;
        }
    }

    signal(SIGINT, server_handle_stop_signal);
    signal(SIGTERM, server_handle_stop_signal);

    printf("Server listening on 0.0.0.0:%u\n", (unsigned int)port);
    printf("Server identity (pin this key on clients later): ");
    {
        size_t key_index;
        for (key_index = 0; key_index < sizeof(identity.public_key); ++key_index)
        {
            printf("%02x", (unsigned int)identity.public_key[key_index]);
        }
        printf("\n");
    }
    printf("Press Ctrl+C to stop.\n");

    while (!server_stop_requested)
    {
        NetSocket sockets[SERVER_MAX_CLIENTS + 1u];
        size_t client_indexes[SERVER_MAX_CLIENTS + 1u];
        bool readable[SERVER_MAX_CLIENTS + 1u];
        size_t socket_index;
        const size_t socket_count = server_build_socket_list(
            listening_socket,
            clients,
            sockets,
            client_indexes);
        const NetStatus wait_status = net_wait_readable(
            sockets,
            socket_count,
            SERVER_WAIT_MILLISECONDS,
            readable);

        if (wait_status != NET_STATUS_OK)
        {
            if (server_stop_requested)
            {
                break;
            }

            fprintf(stderr, "Could not wait for clients: %s\n",
                    net_status_string(wait_status));
            exit_code = 1;
            break;
        }

        if (readable[0])
        {
            server_accept_client(listening_socket, clients);
        }

        for (socket_index = 1; socket_index < socket_count; ++socket_index)
        {
            if (readable[socket_index])
            {
                server_read_from_client(
                    clients,
                    client_indexes[socket_index],
                    &identity,
                    &next_delivery_id);
            }
        }
    }

    {
        size_t client_index;

        for (client_index = 0; client_index < SERVER_MAX_CLIENTS; ++client_index)
        {
            server_disconnect_client(clients, client_index, "server stopped");
        }
    }

    net_close(&listening_socket);
    net_shutdown();
    server_identity_clear(&identity);
    dependencies_shutdown();
    printf("Server stopped.\n");
    return exit_code;
}
