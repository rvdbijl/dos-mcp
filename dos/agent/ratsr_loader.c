#include "dmconfig.h"
#include "dmpacket.h"
#include "dmproto.h"
#include "ratsr_install.h"

#include <direct.h>
#include <dos.h>
#include <i86.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AGENT_PORT 21300
#define INSTALL_ENV "RATSR_CONFIG="

static ratsr_install_config install_config;

static int parse_ip(const char *text, dm_u8 output[4]);
static int parse_u16(const char *text, dm_u16 *output);
static int parse_u8(const char *text, dm_u8 *output);
static int parse_raw_key(const char *text, dm_u8 output[16]);
static int parse_credential(
    const char *text,
    dm_u8 output[16],
    dm_u8 *open_mode
);
static int is_hex_key(const char *text);
static int configure_network(
    int argc,
    char **argv,
    dm_u8 ip[4],
    dm_u16 *port,
    dm_u8 *packet_interrupt,
    char *mtcp_hostname,
    dm_u8 use_mtcp_hostname
);
static int configure_install(
    int argc,
    char **argv,
    const char *mtcp_hostname,
    dm_u8 open_mode
);
static int resident_present(union REGS *result);
static int print_status(void);
static int uninstall_tsr(void);
static int core_path(const char *loader_path, char *output, size_t capacity);
static void encode_install_environment(char *output);

int main(int argc, char **argv)
{
    dm_u8 ip[4] = {10, 0, 2, 15};
    dm_u16 port = AGENT_PORT;
    dm_u8 packet_interrupt = 0x60;
    char mtcp_hostname[DM_MTCP_HOSTNAME_MAX + 1];
    char resident_path[128];
    char install_environment[
        sizeof(INSTALL_ENV) + sizeof(install_config) * 2
    ];
    const char *resident_argv[2];
    const char *resident_environment[2];
    dm_u8 open_mode;
    union REGS installed;
    int result;

    if (argc == 2 && stricmp(argv[1], "/U") == 0)
        return uninstall_tsr();
    if (resident_present(&installed))
        return print_status();
    if (argc > 8) {
        puts("Usage: RA-TSR [credential] [ip] [port] [packet-int] [root] [access] [name]");
        puts("  access: - (none), R, W, or RW; name: 1-31 visible ASCII");
        puts("  root: existing directory, or ALL for unrestricted DOS drives");
        puts("  unload with RA-TSR /U");
        return 1;
    }
    memset(&install_config, 0, sizeof(install_config));
    if (parse_credential(
        argc >= 2 ? argv[1] : 0,
        install_config.key,
        &open_mode
    ) != 0) {
        puts("RA-TSR: invalid or empty credential");
        return 3;
    }
    mtcp_hostname[0] = 0;
    if (configure_network(
        argc,
        argv,
        ip,
        &port,
        &packet_interrupt,
        mtcp_hostname,
        argc < 8 || strcmp(argv[7], "-") == 0
    ) != 0)
        return 5;
    memcpy(install_config.ip, ip, sizeof(ip));
    install_config.port = port;
    install_config.packet_interrupt = packet_interrupt;
    if (configure_install(argc, argv, mtcp_hostname, open_mode) != 0) {
        puts("RA-TSR: invalid sandbox root, access mode, or name");
        return 5;
    }
    if (core_path(argv[0], resident_path, sizeof(resident_path)) != 0) {
        puts("RA-TSR: cannot locate RA-RES.EXE beside the loader");
        return 6;
    }
    install_config.magic = RATSR_INSTALL_MAGIC;
    install_config.version = RATSR_INSTALL_VERSION;
    install_config.size = sizeof(install_config);
    install_config.crc16 = 0;
    install_config.crc16 = dm_crc16_ccitt(
        (const dm_u8 *)&install_config,
        sizeof(install_config)
    );
    encode_install_environment(install_environment);
    resident_argv[0] = "RA-RES";
    resident_argv[1] = 0;
    resident_environment[0] = install_environment;
    resident_environment[1] = 0;
    printf(
        "RA-TSR 0.3 loading \"%s\" at %u.%u.%u.%u:%u, packet int 0x%02X, root %s, access %s%s\n",
        install_config.name,
        ip[0], ip[1], ip[2], ip[3], port, packet_interrupt,
        install_config.root,
        install_config.flags & RATSR_INSTALL_READ ? "R" : "",
        install_config.flags & RATSR_INSTALL_WRITE ? "W" : ""
    );
    if (!(install_config.flags & (RATSR_INSTALL_READ | RATSR_INSTALL_WRITE)))
        puts("Filesystem access disabled.");
    if ((install_config.flags & RATSR_INSTALL_ALL_DRIVES)
        && (install_config.flags
            & (RATSR_INSTALL_READ | RATSR_INSTALL_WRITE)))
        puts("WARNING: UNRESTRICTED FILE ACCESS - ALL DOS DRIVES EXPOSED.");
    if (install_config.flags & RATSR_INSTALL_OPEN_MODE)
        puts("WARNING: OPEN MODE - packets are not authenticated.");
    puts("Unload with RA-TSR /U.");
    fflush(stdout);
    result = spawnve(
        P_OVERLAY,
        resident_path,
        resident_argv,
        resident_environment
    );
    printf("RA-TSR: cannot start resident core (%d)\n", result);
    return 6;
}

