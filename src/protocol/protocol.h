#ifndef SOCKET_CHAT_PROTOCOL_H
#define SOCKET_CHAT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/*
 * This module only translates between C values and portable network bytes.
 * It does not open sockets, allocate memory, access SQLite, or run crypto.
 *
 * Decode functions return views into the supplied input buffer for variable
 * data such as packet bodies and text. Keep that buffer alive while using the
 * decoded view, or copy the bytes into longer-lived storage.
 *
 * In version 1 an identity ID is the raw 32-byte Ed25519 public key. Keeping
 * one representation avoids disagreements between routing and signatures.
 */

#define PROTOCOL_VERSION 1u

#define PROTOCOL_FRAME_HEADER_SIZE 16u
#define PROTOCOL_MAX_SERVER_PAYLOAD (256u * 1024u)
#define PROTOCOL_MAX_PACKET_SIZE (128u * 1024u)
#define PROTOCOL_MAX_TEXT_SIZE (64u * 1024u)
#define PROTOCOL_MAX_ERROR_MESSAGE_SIZE 512u
#define PROTOCOL_MIN_USERNAME_SIZE 3u
#define PROTOCOL_MAX_USERNAME_SIZE 24u
#define PROTOCOL_AEAD_TAG_SIZE 16u

#define PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE 32u
#define PROTOCOL_IDENTITY_ID_SIZE 32u
#define PROTOCOL_X25519_PUBLIC_KEY_SIZE 32u
#define PROTOCOL_CHAT_PUBLIC_KEY_SIZE PROTOCOL_X25519_PUBLIC_KEY_SIZE
#define PROTOCOL_SIGNATURE_SIZE 64u
#define PROTOCOL_CHALLENGE_SIZE 32u
#define PROTOCOL_PACKET_ID_SIZE 16u
#define PROTOCOL_MESSAGE_ID_SIZE 16u
#define PROTOCOL_CHAT_ID_SIZE 16u
#define PROTOCOL_NONCE_SIZE 24u

#define PROTOCOL_E2E_HEADER_SIZE 108u
#define PROTOCOL_E2E_SIGNATURE_SIZE 64u
#define PROTOCOL_CHAT_KEY_PAYLOAD_SIZE 100u
#define PROTOCOL_ENCRYPTED_PAYLOAD_HEADER_SIZE 52u
#define PROTOCOL_CONTENT_HEADER_SIZE 8u
#define PROTOCOL_HELLO_SIZE 96u
#define PROTOCOL_CHALLENGE_PAYLOAD_SIZE 160u
#define PROTOCOL_TRANSPORT_HEADER_SIZE 12u
#define PROTOCOL_MAX_TRANSPORT_CIPHERTEXT \
    (PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_MAX_SERVER_PAYLOAD + PROTOCOL_AEAD_TAG_SIZE)

typedef enum ProtocolStatus
{
    PROTOCOL_STATUS_OK = 0,
    PROTOCOL_STATUS_NEED_MORE_DATA,
    PROTOCOL_STATUS_INVALID_ARGUMENT,
    PROTOCOL_STATUS_INVALID_MAGIC,
    PROTOCOL_STATUS_UNSUPPORTED_VERSION,
    PROTOCOL_STATUS_INVALID_TYPE,
    PROTOCOL_STATUS_INVALID_VALUE,
    PROTOCOL_STATUS_INVALID_LENGTH,
    PROTOCOL_STATUS_BUFFER_TOO_SMALL,
    PROTOCOL_STATUS_ID_MISMATCH
} ProtocolStatus;

typedef enum ProtocolFrameType
{
    PROTOCOL_FRAME_HELLO = 0x01,
    PROTOCOL_FRAME_CHALLENGE = 0x02,
    PROTOCOL_FRAME_AUTHENTICATE = 0x03,
    PROTOCOL_FRAME_AUTHENTICATED = 0x04,

    PROTOCOL_FRAME_REGISTER_USERNAME = 0x08,
    PROTOCOL_FRAME_REGISTER_USERNAME_RESULT = 0x09,
    PROTOCOL_FRAME_LOOKUP_USERNAME = 0x0a,
    PROTOCOL_FRAME_LOOKUP_USERNAME_RESULT = 0x0b,

    PROTOCOL_FRAME_SEND = 0x10,
    PROTOCOL_FRAME_SEND_ACCEPTED = 0x11,
    PROTOCOL_FRAME_DELIVER = 0x12,
    PROTOCOL_FRAME_ACKNOWLEDGE = 0x13,
    PROTOCOL_FRAME_ACKNOWLEDGED = 0x14,

    PROTOCOL_FRAME_PING = 0x20,
    PROTOCOL_FRAME_PONG = 0x21,
    PROTOCOL_FRAME_ERROR = 0x7f
} ProtocolFrameType;

