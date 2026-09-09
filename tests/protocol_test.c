#include "protocol/protocol.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
            return 0;                                                           \
        }                                                                       \
    } while (0)

static void fill_bytes(uint8_t *bytes, size_t size, uint8_t first_value)
{
    size_t index;

    for (index = 0; index < size; ++index)
    {
        bytes[index] = (uint8_t)(first_value + index);
    }
}

static int test_frame_round_trip(void)
{
    const uint8_t payload[] = { 0xaa, 0xbb, 0xcc };
    uint8_t encoded[64];
    size_t encoded_size = 0;
    size_t consumed = 0;
    ProtocolFrame decoded;

    CHECK(protocol_encode_frame(encoded, sizeof(encoded), PROTOCOL_FRAME_PING,
              0x01020304u, payload, sizeof(payload), &encoded_size) ==
          PROTOCOL_STATUS_OK);
    CHECK(encoded_size == PROTOCOL_FRAME_HEADER_SIZE + sizeof(payload));
    CHECK(encoded[0] == 'G' && encoded[1] == 'S' &&
          encoded[2] == 'C' && encoded[3] == '1');
    CHECK(encoded[8] == 0x01 && encoded[9] == 0x02 &&
          encoded[10] == 0x03 && encoded[11] == 0x04);
    CHECK(protocol_decode_frame(encoded, PROTOCOL_FRAME_HEADER_SIZE - 1,
              &decoded, &consumed) == PROTOCOL_STATUS_NEED_MORE_DATA);
    CHECK(protocol_decode_frame(encoded, encoded_size - 1,
              &decoded, &consumed) == PROTOCOL_STATUS_NEED_MORE_DATA);
    CHECK(protocol_decode_frame(encoded, encoded_size, &decoded, &consumed) ==
          PROTOCOL_STATUS_OK);
    CHECK(consumed == encoded_size);
    CHECK(decoded.type == PROTOCOL_FRAME_PING);
    CHECK(decoded.request_id == 0x01020304u);
    CHECK(decoded.payload_length == sizeof(payload));
    CHECK(memcmp(decoded.payload, payload, sizeof(payload)) == 0);

    encoded[0] = 'X';
    CHECK(protocol_decode_frame(encoded, encoded_size, &decoded, &consumed) ==
          PROTOCOL_STATUS_INVALID_MAGIC);
    return 1;
}

