// protocol.h - what goes over the TCP socket.
//
// Every frame is:  u32 length (big endian)  u8 type  payload...
// `length` counts the type byte and the payload. Max frame is 64 KiB.
//
// This file contains definitions, not just declarations. Each binary is one
// translation unit, so include it once (the guard handles repeats).
// Include nob.h before this file.
#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#define FRAME_MAX_SIZE   (64*1024)
#define FRAME_HEADER     5
#define KEY_SIZE         32
#define SIG_SIZE         64
#define NONCE_SIZE       32   // login challenge
#define BOX_NONCE_SIZE   24   // XChaCha20 nonce
#define MAC_SIZE         16
#define MSG_HEADER       (KEY_SIZE + KEY_SIZE + 8 + KEY_SIZE + BOX_NONCE_SIZE + MAC_SIZE + 4)
#define MSG_MAX_TEXT     (FRAME_MAX_SIZE - FRAME_HEADER - MSG_HEADER - SIG_SIZE)

// Everything a recipient needs, besides their own key, to open a sealed
// box: the sender's throwaway public key, the nonce, and the AEAD tag.
typedef struct {
    uint8_t eph[KEY_SIZE];
    uint8_t nonce[BOX_NONCE_SIZE];
    uint8_t mac[MAC_SIZE];
} Box;

typedef enum {
    FRAME_HELLO = 1,   // C -> S   pubkey[32]
    FRAME_CHALLENGE,   // S -> C   nonce[32]
    FRAME_AUTH,        // C -> S   sig[64] = sign(nonce)
    FRAME_MSG,         // both     from[32] to[32] u64 timestamp_ms eph[32] nonce[24] mac[16] u32 len cipher[len] sig[64]
    FRAME_OK,          // S -> C   empty after AUTH; after MSG one byte: 1 delivered, 2 stored
    FRAME_ERROR,       // S -> C   text
    COUNT_FRAMES,
} Frame_Type;

static_assert(COUNT_FRAMES == 7, "Frame_Type changed, update frame_type_names");
static const char *frame_type_names[COUNT_FRAMES] = {
    [FRAME_HELLO]     = "HELLO",
    [FRAME_CHALLENGE] = "CHALLENGE",
    [FRAME_AUTH]      = "AUTH",
    [FRAME_MSG]       = "MSG",
    [FRAME_OK]        = "OK",
    [FRAME_ERROR]     = "ERROR",
};

typedef struct {
    Frame_Type type;
    const uint8_t *payload;   // points into the buffer that was parsed
    uint32_t size;
} Frame;

typedef struct {
    uint8_t from[KEY_SIZE];
    uint8_t to[KEY_SIZE];
    uint64_t timestamp_ms;
    Box box;
    const uint8_t *cipher;      // points into the payload
    uint32_t cipher_size;
    const uint8_t *sig;         // points into the payload
    const uint8_t *signed_data; // everything before sig
    size_t signed_size;
} Msg;

static void put_u32(String_Builder *sb, uint32_t v)
{
    uint8_t b[4] = { v >> 24, v >> 16, v >> 8, v };
    sb_append_buf(sb, b, 4);
}

static void put_u64(String_Builder *sb, uint64_t v)
{
    put_u32(sb, (uint32_t)(v >> 32));
    put_u32(sb, (uint32_t)v);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)get_u32(p) << 32 | get_u32(p + 4);
}

const char *frame_type_name(Frame_Type type)
{
    if (type <= 0 || type >= COUNT_FRAMES) return "?";
    return frame_type_names[type];
}

void frame_append(String_Builder *out, Frame_Type type, const void *payload, uint32_t size)
{
    assert(size + 1 <= FRAME_MAX_SIZE);
    put_u32(out, size + 1);
    da_append(out, (char)type);
    if (size) sb_append_buf(out, payload, size);
}

// Returns 1 and fills *frame when a whole frame is at the start of data,
// 0 when more bytes are needed, -1 when the bytes are garbage.
int frame_parse(const uint8_t *data, size_t size, Frame *frame, size_t *consumed)
{
    if (size < FRAME_HEADER) return 0;
    uint32_t length = get_u32(data);
    if (length < 1 || length > FRAME_MAX_SIZE) return -1;
    uint8_t type = data[4];
    if (type < 1 || type >= COUNT_FRAMES) return -1;
    if (size < 4 + (size_t)length) return 0;
    frame->type = type;
    frame->payload = data + FRAME_HEADER;
    frame->size = length - 1;
    *consumed = 4 + length;
    return 1;
}

// Appends from|to|timestamp|box|len|cipher. The caller signs payload->items
// (all of it) and appends the 64 byte signature.
void msg_append_unsigned(String_Builder *payload, const uint8_t from[KEY_SIZE],
                         const uint8_t to[KEY_SIZE], uint64_t timestamp_ms,
                         const Box *box, const uint8_t *cipher, uint32_t cipher_size)
{
    sb_append_buf(payload, from, KEY_SIZE);
    sb_append_buf(payload, to, KEY_SIZE);
    put_u64(payload, timestamp_ms);
    sb_append_buf(payload, box->eph, KEY_SIZE);
    sb_append_buf(payload, box->nonce, BOX_NONCE_SIZE);
    sb_append_buf(payload, box->mac, MAC_SIZE);
    put_u32(payload, cipher_size);
    sb_append_buf(payload, cipher, cipher_size);
}

bool msg_parse(const uint8_t *payload, uint32_t size, Msg *msg)
{
    if (size < MSG_HEADER + SIG_SIZE) return false;
    const uint8_t *p = payload;
    memcpy(msg->from, p, KEY_SIZE);              p += KEY_SIZE;
    memcpy(msg->to, p, KEY_SIZE);                p += KEY_SIZE;
    msg->timestamp_ms = get_u64(p);              p += 8;
    memcpy(msg->box.eph, p, KEY_SIZE);           p += KEY_SIZE;
    memcpy(msg->box.nonce, p, BOX_NONCE_SIZE);   p += BOX_NONCE_SIZE;
    memcpy(msg->box.mac, p, MAC_SIZE);           p += MAC_SIZE;
    uint32_t len = get_u32(p);                   p += 4;
    if (len > MSG_MAX_TEXT || MSG_HEADER + (size_t)len + SIG_SIZE != size) return false;
    msg->cipher = p;
    msg->cipher_size = len;
    msg->signed_data = payload;
    msg->signed_size = MSG_HEADER + len;
    msg->sig = payload + msg->signed_size;
    return true;
}

#endif // PROTOCOL_H_