typedef enum ProtocolPacketKind
{
    PROTOCOL_PACKET_CHAT_KEY_OFFER = 0x01,
    PROTOCOL_PACKET_CHAT_KEY_ACCEPT = 0x02,
    PROTOCOL_PACKET_ENCRYPTED_MESSAGE = 0x03,
    /* A signed direct-text packet. Its transport is encrypted client-to-server. */
    PROTOCOL_PACKET_TEXT_MESSAGE = 0x04
} ProtocolPacketKind;

typedef enum ProtocolAcknowledgeResult
{
    PROTOCOL_ACK_STORED = 0x01,
    PROTOCOL_ACK_DUPLICATE = 0x02,
    PROTOCOL_ACK_REJECTED = 0x03
} ProtocolAcknowledgeResult;

typedef enum ProtocolUsernameRegistrationResult
{
    PROTOCOL_USERNAME_REGISTERED = 0x01,
    PROTOCOL_USERNAME_TAKEN = 0x02,
    PROTOCOL_USERNAME_INVALID = 0x03,
    PROTOCOL_IDENTITY_ALREADY_REGISTERED = 0x04
} ProtocolUsernameRegistrationResult;

typedef enum ProtocolUsernameLookupStatus
{
    PROTOCOL_USERNAME_FOUND = 0x01,
    PROTOCOL_USERNAME_NOT_FOUND = 0x02
} ProtocolUsernameLookupStatus;

typedef enum ProtocolContentType
{
    PROTOCOL_CONTENT_TEXT = 0x01,
    PROTOCOL_CONTENT_DELIVERY_RECEIPT = 0x02,
    PROTOCOL_CONTENT_CHAT_KEY_UPDATE = 0x11,
    PROTOCOL_CONTENT_CHAT_LEAVE = 0x12
} ProtocolContentType;

typedef enum ProtocolErrorCode
{
    PROTOCOL_ERROR_UNSUPPORTED_VERSION = 0x0001,
    PROTOCOL_ERROR_MALFORMED_FRAME = 0x0002,
    PROTOCOL_ERROR_FRAME_TOO_LARGE = 0x0003,
    PROTOCOL_ERROR_AUTH_REQUIRED = 0x0004,
    PROTOCOL_ERROR_AUTH_FAILED = 0x0005,
    PROTOCOL_ERROR_INVALID_REQUEST = 0x0006,
    PROTOCOL_ERROR_PACKET_TOO_LARGE = 0x0007,
    PROTOCOL_ERROR_QUEUE_FULL = 0x0008,
    PROTOCOL_ERROR_PACKET_ID_CONFLICT = 0x0009,
    PROTOCOL_ERROR_STORAGE_FAILURE = 0x000a,
    PROTOCOL_ERROR_RATE_LIMITED = 0x000b
} ProtocolErrorCode;

typedef struct ProtocolFrame
{
    ProtocolFrameType type;
    uint16_t flags;
    uint32_t request_id;
    const uint8_t *payload;
    uint32_t payload_length;
} ProtocolFrame;

typedef struct ProtocolAuthenticated
{
    uint8_t identity_id[PROTOCOL_IDENTITY_ID_SIZE];
    uint32_t queued_packet_count;
} ProtocolAuthenticated;

typedef struct ProtocolHello
{
    uint8_t identity_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE];
    uint8_t client_nonce[PROTOCOL_CHALLENGE_SIZE];
    uint8_t client_ephemeral_public_key[PROTOCOL_X25519_PUBLIC_KEY_SIZE];
} ProtocolHello;

typedef struct ProtocolChallenge
{
    uint8_t server_identity_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE];
    uint8_t server_nonce[PROTOCOL_CHALLENGE_SIZE];
    uint8_t server_ephemeral_public_key[PROTOCOL_X25519_PUBLIC_KEY_SIZE];
    uint8_t server_signature[PROTOCOL_SIGNATURE_SIZE];
} ProtocolChallenge;

typedef struct ProtocolTransportRecord
{
    uint64_t sequence;
    const uint8_t *ciphertext;
    uint32_t ciphertext_length;
} ProtocolTransportRecord;

typedef struct ProtocolDeliveryReference
{
    uint64_t delivery_id;
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE];
} ProtocolDeliveryReference;

