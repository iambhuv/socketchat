// chat.c - the desktop client. Two screens: setup and chats.
//
//   ./build/chat [--db path]
//
// The server address is baked in at build time: edit build/server.h.
// The database lives in ~/.socketchat/chat.db unless --db says otherwise.
#define NOB_IMPLEMENTATION
#include "nob.h"
#include <math.h>
#include <raylib.h>
#include "sqlite3.h"
#include "protocol.h"
#include "crypto.h"
#include "net.h"
#include "build.h"
#include "server.h"
#include "bundle.h"
#include "client.c"
#include "db.c"
#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "ui.c"

#define DEFAULT_PORT    5050
#define LOGIN_TIMEOUT   5.0
#define SIDEBAR_WIDTH   280.0f
#define HEADER_HEIGHT   64.0f

typedef enum {
    SCREEN_SETUP,    // first run: nickname and passphrase
    SCREEN_UNLOCK,   // every later run: passphrase
    SCREEN_CHATS,
} Screen;

typedef struct {
    sqlite3 *db;
    Identity me;
    uint8_t key[32];            // from the passphrase, encrypts what is on disk
    uint8_t salt[SALT_SIZE];
    Client client;
    Screen screen;

    // Setup and unlock screens
    char passphrase[64], repeat[64];
    Text_State passphrase_st, repeat_st;

    // Login
    char nickname[32];
    Text_State nickname_st;
    double login_started;       // 0 when not logging in
    char status[128];           // why we are offline, or what is wrong with the form
    bool status_bad;

    // Chats screen
    Contacts contacts;
    bool chat_open;
    uint8_t peer[KEY_SIZE];
    char peer_name[48];
    Messages messages;
    char draft[2048];
    Text_State draft_st;
    char send_status[96];
    struct {
        bool waiting;
        uint8_t to[KEY_SIZE];
        uint64_t timestamp_ms;
        char text[2048];
    } outgoing;
    struct {
        bool open;
        char link[LINK_SIZE + 32];
        char nick[48];
        Text_State link_st, nick_st;
        char error[64];
    } add;
    char toast[96];
    double toast_until;
} Chat;

static void set_status(Chat *c, bool bad, const char *text)
{
    snprintf(c->status, sizeof(c->status), "%s", text);
    c->status_bad = bad;
}

static void toast(Chat *c, const char *text)
{
    snprintf(c->toast, sizeof(c->toast), "%s", text);
    c->toast_until = GetTime() + 2.5;
}

static const char *contact_name(Chat *c, const uint8_t pub[KEY_SIZE])
{
    if (keys_equal(pub, c->me.pub)) return "You";
    da_foreach(Contact, k, &c->contacts) {
        if (keys_equal(k->pub, pub)) return k->nickname;
    }
    return key_short(pub);
}

static const char *clock_text(uint64_t timestamp_ms)
{
    time_t seconds = (time_t)(timestamp_ms/1000);
    struct tm *local = localtime(&seconds);
    return local ? temp_sprintf("%02d:%02d", local->tm_hour, local->tm_min) : "";
}

static void reload_messages(Chat *c)
{
    if (c->chat_open) db_messages(c->db, c->key, c->peer, &c->messages);
}

static void open_chat(Chat *c, const uint8_t pub[KEY_SIZE])
{
    c->chat_open = true;
    memcpy(c->peer, pub, KEY_SIZE);
    snprintf(c->peer_name, sizeof(c->peer_name), "%s", contact_name(c, pub));
    c->send_status[0] = '\0';
    reload_messages(c);
    char hex[KEY_SIZE*2 + 1];
    hex_encode(hex, pub, KEY_SIZE);
    db_set(c->db, "last_chat", hex);
}

// Reopens the chat that was open last time.
static void restore_last_chat(Chat *c)
{
    char hex[KEY_SIZE*2 + 1];
    uint8_t pub[KEY_SIZE];
    if (!db_get(c->db, "last_chat", hex, sizeof(hex))) return;
    if (!hex_decode(pub, KEY_SIZE, sv_from_cstr(hex))) return;
    bool known = keys_equal(pub, c->me.pub);
    da_foreach(Contact, k, &c->contacts) known = known || keys_equal(k->pub, pub);
    if (known) open_chat(c, pub);
}

