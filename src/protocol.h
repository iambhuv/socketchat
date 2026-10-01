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
#define NONCE_SIZE       32
#define MSG_HEADER       (KEY_SIZE + KEY_SIZE + 8 + 4)
#define MSG_MAX_TEXT     (FRAME_MAX_SIZE - FRAME_HEADER - MSG_HEADER - SIG_SIZE)

typedef enum {
    FRAME_HELLO = 1,   // C -> S   pubkey[32]
    FRAME_CHALLENGE,   // S -> C   nonce[32]
    FRAME_AUTH,        // C -> S   sig[64] = sign(nonce)
    FRAME_MSG,         // both     from[32] to[32] u64 timestamp_ms u32 len text[len] sig[64]
    FRAME_OK,          // S -> C   empty. Login worked, or the message was delivered.
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
    String_View text;          // points into the payload
    const uint8_t *sig;        // points into the payload
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

// Appends from|to|timestamp|len|text. The caller signs payload->items
// (all of it) and appends the 64 byte signature.
void msg_append_unsigned(String_Builder *payload, const uint8_t from[KEY_SIZE],
                         const uint8_t to[KEY_SIZE], uint64_t timestamp_ms, String_View text)
{
    sb_append_buf(payload, from, KEY_SIZE);
    sb_append_buf(payload, to, KEY_SIZE);
    put_u64(payload, timestamp_ms);
    put_u32(payload, (uint32_t)text.count);
    sb_append_sv(payload, text);
}

bool msg_parse(const uint8_t *payload, uint32_t size, Msg *msg)
{
    if (size < MSG_HEADER + SIG_SIZE) return false;
    uint32_t len = get_u32(payload + KEY_SIZE + KEY_SIZE + 8);
    if (len > MSG_MAX_TEXT || MSG_HEADER + (size_t)len + SIG_SIZE != size) return false;
    memcpy(msg->from, payload, KEY_SIZE);
    memcpy(msg->to, payload + KEY_SIZE, KEY_SIZE);
    msg->timestamp_ms = get_u64(payload + KEY_SIZE + KEY_SIZE);
    msg->text = sv_from_parts((const char *)payload + MSG_HEADER, len);
    msg->signed_data = payload;
    msg->signed_size = MSG_HEADER + len;
    msg->sig = payload + msg->signed_size;
    return true;
}

#endif // PROTOCOL_H_
