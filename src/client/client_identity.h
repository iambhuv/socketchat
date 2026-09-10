#ifndef SOCKET_CHAT_CLIENT_IDENTITY_H
#define SOCKET_CHAT_CLIENT_IDENTITY_H

#include <sodium.h>

#include <stdbool.h>

typedef struct ClientIdentity
{
    unsigned char public_key[crypto_sign_PUBLICKEYBYTES];
    unsigned char secret_key[crypto_sign_SECRETKEYBYTES];
} ClientIdentity;

/* dependencies_initialize() must be called before these functions. */
bool client_identity_generate(ClientIdentity *identity);
bool client_identity_is_valid(const ClientIdentity *identity);
void client_identity_clear(ClientIdentity *identity);

#endif
