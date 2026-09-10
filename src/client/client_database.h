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
    CLIENT_DATABASE_KEY_GENERATION_FAILED
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

const char *client_database_status_string(ClientDatabaseStatus status);

#endif
