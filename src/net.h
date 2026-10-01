// net.h - TCP sockets that work the same on POSIX and Windows.
//
// Every function logs its own failure. Callers just check the result.
// Include nob.h before this file.
#ifndef NET_H_
#define NET_H_

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
     typedef SOCKET Net_Socket;
#    define NET_INVALID INVALID_SOCKET
#    define NET_SEND_FLAGS 0
#else
#    include <errno.h>
#    include <fcntl.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/select.h>
#    include <sys/socket.h>
#    include <unistd.h>
     typedef int Net_Socket;
#    define NET_INVALID (-1)
#    ifdef MSG_NOSIGNAL
#        define NET_SEND_FLAGS MSG_NOSIGNAL
#    else
#        define NET_SEND_FLAGS 0
#    endif
#endif

static const char *net_error(void)
{
#ifdef _WIN32
    return win32_error_message(WSAGetLastError());
#else
    return strerror(errno);
#endif
}

static bool net_would_block(void)
{
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

static void net_set_blocking(Net_Socket s, bool blocking)
{
#ifdef _WIN32
    u_long nonblocking = !blocking;
    ioctlsocket(s, FIONBIO, &nonblocking);
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (blocking) flags &= ~O_NONBLOCK; else flags |= O_NONBLOCK;
    fcntl(s, F_SETFL, flags);
#endif
}

static void net_prepare(Net_Socket s)
{
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)s;
#endif
}

bool net_init(void)
{
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        nob_log(ERROR, "WSAStartup failed");
        return false;
    }
#endif
    return true;
}

void net_close(Net_Socket s)
{
    if (s == NET_INVALID) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

// Listens on every interface. Port 0 picks a free port (see net_local_port).
Net_Socket net_listen(uint16_t port)
{
    Net_Socket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == NET_INVALID) {
        nob_log(ERROR, "Could not create socket: %s", net_error());
        return NET_INVALID;
    }
    net_prepare(s);
#ifndef _WIN32
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        nob_log(ERROR, "Could not bind port %u: %s", port, net_error());
        net_close(s);
        return NET_INVALID;
    }
    if (listen(s, 16) != 0) {
        nob_log(ERROR, "Could not listen on port %u: %s", port, net_error());
        net_close(s);
        return NET_INVALID;
    }
    return s;
}

uint16_t net_local_port(Net_Socket s)
{
    struct sockaddr_in addr = {0};
    socklen_t len = sizeof(addr);
    if (getsockname(s, (struct sockaddr *)&addr, &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

Net_Socket net_accept(Net_Socket listener)
{
    Net_Socket s = accept(listener, NULL, NULL);
    if (s == NET_INVALID) {
        nob_log(ERROR, "Could not accept: %s", net_error());
        return NET_INVALID;
    }
    net_prepare(s);
    return s;
}

// Connects with a deadline so a wrong address fails fast instead of hanging.
Net_Socket net_connect(const char *host, uint16_t port, int timeout_ms)
{
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addrs = NULL;
    int err = getaddrinfo(host, temp_sprintf("%u", port), &hints, &addrs);
    if (err != 0) {
        nob_log(ERROR, "Could not resolve %s: %s", host, gai_strerror(err));
        return NET_INVALID;
    }

    Net_Socket s = NET_INVALID;
    for (struct addrinfo *a = addrs; a != NULL && s == NET_INVALID; a = a->ai_next) {
        Net_Socket candidate = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (candidate == NET_INVALID) continue;
        net_prepare(candidate);
        net_set_blocking(candidate, false);
        bool ok = connect(candidate, a->ai_addr, (socklen_t)a->ai_addrlen) == 0;
        if (!ok && net_would_block()) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(candidate, &writable);
            struct timeval tv = { timeout_ms/1000, (timeout_ms%1000)*1000 };
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            ok = select((int)candidate + 1, NULL, &writable, NULL, &tv) > 0 &&
                 getsockopt(candidate, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len) == 0 &&
                 so_error == 0;
        }
        if (ok) {
            net_set_blocking(candidate, true);
            s = candidate;
        } else {
            net_close(candidate);
        }
    }
    freeaddrinfo(addrs);
    if (s == NET_INVALID) nob_log(ERROR, "Could not connect to %s:%u", host, port);
    return s;
}

bool net_send_all(Net_Socket s, const void *data, size_t size)
{
    const char *p = data;
    while (size > 0) {
        int n = send(s, p, (int)(size > INT_MAX ? INT_MAX : size), NET_SEND_FLAGS);
        if (n <= 0) {
            nob_log(ERROR, "Could not send: %s", net_error());
            return false;
        }
        p += n;
        size -= n;
    }
    return true;
}

// Appends whatever bytes are available right now. False means the other
// side closed the connection or something broke; close the socket then.
bool net_recv(Net_Socket s, String_Builder *into)
{
    da_reserve(into, into->count + 4096);
    int n = recv(s, into->items + into->count, 4096, 0);
    if (n < 0) {
        nob_log(ERROR, "Could not receive: %s", net_error());
        return false;
    }
    if (n == 0) return false; // closed
    into->count += n;
    return true;
}

// Waits up to timeout_ms for any socket to have data. Fills readable[i].
bool net_wait(const Net_Socket *socks, bool *readable, size_t count, int timeout_ms)
{
    fd_set set;
    FD_ZERO(&set);
    int max = 0;
    for (size_t i = 0; i < count; ++i) {
        assert(socks[i] != NET_INVALID);
        FD_SET(socks[i], &set);
        if ((int)socks[i] > max) max = (int)socks[i];
    }
    struct timeval tv = { timeout_ms/1000, (timeout_ms%1000)*1000 };
    int n = select(max + 1, &set, NULL, NULL, timeout_ms < 0 ? NULL : &tv);
    if (n < 0) {
#ifndef _WIN32
        if (errno == EINTR) n = 0;
        else
#endif
        {
            nob_log(ERROR, "select failed: %s", net_error());
            return false;
        }
    }
    for (size_t i = 0; i < count; ++i) readable[i] = n > 0 && FD_ISSET(socks[i], &set);
    return true;
}

#endif // NET_H_