static void enter_chats(Chat *c)
{
    crypto_wipe(c->passphrase, sizeof(c->passphrase));
    crypto_wipe(c->repeat, sizeof(c->repeat));
    if (c->nickname[0] == '\0') snprintf(c->nickname, sizeof(c->nickname), "me");
    nob_log(INFO, "Identity %s", key_short(c->me.pub));
    c->screen = SCREEN_CHATS;
    db_contacts(c->db, &c->contacts);
    restore_last_chat(c);
}

static void start_login(Chat *c)
{
    if (c->login_started > 0 || c->client.state == CLIENT_READY) return;
    String_View port_text = SVLIT(SERVER);
    String_View host = sv_chop_by_delim(&port_text, ':');
    uint16_t port = port_text.count > 0 ? (uint16_t)atoi(temp_sv_to_cstr(port_text)) : DEFAULT_PORT;

    set_status(c, false, "Connecting...");
    if (!client_connect(&c->client, temp_sv_to_cstr(host), port, &c->me)) {
        set_status(c, true, temp_sprintf("Could not reach %s", SERVER));
        return;
    }
    c->login_started = GetTime();
}

static void handle_events(Chat *c)
{
    if (c->login_started > 0 && GetTime() - c->login_started > LOGIN_TIMEOUT) {
        client_close(&c->client);
        c->login_started = 0;
        set_status(c, true, "The server did not answer");
    }

    for (;;) {
        Client_Event e = client_poll(&c->client, &c->me);
        if (e.kind == CLIENT_EVENT_NONE) break;
        switch (e.kind) {
        case CLIENT_EVENT_CONNECTED:
            c->login_started = 0;
            set_status(c, false, "Connected");
            break;
        case CLIENT_EVENT_MESSAGE: {
            bool known = keys_equal(e.msg.from, c->me.pub);
            da_foreach(Contact, k, &c->contacts) known = known || keys_equal(k->pub, e.msg.from);
            if (!known) {
                db_add_contact(c->db, e.msg.from, key_short(e.msg.from));
                db_contacts(c->db, &c->contacts);
            }
            db_store_message(c->db, c->key, e.msg.from, true, e.msg.timestamp_ms, e.text);
            if (c->chat_open && keys_equal(c->peer, e.msg.from)) reload_messages(c);
            else toast(c, temp_sprintf("Message from %s", contact_name(c, e.msg.from)));
            break;
        }
        case CLIENT_EVENT_SENT:
            if (c->outgoing.waiting) {
                db_store_message(c->db, c->key, c->outgoing.to, false, c->outgoing.timestamp_ms,
                                 sv_from_cstr(c->outgoing.text));
                c->outgoing.waiting = false;
                snprintf(c->send_status, sizeof(c->send_status), "%s",
                         e.stored ? "They are offline. The server will hand it over when they log in." : "");
                if (c->chat_open && keys_equal(c->peer, c->outgoing.to)) reload_messages(c);
            }
            break;
        case CLIENT_EVENT_REJECTED:
            c->outgoing.waiting = false;
            snprintf(c->send_status, sizeof(c->send_status), "Not delivered: %.*s", SV_Arg(e.error));
            break;
        case CLIENT_EVENT_DISCONNECTED:
            c->login_started = 0;
            c->outgoing.waiting = false;
            set_status(c, true, temp_sv_to_cstr(e.error));
            toast(c, temp_sprintf("Disconnected: "SV_Fmt, SV_Arg(e.error)));
            break;
        case CLIENT_EVENT_NONE:
        default:
            UNREACHABLE("Client_Event_Kind");
        }
    }
}

