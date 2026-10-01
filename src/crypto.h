// crypto.h - identity keypairs, signatures, random bytes, contact links.
//
// Thin layer over monocypher (thirdparty/monocypher.c). An identity is an
// Ed25519 keypair. The public key is the user's address on the network.
// Include nob.h and protocol.h before this file.
#ifndef CRYPTO_H_
#define CRYPTO_H_

#include "monocypher.h"

#ifdef _WIN32
#    include <windows.h>
#    include <bcrypt.h>
#elif defined(__linux__)
#    include <sys/random.h>
#else
#    include <stdlib.h> // arc4random_buf on macOS and the BSDs
#endif

#define LINK_PREFIX "chat://"
#define LINK_SIZE   (sizeof(LINK_PREFIX) - 1 + KEY_SIZE*2 + 1)

typedef struct {
    uint8_t pub[KEY_SIZE];
    uint8_t sec[64];        // monocypher layout: seed[32] followed by pub[32]
} Identity;

bool random_bytes(void *buf, size_t size)
{
#ifdef _WIN32
    return BCryptGenRandom(NULL, buf, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__linux__)
    uint8_t *p = buf;
    while (size > 0) {
        ssize_t n = getrandom(p, size, 0);
        if (n < 0) return false;
        p += n;
        size -= n;
    }
    return true;
#else
    arc4random_buf(buf, size);
    return true;
#endif
}

bool identity_generate(Identity *id)
{
    uint8_t seed[32];
    if (!random_bytes(seed, sizeof(seed))) {
        nob_log(ERROR, "Could not get random bytes from the OS");
        return false;
    }
    crypto_eddsa_key_pair(id->sec, id->pub, seed); // wipes seed
    return true;
}

bool identity_is_valid(const Identity *id)
{
    return crypto_verify32(id->sec + 32, id->pub) == 0;
}

void identity_sign(uint8_t sig[SIG_SIZE], const Identity *id, const void *data, size_t size)
{
    crypto_eddsa_sign(sig, id->sec, data, size);
}

bool signature_ok(const uint8_t sig[SIG_SIZE], const uint8_t pub[KEY_SIZE], const void *data, size_t size)
{
    return crypto_eddsa_check(sig, pub, data, size) == 0;
}

bool keys_equal(const uint8_t a[KEY_SIZE], const uint8_t b[KEY_SIZE])
{
    return memcmp(a, b, KEY_SIZE) == 0;
}

// Sealed boxes. The same Ed25519 identity doubles as an X25519 key:
// the public key converts directly, the secret scalar is the first 32
// bytes of BLAKE2b(seed), which is how the signing scalar is made too.

static void x25519_secret(uint8_t out[32], const Identity *id)
{
    uint8_t hash[64];
    crypto_blake2b(hash, sizeof(hash), id->sec, 32);
    memcpy(out, hash, 32);
    crypto_wipe(hash, sizeof(hash));
}

// key = BLAKE2b(shared || eph || recipient), as monocypher recommends:
// raw X25519 output is not uniform enough to use as a key on its own.
static void box_key(uint8_t key[32], const uint8_t shared[32],
                    const uint8_t eph[KEY_SIZE], const uint8_t recipient_x[32])
{
    uint8_t material[96];
    memcpy(material, shared, 32);
    memcpy(material + 32, eph, 32);
    memcpy(material + 64, recipient_x, 32);
    crypto_blake2b(key, 32, material, sizeof(material));
    crypto_wipe(material, sizeof(material));
}

// Encrypts `size` bytes so only the owner of `to` can read them. Appends the
// ciphertext (same length) to `cipher` and fills the box.
bool box_seal(Box *box, String_Builder *cipher, const uint8_t to[KEY_SIZE],
              const uint8_t *plain, size_t size)
{
    uint8_t eph_sec[32], to_x[32], shared[32], key[32];
    if (!random_bytes(eph_sec, sizeof(eph_sec)) || !random_bytes(box->nonce, BOX_NONCE_SIZE)) {
        nob_log(ERROR, "Could not get random bytes from the OS");
        return false;
    }
    crypto_x25519_public_key(box->eph, eph_sec);
    crypto_eddsa_to_x25519(to_x, to);
    crypto_x25519(shared, eph_sec, to_x);
    box_key(key, shared, box->eph, to_x);
    da_reserve(cipher, cipher->count + size);
    crypto_aead_lock((uint8_t *)cipher->items + cipher->count, box->mac, key, box->nonce,
                     NULL, 0, plain, size);
    cipher->count += size;
    crypto_wipe(eph_sec, sizeof(eph_sec));
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(key, sizeof(key));
    return true;
}

// The reverse. Appends the plaintext to `plain`. False when the box was not
// sealed to `me` or was tampered with.
bool box_open(String_Builder *plain, const Box *box, const Identity *me,
              const uint8_t *cipher, size_t size)
{
    uint8_t my_x[32], my_x_pub[32], shared[32], key[32];
    x25519_secret(my_x, me);
    crypto_eddsa_to_x25519(my_x_pub, me->pub);
    crypto_x25519(shared, my_x, box->eph);
    box_key(key, shared, box->eph, my_x_pub);
    da_reserve(plain, plain->count + size);
    int r = crypto_aead_unlock((uint8_t *)plain->items + plain->count, box->mac, key, box->nonce,
                               NULL, 0, cipher, size);
    if (r == 0) plain->count += size;
    crypto_wipe(my_x, sizeof(my_x));
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(key, sizeof(key));
    return r == 0;
}

void hex_encode(char *out, const uint8_t *in, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        out[i*2]     = digits[in[i] >> 4];
        out[i*2 + 1] = digits[in[i] & 15];
    }
    out[size*2] = '\0';
}

static int hex_value(char c)
{
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hex_decode(uint8_t *out, size_t size, String_View hex)
{
    if (hex.count != size*2) return false;
    for (size_t i = 0; i < size; ++i) {
        int hi = hex_value(hex.data[i*2]);
        int lo = hex_value(hex.data[i*2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

// Short printable form of a key for logs and labels: first 8 hex digits.
const char *key_short(const uint8_t key[KEY_SIZE])
{
    char *s = temp_alloc(9);
    hex_encode(s, key, 4);
    return s;
}

void link_write(char out[LINK_SIZE], const uint8_t pub[KEY_SIZE])
{
    memcpy(out, LINK_PREFIX, sizeof(LINK_PREFIX) - 1);
    hex_encode(out + sizeof(LINK_PREFIX) - 1, pub, KEY_SIZE);
}

bool link_parse(String_View link, uint8_t pub[KEY_SIZE])
{
    link = sv_trim(link);
    if (!sv_chop_prefix(&link, SVLIT(LINK_PREFIX))) return false;
    return hex_decode(pub, KEY_SIZE, link);
}

#endif // CRYPTO_H_