typedef struct ProtocolUsername
{
    /* Lowercase ASCII: a-z, 0-9, and underscore. Not null-terminated on decode. */
    const char *text;
    uint8_t length;
} ProtocolUsername;

typedef struct ProtocolUsernameLookupResult
{
    ProtocolUsernameLookupStatus status;
    ProtocolUsername username;
    uint8_t identity_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE];
} ProtocolUsernameLookupResult;

typedef struct ProtocolSend
{
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE];
    uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE];
    uint32_t expires_in_seconds;
    const uint8_t *packet;
    uint32_t packet_length;
} ProtocolSend;

typedef struct ProtocolDeliver
{
    uint64_t delivery_id;
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE];
    uint8_t sender_id[PROTOCOL_IDENTITY_ID_SIZE];
    uint64_t server_received_at_unix_ms;
    const uint8_t *packet;
    uint32_t packet_length;
} ProtocolDeliver;

typedef struct ProtocolAcknowledge
{
    uint64_t delivery_id;
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE];
    ProtocolAcknowledgeResult result;
} ProtocolAcknowledge;

typedef struct ProtocolError
{
    ProtocolErrorCode code;
    const uint8_t *message;
    uint16_t message_length;
} ProtocolError;

typedef struct ProtocolPacket
{
    ProtocolPacketKind kind;
    uint16_t flags;
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE];
    uint8_t sender_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE];
    uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE];
    uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE];
    const uint8_t *payload;
    uint32_t payload_length;
    const uint8_t *signature;
    const uint8_t *signed_bytes;
    size_t signed_length;
} ProtocolPacket;

typedef struct ProtocolChatKey
{
    uint32_t key_version;
    uint8_t public_key[PROTOCOL_CHAT_PUBLIC_KEY_SIZE];
    uint8_t signature[PROTOCOL_SIGNATURE_SIZE];
} ProtocolChatKey;

typedef struct ProtocolEncryptedMessage
{
    uint8_t message_id[PROTOCOL_MESSAGE_ID_SIZE];
    uint32_t sender_key_version;
    uint32_t recipient_key_version;
    uint8_t nonce[PROTOCOL_NONCE_SIZE];
    const uint8_t *ciphertext;
    uint32_t ciphertext_length;
    const uint8_t *authenticated_data;
    size_t authenticated_data_length;
} ProtocolEncryptedMessage;

typedef struct ProtocolContent
{
    ProtocolContentType type;
    uint16_t flags;
    const uint8_t *data;
    uint32_t data_length;
} ProtocolContent;

typedef struct ProtocolText
{
    uint64_t sender_timestamp_unix_ms;
    const uint8_t *text;
    uint32_t text_length;
} ProtocolText;

typedef struct ProtocolDeliveryReceipt
{
    uint8_t message_id[PROTOCOL_MESSAGE_ID_SIZE];
    uint64_t recipient_timestamp_unix_ms;
} ProtocolDeliveryReceipt;

const char *protocol_status_string(ProtocolStatus status);

/*
 * Connection setup:
 *   1. Client sends an unencrypted HELLO frame.
 *   2. Server signs the complete handshake and sends CHALLENGE.
 *   3. Client checks the signature against its pinned server public key.
 *   4. Client signs the client-auth transcript and sends AUTHENTICATE.
 *   5. Both sides derive directional keys from the ephemeral X25519 keys.
 *   6. Every later server frame is AEAD-encrypted inside a transport record.
 *   7. The server's first encrypted frame is AUTHENTICATED.
 *
 * crypto_kx provides a different key for each direction. Each direction owns
 * a separate sequence counter starting at zero. A receiver accepts exactly
 * the next sequence value, and the connection closes before a counter wraps.
 * Never use one transport key for both directions.
 */

/* Server frames */
size_t protocol_frame_size(uint32_t payload_length);
ProtocolStatus protocol_encode_frame(
    uint8_t *output,
    size_t output_size,
    ProtocolFrameType type,
    uint32_t request_id,
    const uint8_t *payload,
    uint32_t payload_length,
    size_t *bytes_written);
ProtocolStatus protocol_decode_frame(
    const uint8_t *input,
    size_t input_size,
    ProtocolFrame *frame,
    size_t *bytes_consumed);

ProtocolStatus protocol_encode_hello(
    uint8_t *output,
    size_t output_size,
    const ProtocolHello *hello,
    size_t *bytes_written);
ProtocolStatus protocol_decode_hello(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolHello *hello);

ProtocolStatus protocol_encode_challenge(
    uint8_t *output,
    size_t output_size,
    const ProtocolChallenge *challenge,
    size_t *bytes_written);
