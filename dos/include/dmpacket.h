#ifndef DMPACKET_H
#define DMPACKET_H

#include "dmproto.h"

#define DM_PACKET_MAX_FRAME 1518

int dm_packet_open(dm_u8 interrupt_number);
void dm_packet_close(void);
int dm_packet_send(const dm_u8 *frame, dm_u16 length);
const dm_u8 *dm_packet_receive(dm_u16 *length);
void dm_packet_release_receive(void);
const dm_u8 *dm_packet_address(void);

#endif
