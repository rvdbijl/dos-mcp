#ifndef DMPROTO_H
#define DMPROTO_H

#include <limits.h>
#include <stddef.h>

typedef unsigned char dm_u8;
typedef unsigned short dm_u16;
#if UINT_MAX == 0xFFFFFFFFUL
typedef unsigned int dm_u32;
#else
typedef unsigned long dm_u32;
#endif

#define DM_MAGIC_0 'D'
#define DM_MAGIC_1 'M'
#define DM_VERSION 2
#define DM_HEADER_SIZE 20
#define DM_MAX_FRAGMENT_PAYLOAD 1024
#define DM_MAX_FRAGMENTS 32

#define DM_KIND_REQUEST 1
#define DM_KIND_RESPONSE 2
#define DM_KIND_ERROR 3

#define DM_OP_HELLO 1
#define DM_OP_GET_STATUS 2
#define DM_OP_GET_CAPABILITIES 3
#define DM_OP_CAPTURE_TEXT_SCREEN 4
#define DM_OP_SEND_KEYS 5
#define DM_OP_PING 6
#define DM_OP_CANCEL 7
#define DM_OP_FILE_READ_BEGIN 8
#define DM_OP_FILE_READ_BLOCK 9
#define DM_OP_FILE_READ_END 10
#define DM_OP_FILE_WRITE_BEGIN 11
#define DM_OP_FILE_WRITE_BLOCK 12
#define DM_OP_FILE_WRITE_COMMIT 13
#define DM_OP_FILE_ABORT 14
#define DM_OP_GRAPHICS_BEGIN 15
#define DM_OP_GRAPHICS_BLOCK 16
#define DM_OP_GRAPHICS_END 17
#define DM_OP_GET_DIAGNOSTICS 18
#define DM_OP_MAX DM_OP_GET_DIAGNOSTICS

#define DM_OK 0
#define DM_ERR_TRUNCATED -1
#define DM_ERR_MAGIC -2
#define DM_ERR_VERSION -3
#define DM_ERR_LENGTH -4
#define DM_ERR_FRAGMENT -5
#define DM_ERR_CRC -6
#define DM_ERR_AUTH -7
#define DM_ERR_BUFFER -8
#define DM_ERR_ENUM -9

typedef struct dm_packet {
    dm_u8 kind;
    dm_u8 opcode;
    dm_u8 flags;
    dm_u16 session_id;
    dm_u16 request_id;
    dm_u8 fragment_index;
    dm_u8 fragment_count;
    dm_u16 payload_length;
    const dm_u8 *payload;
} dm_packet;

dm_u16 dm_crc16_ccitt(const dm_u8 *data, dm_u16 length);
void dm_sha256(const dm_u8 *data, dm_u16 length, dm_u8 output[32]);
void dm_derive_password_key(
    const dm_u8 *password,
    dm_u16 length,
    dm_u8 output[16]
);
void dm_open_mode_key(dm_u8 output[16]);
void dm_xtea_encrypt(dm_u8 block[8], const dm_u8 key[16]);
void dm_derive_session_key(
    const dm_u8 key[16],
    dm_u32 client_nonce,
    dm_u32 server_nonce,
    dm_u8 output[16]
);
dm_u32 dm_packet_mac(const dm_u8 *data, dm_u16 length, const dm_u8 key[16]);

int dm_packet_encode(
    const dm_packet *packet,
    const dm_u8 key[16],
    dm_u8 *output,
    dm_u16 output_capacity,
    dm_u16 *output_length
);
int dm_packet_decode(
    const dm_u8 *datagram,
    dm_u16 datagram_length,
    const dm_u8 key[16],
    dm_packet *packet
);

dm_u16 dm_get_u16(const dm_u8 *value);
dm_u32 dm_get_u32(const dm_u8 *value);
void dm_put_u16(dm_u8 *output, dm_u16 value);
void dm_put_u32(dm_u8 *output, dm_u32 value);

#endif
