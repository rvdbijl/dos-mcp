#ifndef DMCONFIG_H
#define DMCONFIG_H

#include "dmproto.h"

#define DM_MTCP_HAVE_IP 0x01
#define DM_MTCP_HAVE_PACKET_INT 0x02
#define DM_MTCP_HAVE_HOSTNAME 0x04

#define DM_MTCP_HOSTNAME_MAX 31

#define DM_MTCP_OK 0
#define DM_MTCP_ERR_ARGUMENT -1
#define DM_MTCP_ERR_OPEN -2
#define DM_MTCP_ERR_LINE_TOO_LONG -3
#define DM_MTCP_ERR_IPADDR -4
#define DM_MTCP_ERR_PACKETINT -5
#define DM_MTCP_ERR_HOSTNAME -6

int dm_mtcp_config_load(
    const char *path,
    dm_u8 ip[4],
    dm_u8 *packet_interrupt,
    char *hostname,
    dm_u16 hostname_capacity,
    dm_u8 *found
);

const char *dm_mtcp_config_error(int result);

#endif
