#include "dmproto.h"

#include <string.h>

#define DM_DELTA 0x9E3779B9UL
#define DM_ROTR(value, count) \
    (((value) >> (count)) | ((value) << (32 - (count))))

typedef struct dm_sha_context {
    dm_u32 state[8];
    dm_u32 total;
    dm_u8 block[64];
    dm_u8 used;
} dm_sha_context;

typedef struct dm_mac_context {
    dm_u8 state[4];
    dm_u8 block[4];
    dm_u8 used;
    dm_u32 length;
    dm_u16 round_keys[22];
} dm_mac_context;

#ifdef RA_TSR
/*
 * The resident worker is serialized by its interrupt hook.  Reuse one bounded
 * scratch area so large near pointers never refer to the switched ISR stack.
 */
static dm_u8 dm_protocol_scratch[DM_HEADER_SIZE + DM_MAX_FRAGMENT_PAYLOAD];
#endif

static void dm_mac_feed(dm_mac_context *context, dm_u8 value);
static void dm_mac_block(dm_mac_context *context);
static void dm_speck32_expand(const dm_u8 key[8], dm_u16 round_keys[22]);
static void dm_speck32_encrypt(
    dm_u8 block[4],
    const dm_u16 round_keys[22]
);
static void dm_sha_init(dm_sha_context *context);
static void dm_sha_update(
    dm_sha_context *context,
    const dm_u8 *data,
    dm_u16 length
);
static void dm_sha_feed(dm_sha_context *context, dm_u8 value);
static void dm_sha_transform(dm_sha_context *context);
static void dm_sha_final(dm_sha_context *context, dm_u8 output[32]);
static dm_u32 dm_get_be32(const dm_u8 *value);
static void dm_put_be32(dm_u8 *output, dm_u32 value);

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

void dm_sha256(const dm_u8 *data, dm_u16 length, dm_u8 output[32])
{
    dm_sha_context context;

    dm_sha_init(&context);
    dm_sha_update(&context, data, length);
    dm_sha_final(&context, output);
}

void dm_derive_password_key(
    const dm_u8 *password,
    dm_u16 length,
    dm_u8 output[16]
)
{
    static const dm_u8 domain[] = "DOS-MCP credential v1";
    dm_sha_context context;
    dm_u8 digest[32];

    dm_sha_init(&context);
    dm_sha_update(&context, domain, sizeof(domain));
    dm_sha_update(&context, password, length);
    dm_sha_final(&context, digest);
    memcpy(output, digest, 16);
}

void dm_open_mode_key(dm_u8 output[16])
{
    static const dm_u8 domain[] = "DOS-MCP open mode v1";
    dm_u8 digest[32];

    dm_sha256(domain, sizeof(domain), digest);
    memcpy(output, digest, 16);
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
    dm_u8 length_block[4];

    memset(&context, 0, sizeof(context));
    dm_speck32_expand(key, context.round_keys);
    context.length = length;
    for (index = 0; index < length; ++index)
        dm_mac_feed(&context, data[index]);
    dm_mac_feed(&context, 0x80);
    while (context.used != 0)
        dm_mac_feed(&context, 0);
    dm_put_u32(length_block, length);
    for (index = 0; index < 4; ++index)
        dm_mac_feed(&context, length_block[index]);
    dm_speck32_expand(key + 8, context.round_keys);
    dm_speck32_encrypt(context.state, context.round_keys);
    return dm_get_u32(context.state);
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
#ifdef RA_TSR
    dm_u8 *scratch = dm_protocol_scratch;
#else
    dm_u8 scratch[16 + DM_MAX_FRAGMENT_PAYLOAD];
#endif

    if (packet->flags != 0)
        return DM_ERR_ENUM;
    if (packet->kind < DM_KIND_REQUEST || packet->kind > DM_KIND_ERROR
        || packet->opcode < DM_OP_HELLO || packet->opcode > DM_OP_MAX)
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
#ifdef RA_TSR
    dm_u8 *scratch = dm_protocol_scratch;
#else
    dm_u8 scratch[DM_HEADER_SIZE + DM_MAX_FRAGMENT_PAYLOAD];
#endif

    if (datagram_length < DM_HEADER_SIZE)
        return DM_ERR_TRUNCATED;
    if (datagram[0] != DM_MAGIC_0 || datagram[1] != DM_MAGIC_1)
        return DM_ERR_MAGIC;
    if (datagram[2] != DM_VERSION)
        return DM_ERR_VERSION;
    if (datagram[5] != 0)
        return DM_ERR_ENUM;
    if (datagram[3] < DM_KIND_REQUEST || datagram[3] > DM_KIND_ERROR
        || datagram[4] < DM_OP_HELLO || datagram[4] > DM_OP_MAX)
        return DM_ERR_ENUM;
    payload_length = dm_get_u16(datagram + 12);
    if (payload_length > DM_MAX_FRAGMENT_PAYLOAD
        || (datagram_length != DM_HEADER_SIZE + payload_length
            && (datagram_length != DM_HEADER_SIZE + payload_length + 1
                || datagram[datagram_length - 1] != 0)))
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
    if (context->used == 4)
        dm_mac_block(context);
}

static void dm_mac_block(dm_mac_context *context)
{
    dm_u8 index;

    for (index = 0; index < 4; ++index)
        context->state[index] ^= context->block[index];
    dm_speck32_encrypt(context->state, context->round_keys);
    context->used = 0;
}