static int test_authentication_payloads(void)
{
    ProtocolHello hello = { 0 };
    ProtocolHello decoded_hello;
    ProtocolChallenge challenge = { 0 };
    ProtocolChallenge decoded_challenge;
    uint8_t hello_bytes[PROTOCOL_HELLO_SIZE];
    uint8_t challenge_bytes[PROTOCOL_CHALLENGE_PAYLOAD_SIZE];
    uint8_t signature_data[256];
    uint8_t client_signature_data[256];
    uint8_t transport_record[128];
    uint8_t transport_nonce[PROTOCOL_NONCE_SIZE];
    uint8_t transport_aad[64];
    ProtocolTransportRecord decoded_record;
    size_t hello_size = 0;
    size_t challenge_size = 0;
    size_t signature_data_size = 0;
    size_t client_signature_data_size = 0;
    size_t prefix_size = 0;
    size_t aad_size = 0;
    size_t consumed = 0;

    fill_bytes(hello.identity_public_key, sizeof(hello.identity_public_key), 0x10);
    fill_bytes(hello.client_nonce, sizeof(hello.client_nonce), 0x30);
    fill_bytes(hello.client_ephemeral_public_key,
               sizeof(hello.client_ephemeral_public_key), 0x50);
    fill_bytes(challenge.server_identity_public_key,
               sizeof(challenge.server_identity_public_key), 0x70);
    fill_bytes(challenge.server_nonce, sizeof(challenge.server_nonce), 0x90);
    fill_bytes(challenge.server_ephemeral_public_key,
               sizeof(challenge.server_ephemeral_public_key), 0xb0);
    fill_bytes(challenge.server_signature,
               sizeof(challenge.server_signature), 0xd0);

    CHECK(protocol_encode_hello(hello_bytes, sizeof(hello_bytes), &hello,
              &hello_size) == PROTOCOL_STATUS_OK);
    CHECK(hello_size == PROTOCOL_HELLO_SIZE);
    CHECK(protocol_decode_hello(hello_bytes, hello_size, &decoded_hello) ==
          PROTOCOL_STATUS_OK);
    CHECK(memcmp(&hello, &decoded_hello, sizeof(hello)) == 0);

    CHECK(protocol_encode_challenge(challenge_bytes, sizeof(challenge_bytes),
              &challenge, &challenge_size) == PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_challenge(challenge_bytes, challenge_size,
              &decoded_challenge) == PROTOCOL_STATUS_OK);
    CHECK(memcmp(&challenge, &decoded_challenge, sizeof(challenge)) == 0);

    CHECK(protocol_build_server_auth_signature_data(signature_data,
              sizeof(signature_data),
              &hello, &challenge, &signature_data_size) == PROTOCOL_STATUS_OK);
    CHECK(signature_data_size == 210u);
    CHECK(memcmp(signature_data, "GSC/SERVER_AUTH/1", 17) == 0);
    CHECK(protocol_build_client_auth_signature_data(client_signature_data,
              sizeof(client_signature_data), &hello, &challenge,
              &client_signature_data_size) == PROTOCOL_STATUS_OK);
    CHECK(client_signature_data_size == 210u);
    CHECK(memcmp(client_signature_data, "GSC/CLIENT_AUTH/1", 17) == 0);

    CHECK(protocol_encode_transport_record_prefix(transport_record,
              sizeof(transport_record), 7, 32, &prefix_size) == PROTOCOL_STATUS_OK);
    CHECK(prefix_size == PROTOCOL_TRANSPORT_HEADER_SIZE);
    memset(transport_record + prefix_size, 0xab, 32);
    CHECK(protocol_decode_transport_record(transport_record, prefix_size + 32,
              &decoded_record, &consumed) == PROTOCOL_STATUS_OK);
    CHECK(decoded_record.sequence == 7);
    CHECK(decoded_record.ciphertext_length == 32);
    CHECK(protocol_build_transport_nonce(transport_nonce, 7) == PROTOCOL_STATUS_OK);
    CHECK(transport_nonce[23] == 7);
    CHECK(protocol_build_transport_authenticated_data(transport_aad,
              sizeof(transport_aad), 7, 32, &aad_size) == PROTOCOL_STATUS_OK);
    CHECK(aad_size == 27u);
    return 1;
}

