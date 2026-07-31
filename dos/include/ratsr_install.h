#ifndef RATSR_INSTALL_H
#define RATSR_INSTALL_H

#include "dmproto.h"

#define RATSR_MULTIPLEX 0xD05A
#define RATSR_REPLY 0x5A5A
#define RATSR_INSTALL_MAGIC 0x5241
#define RATSR_INSTALL_VERSION 1
#define RATSR_PATH_MAX 127
#define RATSR_NAME_MAX 31

#define RATSR_INSTALL_READ 0x01
#define RATSR_INSTALL_WRITE 0x02
#define RATSR_INSTALL_ALL_DRIVES 0x04
#define RATSR_INSTALL_OPEN_MODE 0x08

#pragma pack(push, 1)
typedef struct ratsr_install_config {
    dm_u16 magic;
    dm_u16 version;
    dm_u16 size;
    dm_u16 crc16;
    dm_u8 key[16];
    dm_u8 ip[4];
    dm_u16 port;
    dm_u8 packet_interrupt;
    dm_u8 flags;
    dm_u8 root_length;
    dm_u8 name_length;
    char root[RATSR_PATH_MAX + 1];
    char name[RATSR_NAME_MAX + 1];
} ratsr_install_config;
#pragma pack(pop)

#endif
