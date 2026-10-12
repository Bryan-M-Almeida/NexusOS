/* Video: modo texto VGA 80x25, escrita direta em 0xB800 (via GS) */
#include "nexus.h"

#define W 80
#define H 25

static int cx, cy;
static u8 color = C_NORMAL;

static inline void gs_set(void)
{
    u16 s = 0xB800;
    asm volatile("movw %0,%%gs" ::"r"(s));
}
static inline void vww(u32 i, u16 v) { asm volatile("movw %0,%%gs:(%1)" ::"r"(v), "r"(i * 2)); }
static inline u16 vr(u32 i)
{
    u16 v;
    asm volatile("movw %%gs:(%1),%0" : "=r"(v) : "r"(i * 2));
    return v;
}
static inline void vw(u32 i, u8 ch, u8 at) { vww(i, (u16)(((u16)at << 8) | ch)); }

static void hwcur(void)
{
    u16 p = (u16)(cy * W + cx);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(p & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)(p >> 8));
}

static void scroll(void)
{
    for (u32 i = 0; i < (u32)W * (H - 1); i++)
        vww(i, vr(i + W));
    for (u32 i = 0; i < W; i++)
        vw((u32)W * (H - 1) + i, ' ', color);
}

void vid_init(void)
{
    asm volatile("int $0x10" ::"a"(0x0003) : "memory", "cc", "bx", "cx", "dx", "si", "di");
    gs_set();
}

void vid_clear(void)
{
    gs_set();
    for (u32 i = 0; i < (u32)W * H; i++)
        vw(i, ' ', color);
    cx = cy = 0;
    hwcur();
}

void setcolor(u8 c) { color = c; }

void putc(char ch)
{
    u8 c = (u8)ch;
    gs_set();
    if (c == '\n')
    {
        cx = 0;
        cy++;
    }
    else if (c == '\r')
    {
        cx = 0;
    }
    else if (c == '\b')
    {
        if (cx > 0)
            cx--;
    }
    else
    {
        vw((u32)(cy * W + cx), c, color);
        if (++cx >= W)
        {
            cx = 0;
            cy++;
        }
    }
    if (cy >= H)
    {
        scroll();
        cy = H - 1;
    }
    hwcur();
}

void puts(const char *s)
{
    while (*s)
        putc(*s++);
}

void cputs(u8 c, const char *s)
{
    u8 old = color;
    color = c;
    puts(s);
    color = old;
}

void putu(u32 n)
{
    char b[11];
    int i = 0;
    if (n == 0)
    {
        putc('0');
        return;
    }
    while (n)
    {
        b[i++] = (char)('0' + n % 10);
        n /= 10;
    }
    while (i)
        putc(b[--i]);
}

void puthex(u32 n, int digits)
{
    for (int i = digits - 1; i >= 0; i--)
        putc("0123456789ABCDEF"[(n >> (i * 4)) & 15]);
}
