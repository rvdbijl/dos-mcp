#include "dmconfig.h"
#include "dmnet.h"
#include "dmpacket.h"
#include "dmproto.h"

#include <bios.h>
#include <conio.h>
#include <direct.h>
#include <dos.h>
#include <i86.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AGENT_PORT 21300
#define RESPONSE_DATAGRAM_SIZE (DM_HEADER_SIZE + DM_MAX_FRAGMENT_PAYLOAD)
#define REQUEST_BUFFER_SIZE 4229
#define SCREEN_BUFFER_SIZE 4013

#ifdef RA_TSR
#define RESPONSE_FRAGMENT_PAYLOAD 256
#define MAX_RESPONSE_FRAGMENTS 16
#else
#define RESPONSE_FRAGMENT_PAYLOAD DM_MAX_FRAGMENT_PAYLOAD
#define MAX_RESPONSE_FRAGMENTS 5
#endif

#define CAP_STATUS 0x00000001UL
#define CAP_TEXT 0x00000002UL
#define CAP_KEYBOARD 0x00000004UL
#define CAP_FILESYSTEM_READ 0x00000008UL
#define CAP_FILESYSTEM_WRITE 0x00000010UL
#define CAP_GRAPHICS 0x00000800UL

#define PHASE_AGENT_SHELL_READY 3
#define ADAPTER_MDA 1
#define ADAPTER_CGA 2
#define ADAPTER_EGA 3
#define ADAPTER_VGA 4

#ifdef RA_TSR
#define TSR_MULTIPLEX 0xD05A
#define TSR_REPLY 0x5A5A
#define TRANSFER_NONE 0
#define TRANSFER_READ 1
#define TRANSFER_WRITE 2
#define TRANSFER_GRAPHICS 3
#define TRANSFER_BLOCK_MAX 900
#define TSR_FILE_MAX 1048576UL
#define TSR_PATH_MAX 127
#define TSR_NAME_MAX 31
#define DISCOVERY_PORT 21301
#define DISCOVERY_INTERVAL_TICKS 91
#define SESSION_IDLE_TICKS 546
#define LAYOUT_CGA_2BPP 1
#define LAYOUT_CGA_1BPP 2
#define LAYOUT_HERCULES 3
#define LAYOUT_PLANAR_4BPP 4
#define LAYOUT_PLANAR_1BPP 5
#define LAYOUT_PACKED_8BPP 6
#define DIAGNOSTICS_VERSION 1
#define DIAG_OWNS_INT08 0x01
#define DIAG_OWNS_INT1C 0x02
#define DIAG_OWNS_INT28 0x04
#define DIAG_OWNS_INT2F 0x08
#define DIAG_ENABLED 0x10
#define DIAG_RECEIVE_READY 0x20
#define DIAG_SESSION_ACTIVE 0x40
#define DIAG_RESPONSE_PENDING 0x80
#endif

