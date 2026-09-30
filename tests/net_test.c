#include "net/net.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static bool status_is_ok(NetStatus status, const char *operation)
{
    if (status == NET_STATUS_OK)
    {
        return true;
    }

    fprintf(stderr, "%s: %s\n", operation, net_status_string(status));
    return false;
}

static int finish_test(
    NetSocket *listening_socket,
    NetSocket *client_socket,
    NetSocket *server_socket,
    int result)
{
    net_close(server_socket);
    net_close(client_socket);
    net_close(listening_socket);
    net_shutdown();
    return result;
}

int main(void)
{
    static const unsigned char client_message[] = { 'p', 'i', 'n', 'g' };
    static const unsigned char server_message[] = { 'p', 'o', 'n', 'g' };

    NetSocket listening_socket = net_socket_invalid();
    NetSocket client_socket = net_socket_invalid();
    NetSocket server_socket = net_socket_invalid();
    NetSocket readable_sockets[1];
    bool readable[1];
    unsigned char received[4];
    size_t received_size;
    uint16_t port;

    if (!status_is_ok(net_init(), "net_init"))
    {
        return 1;
    }

    if (!status_is_ok(net_listen(0, 1, &listening_socket), "net_listen") ||
        !status_is_ok(
            net_get_local_port(listening_socket, &port),
            "net_get_local_port") ||
        !status_is_ok(
            net_connect("127.0.0.1", port, &client_socket),
            "net_connect") ||
        !status_is_ok(
            net_accept(listening_socket, &server_socket),
            "net_accept"))
    {
        return finish_test(
            &listening_socket,
            &client_socket,
            &server_socket,
            1);
    }

    readable_sockets[0] = server_socket;

    if (!status_is_ok(
            net_send_all(client_socket, client_message, sizeof(client_message)),
            "client net_send_all") ||
        !status_is_ok(
            net_wait_readable(readable_sockets, 1, 1000, readable),
            "server net_wait_readable") ||
        !readable[0])
    {
        return finish_test(
            &listening_socket,
            &client_socket,
            &server_socket,
            1);
    }

    if (!status_is_ok(
            net_receive_some(
                server_socket,
                received,
                sizeof(received),
                &received_size),
            "server net_receive_some") ||
        received_size != sizeof(received) ||
        memcmp(received, client_message, sizeof(received)) != 0)
    {
        return finish_test(
            &listening_socket,
            &client_socket,
            &server_socket,
            1);
    }

    if (!status_is_ok(
            net_send_all(server_socket, server_message, sizeof(server_message)),
            "server net_send_all") ||
        !status_is_ok(
            net_receive_exact(client_socket, received, sizeof(received)),
            "client net_receive_exact") ||
        memcmp(received, server_message, sizeof(received)) != 0)
    {
        return finish_test(
            &listening_socket,
            &client_socket,
            &server_socket,
            1);
    }

    /* One byte from a stalled peer must not keep a handshake read open forever. */
    if (!status_is_ok(net_send_all(server_socket, server_message, 1),
                      "partial net_send_all") ||
        net_receive_exact_timeout(client_socket, received, 2, 50) !=
            NET_STATUS_TIMED_OUT)
    {
        fprintf(stderr, "partial receive did not time out\n");
        return finish_test(
            &listening_socket,
            &client_socket,
            &server_socket,
            1);
    }

    net_close(&client_socket);

    {
        const NetStatus close_status =
            net_receive_exact(server_socket, received, 1);

        if (close_status != NET_STATUS_CLOSED)
        {
            fprintf(
                stderr,
                "closed connection check: expected connection closed, got %s\n",
                net_status_string(close_status));
            return finish_test(
                &listening_socket,
                &client_socket,
                &server_socket,
                1);
        }
    }

    return finish_test(
        &listening_socket,
        &client_socket,
        &server_socket,
        0);
}