static void dm_speck32_expand(const dm_u8 key[8], dm_u16 round_keys[22])
{
    dm_u16 round_key = dm_get_u16(key);
    dm_u16 schedule[3];
    dm_u16 next_word;
    dm_u8 round;
    dm_u8 slot = 0;

    schedule[0] = dm_get_u16(key + 2);
    schedule[1] = dm_get_u16(key + 4);
    schedule[2] = dm_get_u16(key + 6);
    round_keys[0] = round_key;
    for (round = 0; round < 21; ++round) {
        next_word = (dm_u16)(
            (dm_u16)((schedule[slot] >> 7) | (schedule[slot] << 9))
            + round_key
        );
        next_word ^= round;
        schedule[slot] = next_word;
        if (++slot == 3)
            slot = 0;
        round_key = (dm_u16)((round_key << 2) | (round_key >> 14));
        round_key ^= next_word;
        round_keys[round + 1] = round_key;
    }
}

static void dm_speck32_encrypt(
    dm_u8 block[4],
    const dm_u16 round_keys[22]
)
{
    dm_u16 left = dm_get_u16(block);
    dm_u16 right = dm_get_u16(block + 2);
    dm_u8 round;

    for (round = 0; round < 22; ++round) {
        left = (dm_u16)((dm_u16)((left >> 7) | (left << 9)) + right);
        left ^= round_keys[round];
        right = (dm_u16)((right << 2) | (right >> 14));
        right ^= left;
    }
    dm_put_u16(block, left);
    dm_put_u16(block + 2, right);
}

static void dm_sha_init(dm_sha_context *context)
{
    context->state[0] = 0x6A09E667UL;
    context->state[1] = 0xBB67AE85UL;
    context->state[2] = 0x3C6EF372UL;
    context->state[3] = 0xA54FF53AUL;
    context->state[4] = 0x510E527FUL;
    context->state[5] = 0x9B05688CUL;
    context->state[6] = 0x1F83D9ABUL;
    context->state[7] = 0x5BE0CD19UL;
    context->total = 0;
    context->used = 0;
}

static void dm_sha_update(
    dm_sha_context *context,
    const dm_u8 *data,
    dm_u16 length
)
{
    dm_u16 index;

    context->total += length;
    for (index = 0; index < length; ++index)
        dm_sha_feed(context, data[index]);
}

static void dm_sha_feed(dm_sha_context *context, dm_u8 value)
{
    context->block[context->used++] = value;
    if (context->used == 64)
        dm_sha_transform(context);
}

static void dm_sha_transform(dm_sha_context *context)
{
    static const dm_u32 constants[64] = {
        0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
        0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
        0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
        0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
        0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
        0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
        0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
        0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
        0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
        0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
        0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
        0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
        0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
        0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
        0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
        0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
    };
    dm_u32 words[64];
    dm_u32 a;
    dm_u32 b;
    dm_u32 c;
    dm_u32 d;
    dm_u32 e;
    dm_u32 f;
    dm_u32 g;
    dm_u32 h;
    dm_u32 choice;
    dm_u32 majority;
    dm_u32 sigma0;
    dm_u32 sigma1;
    dm_u32 temp1;
    dm_u32 temp2;
    dm_u8 index;

    for (index = 0; index < 16; ++index)
        words[index] = dm_get_be32(context->block + (dm_u16)index * 4);
    for (index = 16; index < 64; ++index) {
        sigma0 = DM_ROTR(words[index - 15], 7)
            ^ DM_ROTR(words[index - 15], 18)
            ^ (words[index - 15] >> 3);
        sigma1 = DM_ROTR(words[index - 2], 17)
            ^ DM_ROTR(words[index - 2], 19)
            ^ (words[index - 2] >> 10);
        words[index] = words[index - 16] + sigma0
            + words[index - 7] + sigma1;
    }
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (index = 0; index < 64; ++index) {
        sigma1 = DM_ROTR(e, 6) ^ DM_ROTR(e, 11) ^ DM_ROTR(e, 25);
        choice = (e & f) ^ ((~e) & g);
        temp1 = h + sigma1 + choice + constants[index] + words[index];
        sigma0 = DM_ROTR(a, 2) ^ DM_ROTR(a, 13) ^ DM_ROTR(a, 22);
        majority = (a & b) ^ (a & c) ^ (b & c);
        temp2 = sigma0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
    context->used = 0;
}

static void dm_sha_final(dm_sha_context *context, dm_u8 output[32])
{
    dm_u32 bit_high = context->total >> 29;
    dm_u32 bit_low = context->total << 3;
    dm_u8 index;

    dm_sha_feed(context, 0x80);
    while (context->used != 56)
        dm_sha_feed(context, 0);
    dm_put_be32(context->block + 56, bit_high);
    dm_put_be32(context->block + 60, bit_low);
    context->used = 64;
    dm_sha_transform(context);
    for (index = 0; index < 8; ++index)
        dm_put_be32(output + (dm_u16)index * 4, context->state[index]);
}

static dm_u32 dm_get_be32(const dm_u8 *value)
{
    return ((dm_u32)value[0] << 24)
        | ((dm_u32)value[1] << 16)
        | ((dm_u32)value[2] << 8)
        | (dm_u32)value[3];
}

static void dm_put_be32(dm_u8 *output, dm_u32 value)
{
    output[0] = (dm_u8)(value >> 24);
    output[1] = (dm_u8)(value >> 16);
    output[2] = (dm_u8)(value >> 8);
    output[3] = (dm_u8)value;
}
