/* VGA 80x25 text mode, direct writes to 0xB800 via GS */
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

/* NEXUS palette for the 16 text colors (RGB), indexed by VGA color */
static const u32 NEXUS_PALETTE[16] = {
    0x030508, /* 0  black         -> background */
    0x12355E, /* 1  blue          -> deep blue */
    0x2A72C2, /* 2  green         -> mid blue */
    0x3D8BDB, /* 3  cyan          -> blue */
    0xA3241F, /* 4  red           -> dark red */
    0x5A6B80, /* 5  magenta       -> slate */
    0x8D9BB0, /* 6  brown         -> dim silver */
    0xC9D2DE, /* 7  light gray    -> silver (normal text) */
    0x8794A6, /* 8  dark gray     -> muted text */
    0x3D8BDB, /* 9  light blue    -> blue */
    0x6FB3FF, /* 10 light green   -> bright blue (prompt, OK) */
    0xA8D1FF, /* 11 light cyan    -> light blue (folders, paths, logo) */
    0xE5322D, /* 12 light red     -> NEXUS red (errors) */
    0xFF6B66, /* 13 light magenta -> light red */
    0xFFFFFF, /* 14 yellow        -> white (titles, warnings) */
    0xE6EBF2, /* 15 white         -> bright text */
};
/* DAC entries used by the default attribute controller mapping of the 16 colors */
static const u8 DAC_INDEX[16] = {0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63};

static void set_palette(void)
{
    for (int i = 0; i < 16; i++)
    {
        u32 c = NEXUS_PALETTE[i];
        outb(0x3C8, DAC_INDEX[i]);
        outb(0x3C9, (u8)((c >> 18) & 63));
        outb(0x3C9, (u8)((c >> 10) & 63));
        outb(0x3C9, (u8)((c >> 2) & 63));
    }
}

void vid_init(void)
{
    asm volatile("int $0x10" ::"a"(0x0003) : "memory", "cc", "bx", "cx", "dx", "si", "di");
    gs_set();
    set_palette();
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
