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

static int finish_value(char *value)
{
    value = skip_space(value);
    return *value == 0 || *value == '#';
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
    if (!finish_value(value))
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
    if (end == value || number == 0 || number > 255 || !finish_value(end))
        return -1;
    *output = (dm_u8)number;
    return 0;
}

int dm_mtcp_config_load(
    const char *path,
    dm_u8 ip[4],
    dm_u8 *packet_interrupt,
    dm_u8 *found
)
{
    FILE *stream;
    char line[DM_MTCP_LINE_SIZE];
    dm_u8 parsed_ip[4];
    dm_u8 parsed_interrupt = 0;
    dm_u8 parsed_found = 0;
    int result = DM_MTCP_OK;

    if (!path || !*path || !ip || !packet_interrupt || !found)
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
    default:
        return "unknown configuration error";
    }
}