ProtocolStatus protocol_decode_challenge(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolChallenge *challenge);

ProtocolStatus protocol_encode_authenticate(
    uint8_t output[PROTOCOL_SIGNATURE_SIZE],
    const uint8_t signature[PROTOCOL_SIGNATURE_SIZE]);
ProtocolStatus protocol_decode_authenticate(
    const uint8_t *payload,
    size_t payload_size,
    uint8_t signature[PROTOCOL_SIGNATURE_SIZE]);

ProtocolStatus protocol_build_server_auth_signature_data(
    uint8_t *output,
    size_t output_size,
    const ProtocolHello *hello,
    const ProtocolChallenge *challenge,
    size_t *bytes_written);
ProtocolStatus protocol_build_client_auth_signature_data(
    uint8_t *output,
    size_t output_size,
    const ProtocolHello *hello,
    const ProtocolChallenge *challenge,
    size_t *bytes_written);

size_t protocol_transport_record_size(uint32_t ciphertext_length);
ProtocolStatus protocol_encode_transport_record_prefix(
    uint8_t *output,
    size_t output_size,
    uint64_t sequence,
    uint32_t ciphertext_length,
    size_t *bytes_written);
ProtocolStatus protocol_decode_transport_record(
    const uint8_t *input,
    size_t input_size,
    ProtocolTransportRecord *record,
    size_t *bytes_consumed);
ProtocolStatus protocol_build_transport_nonce(
    uint8_t output[PROTOCOL_NONCE_SIZE],
    uint64_t sequence);
ProtocolStatus protocol_build_transport_authenticated_data(
    uint8_t *output,
    size_t output_size,
    uint64_t sequence,
    uint32_t ciphertext_length,
    size_t *bytes_written);

ProtocolStatus protocol_encode_authenticated(
    uint8_t *output,
    size_t output_size,
    const ProtocolAuthenticated *message,
    size_t *bytes_written);
ProtocolStatus protocol_decode_authenticated(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolAuthenticated *message);

/* Username messages are sent only after authentication. */
int protocol_username_is_valid(const char *username, size_t username_length);
size_t protocol_username_payload_size(uint8_t username_length);
ProtocolStatus protocol_encode_username(
    uint8_t *output,
    size_t output_size,
    const ProtocolUsername *username,
    size_t *bytes_written);
ProtocolStatus protocol_decode_username(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolUsername *username);

ProtocolStatus protocol_encode_username_registration_result(
    uint8_t *output,
    size_t output_size,
    ProtocolUsernameRegistrationResult result,
    size_t *bytes_written);
ProtocolStatus protocol_decode_username_registration_result(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolUsernameRegistrationResult *result);

size_t protocol_username_lookup_result_size(
    ProtocolUsernameLookupStatus status,
    uint8_t username_length);
ProtocolStatus protocol_encode_username_lookup_result(
    uint8_t *output,
    size_t output_size,
    const ProtocolUsernameLookupResult *result,
    size_t *bytes_written);
ProtocolStatus protocol_decode_username_lookup_result(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolUsernameLookupResult *result);

size_t protocol_send_payload_size(uint32_t packet_length);
ProtocolStatus protocol_encode_send(
    uint8_t *output,
    size_t output_size,
    const ProtocolSend *message,
    size_t *bytes_written);
ProtocolStatus protocol_decode_send(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolSend *message);

ProtocolStatus protocol_encode_delivery_reference(
    uint8_t *output,
    size_t output_size,
    const ProtocolDeliveryReference *reference,
    size_t *bytes_written);
ProtocolStatus protocol_decode_delivery_reference(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolDeliveryReference *reference);

size_t protocol_deliver_payload_size(uint32_t packet_length);
ProtocolStatus protocol_encode_deliver(
    uint8_t *output,
    size_t output_size,
    const ProtocolDeliver *message,
    size_t *bytes_written);
ProtocolStatus protocol_decode_deliver(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolDeliver *message);

ProtocolStatus protocol_encode_acknowledge(
    uint8_t *output,
    size_t output_size,
    const ProtocolAcknowledge *message,
    size_t *bytes_written);
ProtocolStatus protocol_decode_acknowledge(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolAcknowledge *message);

ProtocolStatus protocol_encode_ping(
    uint8_t *output,
    size_t output_size,
    uint64_t token,
    size_t *bytes_written);
ProtocolStatus protocol_decode_ping(
    const uint8_t *payload,
    size_t payload_size,
    uint64_t *token);