typedef struct agent_session {
    dm_u8 active;
    dm_u16 id;
    dm_u8 key[16];
    dm_net_peer peer;
    dm_u16 last_request_id;
    dm_u8 has_last_request;
    dm_u8 response_count;
#ifdef RA_TSR
    dm_u8 response_next;
    dm_u8 response_pending;
    dm_u8 response_kind;
    dm_u8 response_opcode;
    dm_u16 response_request_id;
    dm_u16 response_payload_length;
    dm_u16 last_activity_tick;
#endif
#ifdef RA_TSR
    dm_u16 response_lengths[1];
    dm_u8 responses[1][RESPONSE_DATAGRAM_SIZE];
#else
    dm_u16 response_lengths[MAX_RESPONSE_FRAGMENTS];
    dm_u8 responses[MAX_RESPONSE_FRAGMENTS][RESPONSE_DATAGRAM_SIZE];
#endif
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
#ifndef RA_TSR
static dm_u8 running = 1;
#endif
static dm_u32 agent_start_ticks;

#ifdef RA_TSR
void (__interrupt __far *ratsr_old_int08)(void);
void (__interrupt __far *ratsr_old_int1c)(void);
void (__interrupt __far *ratsr_old_int28)(void);
void (__interrupt __far *ratsr_old_int2f)(void);
dm_u16 ratsr_psp;
volatile dm_u8 ratsr_enabled;
volatile dm_u16 ratsr_ticks;
volatile dm_u16 ratsr_int08_entries;
volatile dm_u16 ratsr_int1c_entries;
volatile dm_u16 ratsr_int28_entries;
volatile dm_u16 ratsr_worker_runs;
volatile dm_u16 ratsr_fallback_runs;
volatile dm_u16 ratsr_busy_skips;
volatile dm_u16 ratsr_last_worker_bios_tick;
volatile int ratsr_last_protocol_result;
volatile dm_u8 ratsr_last_opcode;
volatile int ratsr_last_send_result;
static char sandbox_root[TSR_PATH_MAX + 1];
static char agent_name[TSR_NAME_MAX + 1];
static dm_u8 discovery_open_mode;
static dm_u16 discovery_last_tick;
static dm_u16 advertised_port;
static dm_u8 allow_file_read;
static dm_u8 allow_file_write;
static dm_u8 unrestricted_filesystem;
static dm_u8 cached_dos_major;
static dm_u8 cached_dos_minor;
static dm_u8 cached_adapter;
static volatile dm_u8 __far *indos_flag;
static volatile dm_u8 __far *critical_error_flag;
volatile dm_u8 transfer_kind;
static dm_u16 transfer_id;
static int transfer_handle = -1;
static dm_u32 transfer_size;
static dm_u32 transfer_offset;
static dm_u32 transfer_crc;
static dm_u32 transfer_expected_crc;
static dm_u8 transfer_overwrite;
static char transfer_path[TSR_PATH_MAX + 1];
static char transfer_temp[TSR_PATH_MAX + 1];
static dm_u8 graphics_layout;
static dm_u8 graphics_planes;
static dm_u16 graphics_width;
static dm_u16 graphics_height;
static dm_u32 graphics_bytes_per_plane;
static dm_u8 graphics_mode;
static dm_u8 text_capture_header[13];
static dm_u16 text_capture_segment;
static dm_u16 text_capture_offset;
static dm_u16 text_capture_cells;
extern void __interrupt __far ratsr_idle_handler(void);
extern void __interrupt __far ratsr_timer_handler(void);
extern void __interrupt __far ratsr_dos_idle_handler(void);
extern void __interrupt __far ratsr_multiplex_handler(void);
extern int ratsr_bios_queue_word(dm_u16 word);
extern void ratsr_set_old_vectors(
    dm_u16 int1c_offset,
    dm_u16 int1c_segment,
    dm_u16 int2f_offset,
    dm_u16 int2f_segment
);
extern void ratsr_set_old_int08(dm_u16 offset, dm_u16 segment);
extern void ratsr_set_old_int28(dm_u16 offset, dm_u16 segment);
extern char __far ratsr_resident_end;
#endif

static int parse_ip(const char *text, dm_u8 output[4]);
static int parse_u16(const char *text, dm_u16 *output);
static int parse_u8(const char *text, dm_u8 *output);
static int configure_network(
    int argc,
    char **argv,
    dm_u8 ip[4],
    dm_u16 *port,
    dm_u8 *packet_interrupt,
    char *mtcp_hostname,
    dm_u8 use_mtcp_hostname,
    const char *program
);
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
#ifdef RA_TSR
static dm_u16 build_diagnostics(dm_u8 *output);
static dm_u8 owns_vector(
    dm_u8 interrupt_number,
    void (__interrupt __far *handler)(void)
);
#endif
#ifndef RA_TSR
static dm_u16 capture_screen(dm_u8 *output);
#else
static dm_u16 prepare_text_capture(void);
static void fill_text_capture_fragment(
    dm_u16 offset,
    dm_u16 length,
    dm_u8 *output
);
#endif
static dm_u16 inject_keys(const dm_u8 *payload, dm_u16 length, dm_u8 *output);
static int bios_queue_word(dm_u16 word);
static dm_u8 ascii_scan(dm_u8 value);
static dm_u16 named_key_word(dm_u8 key);
#ifndef RA_TSR
static void process_shell_keyboard(void);
static void print_prompt(void);
#endif
static dm_u32 read_bios_ticks(void);
static dm_u8 detect_adapter(dm_u8 video_mode);
#ifdef RA_TSR
static int uninstall_tsr(void);
static int configure_tsr(int argc, char **argv, const char *mtcp_hostname);
static void keep_resident(void);
static dm_u32 crc32_update(dm_u32 crc, const dm_u8 *data, dm_u16 length);
static int safe_relative_path(const char *path);
static int safe_absolute_path(const char *path);
static int build_sandbox_path(
    const dm_u8 *value,
    dm_u16 length,
    char *output
);
static void close_transfer(int remove_temporary);
static int create_transfer_temp(dm_u16 id);
static dm_u16 begin_file_read(const dm_u8 *payload, dm_u16 length, dm_u8 *output);
static dm_u16 read_transfer_block(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
);
static dm_u16 end_read_transfer(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
);
static dm_u16 begin_file_write(const dm_u8 *payload, dm_u16 length, dm_u8 *output);
static dm_u16 write_transfer_block(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
);
static dm_u16 commit_file_write(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
);
static dm_u16 abort_transfer(const dm_u8 *payload, dm_u16 length);
static void send_discovery(void);
static dm_u16 begin_graphics(dm_u8 *output);
static int graphics_info(void);
static dm_u16 read_graphics_block(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
);
#endif

#ifndef RA_TSR
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
    if (configure_network(
        argc, argv, ip, &port, &packet_interrupt, 0, 0, "RAGENT"
    ) != 0) {
        return 4;
    }
    memset(&session, 0, sizeof(session));
    result = dm_net_open(packet_interrupt, ip, port);
    if (result != 0) {
        printf("RAGENT: packet driver initialization failed (%d)\n", result);
        return 5;
    }
    agent_start_ticks = read_bios_ticks();
    printf("Retro DOS Agent 0.1 - %u.%u.%u.%u:%u, packet int 0x%02X\n",
        ip[0], ip[1], ip[2], ip[3], port, packet_interrupt);
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
#else
int main(int argc, char **argv)
{
    dm_u8 ip[4] = {10, 0, 2, 15};
    dm_u16 port = AGENT_PORT;
    dm_u8 packet_interrupt = 0x60;
    char mtcp_hostname[DM_MTCP_HOSTNAME_MAX + 1];
    dm_u8 open_mode;
    union REGS input;
    union REGS output;
    union REGPACK registers;
    int result;

    if (argc == 2 && stricmp(argv[1], "/U") == 0)
        return uninstall_tsr();
    if (argc > 8) {
        puts("Usage: RA-TSR [credential] [ip] [port] [packet-int] [root] [access] [name]");
        puts("  access: - (none), R, W, or RW; name: 1-31 visible ASCII");
        puts("  root: existing directory, or ALL for unrestricted DOS drives");
        puts("  unload with RA-TSR /U");
        return 1;
    }
    memset(&input, 0, sizeof(input));
    input.x.ax = TSR_MULTIPLEX;
    input.x.bx = 0;
    int86(0x2F, &input, &output);
    if (output.x.ax == TSR_REPLY) {
        dm_u16 ticks;
        dm_u16 receive_ready;
        dm_u16 receive_length;
        int protocol_result;
        int send_result;

        delay(1000);
        memset(&input, 0, sizeof(input));
        input.x.ax = TSR_MULTIPLEX;
        input.x.bx = 2;
        int86(0x2F, &input, &output);
        ticks = output.x.bx;
        receive_ready = output.x.cx;
        receive_length = output.x.dx;
        protocol_result = (int)output.x.si;
        send_result = (int)output.x.di;
        printf("RA-TSR: installed, ticks %u, receive %u/%u, protocol %d/%d\n",
            ticks, receive_ready, receive_length,
            protocol_result, send_result);
        memset(&input, 0, sizeof(input));
        input.x.ax = TSR_MULTIPLEX;
        input.x.bx = 6;
        int86(0x2F, &input, &output);
        printf("  timer: int08 %u, int1c %u, int28 %u, workers %u, fallback %u\n",
            output.x.bx, output.x.cx, output.x.dx,
            output.x.si, output.x.di);
        memset(&input, 0, sizeof(input));
        input.x.ax = TSR_MULTIPLEX;
        input.x.bx = 7;
        int86(0x2F, &input, &output);
        printf("  packet: busy %u, allocate %u, complete %u, drop %u, send %u\n",
            output.x.bx, output.x.cx, output.x.dx,
            output.x.si, output.x.di);
        memset(&input, 0, sizeof(input));
        input.x.ax = TSR_MULTIPLEX;
        input.x.bx = 8;
        int86(0x2F, &input, &output);
        printf("  activity: send failures %u, last rx/worker %u/%u, PIC %02X/%02X\n",
            output.x.bx, output.x.cx, output.x.dx,
            output.x.si & 0xFF, output.x.si >> 8);
        return 2;
    }
    if (parse_credential(argc >= 2 ? argv[1] : 0, base_key, &open_mode) != 0) {
        puts("RA-TSR: invalid or empty credential");
        return 3;
    }
    mtcp_hostname[0] = 0;
    if (configure_network(
        argc, argv, ip, &port, &packet_interrupt,
        mtcp_hostname, argc < 8 || strcmp(argv[7], "-") == 0, "RA-TSR"
    ) != 0)
        return 5;
    if (configure_tsr(argc, argv, mtcp_hostname) != 0) {
        puts("RA-TSR: invalid sandbox root, access mode, or name");
        return 5;
    }
    memset(&input, 0, sizeof(input));
    input.h.ah = 0x30;
    intdos(&input, &output);
    cached_dos_major = output.h.al;
    cached_dos_minor = output.h.ah;
    cached_adapter = detect_adapter(
        *(volatile dm_u8 __far *)MK_FP(0x40, 0x49)
    );
    memset(&session, 0, sizeof(session));
    discovery_open_mode = open_mode;
    discovery_last_tick = (dm_u16)(0 - DISCOVERY_INTERVAL_TICKS);
    advertised_port = port;
    result = dm_net_open(packet_interrupt, ip, port);
    if (result != 0) {
        printf("RA-TSR: packet driver initialization failed (%d)\n", result);
        return 6;
    }
    agent_start_ticks = read_bios_ticks();
    ratsr_psp = _psp;
    memset(&registers, 0, sizeof(registers));
    registers.h.ah = 0x34;
    intr(0x21, &registers);
    indos_flag =
        (volatile dm_u8 __far *)MK_FP(registers.w.es, registers.w.bx);
    critical_error_flag = indos_flag - 1;
    ratsr_old_int08 = _dos_getvect(0x08);
    ratsr_old_int1c = _dos_getvect(0x1C);
    ratsr_old_int28 = _dos_getvect(0x28);
    ratsr_old_int2f = _dos_getvect(0x2F);
    ratsr_set_old_vectors(
        _FP_OFF(ratsr_old_int1c),
        _FP_SEG(ratsr_old_int1c),
        _FP_OFF(ratsr_old_int2f),
        _FP_SEG(ratsr_old_int2f)
    );
    ratsr_set_old_int08(
        _FP_OFF(ratsr_old_int08),
        _FP_SEG(ratsr_old_int08)
    );
    ratsr_set_old_int28(
        _FP_OFF(ratsr_old_int28),
        _FP_SEG(ratsr_old_int28)
    );
    ratsr_enabled = 1;
    _dos_setvect(0x1C, ratsr_idle_handler);
    _dos_setvect(0x08, ratsr_timer_handler);
    _dos_setvect(0x28, ratsr_dos_idle_handler);
    _dos_setvect(0x2F, ratsr_multiplex_handler);
    printf("RA-TSR 0.2 \"%s\" installed at %u.%u.%u.%u:%u, packet int 0x%02X, video %u, root %s, access %s%s\n",
        agent_name, ip[0], ip[1], ip[2], ip[3], port,
        packet_interrupt, cached_adapter, sandbox_root,
        allow_file_read ? "R" : "",
        allow_file_write ? "W" : "");
    if (!allow_file_read && !allow_file_write)
        puts("Filesystem access disabled.");
    if (unrestricted_filesystem
        && (allow_file_read || allow_file_write))
        puts("WARNING: UNRESTRICTED FILE ACCESS - ALL DOS DRIVES EXPOSED.");
    if (open_mode)
        puts("WARNING: OPEN MODE - packets are not authenticated.");
    puts("Unload with RA-TSR /U.");
    fflush(stdout);
    keep_resident();
    return 0;
}
#endif

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

static int parse_u16(const char *text, dm_u16 *output)
{
    unsigned long value;
    char *end;

    if (!text || !*text || *text == '-')
        return -1;
    value = strtoul(text, &end, 0);
    if (*end || !value || value > 65535UL)
        return -1;
    *output = (dm_u16)value;
    return 0;
}

static int parse_u8(const char *text, dm_u8 *output)
{
    dm_u16 value;

    if (parse_u16(text, &value) != 0 || value > 255)
        return -1;
    *output = (dm_u8)value;
    return 0;
}

static int configure_network(
    int argc,
    char **argv,
    dm_u8 ip[4],
    dm_u16 *port,
    dm_u8 *packet_interrupt,
    char *mtcp_hostname,
    dm_u8 use_mtcp_hostname,
    const char *program
)
{
    const char *config_path = getenv("MTCPCFG");
    int explicit_ip = argc >= 3 && strcmp(argv[2], "-") != 0;
    int explicit_interrupt = argc >= 5 && strcmp(argv[4], "-") != 0;

    if (config_path && *config_path
        && (!explicit_ip || !explicit_interrupt || use_mtcp_hostname)) {
        dm_u8 configured_ip[4];
        dm_u8 configured_interrupt = 0;
        char configured_hostname[DM_MTCP_HOSTNAME_MAX + 1];
        dm_u8 found = 0;
        int result;

        configured_hostname[0] = 0;
        result = dm_mtcp_config_load(
            config_path, configured_ip, &configured_interrupt,
            configured_hostname, sizeof(configured_hostname), &found
        );

        if (result != DM_MTCP_OK) {
            printf("%s: MTCPCFG %s: %s\n",
                program, config_path, dm_mtcp_config_error(result));
            return -1;
        }
        if (!explicit_ip) {
            if (!(found & DM_MTCP_HAVE_IP)) {
                printf("%s: MTCPCFG has no IPADDR\n", program);
                return -1;
            }
            memcpy(ip, configured_ip, 4);
        }
        if (!explicit_interrupt) {
            if (!(found & DM_MTCP_HAVE_PACKET_INT)) {
                printf("%s: MTCPCFG has no PACKETINT\n", program);
                return -1;
            }
            *packet_interrupt = configured_interrupt;
        }
        if (use_mtcp_hostname && (found & DM_MTCP_HAVE_HOSTNAME))
            strcpy(mtcp_hostname, configured_hostname);
        printf("%s: using MTCPCFG %s\n", program, config_path);
    }
    if (explicit_ip && parse_ip(argv[2], ip) != 0) {
        printf("%s: invalid IPv4 address\n", program);
        return -1;
    }
    if (argc >= 4 && strcmp(argv[3], "-") != 0
        && parse_u16(argv[3], port) != 0) {
        printf("%s: invalid UDP port\n", program);
        return -1;
    }
    if (explicit_interrupt && parse_u8(argv[4], packet_interrupt) != 0) {
        printf("%s: invalid packet-driver interrupt\n", program);
        return -1;
    }
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
    int decode_result;
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
    decode_result = dm_packet_decode(
        datagram->payload, datagram->payload_length, key, &packet);
#ifdef RA_TSR
    ratsr_last_protocol_result = decode_result;
#endif
    if (decode_result != DM_OK)
        return;
#ifdef RA_TSR
    ratsr_last_opcode = packet.opcode;
#endif
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
#ifdef RA_TSR
    session.last_activity_tick = ratsr_ticks;
#endif
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
#ifdef RA_TSR
    session.last_activity_tick = ratsr_ticks;
#endif
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
#ifdef RA_TSR
        response_length = prepare_text_capture();
#else
        response_length = capture_screen(screen_buffer);
#endif
        break;
    case DM_OP_SEND_KEYS:
#ifdef RA_TSR
        if (length == 5 && dm_get_u16(payload) == 0 && payload[2] == 0) {
            dm_put_u16(screen_buffer, 0);
            screen_buffer[2] = 0;
            dm_put_u16(screen_buffer + 3, 0);
            response_length = 5;
            break;
        }
#endif
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
#ifdef RA_TSR
    case DM_OP_FILE_READ_BEGIN:
        response_length = begin_file_read(payload, length, screen_buffer);
        if (!response_length) {
            send_error(opcode, id, 6, "file read denied or invalid");
            return;
        }
        break;
    case DM_OP_FILE_READ_BLOCK:
        response_length = read_transfer_block(payload, length, screen_buffer);
        if (!response_length) {
            send_error(opcode, id, 6, "invalid read block");
            return;
        }
        break;
    case DM_OP_FILE_READ_END:
        response_length = end_read_transfer(payload, length, screen_buffer);
        if (!response_length) {
            send_error(opcode, id, 6, "incomplete read transfer");
            return;
        }
        break;
    case DM_OP_FILE_WRITE_BEGIN:
        response_length = begin_file_write(payload, length, screen_buffer);
        if (!response_length) {
            send_error(opcode, id, 7, "file write denied or invalid");
            return;
        }
        break;
    case DM_OP_FILE_WRITE_BLOCK:
        response_length = write_transfer_block(payload, length, screen_buffer);
        if (!response_length) {
            send_error(opcode, id, 6, "invalid write block");
            return;
        }
        break;
    case DM_OP_FILE_WRITE_COMMIT:
        response_length = commit_file_write(payload, length, screen_buffer);
        if (!response_length) {
            send_error(opcode, id, 11, "upload integrity or commit failed");
            return;
        }
        break;
    case DM_OP_FILE_ABORT:
        response_length = abort_transfer(payload, length);
        if (response_length == 0xFFFF) {
            send_error(opcode, id, 6, "invalid transfer");
            return;
        }
        break;
    case DM_OP_GRAPHICS_BEGIN:
        if (length || !(response_length = begin_graphics(screen_buffer))) {
            send_error(opcode, id, 5, "unsupported graphics mode");
            return;
        }
        break;
    case DM_OP_GRAPHICS_BLOCK:
        response_length = read_graphics_block(payload, length, screen_buffer);
        if (!response_length) {
            send_error(opcode, id, 6, "invalid graphics block");
            return;
        }
        break;
    case DM_OP_GRAPHICS_END:
        response_length = end_read_transfer(payload, length, screen_buffer);
        if (!response_length) {
            send_error(opcode, id, 6, "incomplete graphics transfer");
            return;
        }
        break;
    case DM_OP_GET_DIAGNOSTICS:
        if (length) {
            send_error(opcode, id, 6, "diagnostic payload must be empty");
            return;
        }
        response_length = build_diagnostics(screen_buffer);
        break;
#endif
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
#ifndef RA_TSR
    dm_u8 index;
#endif

    fragment_count = (dm_u8)((length + RESPONSE_FRAGMENT_PAYLOAD - 1)
        / RESPONSE_FRAGMENT_PAYLOAD);
    if (!fragment_count)
        fragment_count = 1;
    if (fragment_count > MAX_RESPONSE_FRAGMENTS)
        return;
    session.response_count = fragment_count;
#ifdef RA_TSR
    if (payload != screen_buffer)
        memcpy(screen_buffer, payload, length);
    session.response_kind = kind;
    session.response_opcode = opcode;
    session.response_request_id = id;
    session.response_payload_length = length;
#else
    for (index = 0; index < fragment_count; ++index) {
        dm_packet packet;
        dm_u16 offset = (dm_u16)index * RESPONSE_FRAGMENT_PAYLOAD;
        dm_u16 remaining = length - offset;

        packet.kind = kind;
        packet.opcode = opcode;
        packet.flags = 0;
        packet.session_id = session.id;
        packet.request_id = id;
        packet.fragment_index = index;
        packet.fragment_count = fragment_count;
        packet.payload_length = remaining > RESPONSE_FRAGMENT_PAYLOAD
            ? RESPONSE_FRAGMENT_PAYLOAD : remaining;
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
#endif
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
#ifdef RA_TSR
    session.response_next = 0;
    session.response_pending = session.response_count != 0;
#else
    dm_u8 index;

    for (index = 0; index < session.response_count; ++index) {
        dm_net_send(
            &session.peer,
            session.responses[index],
            session.response_lengths[index]
        );
    }
#endif
}

static dm_u16 build_status(dm_u8 *output)
{
#ifndef RA_TSR
    union REGS input;
    union REGS result;
#endif
    volatile dm_u16 __far *memory_kb =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x13);
    dm_u32 current_ticks;
    dm_u32 elapsed_ticks;

#ifndef RA_TSR
    memset(&input, 0, sizeof(input));
    input.h.ah = 0x30;
    intdos(&input, &result);
#endif
    output[0] = 0;
    output[1] =
#ifdef RA_TSR
        2;
    output[2] = cached_dos_major;
    output[3] = cached_dos_minor;
#else
        1;
    output[2] = result.h.al;
    output[3] = result.h.ah;
#endif
    output[4] = 0;
#ifdef RA_TSR
    output[5] = 2;
#else
    output[5] = PHASE_AGENT_SHELL_READY;
#endif
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

    dm_put_u32(
        output,
        CAP_STATUS | CAP_TEXT | CAP_KEYBOARD
#ifdef RA_TSR
        | CAP_GRAPHICS
        | (allow_file_read ? CAP_FILESYSTEM_READ : 0)
        | (allow_file_write ? CAP_FILESYSTEM_WRITE : 0)
#endif
    );
    output[4] = (dm_u8)(*columns > 80 ? 80 : *columns);
    output[5] = 25;
    output[6] =
#ifdef RA_TSR
        cached_adapter;
#else
        detect_adapter(
        *(volatile dm_u8 __far *)MK_FP(0x40, 0x49)
    );
#endif
    dm_put_u16(output + 7, DM_MAX_FRAGMENT_PAYLOAD);
    dm_put_u16(output + 9, 15);
    return 11;
}

#ifdef RA_TSR
static dm_u16 build_diagnostics(dm_u8 *output)
{
    dm_u8 flags = 0;

    if (owns_vector(0x08, ratsr_timer_handler))
        flags |= DIAG_OWNS_INT08;
    if (owns_vector(0x1C, ratsr_idle_handler))
        flags |= DIAG_OWNS_INT1C;
    if (owns_vector(0x28, ratsr_dos_idle_handler))
        flags |= DIAG_OWNS_INT28;
    if (owns_vector(0x2F, ratsr_multiplex_handler))
        flags |= DIAG_OWNS_INT2F;
    if (ratsr_enabled)
        flags |= DIAG_ENABLED;
    if (dm_receive_ready)
        flags |= DIAG_RECEIVE_READY;
    if (session.active)
        flags |= DIAG_SESSION_ACTIVE;
    if (session.response_pending)
        flags |= DIAG_RESPONSE_PENDING;
    output[0] = DIAGNOSTICS_VERSION;
    output[1] = flags;
    dm_put_u16(output + 2, ratsr_int08_entries);
    dm_put_u16(output + 4, ratsr_int1c_entries);
    dm_put_u16(output + 6, ratsr_int28_entries);
    dm_put_u16(output + 8, ratsr_worker_runs);
    dm_put_u16(output + 10, ratsr_fallback_runs);
    dm_put_u16(output + 12, ratsr_busy_skips);
    dm_put_u16(output + 14, dm_receive_allocations);
    dm_put_u16(output + 16, dm_receive_completions);
    dm_put_u16(output + 18, dm_receive_drops);
    dm_put_u16(output + 20, dm_send_attempts);
    dm_put_u16(output + 22, dm_send_failures);
    dm_put_u16(output + 24, dm_receive_last_bios_tick);
    dm_put_u16(output + 26, ratsr_last_worker_bios_tick);
    dm_put_u16(output + 28, ratsr_ticks);
    dm_put_u16(output + 30, dm_receive_length);
    output[32] = (dm_u8)inp(0x21);
    output[33] = (dm_u8)inp(0xA1);
    dm_put_u16(output + 34, (dm_u16)ratsr_last_protocol_result);
    dm_put_u16(output + 36, (dm_u16)ratsr_last_send_result);
    output[38] = ratsr_last_opcode;
    output[39] = 0;
    return 40;
}

static dm_u8 owns_vector(
    dm_u8 interrupt_number,
    void (__interrupt __far *handler)(void)
)
{
    volatile dm_u16 __far *vector =
        (volatile dm_u16 __far *)MK_FP(0, (dm_u16)interrupt_number * 4);

    return vector[0] == _FP_OFF(handler) && vector[1] == _FP_SEG(handler);
}
#endif

#ifndef RA_TSR
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
    output[8] =
#ifdef RA_TSR
        cached_adapter;
#else
        detect_adapter(*video_mode);
#endif
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

#else
static dm_u16 prepare_text_capture(void)
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
    static dm_u16 generation;
    dm_u8 cols = (dm_u8)(*columns > 80 ? 80 : *columns);
    dm_u8 page = *active_page <= 7 ? *active_page : 0;
    dm_u16 cursor;

    if (!cols)
        cols = 80;
    cursor = cursor_positions[page];
    text_capture_header[0] = cols;
    text_capture_header[1] = 25;
    text_capture_header[2] = *video_mode;
    text_capture_header[3] = page;
    text_capture_header[4] = (dm_u8)(cursor >> 8);
    text_capture_header[5] = (dm_u8)cursor;
    text_capture_header[6] = (dm_u8)(*cursor_shape >> 8);
    text_capture_header[7] = (dm_u8)*cursor_shape;
    text_capture_header[8] = cached_adapter;
    dm_put_u16(text_capture_header + 9, 437);
    dm_put_u16(text_capture_header + 11, ++generation);
    text_capture_segment = *video_mode == 7 ? 0xB000 : 0xB800;
    text_capture_offset = *page_offset;
    text_capture_cells = (dm_u16)cols * 25 * 2;
    return 13 + text_capture_cells;
}

