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
        c->draft_st.cursor = 0;
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
    c->draft_st.cursor = 0;
}

static void draw_toast(Chat *c)
{
    const Theme *t = theme();
    float left = c->toast_until - GetTime();
    if (left <= 0) return;
    float alpha = left < 0.3f ? left/0.3f : 1;
    Vector2 m = ui_measure(c->toast, UI_SMALL, true);
    Rectangle r = { (GetScreenWidth() - m.x - 48)/2, GetScreenHeight() - 96.0f, m.x + 48, 36 };
    ui_panel(r, 18, Fade(t->text, alpha), BLANK);
    ui_text_centered(c->toast, r, UI_SMALL, Fade(t->bg, alpha), true);
}

// The card both login screens sit in. Returns where the form starts.
static Vector2 login_card(Chat *c, float height, const char *title, const char *subtitle)
{
    const Theme *t = theme();
    float w = GetScreenWidth(), h = GetScreenHeight();
    Rectangle card = { (w - 400)/2, (h - height)/2, 400, height };
    ui_panel(card, 12, t->surface, t->border);
    float x = card.x + 32, y = card.y + 32;
    ui_avatar((Vector2){ x + 20, y + 20 }, 20, c->nickname[0] ? c->nickname : "S", true);
    ui_text(title, x + 56, y + 2, UI_TITLE, t->text, true);
    ui_text(subtitle, x + 56, y + 32, UI_SMALL, t->muted, false);
    ui_text(temp_sprintf("build %s", GIT_HASH), 16, h - 28, UI_CAPTION, t->muted, false);
    return (Vector2){ x, y + 80 };
}

static void login_status(Chat *c, float x, float y)
{
    if (c->status[0]) ui_text(c->status, x, y, UI_SMALL, c->status_bad ? theme()->err : theme()->muted, false);
}

