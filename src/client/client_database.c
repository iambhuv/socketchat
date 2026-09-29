#include "client/client_database.h"

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
    ");";

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
        default:
            return "unknown database status";
    }
}