static void fill_text_capture_fragment(
    dm_u16 offset,
    dm_u16 length,
    dm_u8 *output
)
{
    dm_u16 index;

    for (index = 0; index < length; ++index) {
        dm_u16 position = offset + index;

        if (position < sizeof(text_capture_header))
            output[index] = text_capture_header[position];
        else
            output[index] = *(const dm_u8 __far *)MK_FP(
                text_capture_segment,
                text_capture_offset + position - sizeof(text_capture_header)
            );
    }
}
#endif

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
#ifndef RA_TSR
        process_shell_keyboard();
#endif
#ifndef RA_TSR
        if (inter_key_delay)
            delay(inter_key_delay);
#endif
    }
    for (index = 0; index < key_count; ++index) {
        dm_u16 word = named_key_word(payload[5 + text_length + index]);
        if (!word || !bios_queue_word(word))
            break;
        ++accepted_keys;
#ifndef RA_TSR
        process_shell_keyboard();
#endif
#ifndef RA_TSR
        if (inter_key_delay)
            delay(inter_key_delay);
#endif
    }
    dm_put_u16(output, accepted_text);
    output[2] = accepted_keys;
    dm_put_u16(output + 3, ++generation);
    return 5;
}

static int bios_queue_word(dm_u16 word)
{
#ifdef RA_TSR
    return ratsr_bios_queue_word(word);
#else
    volatile dm_u16 __far *head =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x1A);
    volatile dm_u16 __far *tail =
        (volatile dm_u16 __far *)MK_FP(0x40, 0x1C);
    dm_u16 current;
    dm_u16 next;

    _disable();
    current = *tail;
    if (current < 0x1E || current >= 0x3E || (current & 1)
        || *head < 0x1E || *head >= 0x3E || (*head & 1)) {
        _enable();
        return 0;
    }
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
#endif
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

