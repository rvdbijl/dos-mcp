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
    0x44, 0x4D, 0x01, 0x01, 0x02, 0x00, 0x34, 0x12,
    0x78, 0x56, 0x00, 0x01, 0x06, 0x00, 0xE0, 0x59,
    0xBA, 0x98, 0x11, 0x28, 0x73, 0x74, 0x61, 0x74,
    0x75, 0x73
};

int main(void)
{
    dm_u8 block[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    dm_u8 output[64];
    dm_u16 output_length = 0;
    dm_packet packet;
    dm_packet decoded;
    int result;

    if (dm_crc16_ccitt((const dm_u8 *)"123456789", 9) != 0x29B1) {
        puts("FAIL crc16");
        return 1;
    }
    dm_xtea_encrypt(block, key);
    if (memcmp(block, expected_xtea, sizeof(block)) != 0) {
        puts("FAIL xtea");
        return 2;
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
        return 3;
    }
    result = dm_packet_decode(output, output_length, key, &decoded);
    if (result != DM_OK
        || decoded.session_id != packet.session_id
        || decoded.request_id != packet.request_id
        || decoded.payload_length != packet.payload_length
        || memcmp(decoded.payload, packet.payload, packet.payload_length) != 0) {
        puts("FAIL packet decode");
        return 4;
    }
    output[output_length - 1] ^= 1;
    if (dm_packet_decode(output, output_length, key, &decoded) != DM_ERR_CRC) {
        puts("FAIL corruption");
        return 5;
    }
    packet.kind = 0;
    if (dm_packet_encode(
        &packet, key, output, sizeof(output), &output_length) != DM_ERR_ENUM) {
        puts("FAIL enum validation");
        return 6;
    }
    puts("PASS protocol vectors");
    return 0;
}
