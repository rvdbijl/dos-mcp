#include "dmnet.h"
#include "dmproto.h"

#include <bios.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AGENT_PORT 21300
#define MAX_RESPONSE_FRAGMENTS 5
#define RESPONSE_DATAGRAM_SIZE (DM_HEADER_SIZE + DM_MAX_FRAGMENT_PAYLOAD)
#define REQUEST_BUFFER_SIZE 4229
#define SCREEN_BUFFER_SIZE 4013

#define CAP_STATUS 0x00000001UL
#define CAP_TEXT 0x00000002UL
#define CAP_KEYBOARD 0x00000004UL

#define PHASE_AGENT_SHELL_READY 3
#define ADAPTER_MDA 1
#define ADAPTER_CGA 2
#define ADAPTER_EGA 3
#define ADAPTER_VGA 4

typedef struct agent_session {
    dm_u8 active;
    dm_u16 id;
    dm_u8 key[16];
    dm_net_peer peer;
    dm_u16 last_request_id;
    dm_u8 has_last_request;
    dm_u8 response_count;
    dm_u16 response_lengths[MAX_RESPONSE_FRAGMENTS];
    dm_u8 responses[MAX_RESPONSE_FRAGMENTS][RESPONSE_DATAGRAM_SIZE];
} agent_session;

static dm_u8 base_key[16];
static agent_session session;
static dm_u8 request_buffer[REQUEST_BUFFER_SIZE];
static dm_u16 request_fragment_lengths[DM_MAX_FRAGMENTS];
static dm_u32 request_fragment_mask;
static dm_u16 request_id;
static dm_u8 request_fragment_count;
static dm_u8 request_opcode;
static dm_u8 screen_buffer[SCREEN_BUFFER_SIZE];
static dm_u8 running = 1;
static dm_u32 agent_start_ticks;

static int parse_ip(const char *text, dm_u8 output[4]);
static int parse_raw_key(const char *text, dm_u8 output[16]);
static int parse_credential(
    const char *text,
    dm_u8 output[16],
    dm_u8 *open_mode
);
static int is_hex_key(const char *text);
static int request_is_newer(dm_u16 candidate, dm_u16 previous);
static dm_u32 make_nonce(void);
static void process_datagram(const dm_udp_datagram *datagram);
static void process_hello(const dm_packet *packet, const dm_net_peer *peer);
static void process_request(
    dm_u8 opcode,
    dm_u16 id,
    const dm_u8 *payload,
    dm_u16 length
);
static void send_response(
    dm_u8 kind,
    dm_u8 opcode,
    dm_u16 id,
    const dm_u8 *payload,
    dm_u16 length
);
static void send_error(dm_u8 opcode, dm_u16 id, dm_u16 code, const char *message);
static void resend_cached(void);
static dm_u16 build_status(dm_u8 *output);
static dm_u16 build_capabilities(dm_u8 *output);
static dm_u16 capture_screen(dm_u8 *output);
static dm_u16 inject_keys(const dm_u8 *payload, dm_u16 length, dm_u8 *output);
static int bios_queue_word(dm_u16 word);
static dm_u8 ascii_scan(dm_u8 value);
static dm_u16 named_key_word(dm_u8 key);
static void process_shell_keyboard(void);
static void print_prompt(void);
static dm_u32 read_bios_ticks(void);
static dm_u8 detect_adapter(dm_u8 video_mode);

