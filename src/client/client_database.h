#ifndef SOCKET_CHAT_CLIENT_DATABASE_H
#define SOCKET_CHAT_CLIENT_DATABASE_H

#include "client/client_identity.h"

#include <sqlite3.h>

#include <stdbool.h>

typedef enum ClientDatabaseStatus
{
    CLIENT_DATABASE_OK = 0,
    CLIENT_DATABASE_INVALID_ARGUMENT,
    CLIENT_DATABASE_OPEN_FAILED,
    CLIENT_DATABASE_SCHEMA_FAILED,
    CLIENT_DATABASE_READ_FAILED,
    CLIENT_DATABASE_WRITE_FAILED,
    CLIENT_DATABASE_CORRUPT_IDENTITY,
    CLIENT_DATABASE_KEY_GENERATION_FAILED,
    CLIENT_DATABASE_SERVER_IDENTITY_MISMATCH,
    CLIENT_DATABASE_INVALID_CONTACT_LINK
} ClientDatabaseStatus;

typedef struct ClientDatabase
{
    sqlite3 *connection;
} ClientDatabase;

ClientDatabaseStatus client_database_open(
    ClientDatabase *database,
    const char *path);
void client_database_close(ClientDatabase *database);

ClientDatabaseStatus client_database_load_or_create_identity(
    ClientDatabase *database,
    ClientIdentity *identity,
    bool *was_created);

/* Pins a server key on first connection and rejects later key changes. */
ClientDatabaseStatus client_database_trust_server(
    ClientDatabase *database,
    const char *host,
    uint16_t port,
    const unsigned char public_key[crypto_sign_PUBLICKEYBYTES]);

/* Saves a socketchat://contact/<64 lowercase-or-uppercase hex characters>
 * link.  Re-adding the same identity is harmless. */
ClientDatabaseStatus client_database_add_contact_link(
    ClientDatabase *database,
    const char *link);

ClientDatabaseStatus client_database_contact_count(
    ClientDatabase *database,
    uint32_t *count);

/* Adds the owner's identity as the built-in Saved messages contact. */
ClientDatabaseStatus client_database_ensure_self_contact(
    ClientDatabase *database,
    const ClientIdentity *identity);

ClientDatabaseStatus client_database_save_account_username(
    ClientDatabase *database,
    const char *username);
ClientDatabaseStatus client_database_load_account_username(
    ClientDatabase *database,
    char *username,
    size_t username_capacity,
    bool *was_found);

const char *client_database_status_string(ClientDatabaseStatus status);

#endif