size_t protocol_error_payload_size(uint16_t message_length);
ProtocolStatus protocol_encode_error(
    uint8_t *output,
    size_t output_size,
    const ProtocolError *message,
    size_t *bytes_written);
ProtocolStatus protocol_decode_error(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolError *message);

/*
 * Signed end-to-end packet construction:
 *   1. Encode the unsigned packet.
 *   2. Sign exactly the returned number of bytes with Ed25519.
 *   3. Append that detached signature.
 */

/* Signed end-to-end packets */
size_t protocol_packet_size(uint32_t payload_length);
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
    size_t *bytes_written);
ProtocolStatus protocol_append_packet_signature(
    uint8_t *packet,
    size_t packet_capacity,
    size_t unsigned_packet_size,
    const uint8_t signature[PROTOCOL_SIGNATURE_SIZE],
    size_t *bytes_written);
ProtocolStatus protocol_decode_packet(
    const uint8_t *input,
    size_t input_size,
    ProtocolPacket *packet);

/* Per-chat X25519 key records */
ProtocolStatus protocol_build_chat_key_signature_data(
    uint8_t *output,
    size_t output_size,
    const uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE],
    uint32_t key_version,
    const uint8_t member_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE],
    const uint8_t chat_public_key[PROTOCOL_CHAT_PUBLIC_KEY_SIZE],
    size_t *bytes_written);
ProtocolStatus protocol_encode_chat_key_payload(
    uint8_t *output,
    size_t output_size,
    const ProtocolChatKey *chat_key,
    size_t *bytes_written);
ProtocolStatus protocol_decode_chat_key_payload(
    const uint8_t *payload,
    size_t payload_size,
    ProtocolChatKey *chat_key);

/*
 * For a direct chat, the smaller Ed25519 public key uses the crypto_kx client
 * role and the larger key uses the server role. This stable ordering lets both
 * peers derive the same directional keys without extra negotiation.
 */
ProtocolStatus protocol_build_chat_key_context_data(
    uint8_t *output,
    size_t output_size,
    const uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE],
    const uint8_t lower_identity_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE],
    uint32_t lower_key_version,
    const uint8_t higher_identity_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE],
    uint32_t higher_key_version,
    size_t *bytes_written);

/*
 * The encrypted prefix is the AEAD additional data. Encrypt directly after
 * the prefix, sign the prefix plus ciphertext, then append the signature.
 */

/* Encrypted message packet payload */
size_t protocol_encrypted_payload_size(uint32_t ciphertext_length);
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
    size_t *bytes_written);
ProtocolStatus protocol_decode_encrypted_message(
    const ProtocolPacket *packet,
    ProtocolEncryptedMessage *message);
ProtocolStatus protocol_validate_send_packet(
    const ProtocolSend *send,
    const ProtocolPacket *packet,
    const uint8_t authenticated_sender_public_key[
        PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE]);
ProtocolStatus protocol_validate_delivered_packet(
    const ProtocolDeliver *deliver,
    const ProtocolPacket *packet,
    const uint8_t local_identity_public_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE]);

/* Plaintext content that is encrypted inside a message packet */
size_t protocol_content_size(uint32_t data_length);
ProtocolStatus protocol_encode_content(
    uint8_t *output,
    size_t output_size,
    ProtocolContentType type,
    const uint8_t *data,
    uint32_t data_length,
    size_t *bytes_written);
ProtocolStatus protocol_decode_content(
    const uint8_t *input,
    size_t input_size,
    ProtocolContent *content);

size_t protocol_text_data_size(uint32_t text_length);
ProtocolStatus protocol_encode_text_data(
    uint8_t *output,
    size_t output_size,
    const ProtocolText *text,
    size_t *bytes_written);
ProtocolStatus protocol_decode_text_data(
    const uint8_t *data,
    size_t data_size,
    ProtocolText *text);

ProtocolStatus protocol_encode_delivery_receipt_data(
    uint8_t *output,
    size_t output_size,
    const ProtocolDeliveryReceipt *receipt,
    size_t *bytes_written);
ProtocolStatus protocol_decode_delivery_receipt_data(
    const uint8_t *data,
    size_t data_size,
    ProtocolDeliveryReceipt *receipt);

ProtocolStatus protocol_encode_chat_leave_data(
    uint8_t *output,
    size_t output_size,
    uint64_t sender_timestamp_unix_ms,
    size_t *bytes_written);
ProtocolStatus protocol_decode_chat_leave_data(
    const uint8_t *data,
    size_t data_size,
    uint64_t *sender_timestamp_unix_ms);

#endif
