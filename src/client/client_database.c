#include "client/client_database.h"
#include "protocol/protocol.h"

#include <string.h>

static const char CLIENT_DATABASE_SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS client_identity ("
    "singleton INTEGER PRIMARY KEY CHECK (singleton = 1),"
    "public_key BLOB NOT NULL CHECK (length(public_key) = 32),"
    "secret_key BLOB NOT NULL CHECK (length(secret_key) = 64)"
    ");"
    "CREATE TABLE IF NOT EXISTS trusted_server ("
    "host TEXT NOT NULL,"
    "port INTEGER NOT NULL,"
    "public_key BLOB NOT NULL CHECK (length(public_key) = 32),"
    "PRIMARY KEY (host, port)"
    ");"
    "CREATE TABLE IF NOT EXISTS contacts ("
    "public_key BLOB PRIMARY KEY CHECK (length(public_key) = 32),"
    "link TEXT NOT NULL UNIQUE"
    ");"
    "CREATE TABLE IF NOT EXISTS client_account ("
    "singleton INTEGER PRIMARY KEY CHECK (singleton = 1),"
    "username TEXT NOT NULL"
    ");";

static int client_database_hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static ClientDatabaseStatus client_database_load_identity(
    ClientDatabase *database,
    ClientIdentity *identity,
    bool *was_found)
{
    static const char query[] =
        "SELECT public_key, secret_key FROM client_identity WHERE singleton = 1;";
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (sqlite3_prepare_v2(
            database->connection,
            query,
            -1,
            &statement,
            NULL) != SQLITE_OK)
    {
        return CLIENT_DATABASE_READ_FAILED;
    }

    step_result = sqlite3_step(statement);
    if (step_result == SQLITE_DONE)
    {
        *was_found = false;
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_OK;
    }

    if (step_result != SQLITE_ROW ||
        sqlite3_column_bytes(statement, 0) != crypto_sign_PUBLICKEYBYTES ||
        sqlite3_column_bytes(statement, 1) != crypto_sign_SECRETKEYBYTES)
    {
        sqlite3_finalize(statement);
        return step_result == SQLITE_ROW
                   ? CLIENT_DATABASE_CORRUPT_IDENTITY
                   : CLIENT_DATABASE_READ_FAILED;
    }

    memcpy(
        identity->public_key,
        sqlite3_column_blob(statement, 0),
        sizeof(identity->public_key));
    memcpy(
        identity->secret_key,
        sqlite3_column_blob(statement, 1),
        sizeof(identity->secret_key));
    sqlite3_finalize(statement);

    if (!client_identity_is_valid(identity))
    {
        client_identity_clear(identity);
        return CLIENT_DATABASE_CORRUPT_IDENTITY;
    }

    *was_found = true;
    return CLIENT_DATABASE_OK;
}

static ClientDatabaseStatus client_database_store_identity(
    ClientDatabase *database,
    const ClientIdentity *identity)
{
    static const char insert[] =
        "INSERT INTO client_identity (singleton, public_key, secret_key) "
        "VALUES (1, ?, ?);";
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (sqlite3_prepare_v2(
            database->connection,
            insert,
            -1,
            &statement,
            NULL) != SQLITE_OK)
    {
        return CLIENT_DATABASE_WRITE_FAILED;
    }

    if (sqlite3_bind_blob(
            statement,
            1,
            identity->public_key,
            sizeof(identity->public_key),
            SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_blob(
            statement,
            2,
            identity->secret_key,
            sizeof(identity->secret_key),
            SQLITE_STATIC) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_WRITE_FAILED;
    }

    step_result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    return step_result == SQLITE_DONE
               ? CLIENT_DATABASE_OK
               : CLIENT_DATABASE_WRITE_FAILED;
}

ClientDatabaseStatus client_database_open(
    ClientDatabase *database,
    const char *path)
{
    if (database == NULL || path == NULL || path[0] == '\0')
    {
        return CLIENT_DATABASE_INVALID_ARGUMENT;
    }

    database->connection = NULL;
    if (sqlite3_open(path, &database->connection) != SQLITE_OK)
    {
        client_database_close(database);
        return CLIENT_DATABASE_OPEN_FAILED;
    }

    if (sqlite3_exec(
            database->connection,
            CLIENT_DATABASE_SCHEMA,
            NULL,
            NULL,
            NULL) != SQLITE_OK)
    {
        client_database_close(database);
        return CLIENT_DATABASE_SCHEMA_FAILED;
    }

    return CLIENT_DATABASE_OK;
}

void client_database_close(ClientDatabase *database)
{
    if (database != NULL && database->connection != NULL)
    {
        sqlite3_close(database->connection);
        database->connection = NULL;
    }
}

ClientDatabaseStatus client_database_load_or_create_identity(
    ClientDatabase *database,
    ClientIdentity *identity,
    bool *was_created)
{
    ClientDatabaseStatus status;
    bool was_found;

    if (database == NULL || database->connection == NULL || identity == NULL ||
        was_created == NULL)
    {
        return CLIENT_DATABASE_INVALID_ARGUMENT;
    }

    *was_created = false;
    status = client_database_load_identity(database, identity, &was_found);
    if (status != CLIENT_DATABASE_OK || was_found)
    {
        return status;
    }

    if (!client_identity_generate(identity))
    {
        return CLIENT_DATABASE_KEY_GENERATION_FAILED;
    }

    status = client_database_store_identity(database, identity);
    if (status != CLIENT_DATABASE_OK)
    {
        client_identity_clear(identity);
        return status;
    }

    *was_created = true;
    return CLIENT_DATABASE_OK;
}

ClientDatabaseStatus client_database_trust_server(
    ClientDatabase *database,
    const char *host,
    uint16_t port,
    const unsigned char public_key[crypto_sign_PUBLICKEYBYTES])
{
    sqlite3_stmt *statement = NULL;
    int result;

    if (database == NULL || database->connection == NULL || host == NULL ||
        host[0] == '\0' || port == 0 || public_key == NULL)
    {
        return CLIENT_DATABASE_INVALID_ARGUMENT;
    }

    if (sqlite3_prepare_v2(database->connection,
            "SELECT public_key FROM trusted_server WHERE host = ? AND port = ?;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, host, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_int(statement, 2, (int)port) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_READ_FAILED;
    }

    result = sqlite3_step(statement);
    if (result == SQLITE_ROW)
    {
        const void *stored_key = sqlite3_column_blob(statement, 0);
        const int stored_size = sqlite3_column_bytes(statement, 0);
        const bool matches = stored_size == crypto_sign_PUBLICKEYBYTES &&
            sodium_memcmp(stored_key, public_key, crypto_sign_PUBLICKEYBYTES) == 0;

        sqlite3_finalize(statement);
        return matches ? CLIENT_DATABASE_OK : CLIENT_DATABASE_SERVER_IDENTITY_MISMATCH;
    }
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE)
    {
        return CLIENT_DATABASE_READ_FAILED;
    }

    if (sqlite3_prepare_v2(database->connection,
            "INSERT INTO trusted_server (host, port, public_key) VALUES (?, ?, ?);",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, host, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_int(statement, 2, (int)port) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 3, public_key, crypto_sign_PUBLICKEYBYTES,
                          SQLITE_STATIC) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_WRITE_FAILED;
    }

    result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    return result == SQLITE_DONE ? CLIENT_DATABASE_OK : CLIENT_DATABASE_WRITE_FAILED;
}

ClientDatabaseStatus client_database_add_contact_link(
    ClientDatabase *database,
    const char *link)
{
    static const char prefix[] = "socketchat://contact/";
    uint8_t public_key[crypto_sign_PUBLICKEYBYTES];
    sqlite3_stmt *statement = NULL;
    size_t index;
    int result;

    if (database == NULL || database->connection == NULL || link == NULL ||
        strncmp(link, prefix, sizeof(prefix) - 1u) != 0 ||
        strlen(link) != (sizeof(prefix) - 1u) + crypto_sign_PUBLICKEYBYTES * 2u)
    {
        return CLIENT_DATABASE_INVALID_CONTACT_LINK;
    }

    for (index = 0; index < crypto_sign_PUBLICKEYBYTES; ++index)
    {
        const int high = client_database_hex_value(link[sizeof(prefix) - 1u + index * 2u]);
        const int low = client_database_hex_value(link[sizeof(prefix) + index * 2u]);
        if (high < 0 || low < 0)
        {
            return CLIENT_DATABASE_INVALID_CONTACT_LINK;
        }
        public_key[index] = (uint8_t)((high << 4) | low);
    }

    if (sqlite3_prepare_v2(database->connection,
            "INSERT OR IGNORE INTO contacts (public_key, link) VALUES (?, ?);",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 1, public_key, sizeof(public_key), SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, link, -1, SQLITE_TRANSIENT) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_WRITE_FAILED;
    }
    result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    return result == SQLITE_DONE ? CLIENT_DATABASE_OK : CLIENT_DATABASE_WRITE_FAILED;
}

ClientDatabaseStatus client_database_contact_count(ClientDatabase *database, uint32_t *count)
{
    sqlite3_stmt *statement = NULL;
    int result;

    if (database == NULL || database->connection == NULL || count == NULL)
    {
        return CLIENT_DATABASE_INVALID_ARGUMENT;
    }
    if (sqlite3_prepare_v2(database->connection, "SELECT count(*) FROM contacts;", -1,
                           &statement, NULL) != SQLITE_OK)
    {
        return CLIENT_DATABASE_READ_FAILED;
    }
    result = sqlite3_step(statement);
    if (result != SQLITE_ROW || sqlite3_column_int64(statement, 0) > UINT32_MAX)
    {
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_READ_FAILED;
    }
    *count = (uint32_t)sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return CLIENT_DATABASE_OK;
}

ClientDatabaseStatus client_database_list_contacts(
    ClientDatabase *database,
    ClientContact *contacts,
    uint32_t capacity,
    uint32_t *count)
{
    sqlite3_stmt *statement = NULL;
    uint32_t loaded = 0;
    int result;

    if (database == NULL || database->connection == NULL || contacts == NULL ||
        capacity == 0 || count == NULL)
    {
        return CLIENT_DATABASE_INVALID_ARGUMENT;
    }

    if (sqlite3_prepare_v2(database->connection,
            "SELECT public_key FROM contacts ORDER BY rowid LIMIT ?;", -1,
            &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_int(statement, 1, (int)capacity) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_READ_FAILED;
    }

    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        const void *public_key = sqlite3_column_blob(statement, 0);

        if (sqlite3_column_bytes(statement, 0) != crypto_sign_PUBLICKEYBYTES ||
            public_key == NULL || loaded == capacity)
        {
            sqlite3_finalize(statement);
            return CLIENT_DATABASE_READ_FAILED;
        }
        memcpy(contacts[loaded].public_key, public_key,
               sizeof(contacts[loaded].public_key));
        ++loaded;
    }

    sqlite3_finalize(statement);
    if (result != SQLITE_DONE)
    {
        return CLIENT_DATABASE_READ_FAILED;
    }

    *count = loaded;
    return CLIENT_DATABASE_OK;
}

ClientDatabaseStatus client_database_ensure_self_contact(
    ClientDatabase *database,
    const ClientIdentity *identity)
{
    static const char prefix[] = "socketchat://contact/";
    static const char hex[] = "0123456789abcdef";
    char link[(sizeof(prefix) - 1u) + crypto_sign_PUBLICKEYBYTES * 2u + 1u];
    size_t index;

    if (identity == NULL || !client_identity_is_valid(identity))
    {
        return CLIENT_DATABASE_INVALID_ARGUMENT;
    }
    memcpy(link, prefix, sizeof(prefix) - 1u);
    for (index = 0; index < crypto_sign_PUBLICKEYBYTES; ++index)
    {
        link[sizeof(prefix) - 1u + index * 2u] = hex[identity->public_key[index] >> 4];
        link[sizeof(prefix) + index * 2u] = hex[identity->public_key[index] & 0x0fu];
    }
    link[sizeof(link) - 1u] = '\0';
    return client_database_add_contact_link(database, link);
}

ClientDatabaseStatus client_database_save_account_username(
    ClientDatabase *database, const char *username)
{
    sqlite3_stmt *statement = NULL;
    int result;

    if (database == NULL || database->connection == NULL || username == NULL ||
        !protocol_username_is_valid(username, strlen(username)))
    {
        return CLIENT_DATABASE_INVALID_ARGUMENT;
    }
    if (sqlite3_prepare_v2(database->connection,
            "INSERT INTO client_account (singleton, username) VALUES (1, ?) "
            "ON CONFLICT(singleton) DO UPDATE SET username = excluded.username;",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_text(statement, 1, username, -1, SQLITE_TRANSIENT) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_WRITE_FAILED;
    }
    result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    return result == SQLITE_DONE ? CLIENT_DATABASE_OK : CLIENT_DATABASE_WRITE_FAILED;
}

ClientDatabaseStatus client_database_load_account_username(
    ClientDatabase *database, char *username, size_t username_capacity, bool *was_found)
{
    sqlite3_stmt *statement = NULL;
    const unsigned char *stored_username;
    int result;
    size_t stored_length;

    if (database == NULL || database->connection == NULL || username == NULL ||
        username_capacity == 0 || was_found == NULL)
    {
        return CLIENT_DATABASE_INVALID_ARGUMENT;
    }
    *was_found = false;
    username[0] = '\0';
    if (sqlite3_prepare_v2(database->connection,
            "SELECT username FROM client_account WHERE singleton = 1;", -1,
            &statement, NULL) != SQLITE_OK)
    {
        return CLIENT_DATABASE_READ_FAILED;
    }
    result = sqlite3_step(statement);
    if (result == SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_OK;
    }
    stored_username = sqlite3_column_text(statement, 0);
    stored_length = (size_t)sqlite3_column_bytes(statement, 0);
    if (result != SQLITE_ROW || stored_username == NULL || stored_length + 1u > username_capacity ||
        !protocol_username_is_valid((const char *)stored_username, stored_length))
    {
        sqlite3_finalize(statement);
        return CLIENT_DATABASE_READ_FAILED;
    }
    memcpy(username, stored_username, stored_length);
    username[stored_length] = '\0';
    *was_found = true;
    sqlite3_finalize(statement);
    return CLIENT_DATABASE_OK;
}

const char *client_database_status_string(ClientDatabaseStatus status)
{
    switch (status)
    {
        case CLIENT_DATABASE_OK:
            return "ok";
        case CLIENT_DATABASE_INVALID_ARGUMENT:
            return "invalid argument";
        case CLIENT_DATABASE_OPEN_FAILED:
            return "could not open database";
        case CLIENT_DATABASE_SCHEMA_FAILED:
            return "could not create database tables";
        case CLIENT_DATABASE_READ_FAILED:
            return "could not read identity";
        case CLIENT_DATABASE_WRITE_FAILED:
            return "could not save identity";
        case CLIENT_DATABASE_CORRUPT_IDENTITY:
            return "stored identity is invalid";
        case CLIENT_DATABASE_KEY_GENERATION_FAILED:
            return "could not generate identity keys";
        case CLIENT_DATABASE_SERVER_IDENTITY_MISMATCH:
            return "server identity changed";
        case CLIENT_DATABASE_INVALID_CONTACT_LINK:
            return "contact link is invalid";
        default:
            return "unknown database status";
    }
}
