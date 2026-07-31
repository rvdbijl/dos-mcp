#include "dmconfig.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL: %s\n", message);
        ++failures;
    }
}

static int write_file(const char *path, const char *contents)
{
    FILE *stream = fopen(path, "wb");

    if (!stream)
        return -1;
    if (fwrite(contents, 1, strlen(contents), stream) != strlen(contents)) {
        fclose(stream);
        return -1;
    }
    return fclose(stream);
}

static void test_valid(void)
{
    dm_u8 ip[4] = {1, 1, 1, 1};
    dm_u8 packet_int = 1;
    char hostname[DM_MTCP_HOSTNAME_MAX + 1] = "unchanged";
    dm_u8 found = 0;
    int result;

    check(write_file("CFG1.TMP",
        "# mTCP configuration\r\n"
        "DHCPVER DHCP Client version Oct 18 2024\r\n"
        "packetint 0x60\r\n"
        "HOSTNAME 286-BENCH\r\n"
        "IPADDR = 192.168.7.42 ; workstation annotation\r\n") == 0,
        "write valid fixture");
    result = dm_mtcp_config_load(
        "CFG1.TMP", ip, &packet_int,
        hostname, sizeof(hostname), &found
    );
    check(result == DM_MTCP_OK, "valid configuration accepted");
    check(found == (DM_MTCP_HAVE_IP | DM_MTCP_HAVE_PACKET_INT
        | DM_MTCP_HAVE_HOSTNAME), "all known keys found");
    check(ip[0] == 192 && ip[1] == 168 && ip[2] == 7 && ip[3] == 42,
        "IPADDR parsed");
    check(packet_int == 0x60, "PACKETINT parsed");
    check(strcmp(hostname, "286-BENCH") == 0, "HOSTNAME parsed");
    remove("CFG1.TMP");
}

static void test_last_value_wins(void)
{
    dm_u8 ip[4] = {0, 0, 0, 0};
    dm_u8 packet_int = 0;
    char hostname[DM_MTCP_HOSTNAME_MAX + 1] = "unchanged";
    dm_u8 found = 0;
    int result;

    check(write_file("CFG2.TMP",
        "IPADDR 10.1.1.1\nIPADDR 10.2.2.2\n"
        "PACKETINT 96\n") == 0,
        "write duplicate fixture");
    result = dm_mtcp_config_load(
        "CFG2.TMP", ip, &packet_int,
        hostname, sizeof(hostname), &found
    );
    check(result == DM_MTCP_OK, "duplicate known keys accepted");
    check(ip[0] == 10 && ip[1] == 2 && ip[2] == 2 && ip[3] == 2,
        "last IPADDR wins");
    check(packet_int == 96, "decimal PACKETINT accepted");
    remove("CFG2.TMP");
}

static void test_missing_key(void)
{
    dm_u8 ip[4] = {9, 9, 9, 9};
    dm_u8 packet_int = 0x61;
    char hostname[DM_MTCP_HOSTNAME_MAX + 1] = "unchanged";
    dm_u8 found = 0xFF;
    int result;

    check(write_file("CFG3.TMP", "IPADDR 172.16.0.8\n") == 0,
        "write partial fixture");
    result = dm_mtcp_config_load(
        "CFG3.TMP", ip, &packet_int,
        hostname, sizeof(hostname), &found
    );
    check(result == DM_MTCP_OK, "partial configuration parsed");
    check(found == DM_MTCP_HAVE_IP, "missing key reported in mask");
    check(packet_int == 0x61, "missing value does not alter output");
    remove("CFG3.TMP");
}

