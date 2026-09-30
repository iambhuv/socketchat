#ifndef SOCKET_CHAT_CLIENT_DATABASE_H
#define SOCKET_CHAT_CLIENT_DATABASE_H

#include "client/client_identity.h"
#include "protocol/protocol.h"

#include <sqlite3.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

typedef struct ClientContact
{
    uint8_t public_key[crypto_sign_PUBLICKEYBYTES];
} ClientContact;

#define CLIENT_DATABASE_MESSAGE_TEXT_CAPACITY 512u

typedef enum ClientMessageDirection
{
    CLIENT_MESSAGE_INCOMING = 1,
    CLIENT_MESSAGE_OUTGOING = 2
} ClientMessageDirection;

typedef struct ClientChatMessage
{
    ClientMessageDirection direction;
    uint64_t sender_timestamp_unix_ms;
    char text[CLIENT_DATABASE_MESSAGE_TEXT_CAPACITY];
} ClientChatMessage;

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

/* Loads contact identities in creation order so the chat UI can show them. */
ClientDatabaseStatus client_database_list_contacts(
    ClientDatabase *database,
    ClientContact *contacts,
    uint32_t capacity,
    uint32_t *count);

/* Stores one verified message. The packet ID makes retry delivery idempotent.
 * A successful duplicate write sets was_inserted to false. */
ClientDatabaseStatus client_database_store_message(
    ClientDatabase *database,
    const uint8_t contact_public_key[crypto_sign_PUBLICKEYBYTES],
    const uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE],
    ClientMessageDirection direction,
    uint64_t sender_timestamp_unix_ms,
    const uint8_t *text,
    uint32_t text_length,
    bool *was_inserted);

/* Loads the newest messages for one chat in chronological order. */
ClientDatabaseStatus client_database_load_messages(
    ClientDatabase *database,
    const uint8_t contact_public_key[crypto_sign_PUBLICKEYBYTES],
    ClientChatMessage *messages,
    uint32_t capacity,
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