int main(int argc, char **argv)
{
    dm_u8 ip[4] = {10, 0, 2, 15};
    dm_u16 port = AGENT_PORT;
    dm_u8 packet_interrupt = 0x60;
    dm_u8 open_mode;
    dm_udp_datagram datagram;
    int result;

    if (argc > 5) {
        puts("Usage: RAGENT [credential] [ip] [port] [packet-int]");
        puts("  credential: pass:text, key:32hex, bare text/key, or - for open");
        return 1;
    }
    if (parse_credential(argc >= 2 ? argv[1] : 0, base_key, &open_mode) != 0) {
        puts("RAGENT: invalid or empty credential");
        return 2;
    }
    if (argc >= 3 && parse_ip(argv[2], ip) != 0) {
        puts("RAGENT: invalid IPv4 address");
        return 3;
    }
    if (argc >= 4)
        port = (dm_u16)strtoul(argv[3], 0, 0);
    if (argc >= 5)
        packet_interrupt = (dm_u8)strtoul(argv[4], 0, 0);
    if (!port) {
        puts("RAGENT: port must not be zero");
        return 4;
    }
    memset(&session, 0, sizeof(session));
    result = dm_net_open(packet_interrupt, ip, port);
    if (result != 0) {
        printf("RAGENT: packet driver initialization failed (%d)\n", result);
        return 5;
    }
    agent_start_ticks = read_bios_ticks();
    printf("Retro DOS Agent 0.1 - %u.%u.%u.%u:%u\n",
        ip[0], ip[1], ip[2], ip[3], port);
    if (open_mode)
        puts("WARNING: OPEN MODE - packets are not authenticated.");
    else
        puts("Authenticated UDP ready.");
    puts("EXIT or local Ctrl-Alt-Esc stops.");
    print_prompt();
    while (running) {
        if (dm_net_poll(&datagram)) {
            process_datagram(&datagram);
            dm_net_release();
        }
        process_shell_keyboard();
    }
    dm_net_close();
    puts("\nRAGENT stopped.");
    return 0;
}

static int parse_ip(const char *text, dm_u8 output[4])
{
    unsigned values[4];
    char tail;

    if (sscanf(text, "%u.%u.%u.%u%c",
        &values[0], &values[1], &values[2], &values[3], &tail) != 4)
        return -1;
    if (values[0] > 255 || values[1] > 255
        || values[2] > 255 || values[3] > 255)
        return -1;
    output[0] = (dm_u8)values[0];
    output[1] = (dm_u8)values[1];
    output[2] = (dm_u8)values[2];
    output[3] = (dm_u8)values[3];
    return 0;
}

static int parse_raw_key(const char *text, dm_u8 output[16])
{
    dm_u8 index;
    dm_u8 any = 0;

    if (strlen(text) != 32)
        return -1;
    for (index = 0; index < 16; ++index) {
        char pair[3];
        char *end;
        unsigned long value;

        pair[0] = text[index * 2];
        pair[1] = text[index * 2 + 1];
        pair[2] = 0;
        value = strtoul(pair, &end, 16);
        if (*end)
            return -1;
        output[index] = (dm_u8)value;
        any |= output[index];
    }
    return any ? 0 : -1;
}

static int parse_credential(
    const char *text,
    dm_u8 output[16],
    dm_u8 *open_mode
)
{
    const char *password;

    *open_mode = 0;
    if (text == 0 || strcmp(text, "-") == 0) {
        dm_open_mode_key(output);
        *open_mode = 1;
        return 0;
    }
    if (strncmp(text, "key:", 4) == 0)
        return parse_raw_key(text + 4, output);
    if (strncmp(text, "pass:", 5) == 0) {
        password = text + 5;
        if (!*password)
            return -1;
        dm_derive_password_key(
            (const dm_u8 *)password,
            (dm_u16)strlen(password),
            output
        );
        return 0;
    }
    if (!*text)
        return -1;
    if (is_hex_key(text))
        return parse_raw_key(text, output);
    dm_derive_password_key(
        (const dm_u8 *)text,
        (dm_u16)strlen(text),
        output
    );
    return 0;
}

static int is_hex_key(const char *text)
{
    dm_u8 index;

    if (strlen(text) != 32)
        return 0;
    for (index = 0; index < 32; ++index) {
        char value = text[index];

        if (!((value >= '0' && value <= '9')
            || (value >= 'a' && value <= 'f')
            || (value >= 'A' && value <= 'F')))
            return 0;
    }
    return 1;
}

static int request_is_newer(dm_u16 candidate, dm_u16 previous)
{
    dm_u16 distance = (dm_u16)(candidate - previous);
    return distance != 0 && distance < 0x8000;
}

static dm_u32 make_nonce(void)
{
    volatile dm_u32 __far *ticks = (volatile dm_u32 __far *)MK_FP(0x40, 0x6C);
    return *ticks ^ ((dm_u32)clock() << 16) ^ 0xD05A9E37UL;
}