#ifndef RA_TSR
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
#endif

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

#ifdef RA_TSR
void ratsr_idle_worker(dm_u16 dos_idle)
{
    dm_udp_datagram datagram;

    if (!ratsr_enabled)
        return;
    ratsr_last_worker_bios_tick = (dm_u16)read_bios_ticks();
    if (!dos_idle)
        ++ratsr_ticks;
    if (session.active
        && (dm_u16)(ratsr_ticks - session.last_activity_tick)
            >= SESSION_IDLE_TICKS) {
        session.active = 0;
        session.response_pending = 0;
        session.has_last_request = 0;
    }
    if (!session.active
        && transfer_kind != TRANSFER_NONE
        && dos_idle
        && !*critical_error_flag)
        close_transfer(1);
    if (!session.active
        && (dm_u16)(ratsr_ticks - discovery_last_tick)
            >= DISCOVERY_INTERVAL_TICKS) {
        discovery_last_tick = ratsr_ticks;
        send_discovery();
    }
    if (session.response_pending) {
        dm_packet packet;
        dm_u16 offset =
            (dm_u16)session.response_next * RESPONSE_FRAGMENT_PAYLOAD;
        dm_u16 remaining = session.response_payload_length - offset;
        int encode_result;

        packet.kind = session.response_kind;
        packet.opcode = session.response_opcode;
        packet.flags = 0;
        packet.session_id = session.id;
        packet.request_id = session.response_request_id;
        packet.fragment_index = session.response_next;
        packet.fragment_count = session.response_count;
        packet.payload_length = remaining > RESPONSE_FRAGMENT_PAYLOAD
            ? RESPONSE_FRAGMENT_PAYLOAD : remaining;
        if (session.response_opcode == DM_OP_CAPTURE_TEXT_SCREEN) {
            fill_text_capture_fragment(
                offset,
                packet.payload_length,
                screen_buffer
            );
            packet.payload = screen_buffer;
        } else {
            packet.payload = screen_buffer + offset;
        }
        encode_result = dm_packet_encode(
            &packet,
            session.key,
            session.responses[0],
            sizeof(session.responses[0]),
            &session.response_lengths[0]
        );
        ratsr_last_protocol_result = encode_result;
        if (encode_result != DM_OK) {
            session.response_pending = 0;
            return;
        }
        ratsr_last_send_result = dm_net_send(
            &session.peer,
            session.responses[0],
            session.response_lengths[0]
        );
        ++session.response_next;
        if (session.response_next >= session.response_count)
            session.response_pending = 0;
        return;
    }
    if (dm_net_poll(&datagram)) {
        if (datagram.payload_length >= DM_HEADER_SIZE
            && datagram.payload[4] >= DM_OP_FILE_READ_BEGIN
            && datagram.payload[4] <= DM_OP_FILE_ABORT
            && !(datagram.payload[4] == DM_OP_FILE_ABORT
                && transfer_kind == TRANSFER_GRAPHICS)
            && (!dos_idle || *critical_error_flag))
            return;
        process_datagram(&datagram);
        dm_net_release();
    }
}

