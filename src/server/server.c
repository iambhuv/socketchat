#include "server/server.h"

#include "net/net.h"
#include "protocol/protocol.h"
#include "server/server_session.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERVER_MAX_CLIENTS 32u
#define SERVER_LISTEN_BACKLOG 16
#define SERVER_WAIT_MILLISECONDS 250
#define SERVER_CLIENT_BUFFER_SIZE \
    (PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_MAX_SERVER_PAYLOAD)

typedef struct ServerClient
{
    NetSocket socket;
    ServerSession session;
    uint8_t *read_buffer;
    size_t buffered_size;
} ServerClient;

static volatile sig_atomic_t server_stop_requested = 0;

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
    ServerClient *client,
    size_t client_index,
    const ProtocolFrame *frame)
{
    uint8_t response[SERVER_SESSION_MAX_RESPONSE_SIZE];
    size_t response_size;
    ServerSessionResult result = server_session_handle_frame(
        &client->session,
        frame,
        response,
        sizeof(response),
        &response_size);

    if (result == SERVER_SESSION_REPLY_READY)
    {
        if (net_send_all(client->socket, response, response_size) != NET_STATUS_OK)
        {
            return false;
        }

        printf("Client %zu PING answered with PONG.\n", client_index + 1);
        return true;
    }

    if (result != SERVER_SESSION_ACCEPTED)
    {
        server_send_error(
            client->socket,
            frame->request_id,
            PROTOCOL_ERROR_INVALID_REQUEST,
            server_session_result_string(result));
        return false;
    }

    if (frame->type == PROTOCOL_FRAME_HELLO)
    {
        server_print_claimed_identity(
            client_index,
            client->session.hello.identity_public_key);
    }
    else if (frame->type == PROTOCOL_FRAME_PONG)
    {
        printf("Client %zu sent PONG.\n", client_index + 1);
    }

    return true;
}

static void server_read_from_client(
    ServerClient clients[SERVER_MAX_CLIENTS],
    size_t client_index)
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
        size_t consumed_size;
        ProtocolStatus protocol_status = protocol_decode_frame(
            client->read_buffer,
            client->buffered_size,
            &frame,
            &consumed_size);

        if (protocol_status == PROTOCOL_STATUS_NEED_MORE_DATA)
        {
            return;
        }

        if (protocol_status != PROTOCOL_STATUS_OK)
        {
            server_send_error(
                client->socket,
                0,
                PROTOCOL_ERROR_MALFORMED_FRAME,
                protocol_status_string(protocol_status));
            server_disconnect_client(
                clients,
                client_index,
                protocol_status_string(protocol_status));
            return;
        }

        if (!server_handle_frame(client, client_index, &frame))
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
    NetSocket listening_socket = net_socket_invalid();
    int exit_code = 0;

    server_stop_requested = 0;
    server_initialize_clients(clients);

    if (net_init() != NET_STATUS_OK)
    {
        fprintf(stderr, "Could not initialize networking.\n");
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
            return 1;
        }
    }

    signal(SIGINT, server_handle_stop_signal);
    signal(SIGTERM, server_handle_stop_signal);

    printf("Server listening on 0.0.0.0:%u\n", (unsigned int)port);
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
                    client_indexes[socket_index]);
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
    printf("Server stopped.\n");
    return exit_code;
}
