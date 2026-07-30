#include "dmproto.h"

#include <string.h>

#define DM_DELTA 0x9E3779B9UL

typedef struct dm_mac_context {
    dm_u8 state[8];
    dm_u8 block[8];
    dm_u8 used;
    dm_u32 length;
    const dm_u8 *key;
} dm_mac_context;

static void dm_mac_feed(dm_mac_context *context, dm_u8 value);
static void dm_mac_block(dm_mac_context *context);

dm_u16 dm_get_u16(const dm_u8 *value)
{
    return (dm_u16)value[0] | ((dm_u16)value[1] << 8);
}

dm_u32 dm_get_u32(const dm_u8 *value)
{
    return (dm_u32)value[0]
        | ((dm_u32)value[1] << 8)
        | ((dm_u32)value[2] << 16)
        | ((dm_u32)value[3] << 24);
}

void dm_put_u16(dm_u8 *output, dm_u16 value)
{
    output[0] = (dm_u8)value;
    output[1] = (dm_u8)(value >> 8);
}

void dm_put_u32(dm_u8 *output, dm_u32 value)
{
    output[0] = (dm_u8)value;
    output[1] = (dm_u8)(value >> 8);
    output[2] = (dm_u8)(value >> 16);
    output[3] = (dm_u8)(value >> 24);
}