void ratsr_prepare_unload(void)
{
    ratsr_enabled = 0;
}

static int uninstall_tsr(void)
{
    union REGPACK registers;
    void (__interrupt __far *current08)(void);
    void (__interrupt __far *current1c)(void);
    void (__interrupt __far *current28)(void);
    void (__interrupt __far *current2f)(void);
    dm_u16 resident_psp;
    dm_u16 old08_offset;
    dm_u16 old08_segment;
    dm_u16 old1c_offset;
    dm_u16 old1c_segment;
    dm_u16 old28_offset;
    dm_u16 old28_segment;
    dm_u16 old2f_offset;
    dm_u16 old2f_segment;
    dm_u16 packet_ip_handle;
    dm_u16 packet_arp_handle;
    dm_u8 packet_driver_interrupt;

    memset(&registers, 0, sizeof(registers));
    registers.w.ax = TSR_MULTIPLEX;
    registers.w.bx = 0;
    intr(0x2F, &registers);
    if (registers.w.ax != TSR_REPLY) {
        puts("RA-TSR: not installed");
        return 1;
    }
    resident_psp = registers.w.bx;
    current08 = _dos_getvect(0x08);
    current1c = _dos_getvect(0x1C);
    current28 = _dos_getvect(0x28);
    current2f = _dos_getvect(0x2F);
    if (_FP_OFF(current1c) != registers.w.cx
        || _FP_SEG(current1c) != registers.w.dx
        || _FP_OFF(current2f) != registers.w.si
        || _FP_SEG(current2f) != registers.w.di) {
        puts("RA-TSR: cannot unload; another TSR is above it");
        return 2;
    }
    memset(&registers, 0, sizeof(registers));
    registers.w.ax = TSR_MULTIPLEX;
    registers.w.bx = 5;
    intr(0x2F, &registers);
    if (registers.w.ax != TSR_REPLY
        || _FP_OFF(current08) != registers.w.cx
        || _FP_SEG(current08) != registers.w.dx) {
        puts("RA-TSR: cannot unload; another TSR is above INT 08h");
        return 2;
    }
    old08_offset = registers.w.si;
    old08_segment = registers.w.di;
    memset(&registers, 0, sizeof(registers));
    registers.w.ax = TSR_MULTIPLEX;
    registers.w.bx = 3;
    intr(0x2F, &registers);
    if (registers.w.ax != TSR_REPLY
        || _FP_OFF(current28) != registers.w.cx
        || _FP_SEG(current28) != registers.w.dx) {
        puts("RA-TSR: cannot unload; another TSR is above it");
        return 2;
    }
    old28_offset = registers.w.si;
    old28_segment = registers.w.di;
    memset(&registers, 0, sizeof(registers));
    registers.w.ax = TSR_MULTIPLEX;
    registers.w.bx = 4;
    intr(0x2F, &registers);
    if (registers.w.ax != TSR_REPLY) {
        puts("RA-TSR: cannot read resident packet-driver state");
        return 3;
    }
    packet_ip_handle = registers.w.bx;
    packet_arp_handle = registers.w.cx;
    packet_driver_interrupt = registers.h.dl;
    memset(&registers, 0, sizeof(registers));
    registers.w.ax = TSR_MULTIPLEX;
    registers.w.bx = 1;
    intr(0x2F, &registers);
    if (registers.w.ax != TSR_REPLY) {
        if (registers.w.bx == 1)
            puts("RA-TSR: active transfer; abort it or wait for idle cleanup");
        else
            puts("RA-TSR: resident shutdown failed");
        return 3;
    }
    old1c_offset = registers.w.cx;
    old1c_segment = registers.w.dx;
    old2f_offset = registers.w.si;
    old2f_segment = registers.w.di;
    if (dm_packet_release_handles(
        packet_driver_interrupt, packet_ip_handle, packet_arp_handle
    ) != 0) {
        puts("RA-TSR: packet driver refused endpoint release; reboot required");
        return 3;
    }
    _dos_setvect(
        0x08,
        (void (__interrupt __far *)(void))MK_FP(old08_segment, old08_offset)
    );
    _dos_setvect(
        0x1C,
        (void (__interrupt __far *)(void))MK_FP(old1c_segment, old1c_offset)
    );
    _dos_setvect(
        0x28,
        (void (__interrupt __far *)(void))MK_FP(old28_segment, old28_offset)
    );
    _dos_setvect(
        0x2F,
        (void (__interrupt __far *)(void))MK_FP(old2f_segment, old2f_offset)
    );
    if (_dos_freemem(resident_psp) != 0) {
        puts("RA-TSR: vectors restored but resident memory could not be freed");
        return 4;
    }
    puts("RA-TSR unloaded.");
    return 0;
}

