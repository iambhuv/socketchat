#include "client/client_database.h"
#include "client/client_identity.h"
#include "core/dependencies.h"

#include <sodium.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define REQUIRE(condition)                                                      \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
            goto cleanup;                                                       \
        }                                                                       \
    } while (0)

int main(void)
{
    static const char database_path[] = "client_identity_test.db";
    static const unsigned char message[] = "identity test";
    ClientDatabase database = { 0 };
    ClientIdentity created_identity;
    ClientIdentity loaded_identity;
    unsigned char signature[crypto_sign_BYTES];
    unsigned char packet_id[PROTOCOL_PACKET_ID_SIZE] = { 0x42 };
    ClientChatMessage messages[2];
    uint32_t message_count;
    bool was_inserted;
    bool was_created;
    bool dependencies_ready = false;
    int exit_code = 1;

    remove(database_path);

    REQUIRE(dependencies_initialize());
    dependencies_ready = true;

    REQUIRE(client_database_open(&database, database_path) == CLIENT_DATABASE_OK);
    REQUIRE(client_database_load_or_create_identity(
                &database,
                &created_identity,
                &was_created) == CLIENT_DATABASE_OK);
    REQUIRE(was_created);
    REQUIRE(client_identity_is_valid(&created_identity));
    client_database_close(&database);

    REQUIRE(client_database_open(&database, database_path) == CLIENT_DATABASE_OK);
    REQUIRE(client_database_load_or_create_identity(
                &database,
                &loaded_identity,
                &was_created) == CLIENT_DATABASE_OK);
    REQUIRE(!was_created);
    REQUIRE(client_identity_is_valid(&loaded_identity));
    REQUIRE(memcmp(&created_identity, &loaded_identity,
                   sizeof(created_identity)) == 0);

    REQUIRE(crypto_sign_detached(
                signature,
                NULL,
                message,
                sizeof(message) - 1,
                loaded_identity.secret_key) == 0);
    REQUIRE(crypto_sign_verify_detached(
                signature,
                message,
                sizeof(message) - 1,
                loaded_identity.public_key) == 0);

    /* Histories are persistent and delivery retries are idempotent by packet ID. */
    REQUIRE(client_database_store_message(
                &database, loaded_identity.public_key, packet_id,
                CLIENT_MESSAGE_INCOMING, 1234, (const uint8_t *)"saved message",
                (uint32_t)strlen("saved message"), &was_inserted) ==
            CLIENT_DATABASE_OK);
    REQUIRE(was_inserted);
    REQUIRE(client_database_store_message(
                &database, loaded_identity.public_key, packet_id,
                CLIENT_MESSAGE_INCOMING, 1234, (const uint8_t *)"saved message",
                (uint32_t)strlen("saved message"), &was_inserted) ==
            CLIENT_DATABASE_OK);
    REQUIRE(!was_inserted);
    client_database_close(&database);
    REQUIRE(client_database_open(&database, database_path) == CLIENT_DATABASE_OK);
    REQUIRE(client_database_load_messages(
                &database, loaded_identity.public_key, messages, 2,
                &message_count) == CLIENT_DATABASE_OK);
    REQUIRE(message_count == 1);
    REQUIRE(messages[0].direction == CLIENT_MESSAGE_INCOMING);
    REQUIRE(messages[0].sender_timestamp_unix_ms == 1234);
    REQUIRE(strcmp(messages[0].text, "saved message") == 0);

    REQUIRE(sqlite3_exec(
                database.connection,
                "UPDATE client_identity SET public_key = zeroblob(32);",
                NULL,
                NULL,
                NULL) == SQLITE_OK);
    REQUIRE(client_database_load_or_create_identity(
                &database,
                &loaded_identity,
                &was_created) == CLIENT_DATABASE_CORRUPT_IDENTITY);

    exit_code = 0;

cleanup:
    client_identity_clear(&created_identity);
    client_identity_clear(&loaded_identity);
    client_database_close(&database);
    if (dependencies_ready)
    {
        dependencies_shutdown();
    }
    remove(database_path);
    return exit_code;
}
