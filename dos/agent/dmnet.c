#include "dmnet.h"

#include "dmpacket.h"

#include <string.h>

#define ETH_HEADER 14
#define IP_HEADER 20
#define UDP_HEADER 8
#define ETH_TYPE_IP 0x0800
#define ETH_TYPE_ARP 0x0806

static dm_u8 local_ip[4];
static dm_u16 local_port;
static dm_u16 ip_identifier;
static dm_u8 transmit_frame[DM_PACKET_MAX_FRAME];

static dm_u16 read_be16(const dm_u8 *value);
static void write_be16(dm_u8 *output, dm_u16 value);
static dm_u16 ip_checksum(const dm_u8 *data, dm_u16 length);
static void handle_arp(const dm_u8 *frame, dm_u16 length);

int dm_net_open(dm_u8 packet_interrupt, const dm_u8 ip[4], dm_u16 port)
{
    memcpy(local_ip, ip, sizeof(local_ip));
    local_port = port;
    ip_identifier = 1;
    return dm_packet_open(packet_interrupt);
}

void dm_net_close(void)
{
    dm_packet_close();
}

int dm_net_poll(dm_udp_datagram *datagram)
{
    const dm_u8 *frame;
    const dm_u8 *ip;
    const dm_u8 *udp;
    dm_u16 frame_length;
    dm_u16 ethernet_type;
    dm_u16 ip_length;
    dm_u16 udp_length;
    dm_u8 ip_header_length;

    frame = dm_packet_receive(&frame_length);
    if (!frame)
        return 0;
    if (frame_length < ETH_HEADER) {
        dm_packet_release_receive();
        return 0;
    }
    ethernet_type = read_be16(frame + 12);
    if (ethernet_type == ETH_TYPE_ARP) {
        handle_arp(frame, frame_length);
        dm_packet_release_receive();
        return 0;
    }
    if (ethernet_type != ETH_TYPE_IP || frame_length < ETH_HEADER + IP_HEADER) {
        dm_packet_release_receive();
        return 0;
    }
    ip = frame + ETH_HEADER;
    ip_header_length = (dm_u8)((ip[0] & 0x0F) * 4);
    ip_length = read_be16(ip + 2);
    if ((ip[0] >> 4) != 4
        || ip_header_length < IP_HEADER
        || ip_length < ip_header_length + UDP_HEADER
        || ETH_HEADER + ip_length > frame_length
        || ip[9] != 17
        || memcmp(ip + 16, local_ip, 4) != 0
        || (read_be16(ip + 6) & 0x3FFF) != 0
        || ip_checksum(ip, ip_header_length) != 0) {
        dm_packet_release_receive();
        return 0;
    }
    udp = ip + ip_header_length;
    udp_length = read_be16(udp + 4);
    if (udp_length < UDP_HEADER
        || udp_length > ip_length - ip_header_length
        || read_be16(udp + 2) != local_port) {
        dm_packet_release_receive();
        return 0;
    }
    memcpy(datagram->peer.mac, frame + 6, 6);
    memcpy(datagram->peer.ip, ip + 12, 4);
    datagram->peer.port = read_be16(udp);
    datagram->payload = udp + UDP_HEADER;
    datagram->payload_length = udp_length - UDP_HEADER;
    return 1;
}

void dm_net_release(void)
{
    dm_packet_release_receive();
}

int dm_net_send(const dm_net_peer *peer, const dm_u8 *payload, dm_u16 length)
{
    dm_u8 *ip;
    dm_u8 *udp;
    dm_u16 ip_length;
    dm_u16 frame_length;

    if (length > DM_PACKET_MAX_FRAME - ETH_HEADER - IP_HEADER - UDP_HEADER)
        return -1;
    memcpy(transmit_frame, peer->mac, 6);
    memcpy(transmit_frame + 6, dm_packet_address(), 6);
    write_be16(transmit_frame + 12, ETH_TYPE_IP);
    ip = transmit_frame + ETH_HEADER;
    memset(ip, 0, IP_HEADER);
    ip[0] = 0x45;
    ip_length = IP_HEADER + UDP_HEADER + length;
    write_be16(ip + 2, ip_length);
    write_be16(ip + 4, ip_identifier++);
    ip[8] = 64;
    ip[9] = 17;
    memcpy(ip + 12, local_ip, 4);
    memcpy(ip + 16, peer->ip, 4);
    write_be16(ip + 10, ip_checksum(ip, IP_HEADER));
    udp = ip + IP_HEADER;
    write_be16(udp, local_port);
    write_be16(udp + 2, peer->port);
    write_be16(udp + 4, UDP_HEADER + length);
    write_be16(udp + 6, 0);
    memcpy(udp + UDP_HEADER, payload, length);
    frame_length = ETH_HEADER + ip_length;
    if (frame_length < 60) {
        memset(transmit_frame + frame_length, 0, 60 - frame_length);
        frame_length = 60;
    }
    return dm_packet_send(transmit_frame, frame_length);
}

static dm_u16 read_be16(const dm_u8 *value)
{
    return ((dm_u16)value[0] << 8) | value[1];
}

static void write_be16(dm_u8 *output, dm_u16 value)
{
    output[0] = (dm_u8)(value >> 8);
    output[1] = (dm_u8)value;
}

static dm_u16 ip_checksum(const dm_u8 *data, dm_u16 length)
{
    dm_u32 sum = 0;

    while (length > 1) {
        sum += read_be16(data);
        data += 2;
        length -= 2;
    }
    if (length)
        sum += (dm_u16)data[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFUL) + (sum >> 16);
    return (dm_u16)~sum;
}

static void handle_arp(const dm_u8 *frame, dm_u16 length)
{
    const dm_u8 *arp;
    dm_u8 *reply;

    if (length < 42)
        return;
    arp = frame + ETH_HEADER;
    if (read_be16(arp) != 1
        || read_be16(arp + 2) != ETH_TYPE_IP
        || arp[4] != 6
        || arp[5] != 4
        || read_be16(arp + 6) != 1
        || memcmp(arp + 24, local_ip, 4) != 0)
        return;
    reply = transmit_frame;
    memcpy(reply, frame + 6, 6);
    memcpy(reply + 6, dm_packet_address(), 6);
    write_be16(reply + 12, ETH_TYPE_ARP);
    write_be16(reply + 14, 1);
    write_be16(reply + 16, ETH_TYPE_IP);
    reply[18] = 6;
    reply[19] = 4;
    write_be16(reply + 20, 2);
    memcpy(reply + 22, dm_packet_address(), 6);
    memcpy(reply + 28, local_ip, 4);
    memcpy(reply + 32, arp + 8, 6);
    memcpy(reply + 38, arp + 14, 4);
    memset(reply + 42, 0, 18);
    dm_packet_send(reply, 60);
}
