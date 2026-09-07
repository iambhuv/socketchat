#include "net/net.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET NativeSocket;
typedef int NetAddressLength;

#else

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int NativeSocket;
typedef socklen_t NetAddressLength;

#endif

static bool net_initialized = false;

static NativeSocket net_native_socket(NetSocket socket)
{
    return (NativeSocket)socket.handle;
}

static NetSocket net_socket_from_native(NativeSocket socket)
{
    NetSocket result = { (uintptr_t)socket };
    return result;
}

static bool net_native_socket_is_valid(NativeSocket socket)
{
#ifdef _WIN32
    return socket != INVALID_SOCKET;
#else
    return socket >= 0;
#endif
}

static void net_close_native(NativeSocket socket)
{
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

static int net_last_native_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool net_error_was_interrupted(int error)
{
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

static bool net_error_means_closed(int error)
{
#ifdef _WIN32
    return error == WSAECONNABORTED ||
           error == WSAECONNRESET ||
           error == WSAESHUTDOWN ||
           error == WSAENOTCONN;
#else
    return error == ECONNRESET || error == EPIPE || error == ENOTCONN;
#endif
}

static bool net_prepare_socket(NativeSocket socket)
{
#ifdef __APPLE__
    const int enabled = 1;
    return setsockopt(
               socket,
               SOL_SOCKET,
               SO_NOSIGPIPE,
               &enabled,
               sizeof(enabled)) == 0;
#else
    (void)socket;
    return true;
#endif
}

NetStatus net_init(void)
{
    if (net_initialized)
    {
        return NET_STATUS_OK;
    }

#ifdef _WIN32
    WSADATA data;

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        return NET_STATUS_IO_FAILED;
    }

    if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2)
    {
        WSACleanup();
        return NET_STATUS_IO_FAILED;
    }
#endif

    net_initialized = true;
    return NET_STATUS_OK;
}

void net_shutdown(void)
{
    if (!net_initialized)
    {
        return;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    net_initialized = false;
}

NetSocket net_socket_invalid(void)
{
    NetSocket socket = { UINTPTR_MAX };
    return socket;
}

bool net_socket_is_valid(NetSocket socket)
{
    return socket.handle != UINTPTR_MAX;
}

NetStatus net_connect(
    const char *host,
    uint16_t port,
    NetSocket *connected_socket)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char port_text[6];
    bool created_socket = false;

    if (connected_socket == NULL || host == NULL || host[0] == '\0' || port == 0)
    {
        return NET_STATUS_INVALID_ARGUMENT;
    }

    *connected_socket = net_socket_invalid();

    if (!net_initialized)
    {
        return NET_STATUS_NOT_INITIALIZED;
    }

    snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port_text, &hints, &addresses) != 0)
    {
        return NET_STATUS_ADDRESS_LOOKUP_FAILED;
    }

    for (address = addresses; address != NULL; address = address->ai_next)
    {
        NativeSocket candidate = socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol);

        if (!net_native_socket_is_valid(candidate))
        {
            continue;
        }

        created_socket = true;

        if (!net_prepare_socket(candidate))
        {
            net_close_native(candidate);
            continue;
        }

#ifdef _WIN32
        if (connect(candidate, address->ai_addr, (int)address->ai_addrlen) == 0)
#else
        if (connect(candidate, address->ai_addr, address->ai_addrlen) == 0)
#endif
        {
            *connected_socket = net_socket_from_native(candidate);
            freeaddrinfo(addresses);
            return NET_STATUS_OK;
        }

        net_close_native(candidate);
    }

    freeaddrinfo(addresses);
    return created_socket
               ? NET_STATUS_CONNECTION_FAILED
               : NET_STATUS_SOCKET_CREATION_FAILED;
}

NetStatus net_listen(
    uint16_t port,
    int backlog,
    NetSocket *listening_socket)
{
    struct sockaddr_in address;
    NativeSocket socket_handle;

    if (listening_socket == NULL || backlog <= 0)
    {
        return NET_STATUS_INVALID_ARGUMENT;
    }

    *listening_socket = net_socket_invalid();

    if (!net_initialized)
    {
        return NET_STATUS_NOT_INITIALIZED;
    }

    socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (!net_native_socket_is_valid(socket_handle))
    {
        return NET_STATUS_SOCKET_CREATION_FAILED;
    }

    if (!net_prepare_socket(socket_handle))
    {
        net_close_native(socket_handle);
        return NET_STATUS_SOCKET_CREATION_FAILED;
    }

#ifndef _WIN32
    {
        const int enabled = 1;
        setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    }
#endif

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(
            socket_handle,
            (const struct sockaddr *)&address,
            (NetAddressLength)sizeof(address)) != 0)
    {
        net_close_native(socket_handle);
        return NET_STATUS_BIND_FAILED;
    }

    if (listen(socket_handle, backlog) != 0)
    {
        net_close_native(socket_handle);
        return NET_STATUS_LISTEN_FAILED;
    }

    *listening_socket = net_socket_from_native(socket_handle);
    return NET_STATUS_OK;
}