static void test_invalid_values_are_atomic(void)
{
    dm_u8 ip[4] = {9, 8, 7, 6};
    dm_u8 packet_int = 0x62;
    char hostname[DM_MTCP_HOSTNAME_MAX + 1] = "unchanged";
    dm_u8 found = 0xA5;
    int result;

    check(write_file("CFG4.TMP",
        "IPADDR 10.0.0.1\nPACKETINT 0x100\n") == 0,
        "write invalid interrupt fixture");
    result = dm_mtcp_config_load(
        "CFG4.TMP", ip, &packet_int,
        hostname, sizeof(hostname), &found
    );
    check(result == DM_MTCP_ERR_PACKETINT, "oversize PACKETINT rejected");
    check(ip[0] == 9 && ip[1] == 8 && ip[2] == 7 && ip[3] == 6,
        "IP output unchanged after later error");
    check(packet_int == 0x62 && found == 0xA5,
        "other outputs unchanged after error");
    check(strcmp(hostname, "unchanged") == 0,
        "hostname unchanged after error");
    remove("CFG4.TMP");

    check(write_file("CFG5.TMP", "IPADDR 10.0.0.256\n") == 0,
        "write invalid IP fixture");
    result = dm_mtcp_config_load(
        "CFG5.TMP", ip, &packet_int,
        hostname, sizeof(hostname), &found
    );
    check(result == DM_MTCP_ERR_IPADDR, "invalid IPADDR rejected");
    remove("CFG5.TMP");
}

static void test_long_line(void)
{
    FILE *stream = fopen("CFG6.TMP", "wb");
    dm_u8 ip[4] = {1, 2, 3, 4};
    dm_u8 packet_int = 0x60;
    char hostname[DM_MTCP_HOSTNAME_MAX + 1] = "unchanged";
    dm_u8 found = 0;
    int index;
    int result;

    check(stream != 0, "open long-line fixture");
    if (!stream)
        return;
    fputs("UNKNOWN ", stream);
    for (index = 0; index < 300; ++index)
        fputc('X', stream);
    fputc('\n', stream);
    fclose(stream);
    result = dm_mtcp_config_load(
        "CFG6.TMP", ip, &packet_int,
        hostname, sizeof(hostname), &found
    );
    check(result == DM_MTCP_ERR_LINE_TOO_LONG, "overlong line rejected");
    remove("CFG6.TMP");
}

static void test_assigned_hostname_fallback(void)
{
    dm_u8 ip[4] = {0, 0, 0, 0};
    dm_u8 packet_int = 0;
    char hostname[DM_MTCP_HOSTNAME_MAX + 1] = "unchanged";
    dm_u8 found = 0;
    int result;

    check(write_file("CFG7.TMP",
        "DHCPVER DHCP Client version May 6 2022\n"
        "TIMESTAMP ( 1652571761 ) Sat May 14 16:42:41 2022\n"
        "PACKETINT 0x62 generated by setup\n"
        "HOSTNAME_ASSIGNED DHCP-286\n"
        "IPADDR 192.168.2.80 assigned by DHCP\n") == 0,
        "write DHCP fixture");
    result = dm_mtcp_config_load(
        "CFG7.TMP", ip, &packet_int,
        hostname, sizeof(hostname), &found
    );
    check(result == DM_MTCP_OK, "DHCP-generated configuration accepted");
    check(strcmp(hostname, "DHCP-286") == 0,
        "HOSTNAME_ASSIGNED used as fallback");
    check(ip[0] == 192 && ip[1] == 168 && ip[2] == 2 && ip[3] == 80,
        "annotated DHCP IPADDR accepted");
    check(packet_int == 0x62, "annotated DHCP PACKETINT accepted");
    check(found == (DM_MTCP_HAVE_IP | DM_MTCP_HAVE_PACKET_INT
        | DM_MTCP_HAVE_HOSTNAME), "DHCP found mask complete");
    remove("CFG7.TMP");
}

int main(void)
{
    test_valid();
    test_last_value_wins();
    test_missing_key();
    test_invalid_values_are_atomic();
    test_long_line();
    test_assigned_hostname_fallback();
    if (failures) {
        printf("%d mTCP configuration vector(s) failed\n", failures);
        return 1;
    }
    puts("PASS mTCP configuration vectors");
    return 0;
}
