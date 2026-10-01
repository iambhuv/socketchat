// db.c - everything the client remembers, in one SQLite file.
// Included by chat.c. Include nob.h, protocol.h, crypto.h and sqlite3.h first.
#ifndef DB_C_
#define DB_C_

#define MESSAGE_LIMIT 200

#ifdef _MSC_VER
#define strdup _strdup
#endif

static const char *DB_SCHEMA =
    "CREATE TABLE IF NOT EXISTS identity (pub BLOB NOT NULL, sec BLOB NOT NULL);"
    "CREATE TABLE IF NOT EXISTS contacts (pub BLOB PRIMARY KEY, nickname TEXT NOT NULL);"
    "CREATE TABLE IF NOT EXISTS messages ("
    "  id INTEGER PRIMARY KEY, contact BLOB NOT NULL, incoming INTEGER NOT NULL,"
    "  timestamp_ms INTEGER NOT NULL, text TEXT NOT NULL);"
    "CREATE INDEX IF NOT EXISTS messages_by_contact ON messages (contact, id);"
    "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);";

typedef struct {
    uint8_t pub[KEY_SIZE];
    char nickname[48];
} Contact;

typedef struct {
    Contact *items;
    size_t count;
    size_t capacity;
} Contacts;

typedef struct {
    bool incoming;
    uint64_t timestamp_ms;
    char *text;
} Message;

typedef struct {
    Message *items;
    size_t count;
    size_t capacity;
} Messages;

bool db_get(sqlite3 *db, const char *key, char *out, size_t cap);
bool db_set(sqlite3 *db, const char *key, const char *value);

typedef enum {
    DB_IDENTITY_OK,
    DB_IDENTITY_WRONG_KEY,
    DB_IDENTITY_ERROR,
} Db_Identity_Result;

void messages_clear(Messages *ms)
{
    da_foreach(Message, m, ms) free(m->text);
    ms->count = 0;
}

static bool db_prepare(sqlite3 *db, sqlite3_stmt **st, const char *sql)
{
    if (sqlite3_prepare_v2(db, sql, -1, st, NULL) != SQLITE_OK) {
        nob_log(ERROR, "SQL error: %s", sqlite3_errmsg(db));
        return false;
    }
    return true;
}