static void send_discovery(void)
{
    dm_u8 payload[20 + TSR_NAME_MAX + 2];
    const dm_u8 *address = dm_net_address();
    dm_u16 name_length = (dm_u16)strlen(agent_name);
    dm_u32 capabilities = CAP_STATUS | CAP_TEXT | CAP_KEYBOARD | CAP_GRAPHICS
        | (allow_file_read ? CAP_FILESYSTEM_READ : 0)
        | (allow_file_write ? CAP_FILESYSTEM_WRITE : 0);
    dm_u16 length = 20 + name_length;

    payload[0] = 'D';
    payload[1] = 'M';
    payload[2] = 'D';
    payload[3] = '2';
    payload[4] = 1;
    payload[5] = DM_VERSION;
    payload[6] = discovery_open_mode ? 1 : 0;
    payload[7] = (dm_u8)name_length;
    dm_put_u16(payload + 8, advertised_port);
    dm_put_u32(payload + 10, capabilities);
    memcpy(payload + 14, address, 6);
    memcpy(payload + 20, agent_name, name_length);
    dm_put_u16(payload + length, dm_crc16_ccitt(payload, length));
    dm_net_broadcast(DISCOVERY_PORT, payload, length + 2);
}

static int configure_tsr(int argc, char **argv, const char *mtcp_hostname)
{
    char current[TSR_PATH_MAX + 1];
    const char *root = argc >= 6 ? argv[5] : "C:\\RATSR";
    const char *access = argc >= 7 ? argv[6] : "-";
    const char *name = argc >= 8 && strcmp(argv[7], "-") != 0
        ? argv[7]
        : (*mtcp_hostname ? mtcp_hostname : "DOS-PC");
    size_t length = strlen(root);
    size_t name_length = strlen(name);
    size_t index;

    if (!length || length > TSR_PATH_MAX
        || !name_length || name_length > TSR_NAME_MAX
        || !getcwd(current, sizeof(current)))
        return -1;
    for (index = 0; index < name_length; ++index) {
        unsigned char value = (unsigned char)name[index];

        if (value < 33 || value > 126)
            return -1;
    }
    strcpy(agent_name, name);
    strcpy(sandbox_root, root);
    unrestricted_filesystem = stricmp(root, "ALL") == 0;
    while (length > 3
        && (sandbox_root[length - 1] == '\\'
            || sandbox_root[length - 1] == '/'))
        sandbox_root[--length] = 0;
    allow_file_read = strchr(access, 'R') != 0 || strchr(access, 'r') != 0;
    allow_file_write = strchr(access, 'W') != 0 || strchr(access, 'w') != 0;
    if (strcmp(access, "-") != 0
        && !allow_file_read && !allow_file_write)
        return -1;
    if (!unrestricted_filesystem
        && (allow_file_read || allow_file_write)
        && (chdir(sandbox_root) != 0 || chdir(current) != 0))
        return -1;
    return 0;
}

static void keep_resident(void)
{
    dm_u16 environment =
        *(dm_u16 __far *)MK_FP(_psp, 0x2C);
    /*
     * Keep 128 KiB, including the PSP, code/data, receive buffers, and the
     * private 32 KiB interrupt-worker stack.  This intentionally trades a
     * little conventional memory for bounded stack headroom on small DOS
     * systems until the linker-derived resident size is validated across all
     * supported OpenWatcom memory models.
     */
    dm_u16 paragraphs = 0x2000;

    (void)ratsr_resident_end;
    if (environment)
        _dos_freemem(environment);
    _dos_keep(0, paragraphs);
}

static dm_u32 crc32_update(dm_u32 crc, const dm_u8 *data, dm_u16 length)
{
    dm_u16 index;
    dm_u8 bit;

    crc ^= 0xFFFFFFFFUL;
    for (index = 0; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
    }
    return crc ^ 0xFFFFFFFFUL;
}

static int safe_relative_path(const char *path)
{
    const char *component = path;
    const char *cursor;
    dm_u8 component_length = 0;

    if (!*path || *path == '\\' || *path == '/' || strchr(path, ':'))
        return 0;
    for (cursor = path; ; ++cursor) {
        char value = *cursor;

        if (value == '\\' || value == '/' || value == 0) {
            if (component_length == 0
                || (component_length == 1 && component[0] == '.')
                || (component_length == 2
                    && component[0] == '.' && component[1] == '.'))
                return 0;
            component = cursor + 1;
            component_length = 0;
            if (!value)
                break;
        } else {
            if ((unsigned char)value < 32
                || strchr("\":*+,;<=>?[|]", value)
                || ++component_length > 12)
                return 0;
        }
    }
    return 1;
}

