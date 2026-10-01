// test.c - the checks that run with `./nob test`.
#define NOB_IMPLEMENTATION
#include "nob.h"
#include "protocol.h"
#include "crypto.h"
#include "net.h"
#define CHATD_NO_MAIN
#include "chatd.c"
#include "client.c"

#define CHECK(cond) do { \
        if (!(cond)) { nob_log(ERROR, "%s:%d: CHECK(%s) failed", __FILE__, __LINE__, #cond); return false; } \
    } while (0)

bool test_frames(void)
{
    String_Builder sb = {0};
    frame_append(&sb, FRAME_MSG, "abc", 3);
    frame_append(&sb, FRAME_OK, NULL, 0);
    CHECK(sb.count == FRAME_HEADER + 3 + FRAME_HEADER);

    Frame f;
    size_t used;
    CHECK(frame_parse((uint8_t *)sb.items, sb.count, &f, &used) == 1);
    CHECK(f.type == FRAME_MSG && f.size == 3 && memcmp(f.payload, "abc", 3) == 0);
    CHECK(used == FRAME_HEADER + 3);
    CHECK(frame_parse((uint8_t *)sb.items + used, sb.count - used, &f, &used) == 1);
    CHECK(f.type == FRAME_OK && f.size == 0);

    CHECK(frame_parse((uint8_t *)sb.items, 3, &f, &used) == 0);               // not even a header
    CHECK(frame_parse((uint8_t *)sb.items, FRAME_HEADER + 1, &f, &used) == 0); // header, half a payload

    uint8_t bad[FRAME_HEADER] = { 0xFF, 0xFF, 0xFF, 0xFF, FRAME_OK };
    CHECK(frame_parse(bad, sizeof(bad), &f, &used) == -1);                    // length too big
    uint8_t bad_type[FRAME_HEADER] = { 0, 0, 0, 1, 42 };
    CHECK(frame_parse(bad_type, sizeof(bad_type), &f, &used) == -1);
    sb_free(sb);
    return true;
}

bool test_messages(void)
{
    Identity alice, bob;
    CHECK(identity_generate(&alice) && identity_generate(&bob));
    CHECK(identity_is_valid(&alice));

    String_Builder payload = {0};
    msg_append_unsigned(&payload, alice.pub, bob.pub, 1234, SVLIT("hi bob"));
    uint8_t sig[SIG_SIZE];
    identity_sign(sig, &alice, payload.items, payload.count);
    sb_append_buf(&payload, sig, SIG_SIZE);

    Msg m;
    CHECK(msg_parse((uint8_t *)payload.items, (uint32_t)payload.count, &m));
    CHECK(keys_equal(m.from, alice.pub) && keys_equal(m.to, bob.pub));
    CHECK(m.timestamp_ms == 1234 && sv_eq(m.text, SVLIT("hi bob")));
    CHECK(signature_ok(m.sig, alice.pub, m.signed_data, m.signed_size));
    CHECK(!signature_ok(m.sig, bob.pub, m.signed_data, m.signed_size));
    payload.items[MSG_HEADER] ^= 1;  // flip a bit of the text
    CHECK(msg_parse((uint8_t *)payload.items, (uint32_t)payload.count, &m));
    CHECK(!signature_ok(m.sig, alice.pub, m.signed_data, m.signed_size));
    CHECK(!msg_parse((uint8_t *)payload.items, 10, &m));
    sb_free(payload);
    return true;
}

bool test_links(void)
{
    Identity id;
    CHECK(identity_generate(&id));
    char link[LINK_SIZE];
    link_write(link, id.pub);
    CHECK(strlen(link) == LINK_SIZE - 1);
    uint8_t pub[KEY_SIZE];
    CHECK(link_parse(sv_from_cstr(link), pub) && keys_equal(pub, id.pub));
    CHECK(link_parse(sv_from_cstr(temp_sprintf("  %s\n", link)), pub));
    CHECK(!link_parse(SVLIT("chat://nothex"), pub));
    CHECK(!link_parse(SVLIT("http://example.com"), pub));
    return true;
}

// Steps the server and polls the client until something happens.
static Client_Event pump(Server *s, Client *c, const Identity *me)
{
    for (int i = 0; i < 100; ++i) {
        if (!server_step(s, 10)) break;
        Client_Event e = client_poll(c, me);
        if (e.kind != CLIENT_EVENT_NONE) return e;
    }
    return (Client_Event){0};
}

bool test_end_to_end(void)
{
    Server server;
    CHECK(server_start(&server, 0));
    uint16_t port = net_local_port(server.listener);
    CHECK(port != 0);

    Identity alice, bob;
    CHECK(identity_generate(&alice) && identity_generate(&bob));
    Client a = { .sock = NET_INVALID }, b = { .sock = NET_INVALID };
    CHECK(client_connect(&a, "127.0.0.1", port, &alice));
    CHECK(client_connect(&b, "127.0.0.1", port, &bob));
    CHECK(pump(&server, &a, &alice).kind == CLIENT_EVENT_CONNECTED);
    CHECK(pump(&server, &b, &bob).kind == CLIENT_EVENT_CONNECTED);

    uint64_t ts;
    CHECK(client_send_text(&a, &alice, bob.pub, SVLIT("hello bob"), &ts));
    CHECK(pump(&server, &a, &alice).kind == CLIENT_EVENT_SENT);
    Client_Event e = pump(&server, &b, &bob);
    CHECK(e.kind == CLIENT_EVENT_MESSAGE);
    CHECK(keys_equal(e.msg.from, alice.pub) && sv_eq(e.msg.text, SVLIT("hello bob")));
    CHECK(e.msg.timestamp_ms == ts);

    // Talking to yourself: the server echoes it, the client drops the echo
    // and reports SENT.
    CHECK(client_send_text(&a, &alice, alice.pub, SVLIT("note to self"), &ts));
    CHECK(pump(&server, &a, &alice).kind == CLIENT_EVENT_SENT);
    CHECK(pump(&server, &a, &alice).kind == CLIENT_EVENT_NONE);

    // Nobody home.
    uint8_t nobody[KEY_SIZE] = {0};
    CHECK(client_send_text(&a, &alice, nobody, SVLIT("anyone?"), &ts));
    e = pump(&server, &a, &alice);
    CHECK(e.kind == CLIENT_EVENT_REJECTED && sv_eq(e.error, SVLIT("offline")));

    // Second login with the same key is refused.
    Client a2 = { .sock = NET_INVALID };
    CHECK(client_connect(&a2, "127.0.0.1", port, &alice));
    CHECK(pump(&server, &a2, &alice).kind == CLIENT_EVENT_DISCONNECTED);

    // Bob leaves, alice's next message to him bounces.
    client_close(&b);
    CHECK(server_step(&server, 50)); // let the server notice the closed socket
    CHECK(client_send_text(&a, &alice, bob.pub, SVLIT("still there?"), &ts));
    CHECK(pump(&server, &a, &alice).kind == CLIENT_EVENT_REJECTED);

    client_close(&a);
    server_stop(&server);
    return true;
}

typedef struct { const char *name; bool (*run)(void); } Test;

int main(void)
{
    if (!net_init()) return 1;
    Test tests[] = {
        { "frames",     test_frames },
        { "messages",   test_messages },
        { "links",      test_links },
        { "end_to_end", test_end_to_end },
    };
    size_t failed = 0;
    for (size_t i = 0; i < ARRAY_LEN(tests); ++i) {
        temp_reset();
        bool ok = tests[i].run();
        printf("%-12s %s\n", tests[i].name, ok ? "OK" : "FAILED");
        if (!ok) failed++;
    }
    return failed ? 1 : 0;
}
