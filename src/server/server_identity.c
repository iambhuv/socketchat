#include "server/server_identity.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static const char SERVER_IDENTITY_SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS server_identity ("
    "singleton INTEGER PRIMARY KEY CHECK (singleton = 1),"
    "public_key BLOB NOT NULL CHECK (length(public_key) = 32),"
    "secret_key BLOB NOT NULL CHECK (length(secret_key) = 64)"
    ");";

bool server_identity_load_or_create(const char *database_path, ServerIdentity *identity)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    unsigned char derived_public_key[crypto_sign_PUBLICKEYBYTES];
    bool success = false;
    int result;

    if (database_path == NULL || database_path[0] == '\0' || identity == NULL)
    {
        return false;
    }

    memset(identity, 0, sizeof(*identity));

    if (sqlite3_open(database_path, &database) != SQLITE_OK)
    {
        fprintf(stderr, "Could not open the server identity database: %s\n",
                database != NULL ? sqlite3_errmsg(database) : "out of memory");
        goto cleanup;
    }

    if (sqlite3_exec(database, SERVER_IDENTITY_SCHEMA, NULL, NULL, NULL) != SQLITE_OK)
    {
        goto cleanup;
    }

    if (sqlite3_prepare_v2(database,
            "SELECT public_key, secret_key FROM server_identity WHERE singleton = 1;",
            -1, &statement, NULL) != SQLITE_OK)
    {
        goto cleanup;
    }

    result = sqlite3_step(statement);
    if (result == SQLITE_ROW)
    {
        if (sqlite3_column_bytes(statement, 0) != crypto_sign_PUBLICKEYBYTES ||
            sqlite3_column_bytes(statement, 1) != crypto_sign_SECRETKEYBYTES)
        {
            fprintf(stderr, "Stored server identity has invalid key lengths.\n");
            goto cleanup;
        }

        memcpy(identity->public_key, sqlite3_column_blob(statement, 0),
               sizeof(identity->public_key));
        memcpy(identity->secret_key, sqlite3_column_blob(statement, 1),
               sizeof(identity->secret_key));

        if (crypto_sign_ed25519_sk_to_pk(
                derived_public_key, identity->secret_key) != 0 ||
            sodium_memcmp(derived_public_key, identity->public_key,
                          sizeof(derived_public_key)) != 0)
        {
            fprintf(stderr, "Stored server identity is invalid.\n");
            goto cleanup;
        }

        success = true;
        goto cleanup;
    }

    if (result != SQLITE_DONE)
    {
        goto cleanup;
    }

    sqlite3_finalize(statement);
    statement = NULL;

    if (crypto_sign_keypair(identity->public_key, identity->secret_key) != 0 ||
        sqlite3_prepare_v2(database,
            "INSERT INTO server_identity (singleton, public_key, secret_key) "
            "VALUES (1, ?, ?);",
            -1, &statement, NULL) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 1, identity->public_key,
                          sizeof(identity->public_key), SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_blob(statement, 2, identity->secret_key,
                          sizeof(identity->secret_key), SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE)
    {
        goto cleanup;
    }

    success = true;

cleanup:
    sqlite3_finalize(statement);
    if (!success && database != NULL)
    {
        fprintf(stderr, "Could not load or create server identity: %s\n",
                sqlite3_errmsg(database));
        server_identity_clear(identity);
    }
    if (database != NULL)
    {
        sqlite3_close(database);
    }
    sodium_memzero(derived_public_key, sizeof(derived_public_key));
    return success;
}

void server_identity_clear(ServerIdentity *identity)
{
    if (identity != NULL)
    {
        sodium_memzero(identity, sizeof(*identity));
    }
}