static int safe_absolute_path(const char *path)
{
    unsigned char drive = (unsigned char)path[0];

    if (!((drive >= 'A' && drive <= 'Z')
            || (drive >= 'a' && drive <= 'z'))
        || path[1] != ':'
        || (path[2] != '\\' && path[2] != '/')
        || !path[3])
        return 0;
    return safe_relative_path(path + 3);
}

static int build_sandbox_path(
    const dm_u8 *value,
    dm_u16 length,
    char *output
)
{
    char relative[81];
    size_t root_length;

    if (!length || length > 80)
        return -1;
    memcpy(relative, value, length);
    relative[length] = 0;
    if (unrestricted_filesystem) {
        if (!safe_absolute_path(relative))
            return -1;
        strcpy(output, relative);
        return 0;
    }
    root_length = strlen(sandbox_root);
    if (root_length + 1 + length > TSR_PATH_MAX)
        return -1;
    if (!safe_relative_path(relative))
        return -1;
    strcpy(output, sandbox_root);
    if (root_length
        && output[root_length - 1] != '\\'
        && output[root_length - 1] != '/')
        strcat(output, "\\");
    strcat(output, relative);
    return 0;
}

static void close_transfer(int remove_temporary)
{
    if (transfer_handle >= 0)
        _dos_close(transfer_handle);
    transfer_handle = -1;
    if (remove_temporary && transfer_kind == TRANSFER_WRITE)
        remove(transfer_temp);
    transfer_kind = TRANSFER_NONE;
}

static int create_transfer_temp(dm_u16 id)
{
    size_t prefix_length;
    dm_u8 attempt;

    if (unrestricted_filesystem) {
        char *backslash = strrchr(transfer_path, '\\');
        char *slash = strrchr(transfer_path, '/');
        char *separator = backslash;

        if (slash && (!separator || slash > separator))
            separator = slash;
        if (!separator)
            return -1;
        prefix_length = (size_t)(separator - transfer_path) + 1;
        memcpy(transfer_temp, transfer_path, prefix_length);
    } else {
        prefix_length = strlen(sandbox_root);
        memcpy(transfer_temp, sandbox_root, prefix_length);
        if (prefix_length
            && transfer_temp[prefix_length - 1] != '\\'
            && transfer_temp[prefix_length - 1] != '/')
            transfer_temp[prefix_length++] = '\\';
    }
    if (prefix_length + 12 > sizeof(transfer_temp))
        return -1;
    for (attempt = 0; attempt < 16; ++attempt) {
        sprintf(
            transfer_temp + prefix_length,
            "DM%04X%X.TMP",
            id,
            attempt
        );
        if (stricmp(transfer_temp, transfer_path) != 0
            && _dos_creatnew(transfer_temp, 0, &transfer_handle) == 0)
            return 0;
    }
    transfer_temp[0] = 0;
    return -1;
}

static dm_u16 next_transfer_id(void)
{
    dm_u16 value = (dm_u16)read_bios_ticks() ^ (dm_u16)(transfer_id + 1);

    if (!value)
        value = 1;
    transfer_id = value;
    return value;
}

static dm_u16 begin_file_read(const dm_u8 *payload, dm_u16 length, dm_u8 *output)
{
    long size;

    if (!allow_file_read || transfer_kind != TRANSFER_NONE
        || length < 2 || length != (dm_u16)payload[0] + 1
        || build_sandbox_path(payload + 1, payload[0], transfer_path) != 0
        || _dos_open(transfer_path, 0, &transfer_handle) != 0)
        return 0;
    size = lseek(transfer_handle, 0, SEEK_END);
    if (size < 0 || (dm_u32)size > TSR_FILE_MAX
        || lseek(transfer_handle, 0, SEEK_SET) < 0) {
        close_transfer(0);
        return 0;
    }
    transfer_kind = TRANSFER_READ;
    transfer_size = (dm_u32)size;
    transfer_offset = 0;
    transfer_crc = 0;
    dm_put_u16(output, next_transfer_id());
    dm_put_u32(output + 2, transfer_size);
    return 6;
}

static dm_u16 read_transfer_block(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
)
{
    dm_u16 requested;
    unsigned received;
    dm_u32 offset;

    if (transfer_kind != TRANSFER_READ || length != 8
        || dm_get_u16(payload) != transfer_id)
        return 0;
    offset = dm_get_u32(payload + 2);
    requested = dm_get_u16(payload + 6);
    if (offset != transfer_offset || !requested
        || requested > TRANSFER_BLOCK_MAX
        || offset >= transfer_size)
        return 0;
    if ((dm_u32)requested > transfer_size - offset)
        requested = (dm_u16)(transfer_size - offset);
    if (_dos_read(transfer_handle, output + 12, requested, &received) != 0
        || !received)
        return 0;
    transfer_crc = crc32_update(transfer_crc, output + 12, received);
    dm_put_u16(output, transfer_id);
    dm_put_u32(output + 2, transfer_offset);
    dm_put_u16(output + 6, (dm_u16)received);
    dm_put_u32(output + 8, transfer_crc);
    transfer_offset += received;
    return (dm_u16)received + 12;
}

static dm_u16 end_read_transfer(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
)
{
    if ((transfer_kind != TRANSFER_READ
            && transfer_kind != TRANSFER_GRAPHICS)
        || length != 2 || dm_get_u16(payload) != transfer_id
        || transfer_offset != transfer_size)
        return 0;
    dm_put_u32(output, transfer_size);
    dm_put_u32(output + 4, transfer_crc);
    close_transfer(0);
    return 8;
}

static dm_u16 begin_file_write(const dm_u8 *payload, dm_u16 length, dm_u8 *output)
{
    int existing;
    dm_u8 overwrite;
    dm_u8 path_length;
    dm_u16 id;

    if (!allow_file_write || transfer_kind != TRANSFER_NONE || length < 10)
        return 0;
    overwrite = payload[0];
    path_length = payload[9];
    if (overwrite > 1 || length != (dm_u16)path_length + 10
        || dm_get_u32(payload + 1) > TSR_FILE_MAX
        || build_sandbox_path(
            payload + 10, path_length, transfer_path) != 0)
        return 0;
    if (!overwrite && _dos_open(transfer_path, 0, &existing) == 0) {
        _dos_close(existing);
        return 0;
    }
    id = next_transfer_id();
    if (create_transfer_temp(id) != 0)
        return 0;
    transfer_kind = TRANSFER_WRITE;
    transfer_size = dm_get_u32(payload + 1);
    transfer_expected_crc = dm_get_u32(payload + 5);
    transfer_offset = 0;
    transfer_crc = 0;
    transfer_overwrite = overwrite;
    dm_put_u16(output, id);
    dm_put_u32(output + 2, transfer_size);
    return 6;
}

static dm_u16 write_transfer_block(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
)
{
    dm_u16 block_length;
    unsigned written;
    dm_u32 offset;

    if (transfer_kind != TRANSFER_WRITE || length < 8
        || dm_get_u16(payload) != transfer_id)
        return 0;
    offset = dm_get_u32(payload + 2);
    block_length = dm_get_u16(payload + 6);
    if (!block_length || block_length > TRANSFER_BLOCK_MAX
        || length != block_length + 8 || offset != transfer_offset
        || offset + block_length > transfer_size
        || _dos_write(
            transfer_handle, payload + 8, block_length, &written) != 0
        || written != block_length)
        return 0;
    transfer_crc = crc32_update(transfer_crc, payload + 8, block_length);
    transfer_offset += block_length;
    dm_put_u16(output, transfer_id);
    dm_put_u32(output + 2, offset);
    dm_put_u16(output + 6, 0);
    dm_put_u32(output + 8, transfer_crc);
    return 12;
}