static void encode_install_environment(char *output)
{
    static const char digits[] = "0123456789ABCDEF";
    const dm_u8 *source = (const dm_u8 *)&install_config;
    dm_u16 index;

    strcpy(output, INSTALL_ENV);
    output += sizeof(INSTALL_ENV) - 1;
    for (index = 0; index < sizeof(install_config); ++index) {
        *output++ = digits[source[index] >> 4];
        *output++ = digits[source[index] & 0x0F];
    }
    *output = 0;
}

static int parse_ip(const char *text, dm_u8 output[4])
{
    unsigned values[4];
    char tail;

    if (sscanf(
        text,
        "%u.%u.%u.%u%c",
        &values[0], &values[1], &values[2], &values[3], &tail
    ) != 4 || values[0] > 255 || values[1] > 255
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
    char *end;
    unsigned long value;

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

static int configure_network(
    int argc,
    char **argv,
    dm_u8 ip[4],
    dm_u16 *port,
    dm_u8 *packet_interrupt,
    char *mtcp_hostname,
    dm_u8 use_mtcp_hostname
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
            config_path,
            configured_ip,
            &configured_interrupt,
            configured_hostname,
            sizeof(configured_hostname),
            &found
        );
        if (result != DM_MTCP_OK) {
            printf(
                "RA-TSR: MTCPCFG %s: %s\n",
                config_path,
                dm_mtcp_config_error(result)
            );
            return -1;
        }
        if (!explicit_ip) {
            if (!(found & DM_MTCP_HAVE_IP)) {
                puts("RA-TSR: MTCPCFG has no IPADDR");
                return -1;
            }
            memcpy(ip, configured_ip, 4);
        }
        if (!explicit_interrupt) {
            if (!(found & DM_MTCP_HAVE_PACKET_INT)) {
                puts("RA-TSR: MTCPCFG has no PACKETINT");
                return -1;
            }
            *packet_interrupt = configured_interrupt;
        }
        if (use_mtcp_hostname && (found & DM_MTCP_HAVE_HOSTNAME))
            strcpy(mtcp_hostname, configured_hostname);
        printf("RA-TSR: using MTCPCFG %s\n", config_path);
    }
    if (explicit_ip && parse_ip(argv[2], ip) != 0) {
        puts("RA-TSR: invalid IPv4 address");
        return -1;
    }
    if (argc >= 4 && strcmp(argv[3], "-") != 0
        && parse_u16(argv[3], port) != 0) {
        puts("RA-TSR: invalid UDP port");
        return -1;
    }
    if (explicit_interrupt && parse_u8(argv[4], packet_interrupt) != 0) {
        puts("RA-TSR: invalid packet-driver interrupt");
        return -1;
    }
    return 0;
}

static int configure_install(
    int argc,
    char **argv,
    const char *mtcp_hostname,
    dm_u8 open_mode
)
{
    char current[RATSR_PATH_MAX + 1];
    const char *root = argc >= 6 ? argv[5] : "C:\\RATSR";
    const char *access = argc >= 7 ? argv[6] : "-";
    const char *name = argc >= 8 && strcmp(argv[7], "-") != 0
        ? argv[7]
        : (*mtcp_hostname ? mtcp_hostname : "DOS-PC");
    size_t length = strlen(root);
    size_t name_length = strlen(name);
    size_t index;

    if (!length || length > RATSR_PATH_MAX
        || !name_length || name_length > RATSR_NAME_MAX
        || !getcwd(current, sizeof(current)))
        return -1;
    for (index = 0; index < name_length; ++index) {
        unsigned char value = (unsigned char)name[index];

        if (value < 33 || value > 126)
            return -1;
    }
    strcpy(install_config.name, name);
    strcpy(install_config.root, root);
    while (length > 3
        && (install_config.root[length - 1] == '\\'
            || install_config.root[length - 1] == '/'))
        install_config.root[--length] = 0;
    install_config.root_length = (dm_u8)length;
    install_config.name_length = (dm_u8)name_length;
    if (strchr(access, 'R') || strchr(access, 'r'))
        install_config.flags |= RATSR_INSTALL_READ;
    if (strchr(access, 'W') || strchr(access, 'w'))
        install_config.flags |= RATSR_INSTALL_WRITE;
    if (strcmp(access, "-") != 0
        && !(install_config.flags
            & (RATSR_INSTALL_READ | RATSR_INSTALL_WRITE)))
        return -1;
    if (stricmp(root, "ALL") == 0)
        install_config.flags |= RATSR_INSTALL_ALL_DRIVES;
    if (open_mode)
        install_config.flags |= RATSR_INSTALL_OPEN_MODE;
    if (!(install_config.flags & RATSR_INSTALL_ALL_DRIVES)
        && (install_config.flags
            & (RATSR_INSTALL_READ | RATSR_INSTALL_WRITE))
        && (chdir(install_config.root) != 0 || chdir(current) != 0))
        return -1;
    return 0;
}

