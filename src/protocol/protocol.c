#include "protocol/protocol.h"

#include <string.h>

static const uint8_t FRAME_MAGIC[4] = { 'G', 'S', 'C', '1' };
static const uint8_t PACKET_MAGIC[4] = { 'E', '2', 'E', '1' };
static const uint8_t SERVER_AUTH_DOMAIN[] = "GSC/SERVER_AUTH/1";
static const uint8_t CLIENT_AUTH_DOMAIN[] = "GSC/CLIENT_AUTH/1";
static const uint8_t TRANSPORT_DOMAIN[] = "GSC/TRANSPORT/1";
static const uint8_t CHAT_KEY_DOMAIN[] = "GSC/CHAT_KEY/1";
static const uint8_t CHAT_CONTEXT_DOMAIN[] = "GSC/CHAT_CONTEXT/1";

static void write_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static void write_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void write_u64(uint8_t *output, uint64_t value)
{
    output[0] = (uint8_t)(value >> 56);
    output[1] = (uint8_t)(value >> 48);
    output[2] = (uint8_t)(value >> 40);
    output[3] = (uint8_t)(value >> 32);
    output[4] = (uint8_t)(value >> 24);
    output[5] = (uint8_t)(value >> 16);
    output[6] = (uint8_t)(value >> 8);
    output[7] = (uint8_t)value;
}

static uint16_t read_u16(const uint8_t *input)
{
    return (uint16_t)(((uint16_t)input[0] << 8) | (uint16_t)input[1]);
}

static uint32_t read_u32(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24) |
           ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) |
           (uint32_t)input[3];
}

static uint64_t read_u64(const uint8_t *input)
{
    return ((uint64_t)input[0] << 56) |
           ((uint64_t)input[1] << 48) |
           ((uint64_t)input[2] << 40) |
           ((uint64_t)input[3] << 32) |
           ((uint64_t)input[4] << 24) |
           ((uint64_t)input[5] << 16) |
           ((uint64_t)input[6] << 8) |
           (uint64_t)input[7];
}

static int is_frame_type_valid(uint8_t type)
{
    switch ((ProtocolFrameType)type)
    {
        case PROTOCOL_FRAME_HELLO:
        case PROTOCOL_FRAME_CHALLENGE:
        case PROTOCOL_FRAME_AUTHENTICATE:
        case PROTOCOL_FRAME_AUTHENTICATED:
        case PROTOCOL_FRAME_REGISTER_USERNAME:
        case PROTOCOL_FRAME_REGISTER_USERNAME_RESULT:
        case PROTOCOL_FRAME_LOOKUP_USERNAME:
        case PROTOCOL_FRAME_LOOKUP_USERNAME_RESULT:
        case PROTOCOL_FRAME_SEND:
        case PROTOCOL_FRAME_SEND_ACCEPTED:
        case PROTOCOL_FRAME_DELIVER:
        case PROTOCOL_FRAME_ACKNOWLEDGE:
        case PROTOCOL_FRAME_ACKNOWLEDGED:
        case PROTOCOL_FRAME_PING:
        case PROTOCOL_FRAME_PONG:
        case PROTOCOL_FRAME_ERROR:
            return 1;
    }

    return 0;
}

static int is_packet_kind_valid(uint8_t kind)
{
    return kind == PROTOCOL_PACKET_CHAT_KEY_OFFER ||
           kind == PROTOCOL_PACKET_CHAT_KEY_ACCEPT ||
           kind == PROTOCOL_PACKET_ENCRYPTED_MESSAGE ||
           kind == PROTOCOL_PACKET_TEXT_MESSAGE;
}

static int is_acknowledge_result_valid(uint8_t result)
{
    return result == PROTOCOL_ACK_STORED ||
           result == PROTOCOL_ACK_DUPLICATE ||
           result == PROTOCOL_ACK_REJECTED;
}

static int is_username_registration_result_valid(uint8_t result)
{
    return result == PROTOCOL_USERNAME_REGISTERED ||
           result == PROTOCOL_USERNAME_TAKEN ||
           result == PROTOCOL_USERNAME_INVALID ||
           result == PROTOCOL_IDENTITY_ALREADY_REGISTERED;
}

static int is_username_lookup_status_valid(uint8_t status)
{
    return status == PROTOCOL_USERNAME_FOUND ||
           status == PROTOCOL_USERNAME_NOT_FOUND;
}

static int is_content_type_valid(uint8_t type)
{
    return type == PROTOCOL_CONTENT_TEXT ||
           type == PROTOCOL_CONTENT_DELIVERY_RECEIPT ||
           type == PROTOCOL_CONTENT_CHAT_KEY_UPDATE ||
           type == PROTOCOL_CONTENT_CHAT_LEAVE;
}

static int is_valid_utf8(const uint8_t *text, size_t length)
{
    size_t index = 0;

    while (index < length)
    {
        uint8_t first = text[index];

        if (first <= 0x7f)
        {
            index += 1;
            continue;
        }

        if (first >= 0xc2 && first <= 0xdf)
        {
            if (index + 1 >= length || (text[index + 1] & 0xc0) != 0x80)
            {
                return 0;
            }
            index += 2;
            continue;
        }

        if (first >= 0xe0 && first <= 0xef)
        {
            uint8_t second;
            uint8_t third;

            if (index + 2 >= length)
            {
                return 0;
            }

            second = text[index + 1];
            third = text[index + 2];

            if ((second & 0xc0) != 0x80 || (third & 0xc0) != 0x80)
            {
                return 0;
            }

            if ((first == 0xe0 && second < 0xa0) ||
                (first == 0xed && second >= 0xa0))
            {
                return 0;
            }

            index += 3;
            continue;
        }

        if (first >= 0xf0 && first <= 0xf4)
        {
            uint8_t second;
            uint8_t third;
            uint8_t fourth;

            if (index + 3 >= length)
            {
                return 0;
            }

            second = text[index + 1];
            third = text[index + 2];
            fourth = text[index + 3];

            if ((second & 0xc0) != 0x80 ||
                (third & 0xc0) != 0x80 ||
                (fourth & 0xc0) != 0x80)
            {
                return 0;
            }

            if ((first == 0xf0 && second < 0x90) ||
                (first == 0xf4 && second >= 0x90))
            {
                return 0;
            }

            index += 4;
            continue;
        }

        return 0;
    }

    return 1;
}