NetStatus net_accept(
    NetSocket listening_socket,
    NetSocket *connected_socket)
{
    NativeSocket accepted_socket;

    if (!net_socket_is_valid(listening_socket) || connected_socket == NULL)
    {
        return NET_STATUS_INVALID_ARGUMENT;
    }

    *connected_socket = net_socket_invalid();

    if (!net_initialized)
    {
        return NET_STATUS_NOT_INITIALIZED;
    }

    do
    {
        accepted_socket = accept(net_native_socket(listening_socket), NULL, NULL);
    } while (!net_native_socket_is_valid(accepted_socket) &&
             net_error_was_interrupted(net_last_native_error()));

    if (!net_native_socket_is_valid(accepted_socket))
    {
        return NET_STATUS_ACCEPT_FAILED;
    }

    if (!net_prepare_socket(accepted_socket))
    {
        net_close_native(accepted_socket);
        return NET_STATUS_ACCEPT_FAILED;
    }

    *connected_socket = net_socket_from_native(accepted_socket);
    return NET_STATUS_OK;
}

NetStatus net_get_local_port(NetSocket socket, uint16_t *port)
{
    struct sockaddr_storage address;
    NetAddressLength address_length = (NetAddressLength)sizeof(address);

    if (!net_socket_is_valid(socket) || port == NULL)
    {
        return NET_STATUS_INVALID_ARGUMENT;
    }

    if (!net_initialized)
    {
        return NET_STATUS_NOT_INITIALIZED;
    }

    memset(&address, 0, sizeof(address));

    if (getsockname(
            net_native_socket(socket),
            (struct sockaddr *)&address,
            &address_length) != 0)
    {
        return NET_STATUS_IO_FAILED;
    }

    if (address.ss_family == AF_INET)
    {
        const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)&address;
        *port = ntohs(ipv4->sin_port);
        return NET_STATUS_OK;
    }

#ifdef AF_INET6
    if (address.ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *)&address;
        *port = ntohs(ipv6->sin6_port);
        return NET_STATUS_OK;
    }
#endif

    return NET_STATUS_IO_FAILED;
}

NetStatus net_wait_readable(
    const NetSocket *sockets,
    size_t socket_count,
    int timeout_milliseconds,
    bool *readable)
{
    fd_set read_set;
    struct timeval timeout;
    struct timeval *timeout_pointer = NULL;
    NativeSocket largest_socket = 0;
    size_t index;
    int selected;

    if (sockets == NULL || readable == NULL || socket_count == 0 ||
        timeout_milliseconds < -1 || socket_count > (size_t)FD_SETSIZE)
    {
        return NET_STATUS_INVALID_ARGUMENT;
    }

    if (!net_initialized)
    {
        return NET_STATUS_NOT_INITIALIZED;
    }

    FD_ZERO(&read_set);

    for (index = 0; index < socket_count; ++index)
    {
        const NativeSocket socket = net_native_socket(sockets[index]);

        readable[index] = false;

        if (!net_socket_is_valid(sockets[index]))
        {
            return NET_STATUS_INVALID_ARGUMENT;
        }

#ifndef _WIN32
        if (socket >= FD_SETSIZE)
        {
            return NET_STATUS_INVALID_ARGUMENT;
        }
#endif

        FD_SET(socket, &read_set);

        if (socket > largest_socket)
        {
            largest_socket = socket;
        }
    }

    if (timeout_milliseconds >= 0)
    {
        timeout.tv_sec = timeout_milliseconds / 1000;
        timeout.tv_usec = (timeout_milliseconds % 1000) * 1000;
        timeout_pointer = &timeout;
    }

#ifdef _WIN32
    selected = select(0, &read_set, NULL, NULL, timeout_pointer);
#else
    selected = select(largest_socket + 1, &read_set, NULL, NULL, timeout_pointer);
#endif

    if (selected < 0)
    {
        return NET_STATUS_IO_FAILED;
    }

    for (index = 0; index < socket_count; ++index)
    {
        readable[index] =
            FD_ISSET(net_native_socket(sockets[index]), &read_set) != 0;
    }

    return NET_STATUS_OK;
}