static void send_draft(Chat *c)
{
    String_View text = sv_trim(sv_from_cstr(c->draft));
    if (text.count == 0 || c->outgoing.waiting) return;
    if (keys_equal(c->peer, c->me.pub)) {
        // Notes to yourself never leave the machine.
        db_store_message(c->db, c->key, c->peer, false, (uint64_t)time(NULL)*1000, text);
        reload_messages(c);
        c->draft[0] = '\0';
        return;
    }
    if (c->client.state != CLIENT_READY) {
        snprintf(c->send_status, sizeof(c->send_status), "Not connected");
        return;
    }
    if (!client_send_text(&c->client, &c->me, c->peer, text, &c->outgoing.timestamp_ms)) {
        snprintf(c->send_status, sizeof(c->send_status), "Could not send");
        return;
    }
    c->outgoing.waiting = true;
    memcpy(c->outgoing.to, c->peer, KEY_SIZE);
    snprintf(c->outgoing.text, sizeof(c->outgoing.text), SV_Fmt, SV_Arg(text));
    snprintf(c->send_status, sizeof(c->send_status), "Sending...");
    c->draft[0] = '\0';
}

static void draw_toast(Chat *c)
{
    const Theme *t = theme();
    float left = c->toast_until - GetTime();
    if (left <= 0) return;
    float alpha = left < 0.3f ? left/0.3f : 1;
    CLAY({ .floating = {
               .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 2,
               .attachPoints = { CLAY_ATTACH_POINT_CENTER_BOTTOM, CLAY_ATTACH_POINT_CENTER_BOTTOM },
               .offset = { 0, -64 },
           }, .layout = { .padding = CLAY_PADDING_ALL(12),
               .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
           .backgroundColor = ui_clay_color(Fade(t->text, alpha)), .cornerRadius = CLAY_CORNER_RADIUS(18) }) {
        ui_text(c->toast, UI_SMALL, Fade(t->bg, alpha), true);
    }
}

static void ui_spacer(float height)
{
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(height) } } }) {}
}

static void login_header(Chat *c, const char *title, const char *subtitle)
{
    const Theme *t = theme();
    CLAY({ .layout = { .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 16,
               .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER } } }) {
        ui_avatar(c->nickname[0] ? c->nickname : "S", true);
        CLAY({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2 } }) {
            ui_text(title, UI_TITLE, t->text, true);
            ui_text(subtitle, UI_SMALL, t->muted, false);
        }
    }
}

// First run: pick a nickname and a passphrase, make the identity.
static void setup_screen(Chat *c)
{
    const Theme *t = theme();
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
               .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(400), CLAY_SIZING_FIT(0, 520) },
                   .padding = CLAY_PADDING_ALL(32), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 12 },
               .backgroundColor = ui_clay_color(t->surface), .cornerRadius = CLAY_CORNER_RADIUS(12),
               .border = { .color = ui_clay_color(t->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
            login_header(c, "Socket Chat", temp_sprintf("Server %s", SERVER));
            ui_spacer(16);
            ui_text("Nickname", UI_SMALL, t->muted, true);
            bool submit = ui_input("How your friends know you", c->nickname, &c->nickname_st);
            ui_text("Passphrase", UI_SMALL, t->muted, true);
            submit = ui_secret("Locks your messages on this computer", c->passphrase, &c->passphrase_st) || submit;
            ui_text("Repeat it. There is no reset.", UI_SMALL, t->muted, true);
            submit = ui_secret("", c->repeat, &c->repeat_st) || submit;
            ui_spacer(8);
            if (ui_button("Get started", true) || submit) {
                if (sv_trim(sv_from_cstr(c->nickname)).count == 0) {
                    set_status(c, true, "Pick a nickname first");
                } else if (strcmp(c->passphrase, c->repeat) != 0) {
                    set_status(c, true, "The passphrases do not match");
                } else if (!passphrase_key(c->key, sv_from_cstr(c->passphrase), c->salt) ||
                           !db_create_identity(c->db, c->key, &c->me)) {
                    set_status(c, true, "Could not create your identity, see the log");
                } else {
                    db_set(c->db, "nickname", c->nickname);
                    c->status[0] = '\0';
                    enter_chats(c);
                    start_login(c);
                }
            }
            if (c->status[0]) ui_text(c->status, UI_SMALL, c->status_bad ? t->err : t->muted, false);
        }
    }
}