static void process_datagram(const dm_udp_datagram *datagram)
{
    dm_packet packet;
    const dm_u8 *key;
    dm_u16 final_length;
    dm_u8 index;
    dm_u32 expected_mask;

    key = base_key;
    if (datagram->payload_length >= DM_HEADER_SIZE
        && datagram->payload[4] != DM_OP_HELLO) {
        if (!session.active)
            return;
        key = session.key;
    }
    if (dm_packet_decode(
        datagram->payload, datagram->payload_length, key, &packet) != DM_OK)
        return;
    if (packet.kind != DM_KIND_REQUEST)
        return;
    if (packet.opcode == DM_OP_HELLO && packet.session_id == 0) {
        process_hello(&packet, &datagram->peer);
        return;
    }
    if (!session.active
        || packet.session_id != session.id
        || memcmp(session.peer.mac, datagram->peer.mac, 6) != 0
        || memcmp(session.peer.ip, datagram->peer.ip, 4) != 0
        || session.peer.port != datagram->peer.port)
        return;
    if (session.has_last_request && packet.request_id == session.last_request_id) {
        resend_cached();
        return;
    }
    if (session.has_last_request
        && !request_is_newer(packet.request_id, session.last_request_id)) {
        send_error(packet.opcode, packet.request_id, 4, "replayed request");
        return;
    }
    if (packet.request_id != request_id) {
        request_id = packet.request_id;
        request_fragment_count = packet.fragment_count;
        request_opcode = packet.opcode;
        request_fragment_mask = 0;
        memset(request_fragment_lengths, 0, sizeof(request_fragment_lengths));
    }
    if (packet.fragment_count != request_fragment_count
        || packet.opcode != request_opcode
        || (dm_u32)packet.fragment_index * DM_MAX_FRAGMENT_PAYLOAD
            + packet.payload_length > sizeof(request_buffer))
        return;
    memcpy(
        request_buffer + (dm_u16)packet.fragment_index * DM_MAX_FRAGMENT_PAYLOAD,
        packet.payload,
        packet.payload_length
    );
    request_fragment_lengths[packet.fragment_index] = packet.payload_length;
    request_fragment_mask |= 1UL << packet.fragment_index;
    expected_mask = request_fragment_count == 32
        ? 0xFFFFFFFFUL
        : (1UL << request_fragment_count) - 1;
    if (request_fragment_mask != expected_mask)
        return;
    for (index = 0; index + 1 < request_fragment_count; ++index) {
        if (request_fragment_lengths[index] != DM_MAX_FRAGMENT_PAYLOAD)
            return;
    }
    final_length = (dm_u16)(request_fragment_count - 1) * DM_MAX_FRAGMENT_PAYLOAD
        + request_fragment_lengths[request_fragment_count - 1];
    process_request(request_opcode, request_id, request_buffer, final_length);
}

static void process_hello(const dm_packet *packet, const dm_net_peer *peer)
{
    dm_u32 client_nonce;
    dm_u32 server_nonce;
    dm_u8 response[12];
    dm_packet reply;
    dm_u8 datagram[DM_HEADER_SIZE + 12];
    dm_u16 datagram_length;

    if (packet->payload_length != 4 || packet->fragment_count != 1)
        return;
    client_nonce = dm_get_u32(packet->payload);
    server_nonce = make_nonce();
    session.id = (dm_u16)(client_nonce ^ server_nonce);
    if (!session.id)
        session.id = 1;
    memcpy(&session.peer, peer, sizeof(session.peer));
    dm_put_u32(response, client_nonce);
    dm_put_u32(response + 4, server_nonce);
    dm_put_u16(response + 8, session.id);
    dm_put_u16(response + 10, DM_MAX_FRAGMENT_PAYLOAD);
    reply.kind = DM_KIND_RESPONSE;
    reply.opcode = DM_OP_HELLO;
    reply.flags = 0;
    reply.session_id = session.id;
    reply.request_id = packet->request_id;
    reply.fragment_index = 0;
    reply.fragment_count = 1;
    reply.payload_length = sizeof(response);
    reply.payload = response;
    if (dm_packet_encode(
        &reply, base_key, datagram, sizeof(datagram), &datagram_length) == DM_OK)
        dm_net_send(peer, datagram, datagram_length);
    dm_derive_session_key(base_key, client_nonce, server_nonce, session.key);
    session.active = 1;
    session.has_last_request = 0;
    session.response_count = 0;
}

