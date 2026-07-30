#include "dmproto.h"

#include <stdio.h>
#include <string.h>

static const dm_u8 key[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};
static const dm_u8 expected_xtea[8] = {
    0x93, 0x00, 0x99, 0x13, 0xE1, 0xC4, 0xF7, 0x85
};
static const dm_u8 expected_packet[] = {
    0x44, 0x4D, 0x02, 0x01, 0x02, 0x00, 0x34, 0x12,
    0x78, 0x56, 0x00, 0x01, 0x06, 0x00, 0x92, 0x59,
    0xB7, 0x70, 0x73, 0x54, 0x73, 0x74, 0x61, 0x74,
    0x75, 0x73
};
static const dm_u8 expected_packet32[] = {
    0x44, 0x4D, 0x02, 0x01, 0x06, 0x00, 0x34, 0x12,
    0x78, 0x56, 0x00, 0x01, 0x20, 0x00, 0x28, 0x8A,
    0x59, 0x09, 0x00, 0x3D,
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x',
    'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'
};
static const dm_u8 expected_sha256[32] = {
    0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
    0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
    0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
    0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
};
static const dm_u8 expected_password_key[16] = {
    0xCA, 0xA7, 0x89, 0xFD, 0x21, 0x0B, 0xF8, 0x61,
    0xDD, 0x75, 0x68, 0x24, 0x9C, 0x84, 0x35, 0x18
};
static const dm_u8 expected_open_key[16] = {
    0xB1, 0x51, 0xCB, 0x0F, 0x3C, 0xDC, 0x52, 0x7F,
    0x03, 0xED, 0x9A, 0xA5, 0x37, 0x13, 0x6A, 0xDE
};
static const dm_u8 expected_long_password_key[16] = {
    0x87, 0x37, 0xC4, 0xC4, 0xC4, 0x8E, 0xE6, 0x14,
    0x03, 0x7B, 0x8C, 0x19, 0x10, 0xE1, 0xD1, 0x40
};

int main(void)
{
    dm_u8 block[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    dm_u8 output[64];
    dm_u8 payload32[32];
    dm_u16 output_length = 0;
    dm_u8 long_password[100];
    dm_packet packet;
    dm_packet decoded;
    int result;

    dm_sha256((const dm_u8 *)"abc", 3, output);
    if (memcmp(output, expected_sha256, sizeof(expected_sha256)) != 0) {
        puts("FAIL sha256");
        return 1;
    }
    dm_derive_password_key(
        (const dm_u8 *)"correct horse battery staple",
        28,
        output
    );
    if (memcmp(
        output, expected_password_key, sizeof(expected_password_key)) != 0) {
        puts("FAIL password key");
        return 2;
    }
    dm_open_mode_key(output);
    if (memcmp(output, expected_open_key, sizeof(expected_open_key)) != 0) {
        puts("FAIL open key");
        return 3;
    }
    memset(long_password, 'a', sizeof(long_password));
    dm_derive_password_key(long_password, sizeof(long_password), output);
    if (memcmp(
        output,
        expected_long_password_key,
        sizeof(expected_long_password_key)
    ) != 0) {
        puts("FAIL long password key");
        return 4;
    }
    if (dm_crc16_ccitt((const dm_u8 *)"123456789", 9) != 0x29B1) {
        puts("FAIL crc16");
        return 5;
    }
    dm_xtea_encrypt(block, key);
    if (memcmp(block, expected_xtea, sizeof(block)) != 0) {
        puts("FAIL xtea");
        return 6;
    }
    packet.kind = DM_KIND_REQUEST;
    packet.opcode = DM_OP_GET_STATUS;
    packet.flags = 0;
    packet.session_id = 0x1234;
    packet.request_id = 0x5678;
    packet.fragment_index = 0;
    packet.fragment_count = 1;
    packet.payload_length = 6;
    packet.payload = (const dm_u8 *)"status";
    result = dm_packet_encode(&packet, key, output, sizeof(output), &output_length);
    if (result != DM_OK
        || output_length != sizeof(expected_packet)
        || memcmp(output, expected_packet, sizeof(expected_packet)) != 0) {
        puts("FAIL packet encode");
        return 7;
    }
    result = dm_packet_decode(output, output_length, key, &decoded);
    if (result != DM_OK
        || decoded.session_id != packet.session_id
        || decoded.request_id != packet.request_id
        || decoded.payload_length != packet.payload_length
        || memcmp(decoded.payload, packet.payload, packet.payload_length) != 0) {
        puts("FAIL packet decode");
        return 8;
    }
    output[output_length - 1] ^= 1;
    if (dm_packet_decode(output, output_length, key, &decoded) != DM_ERR_CRC) {
        puts("FAIL corruption");
        return 9;
    }
    memset(payload32, 'x', sizeof(payload32));
    packet.kind = DM_KIND_REQUEST;
    packet.opcode = DM_OP_PING;
    packet.payload_length = sizeof(payload32);
    packet.payload = payload32;
    result = dm_packet_encode(&packet, key, output, sizeof(output), &output_length);
    if (result != DM_OK
        || output_length != sizeof(expected_packet32)
        || memcmp(output, expected_packet32, sizeof(expected_packet32)) != 0
        || dm_packet_decode(output, output_length, key, &decoded) != DM_OK
        || decoded.payload_length != sizeof(payload32)
        || memcmp(decoded.payload, payload32, sizeof(payload32)) != 0) {
        puts("FAIL 32-byte packet");
        return 10;
    }
    packet.kind = 0;
    if (dm_packet_encode(
        &packet, key, output, sizeof(output), &output_length) != DM_ERR_ENUM) {
        puts("FAIL enum validation");
        return 11;
    }
    puts("PASS protocol vectors");
    return 0;
}