// Every later run: the passphrase unlocks the identity and the history.
static void unlock_screen(Chat *c)
{
    const Theme *t = theme();
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
               .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
        CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(400), CLAY_SIZING_FIT(0, 360) },
                   .padding = CLAY_PADDING_ALL(32), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 12 },
               .backgroundColor = ui_clay_color(t->surface), .cornerRadius = CLAY_CORNER_RADIUS(12),
               .border = { .color = ui_clay_color(t->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
            login_header(c, "Welcome back", temp_sprintf("%s on %s", c->nickname, SERVER));
            ui_spacer(16);
            ui_text("Passphrase", UI_SMALL, t->muted, true);
            bool submit = ui_secret("", c->passphrase, &c->passphrase_st);
            ui_spacer(8);
            if (ui_button("Unlock", true) || submit) {
                if (!passphrase_key(c->key, sv_from_cstr(c->passphrase), c->salt)) {
                    set_status(c, true, "Could not derive the key, see the log");
                } else switch (db_load_identity(c->db, c->key, &c->me)) {
                case DB_IDENTITY_OK:
                    c->status[0] = '\0';
                    enter_chats(c);
                    start_login(c);
                    break;
                case DB_IDENTITY_WRONG_KEY:
                    set_status(c, true, "Wrong passphrase");
                    break;
                case DB_IDENTITY_ERROR:
                default:
                    set_status(c, true, "The database cannot be read, see the log");
                    break;
                }
            }
            if (c->status[0]) ui_text(c->status, UI_SMALL, c->status_bad ? t->err : t->muted, false);
        }
    }
}

static void add_contact_dialog(Chat *c)
{
    const Theme *t = theme();
    CLAY({ .floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 1,
               .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER } },
           .layout = { .sizing = { CLAY_SIZING_FIXED(440), CLAY_SIZING_FIT(0, 380) },
               .padding = CLAY_PADDING_ALL(32), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 12 },
           .backgroundColor = ui_clay_color(t->surface), .cornerRadius = CLAY_CORNER_RADIUS(12),
           .border = { .color = ui_clay_color(t->border), .width = CLAY_BORDER_OUTSIDE(1) } }) {
        ui_text("Add a contact", UI_HEADING, t->text, true);
        ui_text("Paste the link they sent you and give them a name.", UI_SMALL, t->muted, false);
        ui_spacer(8);
        ui_text("Link", UI_SMALL, t->muted, true);
        bool submit = ui_input("chat://...", c->add.link, &c->add.link_st);
        ui_text("Name", UI_SMALL, t->muted, true);
        submit = ui_input("Their nickname", c->add.nick, &c->add.nick_st) || submit;
        if (c->add.error[0]) ui_text(c->add.error, UI_SMALL, t->err, false);
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8 } }) {
            if (ui_button("Cancel", false) || IsKeyPressed(KEY_ESCAPE)) c->add.open = false;
            if (ui_button("Add", true) || submit) {
                uint8_t pub[KEY_SIZE];
                String_View nickname = sv_trim(sv_from_cstr(c->add.nick));
                if (!link_parse(sv_from_cstr(c->add.link), pub)) {
                    snprintf(c->add.error, sizeof(c->add.error), "That is not a Socket Chat link");
                } else if (nickname.count == 0) {
                    snprintf(c->add.error, sizeof(c->add.error), "Give them a name");
                } else {
                    db_add_contact(c->db, pub, temp_sv_to_cstr(nickname));
                    db_contacts(c->db, &c->contacts);
                    c->add.open = false;
                    toast(c, "Contact added");
                }
            }
        }
    }
}