static dm_u16 commit_file_write(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
)
{
    int exists;
    dm_u8 overwrite = transfer_overwrite;

    if (transfer_kind != TRANSFER_WRITE || length != 2
        || dm_get_u16(payload) != transfer_id
        || transfer_offset != transfer_size
        || transfer_crc != transfer_expected_crc)
        return 0;
    _dos_close(transfer_handle);
    transfer_handle = -1;
    if (_dos_open(transfer_path, 0, &exists) == 0) {
        _dos_close(exists);
        if (!overwrite || remove(transfer_path) != 0) {
            close_transfer(1);
            return 0;
        }
    }
    if (rename(transfer_temp, transfer_path) != 0) {
        close_transfer(1);
        return 0;
    }
    dm_put_u32(output, transfer_size);
    dm_put_u32(output + 4, transfer_crc);
    transfer_kind = TRANSFER_NONE;
    return 8;
}

static dm_u16 abort_transfer(const dm_u8 *payload, dm_u16 length)
{
    if (length != 2 || transfer_kind == TRANSFER_NONE
        || dm_get_u16(payload) != transfer_id)
        return 0xFFFF;
    close_transfer(1);
    return 0;
}

static dm_u16 begin_graphics(dm_u8 *output)
{
    if (transfer_kind != TRANSFER_NONE || !graphics_info())
        return 0;
    transfer_kind = TRANSFER_GRAPHICS;
    transfer_offset = 0;
    transfer_crc = 0;
    transfer_size = graphics_bytes_per_plane * graphics_planes;
    dm_put_u16(output, next_transfer_id());
    output[2] = cached_adapter;
    output[3] = graphics_mode;
    output[4] = graphics_layout;
    output[5] = graphics_planes;
    dm_put_u16(output + 6, graphics_width);
    dm_put_u16(output + 8, graphics_height);
    dm_put_u32(output + 10, transfer_size);
    dm_put_u32(output + 14, graphics_bytes_per_plane);
    return 18;
}

static int graphics_info(void)
{
    graphics_mode = *(volatile dm_u8 __far *)MK_FP(0x40, 0x49);
    graphics_planes = 1;
    switch (graphics_mode) {
    case 4:
    case 5:
        graphics_layout = LAYOUT_CGA_2BPP;
        graphics_width = 320;
        graphics_height = 200;
        graphics_bytes_per_plane = 16384;
        return 1;
    case 6:
        graphics_layout = LAYOUT_CGA_1BPP;
        graphics_width = 640;
        graphics_height = 200;
        graphics_bytes_per_plane = 16384;
        return 1;
    case 7:
        if (!(_inp(0x3B8) & 2))
            return 0;
        graphics_layout = LAYOUT_HERCULES;
        graphics_width = 720;
        graphics_height = 348;
        graphics_bytes_per_plane = 32768;
        return 1;
    case 0x0D:
        graphics_layout = LAYOUT_PLANAR_4BPP;
        graphics_planes = 4;
        graphics_width = 320;
        graphics_height = 200;
        graphics_bytes_per_plane = 8000;
        return 1;
    case 0x0E:
        graphics_layout = LAYOUT_PLANAR_4BPP;
        graphics_planes = 4;
        graphics_width = 640;
        graphics_height = 200;
        graphics_bytes_per_plane = 16000;
        return 1;
    case 0x0F:
        graphics_layout = LAYOUT_PLANAR_1BPP;
        graphics_width = 640;
        graphics_height = 350;
        graphics_bytes_per_plane = 28000;
        return 1;
    case 0x10:
        graphics_layout = LAYOUT_PLANAR_4BPP;
        graphics_planes = 4;
        graphics_width = 640;
        graphics_height = 350;
        graphics_bytes_per_plane = 28000;
        return 1;
    case 0x11:
        graphics_layout = LAYOUT_PLANAR_1BPP;
        graphics_width = 640;
        graphics_height = 480;
        graphics_bytes_per_plane = 38400;
        return 1;
    case 0x12:
        graphics_layout = LAYOUT_PLANAR_4BPP;
        graphics_planes = 4;
        graphics_width = 640;
        graphics_height = 480;
        graphics_bytes_per_plane = 38400;
        return 1;
    case 0x13:
        graphics_layout = LAYOUT_PACKED_8BPP;
        graphics_width = 320;
        graphics_height = 200;
        graphics_bytes_per_plane = 64000;
        return 1;
    }
    return 0;
}

static dm_u16 read_graphics_block(
    const dm_u8 *payload,
    dm_u16 length,
    dm_u8 *output
)
{
    dm_u16 requested;
    dm_u16 index;
    dm_u32 offset;
    dm_u8 old_index = 0;
    dm_u8 old_plane = 0;

    if (transfer_kind != TRANSFER_GRAPHICS || length != 8
        || dm_get_u16(payload) != transfer_id)
        return 0;
    offset = dm_get_u32(payload + 2);
    requested = dm_get_u16(payload + 6);
    if (offset != transfer_offset || !requested
        || requested > TRANSFER_BLOCK_MAX || offset >= transfer_size)
        return 0;
    if ((dm_u32)requested > transfer_size - offset)
        requested = (dm_u16)(transfer_size - offset);
    if (graphics_layout == LAYOUT_PLANAR_4BPP
        || graphics_layout == LAYOUT_PLANAR_1BPP) {
        old_index = (dm_u8)_inp(0x3CE);
        _outp(0x3CE, 4);
        old_plane = (dm_u8)_inp(0x3CF);
    }
    for (index = 0; index < requested; ++index) {
        dm_u32 position = offset + index;
        dm_u16 memory_offset;
        dm_u16 segment;

        if (graphics_layout == LAYOUT_PLANAR_4BPP
            || graphics_layout == LAYOUT_PLANAR_1BPP) {
            dm_u8 plane = (dm_u8)(position / graphics_bytes_per_plane);
            _outp(0x3CE, 4);
            _outp(0x3CF, plane);
            memory_offset = (dm_u16)(position % graphics_bytes_per_plane);
            segment = 0xA000;
        } else {
            memory_offset = (dm_u16)position;
            segment = graphics_layout == LAYOUT_HERCULES ? 0xB000 : 0xB800;
            if (graphics_layout == LAYOUT_PACKED_8BPP)
                segment = 0xA000;
        }
        output[12 + index] =
            *(const dm_u8 __far *)MK_FP(segment, memory_offset);
    }
    if (graphics_layout == LAYOUT_PLANAR_4BPP
        || graphics_layout == LAYOUT_PLANAR_1BPP) {
        _outp(0x3CE, 4);
        _outp(0x3CF, old_plane);
        _outp(0x3CE, old_index);
    }
    transfer_crc = crc32_update(transfer_crc, output + 12, requested);
    dm_put_u16(output, transfer_id);
    dm_put_u32(output + 2, transfer_offset);
    dm_put_u16(output + 6, requested);
    dm_put_u32(output + 8, transfer_crc);
    transfer_offset += requested;
    return requested + 12;
}
#endif
