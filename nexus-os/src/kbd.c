/* Teclado via BIOS INT 16h */
#include "nexus.h"

u16 getkey(void)
{
    u16 ax;
    asm volatile("int $0x16" : "=a"(ax) : "a"(0x0000) : "memory", "cc");
    return ax;
}

/* Le uma linha. Seta para cima recupera o ultimo comando (buffer 'last'). */
void readline(char *buf, int max, char *last)
{
    int n = 0;
    for (;;)
    {
        u16 k = getkey();
        u8 ch = (u8)(k & 0xFF), sc = (u8)(k >> 8);
        if (ch == '\r' || ch == '\n')
        {
            putc('\n');
            buf[n] = 0;
            return;
        }
        if (ch == 8)
        {
            if (n > 0)
            {
                n--;
                putc('\b');
                putc(' ');
                putc('\b');
            }
            continue;
        }
        if (ch == 0 && sc == 0x48 && last)
        { /* seta para cima */
            while (n > 0)
            {
                n--;
                putc('\b');
                putc(' ');
                putc('\b');
            }
            while (last[n] && n < max - 1)
            {
                buf[n] = last[n];
                putc(last[n]);
                n++;
            }
            continue;
        }
        if (ch >= 32 && ch < 127 && n < max - 1)
        {
            buf[n++] = (char)ch;
            putc((char)ch);
        }
    }
}