static void sidebar(Chat *c)
{
    const Theme *t = theme();
    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(SIDEBAR_WIDTH), CLAY_SIZING_GROW(0) },
               .padding = CLAY_PADDING_ALL(16), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8 },
           .backgroundColor = ui_clay_color(t->surface),
           .border = { .color = ui_clay_color(t->border), .width = { .right = 1 } } }) {
        bool online = c->client.state == CLIENT_READY;
        bool connecting = !online && c->client.state != CLIENT_OFFLINE;
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 10,
                   .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER } } }) {
            ui_avatar(c->nickname, true);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0, 64) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
                ui_text(c->nickname, UI_BODY, t->text, true);
                ui_text(online ? "online" : connecting ? "connecting" : "offline", UI_CAPTION, t->muted, false);
            }
        }
        if (!online && !connecting && ui_button("Retry", false)) start_login(c);
        ui_hline();
        CLAY({ .id = CLAY_ID("Contacts"), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
                   .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4 },
               .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
            for (size_t i = 0; i <= c->contacts.count; ++i) {
                const uint8_t *pub = i == 0 ? c->me.pub : c->contacts.items[i - 1].pub;
                if (i > 0 && keys_equal(pub, c->me.pub)) continue;
                bool selected = c->chat_open && keys_equal(c->peer, pub);
                uint64_t id = UI_ID((int)i);
                bool hover = ui_hovered(id);
                const char *name = contact_name(c, pub);
                CLAY({ .id = ui_clay_id("widget", id), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(56) },
                           .padding = CLAY_PADDING_ALL(8), .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 10,
                           .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER } },
                       .backgroundColor = ui_clay_color(selected ? t->accent_soft : hover ? t->surface2 : t->surface),
                       .cornerRadius = CLAY_CORNER_RADIUS(UI_RADIUS) }) {
                    ui_avatar(i == 0 ? "You" : name, selected);
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
                        ui_text(name, UI_BODY, t->text, true);
                        ui_text(i == 0 ? "Saved messages" : "Contact", UI_SMALL, t->muted, false);
                    }
                }
                if (ui_click(id)) open_chat(c, pub);
            }
        }
        ui_hline();
        if (ui_button("Copy my link", false)) {
            char link[LINK_SIZE];
            link_write(link, c->me.pub);
            SetClipboardText(link);
            toast(c, "Link copied. Send it to a friend.");
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8 } }) {
            if (ui_button("Add contact", true)) { memset(&c->add, 0, sizeof(c->add)); c->add.open = true; }
            if (ui_button(ui.dark ? "Light" : "Dark", false)) { ui.dark = !ui.dark; db_set(c->db, "dark", ui.dark ? "1" : "0"); }
        }
    }
}

static void conversation(Chat *c)
{
    const Theme *t = theme();
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM },
           .backgroundColor = ui_clay_color(t->bg) }) {
        if (!c->chat_open) {
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                       .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .childGap = 8 } }) {
                ui_text("Pick a conversation", UI_HEADING, t->text, true);
                ui_text("Or copy your link and send it to someone you trust.", UI_SMALL, t->muted, false);
            }
        } else {
        bool self = keys_equal(c->peer, c->me.pub);
        const char *subtitle = "Notes to yourself";
        if (!self) { char number[SAFETY_NUMBER_SIZE]; safety_number(number, c->me.pub, c->peer); subtitle = temp_sprintf("Encrypted. Safety number %s", number); }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(72) }, .padding = CLAY_PADDING_ALL(16),
                   .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 12, .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER } } }) {
            ui_avatar(self ? "You" : c->peer_name, true);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM } }) { ui_text(c->peer_name, UI_BODY, t->text, true); ui_text(subtitle, UI_CAPTION, t->muted, false); }
        }
        ui_hline();
        CLAY({ .id = CLAY_ID("Messages"), .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16),
                   .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8 }, .clip = { .vertical = true, .childOffset = Clay_GetScrollOffset() } }) {
            if (c->messages.count == 0) {
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
                           .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER }, .childGap = 8 } }) {
                    ui_text("No messages yet", UI_HEADING, t->text, true);
                    ui_text(self ? "Write something to keep it close." : "They need to be online to receive it.", UI_SMALL, t->muted, false);
                }
            }
            for (size_t i = 0; i < c->messages.count; ++i) {
                Message *m = &c->messages.items[i];
                Color fill = m->incoming ? t->surface2 : t->accent;
                Color ink = m->incoming ? t->text : WHITE;
                CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0, 400) },
                           .childAlignment = { m->incoming ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_TOP } } }) {
                    CLAY({ .layout = { .sizing = { CLAY_SIZING_FIT(0, 420), CLAY_SIZING_FIT(0, 1000) }, .padding = CLAY_PADDING_ALL(12), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6 },
                           .backgroundColor = ui_clay_color(fill), .cornerRadius = CLAY_CORNER_RADIUS(12),
                           .border = { .color = ui_clay_color(m->incoming ? t->border : fill), .width = CLAY_BORDER_OUTSIDE(1) } }) {
                        ui_text(m->text, UI_BODY, ink, false);
                        ui_text(clock_text(m->timestamp_ms), UI_CAPTION, m->incoming ? t->muted : Fade(WHITE, 0.7f), false);
                    }
                }
            }
        }
        CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(UI_PAD), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4 } }) {
            if (c->send_status[0]) ui_text(c->send_status, UI_CAPTION, t->muted, false);
            CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8, .childAlignment = { CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER } } }) {
                bool submit = ui_input("Write a message", c->draft, &c->draft_st);
                CLAY({ .layout = { .sizing = { CLAY_SIZING_FIXED(88), CLAY_SIZING_FIXED(UI_CONTROL) } } }) {
                    if (ui_button(c->outgoing.waiting ? "Sending" : "Send", true) || submit) send_draft(c);
                }
            }
        }
        }
    }
}

