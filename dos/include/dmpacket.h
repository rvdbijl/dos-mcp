#ifndef DMPACKET_H
#define DMPACKET_H

#include "dmproto.h"

#define DM_PACKET_MAX_FRAME 1518

int dm_packet_open(dm_u8 interrupt_number);
void dm_packet_close(void);
int dm_packet_release_handles(
    dm_u8 interrupt_number,
    dm_u16 ip_handle,
    dm_u16 arp_handle
);
int dm_packet_send(const dm_u8 *frame, dm_u16 length);
const dm_u8 *dm_packet_receive(dm_u16 *length);
void dm_packet_release_receive(void);
const dm_u8 *dm_packet_address(void);

extern dm_u8 dm_packet_interrupt;
extern dm_u16 dm_packet_ip_handle;
extern dm_u16 dm_packet_arp_handle;
extern volatile dm_u8 dm_receive_ready;
extern volatile dm_u16 dm_receive_length;
extern volatile dm_u16 dm_receive_allocations;
extern volatile dm_u16 dm_receive_completions;
extern volatile dm_u16 dm_receive_drops;
extern volatile dm_u16 dm_receive_last_bios_tick;
extern volatile dm_u16 dm_send_attempts;
extern volatile dm_u16 dm_send_failures;

#endif
