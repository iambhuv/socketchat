#ifndef SOCKET_CHAT_NET_H
#define SOCKET_CHAT_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct NetSocket
{
    uintptr_t handle;
} NetSocket;

typedef enum NetStatus
{
    NET_STATUS_OK = 0,
    NET_STATUS_INVALID_ARGUMENT,
    NET_STATUS_NOT_INITIALIZED,
    NET_STATUS_ADDRESS_LOOKUP_FAILED,
    NET_STATUS_SOCKET_CREATION_FAILED,
    NET_STATUS_CONNECTION_FAILED,
    NET_STATUS_BIND_FAILED,
    NET_STATUS_LISTEN_FAILED,
    NET_STATUS_ACCEPT_FAILED,
    NET_STATUS_CLOSED,
    NET_STATUS_TIMED_OUT,
    NET_STATUS_IO_FAILED
} NetStatus;

/* Call once before using any other networking function. */
NetStatus net_init(void);

/* Call after all sockets have been closed. */
void net_shutdown(void);

NetSocket net_socket_invalid(void);
bool net_socket_is_valid(NetSocket socket);

NetStatus net_connect(
    const char *host,
    uint16_t port,
    NetSocket *connected_socket);

NetStatus net_listen(
    uint16_t port,
    int backlog,
    NetSocket *listening_socket);

NetStatus net_accept(
    NetSocket listening_socket,
    NetSocket *connected_socket);

/* Useful when net_listen() is given port 0 in a test. */
NetStatus net_get_local_port(NetSocket socket, uint16_t *port);

/*
 * Waits until sockets can be read without blocking. A timeout of -1 waits
 * forever, 0 only checks once, and a positive value is measured in milliseconds.
 */
NetStatus net_wait_readable(
    const NetSocket *sockets,
    size_t socket_count,
    int timeout_milliseconds,
    bool *readable);

/* Receives up to capacity bytes and reports how many bytes arrived. */
NetStatus net_receive_some(
    NetSocket socket,
    void *data,
    size_t capacity,
    size_t *bytes_received);

/* These functions keep working until the complete buffer is transferred. */
NetStatus net_send_all(NetSocket socket, const void *data, size_t size);
NetStatus net_receive_exact(NetSocket socket, void *data, size_t size);

/* Reads the complete buffer within one total deadline. */
NetStatus net_receive_exact_timeout(
    NetSocket socket, void *data, size_t size, int timeout_milliseconds);

void net_close(NetSocket *socket);

const char *net_status_string(NetStatus status);

#endif