NetStatus net_receive_some(
    NetSocket socket,
    void *data,
    size_t capacity,
    size_t *bytes_received)
{
    const int chunk_size = capacity > INT_MAX ? INT_MAX : (int)capacity;

    if (!net_socket_is_valid(socket) || data == NULL || capacity == 0 ||
        bytes_received == NULL)
    {
        return NET_STATUS_INVALID_ARGUMENT;
    }

    *bytes_received = 0;

    if (!net_initialized)
    {
        return NET_STATUS_NOT_INITIALIZED;
    }

    for (;;)
    {
#ifdef _WIN32
        const int received = recv(
            net_native_socket(socket),
            (char *)data,
            chunk_size,
            0);
#else
        const ssize_t received = recv(
            net_native_socket(socket),
            data,
            (size_t)chunk_size,
            0);
#endif

        if (received > 0)
        {
            *bytes_received = (size_t)received;
            return NET_STATUS_OK;
        }

        if (received == 0)
        {
            return NET_STATUS_CLOSED;
        }

        {
            const int error = net_last_native_error();

            if (net_error_was_interrupted(error))
            {
                continue;
            }

            return net_error_means_closed(error)
                       ? NET_STATUS_CLOSED
                       : NET_STATUS_IO_FAILED;
        }
    }
}

NetStatus net_send_all(NetSocket socket, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    size_t sent_total = 0;

    if (!net_socket_is_valid(socket) || (data == NULL && size > 0))
    {
        return NET_STATUS_INVALID_ARGUMENT;
    }

    if (!net_initialized)
    {
        return NET_STATUS_NOT_INITIALIZED;
    }

    while (sent_total < size)
    {
        const size_t remaining = size - sent_total;
        const int chunk_size = remaining > INT_MAX ? INT_MAX : (int)remaining;

#ifdef _WIN32
        const int sent = send(
            net_native_socket(socket),
            (const char *)bytes + sent_total,
            chunk_size,
            0);
#else
        const ssize_t sent = send(
            net_native_socket(socket),
            bytes + sent_total,
            (size_t)chunk_size,
#ifdef MSG_NOSIGNAL
            MSG_NOSIGNAL);
#else
            0);
#endif
#endif

        if (sent > 0)
        {
            sent_total += (size_t)sent;
            continue;
        }

        if (sent == 0)
        {
            return NET_STATUS_CLOSED;
        }

        {
            const int error = net_last_native_error();

            if (net_error_was_interrupted(error))
            {
                continue;
            }

            return net_error_means_closed(error)
                       ? NET_STATUS_CLOSED
                       : NET_STATUS_IO_FAILED;
        }
    }

    return NET_STATUS_OK;
}

NetStatus net_receive_exact(NetSocket socket, void *data, size_t size)
{
    unsigned char *bytes = data;
    size_t received_total = 0;

    if (!net_socket_is_valid(socket) || (data == NULL && size > 0))
    {
        return NET_STATUS_INVALID_ARGUMENT;
    }

    if (!net_initialized)
    {
        return NET_STATUS_NOT_INITIALIZED;
    }

    while (received_total < size)
    {
        const size_t remaining = size - received_total;
        const int chunk_size = remaining > INT_MAX ? INT_MAX : (int)remaining;

#ifdef _WIN32
        const int received = recv(
            net_native_socket(socket),
            (char *)bytes + received_total,
            chunk_size,
            0);
#else
        const ssize_t received = recv(
            net_native_socket(socket),
            bytes + received_total,
            (size_t)chunk_size,
            0);
#endif

        if (received > 0)
        {
            received_total += (size_t)received;
            continue;
        }

        if (received == 0)
        {
            return NET_STATUS_CLOSED;
        }

        {
            const int error = net_last_native_error();

            if (net_error_was_interrupted(error))
            {
                continue;
            }

            return net_error_means_closed(error)
                       ? NET_STATUS_CLOSED
                       : NET_STATUS_IO_FAILED;
        }
    }

    return NET_STATUS_OK;
}

void net_close(NetSocket *socket)
{
    if (socket == NULL || !net_socket_is_valid(*socket))
    {
        return;
    }

    net_close_native(net_native_socket(*socket));
    *socket = net_socket_invalid();
}

const char *net_status_string(NetStatus status)
{
    switch (status)
    {
        case NET_STATUS_OK:
            return "ok";
        case NET_STATUS_INVALID_ARGUMENT:
            return "invalid argument";
        case NET_STATUS_NOT_INITIALIZED:
            return "networking is not initialized";
        case NET_STATUS_ADDRESS_LOOKUP_FAILED:
            return "address lookup failed";
        case NET_STATUS_SOCKET_CREATION_FAILED:
            return "socket creation failed";
        case NET_STATUS_CONNECTION_FAILED:
            return "connection failed";
        case NET_STATUS_BIND_FAILED:
            return "bind failed";
        case NET_STATUS_LISTEN_FAILED:
            return "listen failed";
        case NET_STATUS_ACCEPT_FAILED:
            return "accept failed";
        case NET_STATUS_CLOSED:
            return "connection closed";
        case NET_STATUS_IO_FAILED:
            return "network input/output failed";
        default:
            return "unknown network status";
    }
}