static int test_server_payloads(void)
{
    uint8_t packet[32];
    uint8_t send_buffer[128];
    uint8_t deliver_buffer[128];
    uint8_t ack_buffer[32];
    size_t written = 0;
    ProtocolSend send = { 0 };
    ProtocolSend decoded_send;
    ProtocolDeliver deliver = { 0 };
    ProtocolDeliver decoded_deliver;
    ProtocolAcknowledge acknowledge = { 0 };
    ProtocolAcknowledge decoded_acknowledge;
    ProtocolDeliveryReference reference = { 0 };
    ProtocolDeliveryReference decoded_reference;
    uint8_t reference_buffer[32];

    fill_bytes(send.packet_id, sizeof(send.packet_id), 0x01);
    fill_bytes(send.recipient_id, sizeof(send.recipient_id), 0x20);
    fill_bytes(packet, sizeof(packet), 0x60);
    send.expires_in_seconds = 3600;
    send.packet = packet;
    send.packet_length = sizeof(packet);
    CHECK(protocol_encode_send(send_buffer, sizeof(send_buffer), &send, &written) ==
          PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_send(send_buffer, written, &decoded_send) ==
          PROTOCOL_STATUS_OK);
    CHECK(decoded_send.expires_in_seconds == 3600);
    CHECK(decoded_send.packet_length == sizeof(packet));
    CHECK(memcmp(decoded_send.packet, packet, sizeof(packet)) == 0);

    memcpy(deliver.packet_id, send.packet_id, sizeof(deliver.packet_id));
    deliver.delivery_id = 42;
    fill_bytes(deliver.sender_id, sizeof(deliver.sender_id), 0x40);
    deliver.server_received_at_unix_ms = UINT64_C(0x0102030405060708);
    deliver.packet = packet;
    deliver.packet_length = sizeof(packet);
    CHECK(protocol_encode_deliver(deliver_buffer, sizeof(deliver_buffer),
              &deliver, &written) == PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_deliver(deliver_buffer, written, &decoded_deliver) ==
          PROTOCOL_STATUS_OK);
    CHECK(decoded_deliver.server_received_at_unix_ms ==
          UINT64_C(0x0102030405060708));
    CHECK(decoded_deliver.delivery_id == 42);

    acknowledge.delivery_id = 42;
    memcpy(acknowledge.packet_id, send.packet_id, sizeof(acknowledge.packet_id));
    acknowledge.result = PROTOCOL_ACK_STORED;
    CHECK(protocol_encode_acknowledge(ack_buffer, sizeof(ack_buffer),
              &acknowledge, &written) == PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_acknowledge(ack_buffer, written,
              &decoded_acknowledge) == PROTOCOL_STATUS_OK);
    CHECK(decoded_acknowledge.result == PROTOCOL_ACK_STORED);
    CHECK(decoded_acknowledge.delivery_id == acknowledge.delivery_id);
    CHECK(memcmp(decoded_acknowledge.packet_id, acknowledge.packet_id,
                 sizeof(acknowledge.packet_id)) == 0);

    reference.delivery_id = 42;
    memcpy(reference.packet_id, send.packet_id, sizeof(reference.packet_id));
    CHECK(protocol_encode_delivery_reference(reference_buffer,
              sizeof(reference_buffer), &reference, &written) == PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_delivery_reference(reference_buffer, written,
              &decoded_reference) == PROTOCOL_STATUS_OK);
    CHECK(decoded_reference.delivery_id == 42);
    CHECK(memcmp(decoded_reference.packet_id, send.packet_id,
                 sizeof(reference.packet_id)) == 0);
    return 1;
}

static int test_chat_key_packet(void)
{
    ProtocolChatKey chat_key = { 0 };
    ProtocolChatKey decoded_chat_key;
    ProtocolPacket decoded_packet;
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE];
    uint8_t sender_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE];
    uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE];
    uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE];
    uint8_t payload[PROTOCOL_CHAT_KEY_PAYLOAD_SIZE];
    uint8_t packet[512];
    uint8_t packet_signature[PROTOCOL_SIGNATURE_SIZE];
    uint8_t key_signature_data[128];
    uint8_t key_context_data[128];
    size_t payload_size = 0;
    size_t unsigned_size = 0;
    size_t packet_size = 0;
    size_t key_signature_data_size = 0;
    size_t key_context_data_size = 0;

    fill_bytes(packet_id, sizeof(packet_id), 0x01);
    fill_bytes(sender_key, sizeof(sender_key), 0x20);
    fill_bytes(recipient_id, sizeof(recipient_id), 0x40);
    fill_bytes(chat_id, sizeof(chat_id), 0x60);
    fill_bytes(chat_key.public_key, sizeof(chat_key.public_key), 0x80);
    fill_bytes(chat_key.signature, sizeof(chat_key.signature), 0xa0);
    fill_bytes(packet_signature, sizeof(packet_signature), 0xc0);
    chat_key.key_version = 1;

    CHECK(protocol_build_chat_key_signature_data(key_signature_data,
              sizeof(key_signature_data), chat_id, chat_key.key_version,
              sender_key, chat_key.public_key,
              &key_signature_data_size) == PROTOCOL_STATUS_OK);
    CHECK(key_signature_data_size == 98u);
    CHECK(protocol_build_chat_key_context_data(key_context_data,
              sizeof(key_context_data), chat_id, sender_key, 1,
              recipient_id, 1, &key_context_data_size) ==
          PROTOCOL_STATUS_OK);
    CHECK(key_context_data_size == 106u);
    CHECK(protocol_encode_chat_key_payload(payload, sizeof(payload), &chat_key,
              &payload_size) == PROTOCOL_STATUS_OK);
    CHECK(protocol_encode_unsigned_packet(packet, sizeof(packet),
              PROTOCOL_PACKET_CHAT_KEY_OFFER, packet_id, sender_key, recipient_id,
              chat_id, payload, (uint32_t)payload_size, &unsigned_size) ==
          PROTOCOL_STATUS_OK);
    CHECK(protocol_append_packet_signature(packet, sizeof(packet), unsigned_size,
              packet_signature, &packet_size) == PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_packet(packet, packet_size, &decoded_packet) ==
          PROTOCOL_STATUS_OK);
    CHECK(decoded_packet.kind == PROTOCOL_PACKET_CHAT_KEY_OFFER);
    CHECK(decoded_packet.signed_length == unsigned_size);
    CHECK(memcmp(decoded_packet.signature, packet_signature,
                 sizeof(packet_signature)) == 0);
    CHECK(protocol_decode_chat_key_payload(decoded_packet.payload,
              decoded_packet.payload_length, &decoded_chat_key) ==
          PROTOCOL_STATUS_OK);
    CHECK(decoded_chat_key.key_version == 1);
    CHECK(memcmp(decoded_chat_key.public_key, chat_key.public_key,
                 sizeof(chat_key.public_key)) == 0);
    return 1;
}