static ProtocolStatus copy_fixed_payload(
    uint8_t *output,
    const uint8_t *input,
    size_t size)
{
    if (output == NULL || input == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    memcpy(output, input, size);
    return PROTOCOL_STATUS_OK;
}

static ProtocolStatus decode_fixed_payload(
    const uint8_t *payload,
    size_t payload_size,
    uint8_t *output,
    size_t expected_size)
{
    if (payload == NULL || output == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size != expected_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    memcpy(output, payload, expected_size);
    return PROTOCOL_STATUS_OK;
}

static void encode_packet_header(
    uint8_t *output,
    ProtocolPacketKind kind,
    const uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE],
    const uint8_t sender_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE],
    const uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE],
    const uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE],
    uint32_t payload_length)
{
    memcpy(output, PACKET_MAGIC, sizeof(PACKET_MAGIC));
    output[4] = PROTOCOL_VERSION;
    output[5] = (uint8_t)kind;
    write_u16(output + 6, 0);
    memcpy(output + 8, packet_id, PROTOCOL_PACKET_ID_SIZE);
    memcpy(output + 24, sender_public_key, PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    memcpy(output + 56, recipient_id, PROTOCOL_IDENTITY_ID_SIZE);
    memcpy(output + 88, chat_id, PROTOCOL_CHAT_ID_SIZE);
    write_u32(output + 104, payload_length);
}

const char *protocol_status_string(ProtocolStatus status)
{
    switch (status)
    {
        case PROTOCOL_STATUS_OK: return "ok";
        case PROTOCOL_STATUS_NEED_MORE_DATA: return "need more data";
        case PROTOCOL_STATUS_INVALID_ARGUMENT: return "invalid argument";
        case PROTOCOL_STATUS_INVALID_MAGIC: return "invalid magic";
        case PROTOCOL_STATUS_UNSUPPORTED_VERSION: return "unsupported version";
        case PROTOCOL_STATUS_INVALID_TYPE: return "invalid type";
        case PROTOCOL_STATUS_INVALID_VALUE: return "invalid value";
        case PROTOCOL_STATUS_INVALID_LENGTH: return "invalid length";
        case PROTOCOL_STATUS_BUFFER_TOO_SMALL: return "buffer too small";
        case PROTOCOL_STATUS_ID_MISMATCH: return "ID mismatch";
    }

    return "unknown protocol status";
}

size_t protocol_frame_size(uint32_t payload_length)
{
    if (payload_length > PROTOCOL_MAX_SERVER_PAYLOAD)
    {
        return 0;
    }

    return PROTOCOL_FRAME_HEADER_SIZE + (size_t)payload_length;
}

ProtocolStatus protocol_encode_frame(
    uint8_t *output,
    size_t output_size,
    ProtocolFrameType type,
    uint32_t request_id,
    const uint8_t *payload,
    uint32_t payload_length,
    size_t *bytes_written)
{
    size_t total_size;

    if (output == NULL || bytes_written == NULL ||
        (payload_length > 0 && payload == NULL))
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (!is_frame_type_valid((uint8_t)type))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (payload_length > PROTOCOL_MAX_SERVER_PAYLOAD)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    total_size = protocol_frame_size(payload_length);

    if (output_size < total_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output, FRAME_MAGIC, sizeof(FRAME_MAGIC));
    output[4] = PROTOCOL_VERSION;
    output[5] = (uint8_t)type;
    write_u16(output + 6, 0);
    write_u32(output + 8, request_id);
    write_u32(output + 12, payload_length);

    if (payload_length > 0)
    {
        memcpy(output + PROTOCOL_FRAME_HEADER_SIZE, payload, payload_length);
    }

    *bytes_written = total_size;
    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_frame(
    const uint8_t *input,
    size_t input_size,
    ProtocolFrame *frame,
    size_t *bytes_consumed)
{
    uint32_t payload_length;
    size_t total_size;

    if (input == NULL || frame == NULL || bytes_consumed == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (input_size < PROTOCOL_FRAME_HEADER_SIZE)
    {
        return PROTOCOL_STATUS_NEED_MORE_DATA;
    }

    if (memcmp(input, FRAME_MAGIC, sizeof(FRAME_MAGIC)) != 0)
    {
        return PROTOCOL_STATUS_INVALID_MAGIC;
    }

    if (input[4] != PROTOCOL_VERSION)
    {
        return PROTOCOL_STATUS_UNSUPPORTED_VERSION;
    }

    if (!is_frame_type_valid(input[5]))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (read_u16(input + 6) != 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    payload_length = read_u32(input + 12);
    if (payload_length > PROTOCOL_MAX_SERVER_PAYLOAD)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    total_size = protocol_frame_size(payload_length);
    if (input_size < total_size)
    {
        return PROTOCOL_STATUS_NEED_MORE_DATA;
    }

    frame->type = (ProtocolFrameType)input[5];
    frame->flags = 0;
    frame->request_id = read_u32(input + 8);
    frame->payload = input + PROTOCOL_FRAME_HEADER_SIZE;
    frame->payload_length = payload_length;
    *bytes_consumed = total_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_hello(
    uint8_t *output,
    size_t output_size,
    const ProtocolHello *hello,
    size_t *bytes_written)
{
    if (output == NULL || hello == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (output_size < PROTOCOL_HELLO_SIZE)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output,
           hello->identity_public_key,
           PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    memcpy(output + 32, hello->client_nonce, PROTOCOL_CHALLENGE_SIZE);
    memcpy(output + 64,
           hello->client_ephemeral_public_key,
           PROTOCOL_X25519_PUBLIC_KEY_SIZE);
    *bytes_written = PROTOCOL_HELLO_SIZE;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_hello(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolHello *hello)
{
    if (payload == NULL || hello == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size != PROTOCOL_HELLO_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    memcpy(hello->identity_public_key,
           payload,
           PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    memcpy(hello->client_nonce, payload + 32, PROTOCOL_CHALLENGE_SIZE);
    memcpy(hello->client_ephemeral_public_key,
           payload + 64,
           PROTOCOL_X25519_PUBLIC_KEY_SIZE);

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_challenge(
    uint8_t *output,
    size_t output_size,
    const ProtocolChallenge *challenge,
    size_t *bytes_written)
{
    if (output == NULL || challenge == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (output_size < PROTOCOL_CHALLENGE_PAYLOAD_SIZE)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output,
           challenge->server_identity_public_key,
           PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    memcpy(output + 32,
           challenge->server_nonce,
           PROTOCOL_CHALLENGE_SIZE);
    memcpy(output + 64,
           challenge->server_ephemeral_public_key,
           PROTOCOL_X25519_PUBLIC_KEY_SIZE);
    memcpy(output + 96,
           challenge->server_signature,
           PROTOCOL_SIGNATURE_SIZE);
    *bytes_written = PROTOCOL_CHALLENGE_PAYLOAD_SIZE;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_challenge(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolChallenge *challenge)
{
    if (payload == NULL || challenge == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size != PROTOCOL_CHALLENGE_PAYLOAD_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    memcpy(challenge->server_identity_public_key,
           payload,
           PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    memcpy(challenge->server_nonce, payload + 32, PROTOCOL_CHALLENGE_SIZE);
    memcpy(challenge->server_ephemeral_public_key,
           payload + 64,
           PROTOCOL_X25519_PUBLIC_KEY_SIZE);
    memcpy(challenge->server_signature,
           payload + 96,
           PROTOCOL_SIGNATURE_SIZE);

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_authenticate(
    uint8_t output[PROTOCOL_SIGNATURE_SIZE],
    const uint8_t signature[PROTOCOL_SIGNATURE_SIZE])
{
    return copy_fixed_payload(output, signature, PROTOCOL_SIGNATURE_SIZE);
}

ProtocolStatus protocol_decode_authenticate(
    const uint8_t *payload,
    size_t payload_size,
    uint8_t signature[PROTOCOL_SIGNATURE_SIZE])
{
    return decode_fixed_payload(payload, payload_size, signature, PROTOCOL_SIGNATURE_SIZE);
}

static ProtocolStatus build_auth_signature_data(
    uint8_t *output,
    size_t output_size,
    const uint8_t *domain,
    size_t domain_size,
    const ProtocolHello *hello,
    const ProtocolChallenge *challenge,
    size_t *bytes_written)
{
    const size_t required_size = domain_size + 1u +
                                 PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE +
                                 PROTOCOL_CHALLENGE_SIZE +
                                 PROTOCOL_X25519_PUBLIC_KEY_SIZE +
                                 PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE +
                                 PROTOCOL_CHALLENGE_SIZE +
                                 PROTOCOL_X25519_PUBLIC_KEY_SIZE;
    size_t offset = 0;

    if (output == NULL || hello == NULL || challenge == NULL ||
        bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output + offset, domain, domain_size);
    offset += domain_size;
    output[offset++] = PROTOCOL_VERSION;
    memcpy(output + offset,
           hello->identity_public_key,
           PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    offset += PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE;
    memcpy(output + offset, hello->client_nonce, PROTOCOL_CHALLENGE_SIZE);
    offset += PROTOCOL_CHALLENGE_SIZE;
    memcpy(output + offset,
           hello->client_ephemeral_public_key,
           PROTOCOL_X25519_PUBLIC_KEY_SIZE);
    offset += PROTOCOL_X25519_PUBLIC_KEY_SIZE;
    memcpy(output + offset,
           challenge->server_identity_public_key,
           PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    offset += PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE;
    memcpy(output + offset, challenge->server_nonce, PROTOCOL_CHALLENGE_SIZE);
    offset += PROTOCOL_CHALLENGE_SIZE;
    memcpy(output + offset,
           challenge->server_ephemeral_public_key,
           PROTOCOL_X25519_PUBLIC_KEY_SIZE);
    offset += PROTOCOL_X25519_PUBLIC_KEY_SIZE;

    *bytes_written = offset;
    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_build_server_auth_signature_data(
    uint8_t *output,
    size_t output_size,
    const ProtocolHello *hello,
    const ProtocolChallenge *challenge,
    size_t *bytes_written)
{
    return build_auth_signature_data(
        output,
        output_size,
        SERVER_AUTH_DOMAIN,
        sizeof(SERVER_AUTH_DOMAIN) - 1,
        hello,
        challenge,
        bytes_written);
}

ProtocolStatus protocol_build_client_auth_signature_data(
    uint8_t *output,
    size_t output_size,
    const ProtocolHello *hello,
    const ProtocolChallenge *challenge,
    size_t *bytes_written)
{
    return build_auth_signature_data(
        output,
        output_size,
        CLIENT_AUTH_DOMAIN,
        sizeof(CLIENT_AUTH_DOMAIN) - 1,
        hello,
        challenge,
        bytes_written);
}

size_t protocol_transport_record_size(uint32_t ciphertext_length)
{
    if (ciphertext_length < PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_AEAD_TAG_SIZE ||
        ciphertext_length > PROTOCOL_MAX_TRANSPORT_CIPHERTEXT)
    {
        return 0;
    }

    return PROTOCOL_TRANSPORT_HEADER_SIZE + (size_t)ciphertext_length;
}

ProtocolStatus protocol_encode_transport_record_prefix(
    uint8_t *output,
    size_t output_size,
    uint64_t sequence,
    uint32_t ciphertext_length,
    size_t *bytes_written)
{
    size_t total_size = protocol_transport_record_size(ciphertext_length);

    if (output == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (total_size == 0)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (output_size < total_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    write_u32(output, ciphertext_length);
    write_u64(output + 4, sequence);
    *bytes_written = PROTOCOL_TRANSPORT_HEADER_SIZE;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_transport_record(
    const uint8_t *input,
    size_t input_size,
    ProtocolTransportRecord *record,
    size_t *bytes_consumed)
{
    uint32_t ciphertext_length;
    size_t total_size;

    if (input == NULL || record == NULL || bytes_consumed == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (input_size < PROTOCOL_TRANSPORT_HEADER_SIZE)
    {
        return PROTOCOL_STATUS_NEED_MORE_DATA;
    }

    ciphertext_length = read_u32(input);
    total_size = protocol_transport_record_size(ciphertext_length);
    if (total_size == 0)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (input_size < total_size)
    {
        return PROTOCOL_STATUS_NEED_MORE_DATA;
    }

    record->sequence = read_u64(input + 4);
    record->ciphertext = input + PROTOCOL_TRANSPORT_HEADER_SIZE;
    record->ciphertext_length = ciphertext_length;
    *bytes_consumed = total_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_build_transport_nonce(
    uint8_t output[PROTOCOL_NONCE_SIZE],
    uint64_t sequence)
{
    if (output == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    memset(output, 0, PROTOCOL_NONCE_SIZE);
    write_u64(output + PROTOCOL_NONCE_SIZE - 8u, sequence);
    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_build_transport_authenticated_data(
    uint8_t *output,
    size_t output_size,
    uint64_t sequence,
    uint32_t ciphertext_length,
    size_t *bytes_written)
{
    const size_t domain_size = sizeof(TRANSPORT_DOMAIN) - 1;
    const size_t required_size = domain_size + PROTOCOL_TRANSPORT_HEADER_SIZE;

    if (output == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (protocol_transport_record_size(ciphertext_length) == 0)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output, TRANSPORT_DOMAIN, domain_size);
    write_u32(output + domain_size, ciphertext_length);
    write_u64(output + domain_size + 4u, sequence);
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_authenticated(
    uint8_t *output,
    size_t output_size,
    const ProtocolAuthenticated *message,
    size_t *bytes_written)
{
    const size_t required_size = PROTOCOL_IDENTITY_ID_SIZE + 4u;

    if (output == NULL || message == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output, message->identity_id, PROTOCOL_IDENTITY_ID_SIZE);
    write_u32(output + PROTOCOL_IDENTITY_ID_SIZE, message->queued_packet_count);
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_authenticated(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolAuthenticated *message)
{
    const size_t required_size = PROTOCOL_IDENTITY_ID_SIZE + 4u;

    if (payload == NULL || message == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size != required_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    memcpy(message->identity_id, payload, PROTOCOL_IDENTITY_ID_SIZE);
    message->queued_packet_count = read_u32(payload + PROTOCOL_IDENTITY_ID_SIZE);

    return PROTOCOL_STATUS_OK;
}

int protocol_username_is_valid(const char *username, size_t username_length)
{
    size_t index;

    if (username == NULL || username_length < PROTOCOL_MIN_USERNAME_SIZE ||
        username_length > PROTOCOL_MAX_USERNAME_SIZE)
    {
        return 0;
    }

    for (index = 0; index < username_length; ++index)
    {
        const unsigned char character = (unsigned char)username[index];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_'))
        {
            return 0;
        }
    }

    return 1;
}

size_t protocol_username_payload_size(uint8_t username_length)
{
    if (username_length < PROTOCOL_MIN_USERNAME_SIZE ||
        username_length > PROTOCOL_MAX_USERNAME_SIZE)
    {
        return 0;
    }

    return 1u + (size_t)username_length;
}

ProtocolStatus protocol_encode_username(
    uint8_t *output,
    size_t output_size,
    const ProtocolUsername *username,
    size_t *bytes_written)
{
    size_t required_size;

    if (output == NULL || username == NULL || username->text == NULL ||
        bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    required_size = protocol_username_payload_size(username->length);
    if (required_size == 0)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (!protocol_username_is_valid(username->text, username->length))
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    output[0] = username->length;
    memcpy(output + 1, username->text, username->length);
    *bytes_written = required_size;
    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_username(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolUsername *username)
{
    uint8_t username_length;

    if (payload == NULL || username == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size < 1u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    username_length = payload[0];
    if (protocol_username_payload_size(username_length) != payload_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (!protocol_username_is_valid((const char *)(payload + 1), username_length))
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    username->text = (const char *)(payload + 1);
    username->length = username_length;
    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_username_registration_result(
    uint8_t *output,
    size_t output_size,
    ProtocolUsernameRegistrationResult result,
    size_t *bytes_written)
{
    if (output == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (!is_username_registration_result_valid((uint8_t)result))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (output_size < 1u)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    output[0] = (uint8_t)result;
    *bytes_written = 1u;
    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_username_registration_result(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolUsernameRegistrationResult *result)
{
    if (payload == NULL || result == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size != 1u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (!is_username_registration_result_valid(payload[0]))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    *result = (ProtocolUsernameRegistrationResult)payload[0];
    return PROTOCOL_STATUS_OK;
}

size_t protocol_username_lookup_result_size(
    ProtocolUsernameLookupStatus status,
    uint8_t username_length)
{
    size_t required_size;

    if (!is_username_lookup_status_valid((uint8_t)status) ||
        protocol_username_payload_size(username_length) == 0)
    {
        return 0;
    }

    required_size = 2u + (size_t)username_length;
    if (status == PROTOCOL_USERNAME_FOUND)
    {
        required_size += PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE;
    }

    return required_size;
}

ProtocolStatus protocol_encode_username_lookup_result(
    uint8_t *output,
    size_t output_size,
    const ProtocolUsernameLookupResult *result,
    size_t *bytes_written)
{
    size_t required_size;

    if (output == NULL || result == NULL || result->username.text == NULL ||
        bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (!is_username_lookup_status_valid((uint8_t)result->status))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    required_size = protocol_username_lookup_result_size(
        result->status,
        result->username.length);
    if (required_size == 0)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (!protocol_username_is_valid(
            result->username.text,
            result->username.length))
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    output[0] = (uint8_t)result->status;
    output[1] = result->username.length;
    memcpy(output + 2, result->username.text, result->username.length);

    if (result->status == PROTOCOL_USERNAME_FOUND)
    {
        memcpy(
            output + 2u + result->username.length,
            result->identity_public_key,
            PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    }

    *bytes_written = required_size;
    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_username_lookup_result(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolUsernameLookupResult *result)
{
    ProtocolUsernameLookupStatus status;
    uint8_t username_length;

    if (payload == NULL || result == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size < 2u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (!is_username_lookup_status_valid(payload[0]))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    status = (ProtocolUsernameLookupStatus)payload[0];
    username_length = payload[1];
    if (protocol_username_lookup_result_size(status, username_length) !=
        payload_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (!protocol_username_is_valid(
            (const char *)(payload + 2),
            username_length))
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    result->status = status;
    result->username.text = (const char *)(payload + 2);
    result->username.length = username_length;

    if (status == PROTOCOL_USERNAME_FOUND)
    {
        memcpy(
            result->identity_public_key,
            payload + 2u + username_length,
            PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    }
    else
    {
        memset(result->identity_public_key, 0, PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    }

    return PROTOCOL_STATUS_OK;
}

size_t protocol_send_payload_size(uint32_t packet_length)
{
    if (packet_length > PROTOCOL_MAX_PACKET_SIZE)
    {
        return 0;
    }

    return 56u + (size_t)packet_length;
}

ProtocolStatus protocol_encode_send(
    uint8_t *output,
    size_t output_size,
    const ProtocolSend *message,
    size_t *bytes_written)
{
    size_t required_size;

    if (output == NULL || message == NULL || bytes_written == NULL ||
        (message->packet_length > 0 && message->packet == NULL))
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (message->packet_length > PROTOCOL_MAX_PACKET_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    required_size = protocol_send_payload_size(message->packet_length);
    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output, message->packet_id, PROTOCOL_PACKET_ID_SIZE);
    memcpy(output + 16, message->recipient_id, PROTOCOL_IDENTITY_ID_SIZE);
    write_u32(output + 48, message->expires_in_seconds);
    write_u32(output + 52, message->packet_length);
    if (message->packet_length > 0)
    {
        memcpy(output + 56, message->packet, message->packet_length);
    }
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_send(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolSend *message)
{
    uint32_t packet_length;

    if (payload == NULL || message == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size < 56u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    packet_length = read_u32(payload + 52);
    if (packet_length > PROTOCOL_MAX_PACKET_SIZE ||
        protocol_send_payload_size(packet_length) != payload_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    memcpy(message->packet_id, payload, PROTOCOL_PACKET_ID_SIZE);
    memcpy(message->recipient_id, payload + 16, PROTOCOL_IDENTITY_ID_SIZE);
    message->expires_in_seconds = read_u32(payload + 48);
    message->packet_length = packet_length;
    message->packet = payload + 56;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_delivery_reference(
    uint8_t *output,
    size_t output_size,
    const ProtocolDeliveryReference *reference,
    size_t *bytes_written)
{
    const size_t required_size = 8u + PROTOCOL_PACKET_ID_SIZE;

    if (output == NULL || reference == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (reference->delivery_id == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    write_u64(output, reference->delivery_id);
    memcpy(output + 8, reference->packet_id, PROTOCOL_PACKET_ID_SIZE);
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_delivery_reference(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolDeliveryReference *reference)
{
    const size_t required_size = 8u + PROTOCOL_PACKET_ID_SIZE;

    if (payload == NULL || reference == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size != required_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    reference->delivery_id = read_u64(payload);
    if (reference->delivery_id == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    memcpy(reference->packet_id, payload + 8, PROTOCOL_PACKET_ID_SIZE);
    return PROTOCOL_STATUS_OK;
}

size_t protocol_deliver_payload_size(uint32_t packet_length)
{
    if (packet_length > PROTOCOL_MAX_PACKET_SIZE)
    {
        return 0;
    }

    return 68u + (size_t)packet_length;
}

ProtocolStatus protocol_encode_deliver(
    uint8_t *output,
    size_t output_size,
    const ProtocolDeliver *message,
    size_t *bytes_written)
{
    size_t required_size;

    if (output == NULL || message == NULL || bytes_written == NULL ||
        (message->packet_length > 0 && message->packet == NULL))
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (message->packet_length > PROTOCOL_MAX_PACKET_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    required_size = protocol_deliver_payload_size(message->packet_length);
    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    if (message->delivery_id == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    write_u64(output, message->delivery_id);
    memcpy(output + 8, message->packet_id, PROTOCOL_PACKET_ID_SIZE);
    memcpy(output + 24, message->sender_id, PROTOCOL_IDENTITY_ID_SIZE);
    write_u64(output + 56, message->server_received_at_unix_ms);
    write_u32(output + 64, message->packet_length);
    if (message->packet_length > 0)
    {
        memcpy(output + 68, message->packet, message->packet_length);
    }
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_deliver(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolDeliver *message)
{
    uint32_t packet_length;

    if (payload == NULL || message == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size < 68u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    packet_length = read_u32(payload + 64);
    if (packet_length > PROTOCOL_MAX_PACKET_SIZE ||
        protocol_deliver_payload_size(packet_length) != payload_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    message->delivery_id = read_u64(payload);
    if (message->delivery_id == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    memcpy(message->packet_id, payload + 8, PROTOCOL_PACKET_ID_SIZE);
    memcpy(message->sender_id, payload + 24, PROTOCOL_IDENTITY_ID_SIZE);
    message->server_received_at_unix_ms = read_u64(payload + 56);
    message->packet_length = packet_length;
    message->packet = payload + 68;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_acknowledge(
    uint8_t *output,
    size_t output_size,
    const ProtocolAcknowledge *message,
    size_t *bytes_written)
{
    const size_t required_size = 8u + PROTOCOL_PACKET_ID_SIZE + 1u;

    if (output == NULL || message == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (!is_acknowledge_result_valid((uint8_t)message->result))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    if (message->delivery_id == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    write_u64(output, message->delivery_id);
    memcpy(output + 8, message->packet_id, PROTOCOL_PACKET_ID_SIZE);
    output[24] = (uint8_t)message->result;
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_acknowledge(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolAcknowledge *message)
{
    if (payload == NULL || message == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size != 8u + PROTOCOL_PACKET_ID_SIZE + 1u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (!is_acknowledge_result_valid(payload[24]))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    message->delivery_id = read_u64(payload);
    if (message->delivery_id == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    memcpy(message->packet_id, payload + 8, PROTOCOL_PACKET_ID_SIZE);
    message->result = (ProtocolAcknowledgeResult)payload[24];

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_ping(
    uint8_t *output,
    size_t output_size,
    uint64_t token,
    size_t *bytes_written)
{
    if (output == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (output_size < 8u)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    write_u64(output, token);
    *bytes_written = 8u;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_ping(
    const uint8_t *payload,
    size_t payload_size,
    uint64_t *token)
{
    if (payload == NULL || token == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size != 8u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    *token = read_u64(payload);
    return PROTOCOL_STATUS_OK;
}

size_t protocol_error_payload_size(uint16_t message_length)
{
    return 4u + (size_t)message_length;
}

ProtocolStatus protocol_encode_error(
    uint8_t *output,
    size_t output_size,
    const ProtocolError *message,
    size_t *bytes_written)
{
    size_t required_size;

    if (output == NULL || message == NULL || bytes_written == NULL ||
        (message->message_length > 0 && message->message == NULL))
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (message->message_length > PROTOCOL_MAX_ERROR_MESSAGE_SIZE ||
        !is_valid_utf8(message->message, message->message_length))
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    required_size = protocol_error_payload_size(message->message_length);
    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    write_u16(output, (uint16_t)message->code);
    write_u16(output + 2, message->message_length);
    if (message->message_length > 0)
    {
        memcpy(output + 4, message->message, message->message_length);
    }
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_error(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolError *message)
{
    uint16_t message_length;

    if (payload == NULL || message == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size < 4u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    message_length = read_u16(payload + 2);
    if (message_length > PROTOCOL_MAX_ERROR_MESSAGE_SIZE ||
        protocol_error_payload_size(message_length) != payload_size ||
        !is_valid_utf8(payload + 4, message_length))
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    message->code = (ProtocolErrorCode)read_u16(payload);
    message->message_length = message_length;
    message->message = payload + 4;

    return PROTOCOL_STATUS_OK;
}

size_t protocol_packet_size(uint32_t payload_length)
{
    if (payload_length > PROTOCOL_MAX_PACKET_SIZE -
                             PROTOCOL_E2E_HEADER_SIZE -
                             PROTOCOL_E2E_SIGNATURE_SIZE)
    {
        return 0;
    }

    return PROTOCOL_E2E_HEADER_SIZE + (size_t)payload_length +
           PROTOCOL_E2E_SIGNATURE_SIZE;
}

ProtocolStatus protocol_encode_unsigned_packet(
    uint8_t *output,
    size_t output_size,
    ProtocolPacketKind kind,
    const uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE],
    const uint8_t sender_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE],
    const uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE],
    const uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE],
    const uint8_t *payload,
    uint32_t payload_length,
    size_t *bytes_written)
{
    size_t unsigned_size;

    if (output == NULL || packet_id == NULL || sender_public_key == NULL ||
        recipient_id == NULL || chat_id == NULL || bytes_written == NULL ||
        (payload_length > 0 && payload == NULL))
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (!is_packet_kind_valid((uint8_t)kind))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (protocol_packet_size(payload_length) == 0)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    unsigned_size = PROTOCOL_E2E_HEADER_SIZE + (size_t)payload_length;

    if (output_size < unsigned_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    encode_packet_header(
        output,
        kind,
        packet_id,
        sender_public_key,
        recipient_id,
        chat_id,
        payload_length);
    if (payload_length > 0)
    {
        memcpy(output + PROTOCOL_E2E_HEADER_SIZE, payload, payload_length);
    }
    *bytes_written = unsigned_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_append_packet_signature(
    uint8_t *packet,
    size_t packet_capacity,
    size_t unsigned_packet_size,
    const uint8_t signature[PROTOCOL_SIGNATURE_SIZE],
    size_t *bytes_written)
{
    size_t total_size;

    if (packet == NULL || signature == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (unsigned_packet_size < PROTOCOL_E2E_HEADER_SIZE ||
        unsigned_packet_size > PROTOCOL_MAX_PACKET_SIZE - PROTOCOL_SIGNATURE_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    total_size = unsigned_packet_size + PROTOCOL_SIGNATURE_SIZE;

    if (memcmp(packet, PACKET_MAGIC, sizeof(PACKET_MAGIC)) != 0 ||
        packet[4] != PROTOCOL_VERSION ||
        !is_packet_kind_valid(packet[5]) ||
        read_u16(packet + 6) != 0 ||
        PROTOCOL_E2E_HEADER_SIZE + (size_t)read_u32(packet + 104) !=
            unsigned_packet_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (packet_capacity < total_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(packet + unsigned_packet_size, signature, PROTOCOL_SIGNATURE_SIZE);
    *bytes_written = total_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_packet(
    const uint8_t *input,
    size_t input_size,
    ProtocolPacket *packet)
{
    uint32_t payload_length;
    size_t expected_size;

    if (input == NULL || packet == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (input_size < protocol_packet_size(0) || input_size > PROTOCOL_MAX_PACKET_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (memcmp(input, PACKET_MAGIC, sizeof(PACKET_MAGIC)) != 0)
    {
        return PROTOCOL_STATUS_INVALID_MAGIC;
    }

    if (input[4] != PROTOCOL_VERSION)
    {
        return PROTOCOL_STATUS_UNSUPPORTED_VERSION;
    }

    if (!is_packet_kind_valid(input[5]))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (read_u16(input + 6) != 0)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    payload_length = read_u32(input + 104);
    if (payload_length > PROTOCOL_MAX_PACKET_SIZE -
                             PROTOCOL_E2E_HEADER_SIZE -
                             PROTOCOL_E2E_SIGNATURE_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    expected_size = protocol_packet_size(payload_length);
    if (expected_size != input_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    packet->kind = (ProtocolPacketKind)input[5];
    packet->flags = 0;
    memcpy(packet->packet_id, input + 8, PROTOCOL_PACKET_ID_SIZE);
    memcpy(packet->sender_public_key, input + 24, PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    memcpy(packet->recipient_id, input + 56, PROTOCOL_IDENTITY_ID_SIZE);
    memcpy(packet->chat_id, input + 88, PROTOCOL_CHAT_ID_SIZE);
    packet->payload = input + PROTOCOL_E2E_HEADER_SIZE;
    packet->payload_length = payload_length;
    packet->signature = packet->payload + payload_length;
    packet->signed_bytes = input;
    packet->signed_length = input_size - PROTOCOL_SIGNATURE_SIZE;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_build_chat_key_signature_data(
    uint8_t *output,
    size_t output_size,
    const uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE],
    uint32_t key_version,
    const uint8_t member_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE],
    const uint8_t chat_public_key[PROTOCOL_CHAT_PUBLIC_KEY_SIZE],
    size_t *bytes_written)
{
    const size_t domain_size = sizeof(CHAT_KEY_DOMAIN) - 1;
    const size_t required_size = domain_size + PROTOCOL_CHAT_ID_SIZE + 4u +
                                 PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE +
                                 PROTOCOL_CHAT_PUBLIC_KEY_SIZE;
    size_t offset = 0;

    if (output == NULL || chat_id == NULL || member_public_key == NULL ||
        chat_public_key == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (key_version == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output + offset, CHAT_KEY_DOMAIN, domain_size);
    offset += domain_size;
    memcpy(output + offset, chat_id, PROTOCOL_CHAT_ID_SIZE);
    offset += PROTOCOL_CHAT_ID_SIZE;
    write_u32(output + offset, key_version);
    offset += 4;
    memcpy(output + offset, member_public_key, PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    offset += PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE;
    memcpy(output + offset, chat_public_key, PROTOCOL_CHAT_PUBLIC_KEY_SIZE);
    offset += PROTOCOL_CHAT_PUBLIC_KEY_SIZE;

    *bytes_written = offset;
    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_chat_key_payload(
    uint8_t *output,
    size_t output_size,
    const ProtocolChatKey *chat_key,
    size_t *bytes_written)
{
    if (output == NULL || chat_key == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (chat_key->key_version == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    if (output_size < PROTOCOL_CHAT_KEY_PAYLOAD_SIZE)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    write_u32(output, chat_key->key_version);
    memcpy(output + 4, chat_key->public_key, PROTOCOL_CHAT_PUBLIC_KEY_SIZE);
    memcpy(output + 36, chat_key->signature, PROTOCOL_SIGNATURE_SIZE);
    *bytes_written = PROTOCOL_CHAT_KEY_PAYLOAD_SIZE;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_chat_key_payload(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolChatKey *chat_key)
{
    if (payload == NULL || chat_key == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (payload_size != PROTOCOL_CHAT_KEY_PAYLOAD_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (read_u32(payload) == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    chat_key->key_version = read_u32(payload);
    memcpy(chat_key->public_key, payload + 4, PROTOCOL_CHAT_PUBLIC_KEY_SIZE);
    memcpy(chat_key->signature, payload + 36, PROTOCOL_SIGNATURE_SIZE);

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_build_chat_key_context_data(
    uint8_t *output,
    size_t output_size,
    const uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE],
    const uint8_t lower_identity_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE],
    uint32_t lower_key_version,
    const uint8_t higher_identity_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE],
    uint32_t higher_key_version,
    size_t *bytes_written)
{
    const size_t domain_size = sizeof(CHAT_CONTEXT_DOMAIN) - 1;
    const size_t required_size = domain_size + PROTOCOL_CHAT_ID_SIZE +
                                 PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE + 4u +
                                 PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE + 4u;
    size_t offset = 0;

    if (output == NULL || chat_id == NULL ||
        lower_identity_public_key == NULL ||
        higher_identity_public_key == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (lower_key_version == 0 || higher_key_version == 0 ||
        memcmp(lower_identity_public_key,
               higher_identity_public_key,
               PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE) >= 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output, CHAT_CONTEXT_DOMAIN, domain_size);
    offset += domain_size;
    memcpy(output + offset, chat_id, PROTOCOL_CHAT_ID_SIZE);
    offset += PROTOCOL_CHAT_ID_SIZE;
    memcpy(output + offset,
           lower_identity_public_key,
           PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    offset += PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE;
    write_u32(output + offset, lower_key_version);
    offset += 4;
    memcpy(output + offset,
           higher_identity_public_key,
           PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE);
    offset += PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE;
    write_u32(output + offset, higher_key_version);
    offset += 4;

    *bytes_written = offset;
    return PROTOCOL_STATUS_OK;
}

size_t protocol_encrypted_payload_size(uint32_t ciphertext_length)
{
    if (ciphertext_length > PROTOCOL_MAX_PACKET_SIZE -
                                PROTOCOL_E2E_HEADER_SIZE -
                                PROTOCOL_E2E_SIGNATURE_SIZE -
                                PROTOCOL_ENCRYPTED_PAYLOAD_HEADER_SIZE)
    {
        return 0;
    }

    return PROTOCOL_ENCRYPTED_PAYLOAD_HEADER_SIZE + (size_t)ciphertext_length;
}

ProtocolStatus protocol_encode_encrypted_packet_prefix(
    uint8_t *output,
    size_t output_size,
    const uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE],
    const uint8_t sender_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE],
    const uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE],
    const uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE],
    const uint8_t message_id[PROTOCOL_MESSAGE_ID_SIZE],
    uint32_t sender_key_version,
    uint32_t recipient_key_version,
    const uint8_t nonce[PROTOCOL_NONCE_SIZE],
    uint32_t ciphertext_length,
    size_t *bytes_written)
{
    const size_t prefix_size = PROTOCOL_E2E_HEADER_SIZE +
                               PROTOCOL_ENCRYPTED_PAYLOAD_HEADER_SIZE;
    uint32_t payload_length;
    uint8_t *payload;

    if (output == NULL || packet_id == NULL || sender_public_key == NULL ||
        recipient_id == NULL || chat_id == NULL || message_id == NULL ||
        nonce == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (sender_key_version == 0 || recipient_key_version == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    if (ciphertext_length < PROTOCOL_AEAD_TAG_SIZE ||
        ciphertext_length > PROTOCOL_MAX_PACKET_SIZE -
                                PROTOCOL_E2E_HEADER_SIZE -
                                PROTOCOL_E2E_SIGNATURE_SIZE -
                                PROTOCOL_ENCRYPTED_PAYLOAD_HEADER_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    payload_length = (uint32_t)protocol_encrypted_payload_size(ciphertext_length);
    if (output_size < protocol_packet_size(payload_length))
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    encode_packet_header(
        output,
        PROTOCOL_PACKET_ENCRYPTED_MESSAGE,
        packet_id,
        sender_public_key,
        recipient_id,
        chat_id,
        payload_length);

    payload = output + PROTOCOL_E2E_HEADER_SIZE;
    memcpy(payload, message_id, PROTOCOL_MESSAGE_ID_SIZE);
    write_u32(payload + 16, sender_key_version);
    write_u32(payload + 20, recipient_key_version);
    memcpy(payload + 24, nonce, PROTOCOL_NONCE_SIZE);
    write_u32(payload + 48, ciphertext_length);

    *bytes_written = prefix_size;
    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_encrypted_message(
    const ProtocolPacket *packet,
    ProtocolEncryptedMessage *message)
{
    uint32_t ciphertext_length;

    if (packet == NULL || message == NULL || packet->payload == NULL ||
        packet->signed_bytes == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (packet->kind != PROTOCOL_PACKET_ENCRYPTED_MESSAGE)
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (packet->payload_length < PROTOCOL_ENCRYPTED_PAYLOAD_HEADER_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    ciphertext_length = read_u32(packet->payload + 48);
    if (ciphertext_length < PROTOCOL_AEAD_TAG_SIZE ||
        protocol_encrypted_payload_size(ciphertext_length) != packet->payload_length)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    memcpy(message->message_id, packet->payload, PROTOCOL_MESSAGE_ID_SIZE);
    message->sender_key_version = read_u32(packet->payload + 16);
    message->recipient_key_version = read_u32(packet->payload + 20);
    if (message->sender_key_version == 0 || message->recipient_key_version == 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    memcpy(message->nonce, packet->payload + 24, PROTOCOL_NONCE_SIZE);
    message->ciphertext_length = ciphertext_length;
    message->ciphertext = packet->payload + PROTOCOL_ENCRYPTED_PAYLOAD_HEADER_SIZE;
    message->authenticated_data = packet->signed_bytes;
    message->authenticated_data_length = PROTOCOL_E2E_HEADER_SIZE +
                                         PROTOCOL_ENCRYPTED_PAYLOAD_HEADER_SIZE;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_validate_send_packet(
    const ProtocolSend *send,
    const ProtocolPacket *packet,
    const uint8_t authenticated_sender_public_key[
        PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE])
{
    if (send == NULL || packet == NULL ||
        authenticated_sender_public_key == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (memcmp(send->packet_id, packet->packet_id, PROTOCOL_PACKET_ID_SIZE) != 0 ||
        memcmp(send->recipient_id, packet->recipient_id, PROTOCOL_IDENTITY_ID_SIZE) != 0 ||
        memcmp(packet->sender_public_key,
               authenticated_sender_public_key,
               PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE) != 0 ||
        send->packet_length != packet->signed_length + PROTOCOL_SIGNATURE_SIZE)
    {
        return PROTOCOL_STATUS_ID_MISMATCH;
    }

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_validate_delivered_packet(
    const ProtocolDeliver *deliver,
    const ProtocolPacket *packet,
    const uint8_t local_identity_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE])
{
    if (deliver == NULL || packet == NULL || local_identity_public_key == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (memcmp(deliver->packet_id, packet->packet_id, PROTOCOL_PACKET_ID_SIZE) != 0 ||
        memcmp(deliver->sender_id,
               packet->sender_public_key,
               PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE) != 0 ||
        memcmp(packet->recipient_id,
               local_identity_public_key,
               PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE) != 0 ||
        deliver->packet_length != packet->signed_length + PROTOCOL_SIGNATURE_SIZE)
    {
        return PROTOCOL_STATUS_ID_MISMATCH;
    }

    return PROTOCOL_STATUS_OK;
}

size_t protocol_content_size(uint32_t data_length)
{
    if (data_length > PROTOCOL_MAX_PACKET_SIZE - PROTOCOL_CONTENT_HEADER_SIZE)
    {
        return 0;
    }

    return PROTOCOL_CONTENT_HEADER_SIZE + (size_t)data_length;
}

ProtocolStatus protocol_encode_content(
    uint8_t *output,
    size_t output_size,
    ProtocolContentType type,
    const uint8_t *data,
    uint32_t data_length,
    size_t *bytes_written)
{
    size_t required_size;

    if (output == NULL || bytes_written == NULL ||
        (data_length > 0 && data == NULL))
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (!is_content_type_valid((uint8_t)type))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (data_length > PROTOCOL_MAX_PACKET_SIZE - PROTOCOL_CONTENT_HEADER_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    required_size = protocol_content_size(data_length);

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    output[0] = PROTOCOL_VERSION;
    output[1] = (uint8_t)type;
    write_u16(output + 2, 0);
    write_u32(output + 4, data_length);
    if (data_length > 0)
    {
        memcpy(output + PROTOCOL_CONTENT_HEADER_SIZE, data, data_length);
    }
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_content(
    const uint8_t *input,
    size_t input_size,
    ProtocolContent *content)
{
    uint32_t data_length;

    if (input == NULL || content == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (input_size < PROTOCOL_CONTENT_HEADER_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (input[0] != PROTOCOL_VERSION)
    {
        return PROTOCOL_STATUS_UNSUPPORTED_VERSION;
    }

    if (!is_content_type_valid(input[1]))
    {
        return PROTOCOL_STATUS_INVALID_TYPE;
    }

    if (read_u16(input + 2) != 0)
    {
        return PROTOCOL_STATUS_INVALID_VALUE;
    }

    data_length = read_u32(input + 4);
    if (data_length > PROTOCOL_MAX_PACKET_SIZE - PROTOCOL_CONTENT_HEADER_SIZE)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    if (protocol_content_size(data_length) != input_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    content->type = (ProtocolContentType)input[1];
    content->flags = 0;
    content->data_length = data_length;
    content->data = input + PROTOCOL_CONTENT_HEADER_SIZE;

    return PROTOCOL_STATUS_OK;
}

size_t protocol_text_data_size(uint32_t text_length)
{
    if (text_length > PROTOCOL_MAX_TEXT_SIZE)
    {
        return 0;
    }

    return 12u + (size_t)text_length;
}

ProtocolStatus protocol_encode_text_data(
    uint8_t *output,
    size_t output_size,
    const ProtocolText *text,
    size_t *bytes_written)
{
    size_t required_size;

    if (output == NULL || text == NULL || bytes_written == NULL ||
        (text->text_length > 0 && text->text == NULL))
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (text->text_length > PROTOCOL_MAX_TEXT_SIZE ||
        !is_valid_utf8(text->text, text->text_length))
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    required_size = protocol_text_data_size(text->text_length);
    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    write_u64(output, text->sender_timestamp_unix_ms);
    write_u32(output + 8, text->text_length);
    if (text->text_length > 0)
    {
        memcpy(output + 12, text->text, text->text_length);
    }
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_text_data(
    const uint8_t *data,
    size_t data_size,
    ProtocolText *text)
{
    uint32_t text_length;

    if (data == NULL || text == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (data_size < 12u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    text_length = read_u32(data + 8);
    if (text_length > PROTOCOL_MAX_TEXT_SIZE ||
        protocol_text_data_size(text_length) != data_size ||
        !is_valid_utf8(data + 12, text_length))
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    text->sender_timestamp_unix_ms = read_u64(data);
    text->text_length = text_length;
    text->text = data + 12;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_delivery_receipt_data(
    uint8_t *output,
    size_t output_size,
    const ProtocolDeliveryReceipt *receipt,
    size_t *bytes_written)
{
    const size_t required_size = PROTOCOL_MESSAGE_ID_SIZE + 8u;

    if (output == NULL || receipt == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (output_size < required_size)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(output, receipt->message_id, PROTOCOL_MESSAGE_ID_SIZE);
    write_u64(output + PROTOCOL_MESSAGE_ID_SIZE,
              receipt->recipient_timestamp_unix_ms);
    *bytes_written = required_size;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_delivery_receipt_data(
    const uint8_t *data,
    size_t data_size,
    ProtocolDeliveryReceipt *receipt)
{
    const size_t required_size = PROTOCOL_MESSAGE_ID_SIZE + 8u;

    if (data == NULL || receipt == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (data_size != required_size)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    memcpy(receipt->message_id, data, PROTOCOL_MESSAGE_ID_SIZE);
    receipt->recipient_timestamp_unix_ms = read_u64(data + PROTOCOL_MESSAGE_ID_SIZE);

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_encode_chat_leave_data(
    uint8_t *output,
    size_t output_size,
    uint64_t sender_timestamp_unix_ms,
    size_t *bytes_written)
{
    if (output == NULL || bytes_written == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (output_size < 8u)
    {
        return PROTOCOL_STATUS_BUFFER_TOO_SMALL;
    }

    write_u64(output, sender_timestamp_unix_ms);
    *bytes_written = 8u;

    return PROTOCOL_STATUS_OK;
}

ProtocolStatus protocol_decode_chat_leave_data(
    const uint8_t *data,
    size_t data_size,
    uint64_t *sender_timestamp_unix_ms)
{
    if (data == NULL || sender_timestamp_unix_ms == NULL)
    {
        return PROTOCOL_STATUS_INVALID_ARGUMENT;
    }

    if (data_size != 8u)
    {
        return PROTOCOL_STATUS_INVALID_LENGTH;
    }

    *sender_timestamp_unix_ms = read_u64(data);
    return PROTOCOL_STATUS_OK;
}