static void process_request(
    dm_u8 opcode,
    dm_u16 id,
    const dm_u8 *payload,
    dm_u16 length
)
{
    dm_u16 response_length;

    switch (opcode) {
    case DM_OP_GET_STATUS:
        if (length) {
            send_error(opcode, id, 6, "status payload must be empty");
            return;
        }
        response_length = build_status(screen_buffer);
        break;
    case DM_OP_GET_CAPABILITIES:
        if (length) {
            send_error(opcode, id, 6, "capability payload must be empty");
            return;
        }
        response_length = build_capabilities(screen_buffer);
        break;
    case DM_OP_CAPTURE_TEXT_SCREEN:
        if (length) {
            send_error(opcode, id, 6, "screen payload must be empty");
            return;
        }
        response_length = capture_screen(screen_buffer);
        break;
    case DM_OP_SEND_KEYS:
        response_length = inject_keys(payload, length, screen_buffer);
        if (!response_length) {
            send_error(opcode, id, 6, "invalid key request");
            return;
        }
        break;
    case DM_OP_PING:
        if (length > sizeof(screen_buffer)) {
            send_error(opcode, id, 6, "ping payload too large");
            return;
        }
        memcpy(screen_buffer, payload, length);
        response_length = length;
        break;
    default:
        send_error(opcode, id, 5, "unsupported operation");
        return;
    }
    send_response(DM_KIND_RESPONSE, opcode, id, screen_buffer, response_length);
}

static void send_response(
    dm_u8 kind,
    dm_u8 opcode,
    dm_u16 id,
    const dm_u8 *payload,
    dm_u16 length
)
{
    dm_u8 fragment_count;
    dm_u8 index;

    fragment_count = (dm_u8)((length + DM_MAX_FRAGMENT_PAYLOAD - 1)
        / DM_MAX_FRAGMENT_PAYLOAD);
    if (!fragment_count)
        fragment_count = 1;
    if (fragment_count > MAX_RESPONSE_FRAGMENTS)
        return;
    session.response_count = fragment_count;
    for (index = 0; index < fragment_count; ++index) {
        dm_packet packet;
        dm_u16 offset = (dm_u16)index * DM_MAX_FRAGMENT_PAYLOAD;
        dm_u16 remaining = length - offset;

        packet.kind = kind;
        packet.opcode = opcode;
        packet.flags = 0;
        packet.session_id = session.id;
        packet.request_id = id;
        packet.fragment_index = index;
        packet.fragment_count = fragment_count;
        packet.payload_length = remaining > DM_MAX_FRAGMENT_PAYLOAD
            ? DM_MAX_FRAGMENT_PAYLOAD : remaining;
        packet.payload = payload + offset;
        if (dm_packet_encode(
            &packet,
            session.key,
            session.responses[index],
            sizeof(session.responses[index]),
            &session.response_lengths[index]
        ) != DM_OK)
            return;
    }
    session.last_request_id = id;
    session.has_last_request = 1;
    resend_cached();
}

static void send_error(dm_u8 opcode, dm_u16 id, dm_u16 code, const char *message)
{
    dm_u8 payload[122];
    dm_u16 length = (dm_u16)strlen(message);

    if (length > 120)
        length = 120;
    dm_put_u16(payload, code);
    memcpy(payload + 2, message, length);
    send_response(DM_KIND_ERROR, opcode, id, payload, length + 2);
}

static void resend_cached(void)
{
    dm_u8 index;

    for (index = 0; index < session.response_count; ++index) {
        dm_net_send(
            &session.peer,
            session.responses[index],
            session.response_lengths[index]
        );
    }
}

static dm_u16 build_status(dm_u8 *output)
{
    union REGS input;
    union REGS result;
    volatile dm_u16 __far *memory_kb =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x13);
    dm_u32 current_ticks;
    dm_u32 elapsed_ticks;

    memset(&input, 0, sizeof(input));
    input.h.ah = 0x30;
    intdos(&input, &result);
    output[0] = 0;
    output[1] = 1;
    output[2] = result.h.al;
    output[3] = result.h.ah;
    output[4] = 0;
    output[5] = PHASE_AGENT_SHELL_READY;
    dm_put_u16(output + 6, *memory_kb);
    current_ticks = read_bios_ticks();
    if (current_ticks >= agent_start_ticks)
        elapsed_ticks = current_ticks - agent_start_ticks;
    else
        elapsed_ticks = (0x1800B0UL - agent_start_ticks) + current_ticks;
    dm_put_u32(output + 8, elapsed_ticks);
    return 12;
}

