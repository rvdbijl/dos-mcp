#include "dmpacket.h"

#include <dos.h>
#include <i86.h>
#include <string.h>

static dm_u8 packet_interrupt;
static dm_u16 ip_handle;
static dm_u16 arp_handle;
static dm_u8 hardware_address[6];
static dm_u8 ip_type[2] = {0x08, 0x00};
static dm_u8 arp_type[2] = {0x08, 0x06};
volatile dm_u8 dm_receive_ready;
volatile dm_u16 dm_receive_length;
dm_u8 dm_receive_buffer[DM_PACKET_MAX_FRAME];

static int packet_access(const dm_u8 type[2], dm_u16 *handle);
static void packet_release(dm_u16 handle);
static int packet_driver_present(dm_u8 interrupt_number);
extern void __interrupt __far dm_packet_receiver(void);

int dm_packet_open(dm_u8 interrupt_number)
{
    union REGPACK registers;

    if (!packet_driver_present(interrupt_number))
        return -1;
    packet_interrupt = interrupt_number;
    dm_receive_ready = 0;
    ip_handle = 0;
    arp_handle = 0;
    if (packet_access(ip_type, &ip_handle) != 0)
        return -2;
    if (packet_access(arp_type, &arp_handle) != 0) {
        packet_release(ip_handle);
        ip_handle = 0;
        return -3;
    }
    memset(&registers, 0, sizeof(registers));
    registers.w.ax = 0x0600;
    registers.w.bx = ip_handle;
    registers.w.cx = sizeof(hardware_address);
    registers.w.es = _FP_SEG(hardware_address);
    registers.w.di = _FP_OFF(hardware_address);
    intr(packet_interrupt, &registers);
    if (registers.w.flags & INTR_CF) {
        dm_packet_close();
        return -4;
    }
    return 0;
}

void dm_packet_close(void)
{
    if (arp_handle)
        packet_release(arp_handle);
    if (ip_handle)
        packet_release(ip_handle);
    arp_handle = 0;
    ip_handle = 0;
    dm_receive_ready = 0;
}

int dm_packet_send(const dm_u8 *frame, dm_u16 length)
{
    union REGPACK registers;

    if (!ip_handle || length < 14 || length > DM_PACKET_MAX_FRAME)
        return -1;
    memset(&registers, 0, sizeof(registers));
    registers.w.ax = 0x0400;
    registers.w.cx = length;
    registers.w.ds = _FP_SEG(frame);
    registers.w.si = _FP_OFF(frame);
    intr(packet_interrupt, &registers);
    return (registers.w.flags & INTR_CF) ? -2 : 0;
}

const dm_u8 *dm_packet_receive(dm_u16 *length)
{
    if (!dm_receive_ready)
        return 0;
    *length = dm_receive_length;
    return dm_receive_buffer;
}

void dm_packet_release_receive(void)
{
    dm_receive_ready = 0;
}

const dm_u8 *dm_packet_address(void)
{
    return hardware_address;
}

static int packet_access(const dm_u8 type[2], dm_u16 *handle)
{
    union REGPACK registers;

    memset(&registers, 0, sizeof(registers));
    registers.w.ax = 0x0201;
    registers.w.bx = 0xFFFF;
    registers.w.cx = 2;
    registers.w.dx = 0;
    registers.w.ds = _FP_SEG(type);
    registers.w.si = _FP_OFF(type);
    registers.w.es = _FP_SEG(dm_packet_receiver);
    registers.w.di = _FP_OFF(dm_packet_receiver);
    intr(packet_interrupt, &registers);
    if (registers.w.flags & INTR_CF)
        return -1;
    *handle = registers.w.ax;
    return 0;
}

static void packet_release(dm_u16 handle)
{
    union REGPACK registers;

    memset(&registers, 0, sizeof(registers));
    registers.w.ax = 0x0300;
    registers.w.bx = handle;
    intr(packet_interrupt, &registers);
}

static int packet_driver_present(dm_u8 interrupt_number)
{
    const dm_u8 __far *entry = (const dm_u8 __far *)_dos_getvect(interrupt_number);
    static const char signature[] = "PKT DRVR";
    dm_u8 index;

    if (!entry)
        return 0;
    for (index = 0; index < 8; ++index) {
        if (entry[index + 3] != (dm_u8)signature[index])
            return 0;
    }
    return 1;
}
