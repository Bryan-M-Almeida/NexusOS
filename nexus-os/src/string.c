#include "nexus.h"

u32 strlen(const char *s)
{
    u32 n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

void *memcpy(void *d, const void *s, u32 n)
{
    u8 *dd = d;
    const u8 *ss = s;
    while (n--)
        *dd++ = *ss++;
    return d;
}

void *memset(void *d, int c, u32 n)
{
    u8 *p = d;
    while (n--)
        *p++ = (u8)c;
    return d;
}
