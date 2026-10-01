// chatd.c - the server. Accepts clients, checks they own their key, forwards
// messages to whoever is online. Keeps nothing on disk.
//
//   cc -o nob nob.c && ./nob && ./build/chatd [port]
#ifndef CHATD_C_
#define CHATD_C_

#ifndef CHATD_NO_MAIN
#define NOB_IMPLEMENTATION
#include "nob.h"
#include "protocol.h"
#include "crypto.h"
#include "net.h"
#endif

#define DEFAULT_PORT 5050
#define MAX_PEERS    64

typedef enum {
    PEER_WAIT_HELLO,
    PEER_WAIT_AUTH,
    PEER_READY,
} Peer_State;

typedef struct {
    Net_Socket sock;
    Peer_State state;
    uint8_t pub[KEY_SIZE];
    uint8_t nonce[NONCE_SIZE];
    String_Builder inbox;
} Peer;

typedef struct {
    Net_Socket listener;
    Peer peers[MAX_PEERS];
    size_t peers_count;
    String_Builder scratch;
} Server;

static bool peer_send(Server *s, Peer *p, Frame_Type type, const void *payload, uint32_t size)
{
    s->scratch.count = 0;
    frame_append(&s->scratch, type, payload, size);
    return net_send_all(p->sock, s->scratch.items, s->scratch.count);
}

static bool peer_error(Server *s, Peer *p, const char *text)
{
    nob_log(WARNING, "%s: %s", key_short(p->pub), text);
    peer_send(s, p, FRAME_ERROR, text, (uint32_t)strlen(text));
    return false;
}

static void server_drop(Server *s, size_t i, const char *why)
{
    Peer *p = &s->peers[i];
    nob_log(INFO, "%s left: %s", key_short(p->pub), why);
    net_close(p->sock);
    sb_free(p->inbox);
    s->peers[i] = s->peers[--s->peers_count];
}

static Peer *server_find(Server *s, const uint8_t pub[KEY_SIZE])
{
    for (size_t i = 0; i < s->peers_count; ++i) {
        Peer *p = &s->peers[i];
        if (p->state == PEER_READY && keys_equal(p->pub, pub)) return p;
    }
    return NULL;
}

// False means "disconnect this peer".
static bool peer_handle(Server *s, Peer *p, Frame *f)
{
    switch (p->state) {
    case PEER_WAIT_HELLO: {
        if (f->type != FRAME_HELLO || f->size != KEY_SIZE) return peer_error(s, p, "expected HELLO");
        memcpy(p->pub, f->payload, KEY_SIZE);
        if (!random_bytes(p->nonce, NONCE_SIZE)) return peer_error(s, p, "server has no randomness");
        if (!peer_send(s, p, FRAME_CHALLENGE, p->nonce, NONCE_SIZE)) return false;
        p->state = PEER_WAIT_AUTH;
        return true;
    }
    case PEER_WAIT_AUTH: {
        if (f->type != FRAME_AUTH || f->size != SIG_SIZE) return peer_error(s, p, "expected AUTH");
        if (!signature_ok(f->payload, p->pub, p->nonce, NONCE_SIZE)) return peer_error(s, p, "bad signature");
        if (server_find(s, p->pub)) return peer_error(s, p, "already logged in");
        if (!peer_send(s, p, FRAME_OK, NULL, 0)) return false;
        p->state = PEER_READY;
        nob_log(INFO, "%s logged in", key_short(p->pub));
        return true;
    }
    case PEER_READY: {
        if (f->type != FRAME_MSG) return peer_error(s, p, "expected MSG");
        Msg msg;
        if (!msg_parse(f->payload, f->size, &msg)) return peer_error(s, p, "malformed MSG");
        if (!keys_equal(msg.from, p->pub)) return peer_error(s, p, "MSG.from is not you");
        if (!signature_ok(msg.sig, msg.from, msg.signed_data, msg.signed_size)) return peer_error(s, p, "bad MSG signature");
        Peer *to = server_find(s, msg.to);
        if (to == NULL) {
            peer_send(s, p, FRAME_ERROR, "offline", 7);
            return true;
        }
        if (!peer_send(s, to, FRAME_MSG, f->payload, f->size)) {
            // The recipient is gone. Its socket will fail on the next poll.
            peer_send(s, p, FRAME_ERROR, "offline", 7);
            return true;
        }
        nob_log(INFO, "%s -> %s (%zu bytes)", key_short(msg.from), key_short(msg.to), msg.text.count);
        return peer_send(s, p, FRAME_OK, NULL, 0);
    }
    default:
        UNREACHABLE("Peer_State");
    }
}

// Reads from one peer and handles every complete frame. False = drop it.
static bool peer_read(Server *s, Peer *p)
{
    if (!net_recv(p->sock, &p->inbox)) return false;
    if (p->inbox.count > FRAME_MAX_SIZE + FRAME_HEADER) return peer_error(s, p, "frame too large");
    for (;;) {
        Frame f;
        size_t consumed;
        int r = frame_parse((uint8_t *)p->inbox.items, p->inbox.count, &f, &consumed);
        if (r < 0) return peer_error(s, p, "garbage");
        if (r == 0) return true;
        if (!peer_handle(s, p, &f)) return false;
        memmove(p->inbox.items, p->inbox.items + consumed, p->inbox.count - consumed);
        p->inbox.count -= consumed;
    }
}

bool server_start(Server *s, uint16_t port)
{
    memset(s, 0, sizeof(*s));
    s->listener = net_listen(port);
    return s->listener != NET_INVALID;
}

// One round of select(): accept newcomers, read from everyone with data.
bool server_step(Server *s, int timeout_ms)
{
    Net_Socket socks[MAX_PEERS + 1];
    bool readable[MAX_PEERS + 1];
    socks[0] = s->listener;
    for (size_t i = 0; i < s->peers_count; ++i) socks[i + 1] = s->peers[i].sock;
    if (!net_wait(socks, readable, s->peers_count + 1, timeout_ms)) return false;

    // Walk backwards so dropping a peer (swap with last) does not skip one.
    for (size_t i = s->peers_count; i > 0; --i) {
        if (!readable[i]) continue;
        if (!peer_read(s, &s->peers[i - 1])) server_drop(s, i - 1, "closed");
    }

    if (readable[0]) {
        Net_Socket sock = net_accept(s->listener);
        if (sock == NET_INVALID) return true;
        if (s->peers_count == MAX_PEERS) {
            nob_log(WARNING, "Server is full, rejecting a connection");
            net_close(sock);
            return true;
        }
        Peer *p = &s->peers[s->peers_count++];
        memset(p, 0, sizeof(*p));
        p->sock = sock;
        nob_log(INFO, "New connection (%zu online)", s->peers_count);
    }
    return true;
}

void server_stop(Server *s)
{
    while (s->peers_count > 0) server_drop(s, s->peers_count - 1, "server stopped");
    net_close(s->listener);
    sb_free(s->scratch);
}

#ifndef CHATD_NO_MAIN
int main(int argc, char **argv)
{
    uint16_t port = DEFAULT_PORT;
    if (argc > 1) port = (uint16_t)atoi(argv[1]);
    if (!net_init()) return 1;

    Server server;
    if (!server_start(&server, port)) return 1;
    nob_log(INFO, "chatd listening on port %u", net_local_port(server.listener));

    for (;;) {
        temp_reset();
        if (!server_step(&server, 250)) break;
    }
    server_stop(&server);
    return 0;
}
#endif // CHATD_NO_MAIN

#endif // CHATD_C_
