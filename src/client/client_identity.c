#include "client/client_identity.h"

bool client_identity_generate(ClientIdentity *identity)
{
    if (identity == NULL)
    {
        return false;
    }

    return crypto_sign_keypair(identity->public_key, identity->secret_key) == 0;
}

bool client_identity_is_valid(const ClientIdentity *identity)
{
    unsigned char derived_public_key[crypto_sign_PUBLICKEYBYTES];
    bool matches;

    if (identity == NULL ||
        crypto_sign_ed25519_sk_to_pk(
            derived_public_key,
            identity->secret_key) != 0)
    {
        return false;
    }

    matches = sodium_memcmp(
                  derived_public_key,
                  identity->public_key,
                  sizeof(derived_public_key)) == 0;
    sodium_memzero(derived_public_key, sizeof(derived_public_key));
    return matches;
}

void client_identity_clear(ClientIdentity *identity)
{
    if (identity != NULL)
    {
        sodium_memzero(identity, sizeof(*identity));
    }
}