static int resident_present(union REGS *result)
{
    union REGS input;

    memset(&input, 0, sizeof(input));
    input.x.ax = RATSR_MULTIPLEX;
    input.x.bx = 0;
    int86(0x2F, &input, result);
    return result->x.ax == RATSR_REPLY;
}

static int print_status(void)
{
    union REGS input;
    union REGS output;

    delay(1000);
    memset(&input, 0, sizeof(input));
    input.x.ax = RATSR_MULTIPLEX;
    input.x.bx = 2;
    int86(0x2F, &input, &output);
    printf(
        "RA-TSR: installed, ticks %u, receive %u/%u, protocol %d/%d\n",
        output.x.bx, output.x.cx, output.x.dx,
        (int)output.x.si, (int)output.x.di
    );
    input.x.bx = 6;
    int86(0x2F, &input, &output);
    printf(
        "  timer: int08 %u, int1c %u, int28 %u, workers %u, fallback %u\n",
        output.x.bx, output.x.cx, output.x.dx, output.x.si, output.x.di
    );
    input.x.bx = 7;
    int86(0x2F, &input, &output);
    printf(
        "  packet: busy %u, allocate %u, complete %u, drop %u, send %u\n",
        output.x.bx, output.x.cx, output.x.dx, output.x.si, output.x.di
    );
    input.x.bx = 8;
    int86(0x2F, &input, &output);
    printf(
        "  activity: send failures %u, last rx/worker %u/%u, PIC %02X/%02X\n",
        output.x.bx, output.x.cx, output.x.dx,
        output.x.si & 0xFF, output.x.si >> 8
    );
    input.x.bx = 9;
    int86(0x2F, &input, &output);
    printf(
        "  memory: resident %u KiB, private stack %u/%u bytes, guard %u, workspace %u\n",
        (output.x.dx + 63U) / 64U,
        output.x.cx,
        output.x.bx,
        output.x.si,
        output.x.di
    );
    return 2;
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
    registers.w.ax = RATSR_MULTIPLEX;
    registers.w.bx = 0;
    intr(0x2F, &registers);
    if (registers.w.ax != RATSR_REPLY) {
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
    registers.w.ax = RATSR_MULTIPLEX;
    registers.w.bx = 5;
    intr(0x2F, &registers);
    if (registers.w.ax != RATSR_REPLY
        || _FP_OFF(current08) != registers.w.cx
        || _FP_SEG(current08) != registers.w.dx) {
        puts("RA-TSR: cannot unload; another TSR is above INT 08h");
        return 2;
    }
    old08_offset = registers.w.si;
    old08_segment = registers.w.di;
    registers.w.ax = RATSR_MULTIPLEX;
    registers.w.bx = 3;
    intr(0x2F, &registers);
    if (registers.w.ax != RATSR_REPLY
        || _FP_OFF(current28) != registers.w.cx
        || _FP_SEG(current28) != registers.w.dx) {
        puts("RA-TSR: cannot unload; another TSR is above it");
        return 2;
    }
    old28_offset = registers.w.si;
    old28_segment = registers.w.di;
    registers.w.ax = RATSR_MULTIPLEX;
    registers.w.bx = 4;
    intr(0x2F, &registers);
    if (registers.w.ax != RATSR_REPLY) {
        puts("RA-TSR: cannot read resident packet-driver state");
        return 3;
    }
    packet_ip_handle = registers.w.bx;
    packet_arp_handle = registers.w.cx;
    packet_driver_interrupt = registers.h.dl;
    registers.w.ax = RATSR_MULTIPLEX;
    registers.w.bx = 1;
    intr(0x2F, &registers);
    if (registers.w.ax != RATSR_REPLY) {
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
        packet_driver_interrupt,
        packet_ip_handle,
        packet_arp_handle
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

static int core_path(const char *loader_path, char *output, size_t capacity)
{
    const char *backslash = strrchr(loader_path, '\\');
    const char *slash = strrchr(loader_path, '/');
    const char *colon = strrchr(loader_path, ':');
    const char *separator = backslash;
    size_t prefix;

    if (!separator || (slash && slash > separator))
        separator = slash;
    if (!separator || (colon && colon > separator))
        separator = colon;
    prefix = separator ? (size_t)(separator - loader_path + 1) : 0;
    if (prefix + sizeof("RA-RES.EXE") > capacity)
        return -1;
    if (prefix)
        memcpy(output, loader_path, prefix);
    strcpy(output + prefix, "RA-RES.EXE");
    return 0;
}