static int test_encrypted_message_packet(void)
{
    uint8_t packet[512];
    uint8_t packet_id[PROTOCOL_PACKET_ID_SIZE];
    uint8_t message_id[PROTOCOL_MESSAGE_ID_SIZE];
    uint8_t sender_key[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE];
    uint8_t recipient_id[PROTOCOL_IDENTITY_ID_SIZE];
    uint8_t chat_id[PROTOCOL_CHAT_ID_SIZE];
    uint8_t nonce[PROTOCOL_NONCE_SIZE];
    uint8_t ciphertext[48];
    uint8_t signature[PROTOCOL_SIGNATURE_SIZE];
    size_t prefix_size = 0;
    size_t packet_size = 0;
    ProtocolPacket decoded_packet;
    ProtocolEncryptedMessage decoded_message;

    fill_bytes(packet_id, sizeof(packet_id), 0x01);
    fill_bytes(message_id, sizeof(message_id), 0x11);
    fill_bytes(sender_key, sizeof(sender_key), 0x21);
    fill_bytes(recipient_id, sizeof(recipient_id), 0x41);
    fill_bytes(chat_id, sizeof(chat_id), 0x61);
    fill_bytes(nonce, sizeof(nonce), 0x81);
    fill_bytes(ciphertext, sizeof(ciphertext), 0xa1);
    fill_bytes(signature, sizeof(signature), 0xc1);

    CHECK(protocol_encode_encrypted_packet_prefix(packet, sizeof(packet), packet_id,
              sender_key, recipient_id, chat_id, message_id, 2, 3, nonce,
              sizeof(ciphertext), &prefix_size) == PROTOCOL_STATUS_OK);
    CHECK(prefix_size == PROTOCOL_E2E_HEADER_SIZE +
                         PROTOCOL_ENCRYPTED_PAYLOAD_HEADER_SIZE);
    memcpy(packet + prefix_size, ciphertext, sizeof(ciphertext));
    CHECK(protocol_append_packet_signature(packet, sizeof(packet),
              prefix_size + sizeof(ciphertext), signature, &packet_size) ==
          PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_packet(packet, packet_size, &decoded_packet) ==
          PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_encrypted_message(&decoded_packet, &decoded_message) ==
          PROTOCOL_STATUS_OK);
    CHECK(decoded_message.sender_key_version == 2);
    CHECK(decoded_message.recipient_key_version == 3);
    CHECK(decoded_message.ciphertext_length == sizeof(ciphertext));
    CHECK(decoded_message.authenticated_data_length == prefix_size);
    CHECK(memcmp(decoded_message.ciphertext, ciphertext, sizeof(ciphertext)) == 0);
    return 1;
}