static void chats_screen(Chat *c)
{
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
               .layoutDirection = CLAY_LEFT_TO_RIGHT } }) {
        sidebar(c);
        conversation(c);
        if (c->add.open) add_contact_dialog(c);
    }
}

// ~/.socketchat/chat.db, created on first run. Falls back to the current
// directory when there is no home.
static const char *default_db_path(void)
{
#ifdef _WIN32
    const char *home = getenv("APPDATA");
#else
    const char *home = getenv("HOME");
#endif
    if (!home) return "chat.db";
    const char *dir = temp_sprintf("%s/.socketchat", home);
    if (!file_exists(dir) && !mkdir_if_not_exists(dir)) return "chat.db";
    return temp_sprintf("%s/chat.db", dir);
}

int main(int argc, char **argv)
{
    const char *db_path = NULL;
    if (argc == 3 && strcmp(argv[1], "--db") == 0) db_path = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "Usage: %s [--db path]\n", argv[0]);
        return 1;
    }
    if (!db_path) db_path = default_db_path();

    Chat c = { .client = { .sock = NET_INVALID } };
    if (!net_init()) return 1;
    if (!db_open(&c.db, db_path)) return 1;
    if (!db_salt(c.db, c.salt)) return 1;
    db_get(c.db, "nickname", c.nickname, sizeof(c.nickname));
    char dark[8] = "0";
    db_get(c.db, "dark", dark, sizeof(dark));
    ui.dark = dark[0] == '1';
    c.screen = db_has_identity(c.db) ? SCREEN_UNLOCK : SCREEN_SETUP;
    nob_log(INFO, "Database %s, server %s", db_path, SERVER);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(960, 600, "Socket Chat");
    SetWindowMinSize(720, 480);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    ui_init(ui_load_font(font_regular, font_regular_size), ui_load_font(font_bold, font_bold_size));

    while (!WindowShouldClose()) {
        temp_reset();
        handle_events(&c);

        BeginDrawing();
        ui_begin();
        ClearBackground(theme()->bg);
        switch (c.screen) {
        case SCREEN_SETUP:  setup_screen(&c);  break;
        case SCREEN_UNLOCK: unlock_screen(&c); break;
        case SCREEN_CHATS:  chats_screen(&c);  break;
        }
        draw_toast(&c);
        ui_end();
        EndDrawing();
    }

    ui_shutdown();
    CloseWindow();
    client_close(&c.client);
    sqlite3_close(c.db);
    crypto_wipe(&c.me, sizeof(c.me));
    crypto_wipe(c.key, sizeof(c.key));
    return 0;
}
