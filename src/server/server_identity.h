#ifndef SOCKET_CHAT_SERVER_IDENTITY_H
#define SOCKET_CHAT_SERVER_IDENTITY_H

#include <sodium.h>
#include <stdbool.h>

typedef struct ServerIdentity
{
    unsigned char public_key[crypto_sign_PUBLICKEYBYTES];
    unsigned char secret_key[crypto_sign_SECRETKEYBYTES];
} ServerIdentity;

/* The same signing key must survive restarts so clients can pin it later. */
bool server_identity_load_or_create(const char *database_path, ServerIdentity *identity);
void server_identity_clear(ServerIdentity *identity);

#endif