// First run: pick a nickname and a passphrase, make the identity.
static void setup_screen(Chat *c)
{
    const Theme *t = theme();
    Vector2 at = login_card(c, 468, "Socket Chat", temp_sprintf("Server %s", SERVER));
    float x = at.x, y = at.y, width = 400 - 64;

    ui_text("Nickname", x, y, UI_SMALL, t->muted, true);
    bool submit = ui_input(((Rectangle){ x, y + 22, width, UI_CONTROL }), "How your friends know you", c->nickname, &c->nickname_st);
    y += 78;
    ui_text("Passphrase", x, y, UI_SMALL, t->muted, true);
    submit = ui_secret(((Rectangle){ x, y + 22, width, UI_CONTROL }), "Locks your messages on this computer", c->passphrase, &c->passphrase_st) || submit;
    y += 78;
    ui_text("Repeat it. There is no reset.", x, y, UI_SMALL, t->muted, true);
    submit = ui_secret(((Rectangle){ x, y + 22, width, UI_CONTROL }), "", c->repeat, &c->repeat_st) || submit;
    y += 86;

    Rectangle btn = { x, y, width, UI_CONTROL };
    if (ui_button(btn, "Get started", true) || submit) {
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
    login_status(c, x, btn.y + btn.height + 16);
}

// Every later run: the passphrase unlocks the identity and the history.
static void unlock_screen(Chat *c)
{
    const Theme *t = theme();
    Vector2 at = login_card(c, 300, "Welcome back", temp_sprintf("%s on %s", c->nickname, SERVER));
    float x = at.x, y = at.y, width = 400 - 64;

    ui_text("Passphrase", x, y, UI_SMALL, t->muted, true);
    bool submit = ui_secret(((Rectangle){ x, y + 22, width, UI_CONTROL }), "", c->passphrase, &c->passphrase_st);
    y += 86;
    Rectangle btn = { x, y, width, UI_CONTROL };
    if (ui_button(btn, "Unlock", true) || submit) {
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
    login_status(c, x, btn.y + btn.height + 16);
}

static void add_contact_dialog(Chat *c)
{
    const Theme *t = theme();
    float w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, (int)w, (int)h, Fade(ui.dark ? BLACK : t->text, 0.4f));
    Rectangle box = { (w - 440)/2, (h - 328)/2, 440, 328 };
    bool outside = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !ui_hovered(box);
    ui_panel(box, 12, t->surface, t->border);

    float x = box.x + 32, y = box.y + 28;
    ui_text("Add a contact", x, y, UI_HEADING, t->text, true);
    ui_text("Paste the link they sent you and give them a name.", x, y + 30, UI_SMALL, t->muted, false);

    y += 72;
    ui_text("Link", x, y, UI_SMALL, t->muted, true);
    Rectangle link = { x, y + 22, box.width - 64, UI_CONTROL };
    bool submit = ui_input(link, "chat://...", c->add.link, &c->add.link_st);
    y += 86;
    ui_text("Name", x, y, UI_SMALL, t->muted, true);
    Rectangle nick = { x, y + 22, box.width - 64, UI_CONTROL };
    submit = ui_input(nick, "Their nickname", c->add.nick, &c->add.nick_st) || submit;

    Rectangle cancel = { box.x + box.width - 32 - 96 - 8 - 96, box.y + box.height - 32 - UI_CONTROL, 96, UI_CONTROL };
    Rectangle add    = { box.x + box.width - 32 - 96, cancel.y, 96, UI_CONTROL };
    if (ui_button(add, "Add", true) || submit) {
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
    if (ui_button(cancel, "Cancel", false) || IsKeyPressed(KEY_ESCAPE) || outside) c->add.open = false;
    if (c->add.error[0]) ui_text(c->add.error, x, cancel.y + 12, UI_SMALL, t->err, false);
}

static void sidebar(Chat *c)
{
    const Theme *t = theme();
    float h = GetScreenHeight();
    DrawRectangle(0, 0, (int)SIDEBAR_WIDTH, (int)h, t->surface);
    DrawRectangle((int)SIDEBAR_WIDTH - 1, 0, 1, (int)h, t->border);

    ui_avatar((Vector2){ 36, HEADER_HEIGHT/2 }, 18, c->nickname, true);
    ui_text(c->nickname, 64, HEADER_HEIGHT/2 - 18, UI_BODY, t->text, true);
    bool online = c->client.state == CLIENT_READY;
    bool connecting = !online && c->client.state != CLIENT_OFFLINE;
    DrawCircleV((Vector2){ 68, HEADER_HEIGHT/2 + 10 }, 3, online ? t->ok : connecting ? t->warn : t->err);
    ui_text(online ? "online" : connecting ? "connecting" : "offline", 76, HEADER_HEIGHT/2 + 2, UI_CAPTION, t->muted, false);
    if (!online && !connecting) {
        Rectangle retry = { SIDEBAR_WIDTH - 16 - 64, HEADER_HEIGHT/2 - 16, 64, 32 };
        if (ui_button(retry, "Retry", false)) start_login(c);
    }
    ui_hline(0, HEADER_HEIGHT, SIDEBAR_WIDTH);

    // "You" first, then every contact that is not us.
    float y = HEADER_HEIGHT + 8;
    float bottom = h - 8 - UI_CONTROL*2 - 8 - 16;
    for (size_t i = 0; i <= c->contacts.count; ++i) {
        const uint8_t *pub = i == 0 ? c->me.pub : c->contacts.items[i - 1].pub;
        if (i > 0 && keys_equal(pub, c->me.pub)) continue;
        Rectangle r = { 8, y, SIDEBAR_WIDTH - 16, 56 };
        if (r.y + r.height > bottom) break;
        bool selected = c->chat_open && keys_equal(c->peer, pub);
        uint64_t id = UI_ID((int)i);
        bool clicked = ui_click(id, r);
        float hover = ui_anim(id, ui_hovered(r));
        Color fill = selected ? t->accent_soft : ui_lerp(t->surface, t->surface2, hover);
        if (selected || hover > 0.01f) ui_panel(r, UI_RADIUS, fill, BLANK);
        const char *name = contact_name(c, pub);
        ui_avatar((Vector2){ r.x + 28, r.y + 28 }, 18, i == 0 ? "You" : name, selected);
        ui_text(name, r.x + 56, r.y + 11, UI_BODY, t->text, true);
        ui_text(i == 0 ? "Saved messages" : "Contact", r.x + 56, r.y + 31, UI_SMALL, t->muted, false);
        if (clicked) open_chat(c, pub);
        y += 60;
    }

    ui_hline(0, bottom, SIDEBAR_WIDTH);
    Rectangle copy = { 16, h - 16 - UI_CONTROL*2 - 8, SIDEBAR_WIDTH - 32, UI_CONTROL };
    Rectangle add  = { 16, h - 16 - UI_CONTROL, (SIDEBAR_WIDTH - 32 - 8)/2, UI_CONTROL };
    Rectangle mode = { add.x + add.width + 8, add.y, add.width, UI_CONTROL };
    if (ui_button(copy, "Copy my link", false)) {
        char link[LINK_SIZE];
        link_write(link, c->me.pub);
        SetClipboardText(link);
        toast(c, "Link copied. Send it to a friend.");
    }
    if (ui_button(add, "Add contact", true)) {
        memset(&c->add, 0, sizeof(c->add));
        c->add.open = true;
    }
    if (ui_button(mode, ui.dark ? "Light" : "Dark", false)) {
        ui.dark = !ui.dark;
        db_set(c->db, "dark", ui.dark ? "1" : "0");
    }
}

static void conversation(Chat *c)
{
    const Theme *t = theme();
    float w = GetScreenWidth(), h = GetScreenHeight();
    float left = SIDEBAR_WIDTH;

    if (!c->chat_open) {
        Rectangle middle = { left, 0, w - left, h };
        ui_text_centered("Pick a conversation", middle, UI_HEADING, t->text, true);
        middle.y += 40;
        ui_text_centered("Or copy your link and send it to someone you trust.", middle, UI_SMALL, t->muted, false);
        return;
    }

    bool self = keys_equal(c->peer, c->me.pub);
    ui_avatar((Vector2){ left + 36, HEADER_HEIGHT/2 }, 18, self ? "You" : c->peer_name, true);
    ui_text(c->peer_name, left + 64, HEADER_HEIGHT/2 - 18, UI_BODY, t->text, true);
    const char *subtitle = "Notes to yourself";
    if (!self) {
        char number[SAFETY_NUMBER_SIZE];
        safety_number(number, c->me.pub, c->peer);
        subtitle = temp_sprintf("Encrypted. Safety number %s", number);
    }
    ui_text(subtitle, left + 64, HEADER_HEIGHT/2 + 2, UI_CAPTION, t->muted, false);
    ui_hline(left, HEADER_HEIGHT, w - left);

    // Composer at the bottom, messages stacked upwards from it.
    float cy = h - UI_PAD - UI_CONTROL;
    Rectangle send = { w - UI_PAD - 88, cy, 88, UI_CONTROL };
    Rectangle box  = { left + UI_PAD, cy, send.x - 8 - left - UI_PAD, UI_CONTROL };
    bool submit = ui_input(box, "Write a message", c->draft, &c->draft_st);
    if (ui_button(send, c->outgoing.waiting ? "Sending" : "Send", true) || submit) send_draft(c);
    if (c->send_status[0]) ui_text(c->send_status, box.x + 2, box.y - 22, UI_CAPTION, t->muted, false);

    float max_width = (w - left)*0.6f;
    float line_h = ui_line_height(UI_BODY);
    float bottom = box.y - 32;
    for (size_t i = c->messages.count; i > 0; --i) {
        Message *m = &c->messages.items[i - 1];
        const char *when = clock_text(m->timestamp_ms);
        float when_w = ui_measure(when, UI_CAPTION, false).x;
        float text_w = ui_measure(m->text, UI_BODY, false).x;
        float inner = text_w + 12 + when_w;
        if (inner < when_w + 40) inner = when_w + 40;
        float bubble_w = (inner < max_width ? inner : max_width) + 28;
        int lines = ui_wrap(m->text, UI_BODY, bubble_w - 28, (Vector2){0}, t->text, false);
        bool when_fits = lines == 1 && inner <= max_width;
        float bubble_h = lines*line_h + (when_fits ? 0 : 16) + 20;
        float y = bottom - bubble_h;
        if (y < HEADER_HEIGHT - bubble_h) break;

        Rectangle r = { m->incoming ? left + UI_PAD : w - UI_PAD - bubble_w, y, bubble_w, bubble_h };
        Color text = m->incoming ? t->text : WHITE;
        Color faint = m->incoming ? t->muted : Fade(WHITE, 0.7f);
        if (m->incoming) ui_panel(r, 12, t->surface2, t->border);
        else ui_panel(r, 12, t->accent, BLANK);
        ui_wrap(m->text, UI_BODY, bubble_w - 28, (Vector2){ r.x + 14, r.y + 10 }, text, true);
        ui_text(when, r.x + r.width - 14 - when_w, r.y + r.height - 10 - UI_CAPTION, UI_CAPTION, faint, false);
        bottom = y - 8;
    }
    if (c->messages.count == 0) {
        Rectangle middle = { left, HEADER_HEIGHT, w - left, h - HEADER_HEIGHT - 80 };
        ui_text_centered("No messages yet", middle, UI_HEADING, t->text, true);
        middle.y += 36;
        ui_text_centered(self ? "Write something to keep it close." : "They need to be online to receive it.",
                         middle, UI_SMALL, t->muted, false);
    }
}

static void chats_screen(Chat *c)
{
    sidebar(c);
    conversation(c);
    if (c->add.open) add_contact_dialog(c);
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

    CloseWindow();
    client_close(&c.client);
    sqlite3_close(c.db);
    crypto_wipe(&c.me, sizeof(c.me));
    crypto_wipe(c.key, sizeof(c.key));
    return 0;
}