dm_u16 dm_crc16_ccitt(const dm_u8 *data, dm_u16 length)
{
    dm_u16 crc = 0xFFFF;
    dm_u16 index;
    dm_u8 bit;

    for (index = 0; index < length; ++index) {
        crc ^= (dm_u16)data[index] << 8;
        for (bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000)
                crc = (dm_u16)((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
    }
    return crc;
}

void dm_xtea_encrypt(dm_u8 block[8], const dm_u8 key[16])
{
    dm_u32 left = dm_get_u32(block);
    dm_u32 right = dm_get_u32(block + 4);
    dm_u32 words[4];
    dm_u32 sum = 0;
    dm_u8 round;

    words[0] = dm_get_u32(key);
    words[1] = dm_get_u32(key + 4);
    words[2] = dm_get_u32(key + 8);
    words[3] = dm_get_u32(key + 12);
    for (round = 0; round < 32; ++round) {
        left += ((((right << 4) ^ (right >> 5)) + right)
            ^ (sum + words[sum & 3]));
        sum += DM_DELTA;
        right += ((((left << 4) ^ (left >> 5)) + left)
            ^ (sum + words[(sum >> 11) & 3]));
    }
    dm_put_u32(block, left);
    dm_put_u32(block + 4, right);
}

void dm_derive_session_key(
    const dm_u8 key[16],
    dm_u32 client_nonce,
    dm_u32 server_nonce,
    dm_u8 output[16]
)
{
    dm_put_u32(output, client_nonce);
    dm_put_u32(output + 4, server_nonce);
    dm_put_u32(output + 8, server_nonce ^ 0xA5A5A5A5UL);
    dm_put_u32(output + 12, client_nonce ^ 0x5A5A5A5AUL);
    dm_xtea_encrypt(output, key);
    dm_xtea_encrypt(output + 8, key);
}

dm_u32 dm_packet_mac(const dm_u8 *data, dm_u16 length, const dm_u8 key[16])
{
    dm_mac_context context;
    dm_u16 index;
    dm_u8 length_block[8];

    memset(&context, 0, sizeof(context));
    context.key = key;
    context.length = length;
    for (index = 0; index < length; ++index)
        dm_mac_feed(&context, data[index]);
    dm_mac_feed(&context, 0x80);
    while (context.used != 0)
        dm_mac_feed(&context, 0);
    dm_put_u32(length_block, length);
    dm_put_u32(length_block + 4, ((dm_u32)length) ^ 0xFFFFFFFFUL);
    for (index = 0; index < 8; ++index)
        dm_mac_feed(&context, length_block[index]);
    return dm_get_u32(context.state) ^ dm_get_u32(context.state + 4);
}

int dm_packet_encode(
    const dm_packet *packet,
    const dm_u8 key[16],
    dm_u8 *output,
    dm_u16 output_capacity,
    dm_u16 *output_length
)
{
    dm_u16 total;
    dm_u16 crc;
    dm_u32 mac;
    dm_u8 scratch[16 + DM_MAX_FRAGMENT_PAYLOAD];

    if (packet->kind < DM_KIND_REQUEST || packet->kind > DM_KIND_ERROR
        || packet->opcode < DM_OP_HELLO || packet->opcode > DM_OP_CANCEL)
        return DM_ERR_ENUM;
    if (packet->fragment_count == 0
        || packet->fragment_count > DM_MAX_FRAGMENTS
        || packet->fragment_index >= packet->fragment_count)
        return DM_ERR_FRAGMENT;
    if (packet->payload_length > DM_MAX_FRAGMENT_PAYLOAD)
        return DM_ERR_LENGTH;
    total = DM_HEADER_SIZE + packet->payload_length;
    if (output_capacity < total)
        return DM_ERR_BUFFER;

    output[0] = DM_MAGIC_0;
    output[1] = DM_MAGIC_1;
    output[2] = DM_VERSION;
    output[3] = packet->kind;
    output[4] = packet->opcode;
    output[5] = packet->flags;
    dm_put_u16(output + 6, packet->session_id);
    dm_put_u16(output + 8, packet->request_id);
    output[10] = packet->fragment_index;
    output[11] = packet->fragment_count;
    dm_put_u16(output + 12, packet->payload_length);
    if (packet->payload_length)
        memcpy(output + DM_HEADER_SIZE, packet->payload, packet->payload_length);
    memcpy(scratch, output, 14);
    memcpy(scratch + 14, packet->payload, packet->payload_length);
    crc = dm_crc16_ccitt(scratch, 14 + packet->payload_length);
    dm_put_u16(output + 14, crc);
    memcpy(scratch, output, 16);
    memcpy(scratch + 16, packet->payload, packet->payload_length);
    mac = dm_packet_mac(scratch, 16 + packet->payload_length, key);
    dm_put_u32(output + 16, mac);
    *output_length = total;
    return DM_OK;
}

int dm_packet_decode(
    const dm_u8 *datagram,
    dm_u16 datagram_length,
    const dm_u8 key[16],
    dm_packet *packet
)
{
    dm_u16 payload_length;
    dm_u16 expected_crc;
    dm_u32 expected_mac;
    dm_u8 scratch[DM_HEADER_SIZE + DM_MAX_FRAGMENT_PAYLOAD];

    if (datagram_length < DM_HEADER_SIZE)
        return DM_ERR_TRUNCATED;
    if (datagram[0] != DM_MAGIC_0 || datagram[1] != DM_MAGIC_1)
        return DM_ERR_MAGIC;
    if (datagram[2] != DM_VERSION)
        return DM_ERR_VERSION;
    if (datagram[3] < DM_KIND_REQUEST || datagram[3] > DM_KIND_ERROR
        || datagram[4] < DM_OP_HELLO || datagram[4] > DM_OP_CANCEL)
        return DM_ERR_ENUM;
    payload_length = dm_get_u16(datagram + 12);
    if (payload_length > DM_MAX_FRAGMENT_PAYLOAD
        || datagram_length != DM_HEADER_SIZE + payload_length)
        return DM_ERR_LENGTH;
    if (datagram[11] == 0 || datagram[11] > DM_MAX_FRAGMENTS
        || datagram[10] >= datagram[11])
        return DM_ERR_FRAGMENT;

    memcpy(scratch, datagram, 14);
    memcpy(scratch + 14, datagram + DM_HEADER_SIZE, payload_length);
    expected_crc = dm_crc16_ccitt(scratch, 14 + payload_length);
    if (expected_crc != dm_get_u16(datagram + 14))
        return DM_ERR_CRC;
    memcpy(scratch, datagram, 16);
    memcpy(scratch + 16, datagram + DM_HEADER_SIZE, payload_length);
    expected_mac = dm_packet_mac(scratch, 16 + payload_length, key);
    if (expected_mac != dm_get_u32(datagram + 16))
        return DM_ERR_AUTH;

    packet->kind = datagram[3];
    packet->opcode = datagram[4];
    packet->flags = datagram[5];
    packet->session_id = dm_get_u16(datagram + 6);
    packet->request_id = dm_get_u16(datagram + 8);
    packet->fragment_index = datagram[10];
    packet->fragment_count = datagram[11];
    packet->payload_length = payload_length;
    packet->payload = datagram + DM_HEADER_SIZE;
    return DM_OK;
}

static void dm_mac_feed(dm_mac_context *context, dm_u8 value)
{
    context->block[context->used++] = value;
    if (context->used == 8)
        dm_mac_block(context);
}

static void dm_mac_block(dm_mac_context *context)
{
    dm_u8 index;

    for (index = 0; index < 8; ++index)
        context->state[index] ^= context->block[index];
    dm_xtea_encrypt(context->state, context->key);
    context->used = 0;
}
