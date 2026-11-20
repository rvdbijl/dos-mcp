#include "dmpacket.h"

#include <stdio.h>

extern void __far dm_packet_receiver(void);

/* A plain far call leaves our saved word above CS:IP, just as ODIPKT does.
 * Use a valid flags word so an erroneous IRET can return to report failure. */
static unsigned receiver_stack_delta(unsigned phase, unsigned length);
#pragma aux receiver_stack_delta = \
    "pushf" \
    "mov bx,sp" \
    "call far ptr dm_packet_receiver" \
    "mov ax,sp" \
    "mov sp,bx" \
    "sub ax,bx" \
    "popf" \
    parm [ax] [cx] value [ax] modify [bx di es];

int main(void)
{
    if (receiver_stack_delta(0, DM_PACKET_MAX_FRAME + 1)
        || receiver_stack_delta(0, DM_PACKET_MAX_FRAME)
        || receiver_stack_delta(1, DM_PACKET_MAX_FRAME)
        || receiver_stack_delta(0, 60)
        || dm_receive_length != DM_PACKET_MAX_FRAME || !dm_receive_ready
        || dm_receive_allocations != 3 || dm_receive_completions != 1
        || dm_receive_drops != 2) {
        puts("FAIL packet receiver far call");
        return 1;
    }
    puts("PASS packet receiver far call");
    return 0;
}
