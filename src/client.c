// client.c - the client side of the protocol, with no UI in it.
//
// Usage: client_connect() once, then client_poll() every frame until it
// returns CLIENT_EVENT_NONE. Nothing here blocks except the TCP connect,
// which has a deadline.
#ifndef CLIENT_C_
#define CLIENT_C_

#define CLIENT_CONNECT_TIMEOUT_MS 3000

typedef enum {
    CLIENT_OFFLINE,
    CLIENT_WAIT_CHALLENGE,
    CLIENT_WAIT_OK,
    CLIENT_READY,
} Client_State;

typedef struct {
    Net_Socket sock;
    Client_State state;
    String_Builder inbox;   // received bytes not parsed yet
    String_Builder outbox;  // scratch for encoding one frame
    String_Builder plain;   // decrypted text of the last MESSAGE event
    size_t consumed;        // bytes of inbox handed out by the last poll
    bool waiting_for_ok;    // a MSG was sent and the server has not replied
} Client;

typedef enum {
    CLIENT_EVENT_NONE,
    CLIENT_EVENT_CONNECTED,     // login finished
    CLIENT_EVENT_MESSAGE,       // .msg and .text are valid until the next client_poll
    CLIENT_EVENT_SENT,          // the server took the last message, see .stored
    CLIENT_EVENT_REJECTED,      // the server refused the last message, see .error
    CLIENT_EVENT_DISCONNECTED,  // the socket is closed, see .error
} Client_Event_Kind;

typedef struct {
    Client_Event_Kind kind;
    Msg msg;
    String_View text;    // decrypted
    String_View error;
    bool stored;         // SENT: kept on the server until they log in
} Client_Event;

static bool client_send(Client *c, Frame_Type type, const void *payload, uint32_t size)
{
    c->outbox.count = 0;
    frame_append(&c->outbox, type, payload, size);
    return net_send_all(c->sock, c->outbox.items, c->outbox.count);
}

void client_close(Client *c)
{
    net_close(c->sock);
    c->sock = NET_INVALID;
    c->state = CLIENT_OFFLINE;
    c->inbox.count = 0;
    c->consumed = 0;
    c->waiting_for_ok = false;
}

bool client_connect(Client *c, const char *host, uint16_t port, const Identity *me)
{
    client_close(c);
    c->sock = net_connect(host, port, CLIENT_CONNECT_TIMEOUT_MS);
    if (c->sock == NET_INVALID) return false;
    if (!client_send(c, FRAME_HELLO, me->pub, KEY_SIZE)) {
        client_close(c);
        return false;
    }
    c->state = CLIENT_WAIT_CHALLENGE;
    return true;
}

bool client_send_text(Client *c, const Identity *me, const uint8_t to[KEY_SIZE],
                      String_View text, uint64_t *timestamp_ms)
{
    if (c->state != CLIENT_READY || c->waiting_for_ok) return false;
    if (text.count == 0 || text.count + PAD_BLOCK > MSG_MAX_TEXT) return false;
    *timestamp_ms = (uint64_t)time(NULL)*1000;
    Box box;
    String_Builder padded = {0}, cipher = {0}, payload = {0};
    pad_append(&padded, (const uint8_t *)text.data, text.count);
    bool ok = box_seal(&box, &cipher, to, (const uint8_t *)padded.items, padded.count);
    if (ok) {
        msg_append_unsigned(&payload, me->pub, to, *timestamp_ms, &box,
                            (uint8_t *)cipher.items, (uint32_t)cipher.count);
        uint8_t sig[SIG_SIZE];
        identity_sign(sig, me, payload.items, payload.count);
        sb_append_buf(&payload, sig, SIG_SIZE);
        ok = client_send(c, FRAME_MSG, payload.items, (uint32_t)payload.count);
    }
    sb_free(padded);
    sb_free(cipher);
    sb_free(payload);
    if (ok) c->waiting_for_ok = true;
    return ok;
}

static Client_Event client_event(Client_Event_Kind kind, const char *error)
{
    Client_Event e = { .kind = kind, .error = sv_from_cstr(error) };
    return e;
}

static Client_Event client_fail(Client *c, const char *error)
{
    nob_log(ERROR, "%s", error);
    client_close(c);
    return client_event(CLIENT_EVENT_DISCONNECTED, error);
}