static dm_u16 build_capabilities(dm_u8 *output)
{
    volatile dm_u16 __far *columns =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x4A);

    dm_put_u32(output, CAP_STATUS | CAP_TEXT | CAP_KEYBOARD);
    output[4] = (dm_u8)(*columns > 80 ? 80 : *columns);
    output[5] = 25;
    output[6] = detect_adapter(
        *(volatile dm_u8 __far *)MK_FP(0x40, 0x49)
    );
    dm_put_u16(output + 7, DM_MAX_FRAGMENT_PAYLOAD);
    dm_put_u16(output + 9, 15);
    return 11;
}

static dm_u16 capture_screen(dm_u8 *output)
{
    volatile dm_u8 __far *video_mode =
        (volatile dm_u8 __far *)MK_FP(0x40, 0x49);
    volatile dm_u16 __far *columns =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x4A);
    volatile dm_u16 __far *page_offset =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x4E);
    volatile dm_u16 __far *cursor_positions =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x50);
    volatile dm_u16 __far *cursor_shape =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x60);
    volatile dm_u8 __far *active_page =
        (volatile dm_u8 __far *)MK_FP(0x40, 0x62);
    const dm_u8 __far *video;
    dm_u16 cursor;
    dm_u16 cells;
    dm_u16 index;
    static dm_u16 generation;
    dm_u8 cols = (dm_u8)(*columns > 80 ? 80 : *columns);
    dm_u8 rows = 25;

    if (!cols)
        cols = 80;
    cursor = cursor_positions[*active_page];
    output[0] = cols;
    output[1] = rows;
    output[2] = *video_mode;
    output[3] = *active_page;
    output[4] = (dm_u8)(cursor >> 8);
    output[5] = (dm_u8)cursor;
    output[6] = (dm_u8)(*cursor_shape >> 8);
    output[7] = (dm_u8)*cursor_shape;
    output[8] = detect_adapter(*video_mode);
    dm_put_u16(output + 9, 437);
    dm_put_u16(output + 11, ++generation);
    video = (const dm_u8 __far *)MK_FP(
        *video_mode == 7 ? 0xB000 : 0xB800,
        *page_offset
    );
    cells = (dm_u16)cols * rows * 2;
    for (index = 0; index < cells; ++index)
        output[13 + index] = video[index];
    return 13 + cells;
}

static dm_u16 inject_keys(const dm_u8 *payload, dm_u16 length, dm_u8 *output)
{
    dm_u16 text_length;
    dm_u16 inter_key_delay;
    dm_u8 key_count;
    dm_u16 index;
    dm_u16 accepted_text = 0;
    dm_u8 accepted_keys = 0;
    static dm_u16 generation;

    if (length < 5)
        return 0;
    text_length = dm_get_u16(payload);
    key_count = payload[2];
    inter_key_delay = dm_get_u16(payload + 3);
    if (text_length > 4096
        || key_count > 128
        || inter_key_delay > 1000
        || length != 5 + text_length + key_count)
        return 0;
    for (index = 0; index < text_length; ++index) {
        dm_u8 ascii = payload[5 + index];
        dm_u16 word = ((dm_u16)ascii_scan(ascii) << 8) | ascii;
        if (!bios_queue_word(word))
            break;
        ++accepted_text;
        process_shell_keyboard();
        if (inter_key_delay)
            delay(inter_key_delay);
    }
    for (index = 0; index < key_count; ++index) {
        dm_u16 word = named_key_word(payload[5 + text_length + index]);
        if (!word || !bios_queue_word(word))
            break;
        ++accepted_keys;
        process_shell_keyboard();
        if (inter_key_delay)
            delay(inter_key_delay);
    }
    dm_put_u16(output, accepted_text);
    output[2] = accepted_keys;
    dm_put_u16(output + 3, ++generation);
    return 5;
}

