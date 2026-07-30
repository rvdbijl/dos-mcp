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

int main(void)
{
    char command[128];
    unsigned length = 0;
    int graphics_mode = 0;

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
            memset(&input, 0, sizeof(input));
            int86(0x28, &input, &output);
            continue;
        }
        key = _bios_keybrd(_KEYBRD_READ);
        ascii = (unsigned char)key;
        if (ascii == 13) {
            command[length] = 0;
            putchar('\n');
            if (stricmp(command, "EXIT") == 0)
                break;
            if (stricmp(command, "GFX13") == 0) {
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
    return 0;
}