static Client_Event client_handle(Client *c, const Identity *me, Frame *f)
{
    switch (c->state) {
    case CLIENT_WAIT_CHALLENGE: {
        if (f->type == FRAME_ERROR) return client_fail(c, temp_sprintf("Server said: %.*s", (int)f->size, f->payload));
        if (f->type != FRAME_CHALLENGE || f->size != NONCE_SIZE) return client_fail(c, "Expected a CHALLENGE");
        uint8_t sig[SIG_SIZE];
        identity_sign(sig, me, f->payload, f->size);
        if (!client_send(c, FRAME_AUTH, sig, SIG_SIZE)) return client_fail(c, "Could not send AUTH");
        c->state = CLIENT_WAIT_OK;
        return client_event(CLIENT_EVENT_NONE, "");
    }
    case CLIENT_WAIT_OK: {
        if (f->type == FRAME_ERROR) return client_fail(c, temp_sprintf("Login refused: %.*s", (int)f->size, f->payload));
        if (f->type != FRAME_OK) return client_fail(c, "Expected OK after AUTH");
        c->state = CLIENT_READY;
        return client_event(CLIENT_EVENT_CONNECTED, "");
    }
    case CLIENT_READY: {
        if (f->type == FRAME_OK) {
            c->waiting_for_ok = false;
            Client_Event e = client_event(CLIENT_EVENT_SENT, "");
            e.stored = f->size == 1 && f->payload[0] == 2;
            return e;
        }
        if (f->type == FRAME_ERROR) {
            c->waiting_for_ok = false;
            Client_Event e = client_event(CLIENT_EVENT_REJECTED, "");
            e.error = sv_from_parts((const char *)f->payload, f->size);
            return e;
        }
        if (f->type == FRAME_MSG) {
            Client_Event e = client_event(CLIENT_EVENT_MESSAGE, "");
            if (!msg_parse(f->payload, f->size, &e.msg)) return client_fail(c, "Malformed MSG from server");
            if (!keys_equal(e.msg.to, me->pub)) return client_fail(c, "MSG addressed to someone else");
            if (!signature_ok(e.msg.sig, e.msg.from, e.msg.signed_data, e.msg.signed_size)) {
                nob_log(WARNING, "Dropping MSG from %s with a bad signature", key_short(e.msg.from));
                return client_event(CLIENT_EVENT_NONE, "");
            }
            c->plain.count = 0;
            if (!box_open(&c->plain, &e.msg.box, me, e.msg.cipher, e.msg.cipher_size) || !unpad(&c->plain)) {
                nob_log(WARNING, "Dropping MSG from %s that was not sealed to us", key_short(e.msg.from));
                return client_event(CLIENT_EVENT_NONE, "");
            }
            e.text = sb_to_sv(c->plain);
            return e;
        }
        return client_fail(c, temp_sprintf("Unexpected %s frame", frame_type_name(f->type)));
    }
    case CLIENT_OFFLINE:
    default:
        UNREACHABLE("Client_State");
    }
}

static void client_drop_consumed(Client *c)
{
    memmove(c->inbox.items, c->inbox.items + c->consumed, c->inbox.count - c->consumed);
    c->inbox.count -= c->consumed;
    c->consumed = 0;
}

Client_Event client_poll(Client *c, const Identity *me)
{
    if (c->state == CLIENT_OFFLINE) return client_event(CLIENT_EVENT_NONE, "");
    client_drop_consumed(c); // the frame handed out last time

    bool received = false;
    for (;;) {
        Frame f;
        size_t consumed;
        int r = frame_parse((uint8_t *)c->inbox.items, c->inbox.count, &f, &consumed);
        if (r < 0) return client_fail(c, "Garbage from server");
        if (r > 0) {
            c->consumed = consumed;
            Client_Event e = client_handle(c, me, &f);
            if (e.kind != CLIENT_EVENT_NONE) return e; // .msg points into inbox until the next poll
            client_drop_consumed(c);                   // handled silently, look at the next one
            continue;
        }
        if (received) break; // read once per poll, do not spin
        bool readable;
        if (!net_wait(&c->sock, &readable, 1, 0)) return client_fail(c, "Socket error");
        if (!readable) break;
        if (!net_recv(c->sock, &c->inbox)) return client_fail(c, "Connection closed");
        received = true;
    }
    return client_event(CLIENT_EVENT_NONE, "");
}

#endif // CLIENT_C_