static int bios_queue_word(dm_u16 word)
{
    volatile dm_u16 __far *head =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x1A);
    volatile dm_u16 __far *tail =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x1C);
    dm_u16 current;
    dm_u16 next;

    _disable();
    current = *tail;
    next = current + 2;
    if (next >= 0x3E)
        next = 0x1E;
    if (next == *head) {
        _enable();
        return 0;
    }
    *(volatile dm_u16 __far *)MK_FP(0x40, current) = word;
    *tail = next;
    _enable();
    return 1;
}

static dm_u8 ascii_scan(dm_u8 value)
{
    static const dm_u8 letter_scan[26] = {
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
        0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
        0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
    };
    static const dm_u8 digit_scan[10] = {
        0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A
    };

    if (value >= 'a' && value <= 'z')
        return letter_scan[value - 'a'];
    if (value >= 'A' && value <= 'Z')
        return letter_scan[value - 'A'];
    if (value >= '0' && value <= '9')
        return digit_scan[value - '0'];
    switch (value) {
    case ' ': return 0x39;
    case '-': return 0x0C;
    case '=': return 0x0D;
    case '[': return 0x1A;
    case ']': return 0x1B;
    case ';': return 0x27;
    case '\'': return 0x28;
    case '`': return 0x29;
    case '\\': return 0x2B;
    case ',': return 0x33;
    case '.': return 0x34;
    case '/': return 0x35;
    default: return 0;
    }
}

static dm_u16 named_key_word(dm_u8 key)
{
    static const dm_u16 values[] = {
        0,
        0x1C0D, 0x011B, 0x0F09, 0x0E08,
        0x4800, 0x5000, 0x4D00, 0x4B00,
        0x4700, 0x4F00, 0x5300, 0x4900, 0x5100,
        0x3B00, 0x3C00, 0x3D00, 0x3E00, 0x3F00, 0x4000,
        0x4100, 0x4200, 0x4300, 0x4400, 0x8500, 0x8600,
        0x2E03, 0x2004
    };
    if (key >= sizeof(values) / sizeof(values[0]))
        return 0;
    return values[key];
}

static void process_shell_keyboard(void)
{
    static char command[128];
    static dm_u8 length;
    unsigned key;
    dm_u8 ascii;

    if (!_bios_keybrd(_KEYBRD_READY))
        return;
    key = _bios_keybrd(_KEYBRD_READ);
    ascii = (dm_u8)key;
    if (ascii == 27
        && (_bios_keybrd(_KEYBRD_SHIFTSTATUS) & 0x0C) == 0x0C) {
        running = 0;
        return;
    }
    if (ascii == 13) {
        command[length] = 0;
        putchar('\n');
        if (stricmp(command, "EXIT") == 0) {
            running = 0;
            return;
        }
        if (length)
            system(command);
        length = 0;
        print_prompt();
    } else if (ascii == 8) {
        if (length) {
            --length;
            fputs("\b \b", stdout);
        }
    } else if (ascii >= 32 && length + 1 < sizeof(command)) {
        command[length++] = (char)ascii;
        putchar(ascii);
    }
}

static void print_prompt(void)
{
    fputs("RAGENT> ", stdout);
    fflush(stdout);
}

static dm_u32 read_bios_ticks(void)
{
    volatile dm_u32 __far *ticks =
        (volatile dm_u32 __far *)MK_FP(0x40, 0x6C);
    dm_u32 value;

    _disable();
    value = *ticks;
    _enable();
    return value;
}

static dm_u8 detect_adapter(dm_u8 video_mode)
{
    union REGS input;
    union REGS result;

    memset(&input, 0, sizeof(input));
    input.x.ax = 0x1A00;
    int86(0x10, &input, &result);
    if (result.h.al == 0x1A) {
        switch (result.h.bl) {
        case 1:
            return ADAPTER_MDA;
        case 2:
        case 3:
            return ADAPTER_CGA;
        case 4:
        case 5:
            return ADAPTER_EGA;
        case 7:
        case 8:
            return ADAPTER_VGA;
        }
    }
    return video_mode == 7 ? ADAPTER_MDA : ADAPTER_CGA;
}
