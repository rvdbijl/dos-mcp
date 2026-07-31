/*
 * Deterministic foreground host used by the DOSBox RA-TSR integration test.
 *
 * DOSBox-X's internal command shell suspends guest execution while waiting at
 * its prompt, so hardware-timer TSR hooks do not run there.  This small BIOS
 * keyboard loop keeps the emulated CPU active, echoes commands, and delegates
 * them to COMMAND.COM.  It is test infrastructure, not part of RA-TSR.
 */
#include <bios.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void (__interrupt __far *saved_int1c)(void);

static void __interrupt __far suppress_int1c(void)
{
    /* Deliberately do not chain: exercises RA-TSR's INT 08h watchdog. */
}

int main(void)
{
    char command[128];
    unsigned length = 0;
    int graphics_mode = 0;
    int issue_dos_idle = 1;
    int force_invalid_cursor = 0;

    puts("RA-TSR integration host");
    fputs("TSRHOST> ", stdout);
    fflush(stdout);
    for (;;) {
        unsigned key;
        unsigned char ascii;

        if (!_bios_keybrd(_KEYBRD_READY)) {
            union REGS input;
            union REGS output;

            /*
             * Cooperate like a DOS console wait: resident filesystem calls
             * are serviced only through the chained DOS-idle interrupt.
             */
            if (issue_dos_idle) {
                memset(&input, 0, sizeof(input));
                int86(0x28, &input, &output);
            }
            continue;
        }
        key = _bios_keybrd(_KEYBRD_READ);
        ascii = (unsigned char)key;
        if (ascii == 13) {
            command[length] = 0;
            putchar('\n');
            if (stricmp(command, "EXIT") == 0)
                break;
            if (stricmp(command, "CUT1C") == 0) {
                if (!saved_int1c) {
                    saved_int1c = _dos_getvect(0x1C);
                    _dos_setvect(0x1C, suppress_int1c);
                }
            } else if (stricmp(command, "REST1C") == 0) {
                if (saved_int1c) {
                    _dos_setvect(0x1C, saved_int1c);
                    saved_int1c = 0;
                }
            } else if (stricmp(command, "NO28") == 0) {
                issue_dos_idle = 0;
            } else if (stricmp(command, "YES28") == 0) {
                issue_dos_idle = 1;
            } else if (stricmp(command, "BADC") == 0) {
                force_invalid_cursor = 1;
            } else if (stricmp(command, "GOODC") == 0) {
                force_invalid_cursor = 0;
            } else if (stricmp(command, "GFX13") == 0) {
                union REGS input;
                union REGS output;
                unsigned char __far *video =
                    (unsigned char __far *)MK_FP(0xA000, 0);
                unsigned index;

                input.x.ax = 0x0013;
                int86(0x10, &input, &output);
                for (index = 0; index < 64000U; ++index)
                    video[index] = (unsigned char)(index ^ (index >> 8));
                graphics_mode = 1;
            } else if (stricmp(command, "TEXT") == 0) {
                union REGS input;
                union REGS output;

                input.x.ax = 0x0003;
                int86(0x10, &input, &output);
                graphics_mode = 0;
            } else if (length) {
                system(command);
            }
            length = 0;
            if (!graphics_mode) {
                fputs("TSRHOST> ", stdout);
                fflush(stdout);
                if (force_invalid_cursor) {
                    volatile unsigned char __far *active_page =
                        (volatile unsigned char __far *)MK_FP(0x40, 0x62);
                    volatile unsigned short __far *cursor_positions =
                        (volatile unsigned short __far *)MK_FP(0x40, 0x50);
                    unsigned page = *active_page <= 7 ? *active_page : 0;

                    cursor_positions[page] = 0x1900;
                }
            }
        } else if (ascii == 8) {
            if (length) {
                --length;
                fputs("\b \b", stdout);
            }
        } else if (ascii >= 32 && length + 1 < sizeof(command)) {
            command[length++] = (char)ascii;
            putchar(ascii);
            fflush(stdout);
        }
    }
    if (saved_int1c)
        _dos_setvect(0x1C, saved_int1c);
    return 0;
}