static bool db_run(sqlite3 *db, sqlite3_stmt *st)
{
    int r = sqlite3_step(st);
    if (r != SQLITE_DONE) nob_log(ERROR, "SQL error: %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return r == SQLITE_DONE;
}

bool db_open(sqlite3 **db, const char *path)
{
    if (sqlite3_open(path, db) != SQLITE_OK) {
        nob_log(ERROR, "Could not open %s: %s", path, sqlite3_errmsg(*db));
        return false;
    }
    char *err = NULL;
    if (sqlite3_exec(*db, DB_SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        nob_log(ERROR, "Could not create tables: %s", err);
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool db_has_identity(sqlite3 *db)
{
    sqlite3_stmt *st;
    if (!db_prepare(db, &st, "SELECT 1 FROM identity")) return false;
    bool found = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return found;
}

// The salt for the passphrase, made once per database.
bool db_salt(sqlite3 *db, uint8_t salt[SALT_SIZE])
{
    char hex[SALT_SIZE*2 + 1];
    if (db_get(db, "salt", hex, sizeof(hex))) return hex_decode(salt, SALT_SIZE, sv_from_cstr(hex));
    if (!random_bytes(salt, SALT_SIZE)) return false;
    hex_encode(hex, salt, SALT_SIZE);
    return db_set(db, "salt", hex);
}

// Loads the keypair, decrypting the secret half with `key`.
Db_Identity_Result db_load_identity(sqlite3 *db, const uint8_t key[32], Identity *me)
{
    sqlite3_stmt *st;
    if (!db_prepare(db, &st, "SELECT pub, sec FROM identity")) return DB_IDENTITY_ERROR;
    Db_Identity_Result result = DB_IDENTITY_ERROR;
    if (sqlite3_step(st) != SQLITE_ROW) {
        nob_log(ERROR, "No identity in the database");
    } else if (sqlite3_column_bytes(st, 0) != KEY_SIZE ||
               sqlite3_column_bytes(st, 1) != 64 + LOCKED_OVERHEAD) {
        nob_log(ERROR, "The database is from an older version. Delete it and start again.");
    } else {
        String_Builder sec = {0};
        memcpy(me->pub, sqlite3_column_blob(st, 0), KEY_SIZE);
        if (!unlock_bytes(&sec, key, sqlite3_column_blob(st, 1), 64 + LOCKED_OVERHEAD)) {
            result = DB_IDENTITY_WRONG_KEY;
        } else {
            memcpy(me->sec, sec.items, 64);
            result = identity_is_valid(me) ? DB_IDENTITY_OK : DB_IDENTITY_ERROR;
            if (result != DB_IDENTITY_OK) nob_log(ERROR, "Stored identity is corrupt");
        }
        crypto_wipe(sec.items, sec.capacity);
        sb_free(sec);
    }
    sqlite3_finalize(st);
    return result;
}

// Makes a new keypair on the first run and stores it under `key`.
bool db_create_identity(sqlite3 *db, const uint8_t key[32], Identity *me)
{
    if (!identity_generate(me)) return false;
    String_Builder locked = {0};
    sqlite3_stmt *st;
    bool ok = lock_bytes(&locked, key, me->sec, 64) &&
              db_prepare(db, &st, "INSERT INTO identity (pub, sec) VALUES (?, ?)");
    if (ok) {
        sqlite3_bind_blob(st, 1, me->pub, KEY_SIZE, SQLITE_STATIC);
        sqlite3_bind_blob(st, 2, locked.items, (int)locked.count, SQLITE_STATIC);
        ok = db_run(db, st);
    }
    sb_free(locked);
    if (ok) nob_log(INFO, "Created a new identity %s", key_short(me->pub));
    return ok;
}

bool db_add_contact(sqlite3 *db, const uint8_t pub[KEY_SIZE], const char *nickname)
{
    sqlite3_stmt *st;
    if (!db_prepare(db, &st, "INSERT INTO contacts (pub, nickname) VALUES (?, ?) "
                             "ON CONFLICT(pub) DO UPDATE SET nickname = excluded.nickname")) return false;
    sqlite3_bind_blob(st, 1, pub, KEY_SIZE, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, nickname, -1, SQLITE_STATIC);
    return db_run(db, st);
}

bool db_contacts(sqlite3 *db, Contacts *out)
{
    out->count = 0;
    sqlite3_stmt *st;
    if (!db_prepare(db, &st, "SELECT pub, nickname FROM contacts ORDER BY rowid")) return false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        Contact c = {0};
        if (sqlite3_column_bytes(st, 0) != KEY_SIZE) continue;
        memcpy(c.pub, sqlite3_column_blob(st, 0), KEY_SIZE);
        snprintf(c.nickname, sizeof(c.nickname), "%s", sqlite3_column_text(st, 1));
        da_append(out, c);
    }
    sqlite3_finalize(st);
    return true;
}

// The text is encrypted with `key` before it touches the disk.
bool db_store_message(sqlite3 *db, const uint8_t key[32], const uint8_t contact[KEY_SIZE],
                      bool incoming, uint64_t timestamp_ms, String_View text)
{
    String_Builder locked = {0};
    if (!lock_bytes(&locked, key, (const uint8_t *)text.data, text.count)) return false;
    sqlite3_stmt *st;
    bool ok = db_prepare(db, &st, "INSERT INTO messages (contact, incoming, timestamp_ms, text) "
                                  "VALUES (?, ?, ?, ?)");
    if (ok) {
        sqlite3_bind_blob(st, 1, contact, KEY_SIZE, SQLITE_STATIC);
        sqlite3_bind_int(st, 2, incoming);
        sqlite3_bind_int64(st, 3, (sqlite3_int64)timestamp_ms);
        sqlite3_bind_blob(st, 4, locked.items, (int)locked.count, SQLITE_STATIC);
        ok = db_run(db, st);
    }
    sb_free(locked);
    return ok;
}

// The newest MESSAGE_LIMIT messages of one chat, oldest first.
bool db_messages(sqlite3 *db, const uint8_t key[32], const uint8_t contact[KEY_SIZE], Messages *out)
{
    messages_clear(out);
    sqlite3_stmt *st;
    if (!db_prepare(db, &st, "SELECT incoming, timestamp_ms, text FROM ("
                             "  SELECT id, incoming, timestamp_ms, text FROM messages"
                             "  WHERE contact = ? ORDER BY id DESC LIMIT ?"
                             ") ORDER BY id ASC")) return false;
    sqlite3_bind_blob(st, 1, contact, KEY_SIZE, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, MESSAGE_LIMIT);
    String_Builder plain = {0};
    while (sqlite3_step(st) == SQLITE_ROW) {
        plain.count = 0;
        if (!unlock_bytes(&plain, key, sqlite3_column_blob(st, 2), (size_t)sqlite3_column_bytes(st, 2))) {
            sb_append_cstr(&plain, "[unreadable]");
        }
        sb_append_null(&plain);
        Message m = {
            .incoming = sqlite3_column_int(st, 0) != 0,
            .timestamp_ms = (uint64_t)sqlite3_column_int64(st, 1),
            .text = strdup(plain.items),
        };
        da_append(out, m);
    }
    sb_free(plain);
    sqlite3_finalize(st);
    return true;
}

// False when the key is not stored. `out` is untouched then.
bool db_get(sqlite3 *db, const char *key, char *out, size_t cap)
{
    sqlite3_stmt *st;
    if (!db_prepare(db, &st, "SELECT value FROM settings WHERE key = ?")) return false;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    bool found = sqlite3_step(st) == SQLITE_ROW;
    if (found) snprintf(out, cap, "%s", sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
    return found;
}

bool db_set(sqlite3 *db, const char *key, const char *value)
{
    sqlite3_stmt *st;
    if (!db_prepare(db, &st, "INSERT INTO settings (key, value) VALUES (?, ?) "
                             "ON CONFLICT(key) DO UPDATE SET value = excluded.value")) return false;
    sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, value, -1, SQLITE_STATIC);
    return db_run(db, st);
}

#endif // DB_C_