static int test_content_payloads(void)
{
    static const uint8_t message_text[] = "Hello, world!";
    uint8_t text_data[128];
    uint8_t content_bytes[160];
    uint8_t receipt_data[32];
    size_t text_data_size = 0;
    size_t content_size = 0;
    size_t receipt_size = 0;
    ProtocolText text;
    ProtocolText decoded_text;
    ProtocolContent decoded_content;
    ProtocolDeliveryReceipt receipt = { 0 };
    ProtocolDeliveryReceipt decoded_receipt;

    text.sender_timestamp_unix_ms = UINT64_C(1777000000123);
    text.text = message_text;
    text.text_length = sizeof(message_text) - 1;
    CHECK(protocol_encode_text_data(text_data, sizeof(text_data), &text,
              &text_data_size) == PROTOCOL_STATUS_OK);
    CHECK(protocol_encode_content(content_bytes, sizeof(content_bytes),
              PROTOCOL_CONTENT_TEXT, text_data, (uint32_t)text_data_size,
              &content_size) == PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_content(content_bytes, content_size, &decoded_content) ==
          PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_text_data(decoded_content.data,
              decoded_content.data_length, &decoded_text) == PROTOCOL_STATUS_OK);
    CHECK(decoded_text.sender_timestamp_unix_ms == text.sender_timestamp_unix_ms);
    CHECK(memcmp(decoded_text.text, message_text, text.text_length) == 0);

    fill_bytes(receipt.message_id, sizeof(receipt.message_id), 0x30);
    receipt.recipient_timestamp_unix_ms = UINT64_C(1777000000999);
    CHECK(protocol_encode_delivery_receipt_data(receipt_data, sizeof(receipt_data),
              &receipt, &receipt_size) == PROTOCOL_STATUS_OK);
    CHECK(protocol_decode_delivery_receipt_data(receipt_data, receipt_size,
              &decoded_receipt) == PROTOCOL_STATUS_OK);
    CHECK(decoded_receipt.recipient_timestamp_unix_ms ==
          receipt.recipient_timestamp_unix_ms);
    return 1;
}

static int test_invalid_lengths_and_values(void)
{
    uint8_t buffer[256] = { 0 };
    uint8_t id[PROTOCOL_IDENTITY_PUBLIC_KEY_SIZE] = { 0 };
    uint8_t short_invalid_utf8[] = { 0xc0, 0x80 };
    ProtocolText text = { 0 };
    size_t written = 0;

    CHECK(protocol_frame_size(PROTOCOL_MAX_SERVER_PAYLOAD + 1u) == 0);
    CHECK(protocol_packet_size(UINT32_MAX) == 0);
    CHECK(protocol_content_size(UINT32_MAX) == 0);
    CHECK(protocol_transport_record_size(
              PROTOCOL_FRAME_HEADER_SIZE + PROTOCOL_AEAD_TAG_SIZE - 1u) == 0);
    CHECK(protocol_encode_encrypted_packet_prefix(buffer, sizeof(buffer),
              id, id, id, id, id, 1, 1, id, UINT32_MAX, &written) ==
          PROTOCOL_STATUS_INVALID_LENGTH);
    CHECK(protocol_encode_encrypted_packet_prefix(buffer, sizeof(buffer),
              id, id, id, id, id, 1, 1, id,
              PROTOCOL_AEAD_TAG_SIZE - 1u, &written) ==
          PROTOCOL_STATUS_INVALID_LENGTH);

    text.text = short_invalid_utf8;
    text.text_length = sizeof(short_invalid_utf8);
    CHECK(protocol_encode_text_data(buffer, sizeof(buffer), &text, &written) ==
          PROTOCOL_STATUS_INVALID_LENGTH);
    return 1;
}

int main(void)
{
    CHECK(test_frame_round_trip());
    CHECK(test_authentication_payloads());
    CHECK(test_server_payloads());
    CHECK(test_chat_key_packet());
    CHECK(test_encrypted_message_packet());
    CHECK(test_content_payloads());
    CHECK(test_invalid_lengths_and_values());

    puts("protocol tests passed");
    return 0;
}
