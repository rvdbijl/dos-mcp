#include "dmconfig.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DM_MTCP_LINE_SIZE 256

static int key_equal(const char *left, const char *right)
{
    while (*left && *right) {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right))
            return 0;
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

static char *skip_space(char *value)
{
    while (*value && isspace((unsigned char)*value))
        ++value;
    return value;
}

static int finish_token(char *value)
{
    return *value == 0 || isspace((unsigned char)*value) || *value == '#';
}

static int parse_ip_value(char *value, dm_u8 output[4])
{
    unsigned long parts[4];
    unsigned index;
    char *end;

    for (index = 0; index < 4; ++index) {
        if (!isdigit((unsigned char)*value))
            return -1;
        parts[index] = strtoul(value, &end, 10);
        if (end == value || parts[index] > 255)
            return -1;
        value = end;
        if (index < 3) {
            if (*value != '.')
                return -1;
            ++value;
        }
    }
    if (!finish_token(value))
        return -1;
    for (index = 0; index < 4; ++index)
        output[index] = (dm_u8)parts[index];
    return 0;
}

static int parse_interrupt_value(char *value, dm_u8 *output)
{
    unsigned long number;
    char *end;

    if (!*value || *value == '-')
        return -1;
    number = strtoul(value, &end, 0);
    if (end == value || number == 0 || number > 255 || !finish_token(end))
        return -1;
    *output = (dm_u8)number;
    return 0;
}

static int parse_hostname_value(char *value, char *output)
{
    dm_u16 length = 0;

    while (*value && !isspace((unsigned char)*value) && *value != '#') {
        unsigned char character = (unsigned char)*value;

        if (character < 33 || character > 126
            || length >= DM_MTCP_HOSTNAME_MAX)
            return -1;
        output[length++] = *value++;
    }
    if (!length || !finish_token(value))
        return -1;
    output[length] = 0;
    return 0;
}

int dm_mtcp_config_load(
    const char *path,
    dm_u8 ip[4],
    dm_u8 *packet_interrupt,
    char *hostname,
    dm_u16 hostname_capacity,
    dm_u8 *found
)
{
    FILE *stream;
    char line[DM_MTCP_LINE_SIZE];
    dm_u8 parsed_ip[4];
    dm_u8 parsed_interrupt = 0;
    char parsed_hostname[DM_MTCP_HOSTNAME_MAX + 1];
    char assigned_hostname[DM_MTCP_HOSTNAME_MAX + 1];
    dm_u8 parsed_found = 0;
    int result = DM_MTCP_OK;

    parsed_hostname[0] = 0;
    assigned_hostname[0] = 0;
    if (!path || !*path || !ip || !packet_interrupt || !hostname
        || hostname_capacity < DM_MTCP_HOSTNAME_MAX + 1 || !found)
        return DM_MTCP_ERR_ARGUMENT;
    stream = fopen(path, "rt");
    if (!stream)
        return DM_MTCP_ERR_OPEN;
    while (fgets(line, sizeof(line), stream)) {
        char key[32];
        char *cursor = skip_space(line);
        char *key_end;
        size_t key_length;

        if (!strchr(line, '\n') && !feof(stream)) {
            int ch;

            do {
                ch = fgetc(stream);
            } while (ch != '\n' && ch != EOF);
            result = DM_MTCP_ERR_LINE_TOO_LONG;
            break;
        }
        if (!*cursor || *cursor == '#')
            continue;
        key_end = cursor;
        while (*key_end && !isspace((unsigned char)*key_end)
            && *key_end != '=')
            ++key_end;
        key_length = (size_t)(key_end - cursor);
        if (!key_length || key_length >= sizeof(key))
            continue;
        memcpy(key, cursor, key_length);
        key[key_length] = 0;
        cursor = skip_space(key_end);
        if (*cursor == '=') {
            ++cursor;
            cursor = skip_space(cursor);
        }
        if (key_equal(key, "IPADDR")) {
            if (parse_ip_value(cursor, parsed_ip) != 0) {
                result = DM_MTCP_ERR_IPADDR;
                break;
            }
            parsed_found |= DM_MTCP_HAVE_IP;
        } else if (key_equal(key, "PACKETINT")) {
            if (parse_interrupt_value(cursor, &parsed_interrupt) != 0) {
                result = DM_MTCP_ERR_PACKETINT;
                break;
            }
            parsed_found |= DM_MTCP_HAVE_PACKET_INT;
        } else if (key_equal(key, "HOSTNAME")) {
            if (parse_hostname_value(cursor, parsed_hostname) != 0) {
                result = DM_MTCP_ERR_HOSTNAME;
                break;
            }
            parsed_found |= DM_MTCP_HAVE_HOSTNAME;
        } else if (key_equal(key, "HOSTNAME_ASSIGNED")) {
            if (parse_hostname_value(cursor, assigned_hostname) != 0) {
                result = DM_MTCP_ERR_HOSTNAME;
                break;
            }
        }
    }
    if (ferror(stream) && result == DM_MTCP_OK)
        result = DM_MTCP_ERR_OPEN;
    fclose(stream);
    if (result != DM_MTCP_OK)
        return result;
    if (parsed_found & DM_MTCP_HAVE_IP)
        memcpy(ip, parsed_ip, sizeof(parsed_ip));
    if (parsed_found & DM_MTCP_HAVE_PACKET_INT)
        *packet_interrupt = parsed_interrupt;
    if (parsed_found & DM_MTCP_HAVE_HOSTNAME)
        strcpy(hostname, parsed_hostname);
    else if (*assigned_hostname) {
        strcpy(hostname, assigned_hostname);
        parsed_found |= DM_MTCP_HAVE_HOSTNAME;
    }
    *found = parsed_found;
    return DM_MTCP_OK;
}

const char *dm_mtcp_config_error(int result)
{
    switch (result) {
    case DM_MTCP_OK:
        return "ok";
    case DM_MTCP_ERR_ARGUMENT:
        return "invalid argument";
    case DM_MTCP_ERR_OPEN:
        return "cannot open or read file";
    case DM_MTCP_ERR_LINE_TOO_LONG:
        return "line exceeds 255 bytes";
    case DM_MTCP_ERR_IPADDR:
        return "invalid IPADDR";
    case DM_MTCP_ERR_PACKETINT:
        return "invalid PACKETINT";
    case DM_MTCP_ERR_HOSTNAME:
        return "invalid HOSTNAME";
    default:
        return "unknown configuration error";
    }
}
