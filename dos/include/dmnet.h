#ifndef DMNET_H
#define DMNET_H

#include "dmproto.h"

typedef struct dm_net_peer {
    dm_u8 mac[6];
    dm_u8 ip[4];
    dm_u16 port;
} dm_net_peer;

typedef struct dm_udp_datagram {
    const dm_u8 *payload;
    dm_u16 payload_length;
    dm_net_peer peer;
} dm_udp_datagram;

int dm_net_open(dm_u8 packet_interrupt, const dm_u8 ip[4], dm_u16 port);
void dm_net_close(void);
int dm_net_poll(dm_udp_datagram *datagram);
void dm_net_release(void);
int dm_net_send(const dm_net_peer *peer, const dm_u8 *payload, dm_u16 length);
int dm_net_broadcast(dm_u16 port, const dm_u8 *payload, dm_u16 length);
const dm_u8 *dm_net_address(void);

#endif
