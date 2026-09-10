#include "client/client_connection.h"
#include "client/client_identity.h"
#include "core/dependencies.h"
#include "net/net.h"
#include "protocol/protocol.h"

#include <stdbool.h>
#include <stdio.h>
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

int main(void)
{
    uint8_t frame_bytes[PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_HELLO_SIZE];
    ClientConnection connection;
    ClientIdentity identity;
    NetSocket listening_socket = net_socket_invalid();
    NetSocket accepted_socket = net_socket_invalid();
    ProtocolFrame frame;
    ProtocolHello hello;
    size_t consumed;
    uint16_t port;
    bool dependencies_ready = false;
    bool networking_ready = false;
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
    exit_code = 0;

cleanup:
    client_connection_close(&connection);
    client_identity_clear(&identity);
    net_close(&accepted_socket);
    net_close(&listening_socket);
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
