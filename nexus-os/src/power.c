/* Power off and reboot.
 * Power off: ACPI (RSDP -> RSDT -> FADT -> DSDT "_S5_", needs unreal mode for tables
 * above 1 MB), then VM ports (QEMU/Bochs/VirtualBox), then APM.
 */
#include "nexus.h"

static int match(u32 a, const char *s, int n)
{
    for (int i = 0; i < n; i++)
        if (rd8(a + (u32)i) != (u8)s[i])
            return 0;
    return 1;
}

static int rsdp_at(u32 a)
{
    u8 sum = 0;
    if (!match(a, "RSD PTR ", 8))
        return 0;
    for (int i = 0; i < 20; i++)
        sum = (u8)(sum + rd8(a + (u32)i));
    return sum == 0;
}

static u32 find_rsdp(void)
{
    u32 ebda = (u32)rd16(0x40E) << 4;
    if (ebda)
        for (u32 a = ebda; a < ebda + 1024; a += 16)
            if (rsdp_at(a))
                return a;
    for (u32 a = 0xE0000; a < 0x100000; a += 16)
        if (rsdp_at(a))
            return a;
    return 0;
}

static void acpi_off(void)
{
    u32 rsdp = find_rsdp();
    if (!rsdp)
        return;
    u32 rsdt = rd32(rsdp + 16);
    if (!rsdt || !match(rsdt, "RSDT", 4))
        return;
    u32 n = (rd32(rsdt + 4) - 36) / 4, fadt = 0;
    for (u32 i = 0; i < n; i++)
    {
        u32 t = rd32(rsdt + 36 + i * 4);
        if (t && match(t, "FACP", 4))
        {
            fadt = t;
            break;
        }
    }
    if (!fadt)
        return;

    u32 dsdt = rd32(fadt + 40), smi = rd32(fadt + 48);
    u8 enable = rd8(fadt + 52);
    u32 pm1a = rd32(fadt + 64), pm1b = rd32(fadt + 68);
    if (!pm1a || !dsdt || !match(dsdt, "DSDT", 4))
        return;

    u32 dlen = rd32(dsdt + 4);
    u16 typa = 0, typb = 0;
    int found = 0;
    for (u32 a = dsdt + 36; a + 8 < dsdt + dlen; a++)
    {
        if (!match(a, "_S5_", 4))
            continue;
        if (!(rd8(a - 1) == 0x08 || (rd8(a - 2) == 0x08 && rd8(a - 1) == '\\')))
            continue;
        if (rd8(a + 4) != 0x12)
            continue;
        u32 p = a + 5;
        p += ((rd8(p) & 0xC0) >> 6) + 2;
        if (rd8(p) == 0x0A)
            p++;
        typa = (u16)(rd8(p) << 10);
        p++;
        if (rd8(p) == 0x0A)
            p++;
        typb = (u16)(rd8(p) << 10);
        found = 1;
        break;
    }
    if (!found)
        return;

    if (!(inw((u16)pm1a) & 1) && smi && enable)
    { /* enable ACPI if needed */
        outb((u16)smi, enable);
        for (u32 i = 0; i < 300000 && !(inw((u16)pm1a) & 1); i++)
            asm volatile("nop");
    }
    asm volatile("cli");
    outw((u16)pm1a, (u16)(typa | (1 << 13)));
    if (pm1b)
        outw((u16)pm1b, (u16)(typb | (1 << 13)));
    for (u32 i = 0; i < 2000000; i++)
        asm volatile("nop");
}

static void apm_off(void)
{
    u16 ax, bx, cx;
    u8 cf;
    ax = 0x5301;
    bx = 0;
    asm volatile("int $0x15" : "+a"(ax), "+b"(bx), "=@ccc"(cf)::"cx", "dx", "memory");
    ax = 0x530E;
    bx = 0;
    cx = 0x0102;
    asm volatile("int $0x15" : "+a"(ax), "+b"(bx), "+c"(cx), "=@ccc"(cf)::"dx", "memory");
    ax = 0x5307;
    bx = 1;
    cx = 3;
    asm volatile("int $0x15" : "+a"(ax), "+b"(bx), "+c"(cx), "=@ccc"(cf)::"dx", "memory");
}

void power_off(void)
{
    enter_unreal();
    acpi_off();
    outw(0x604, 0x2000);  /* QEMU */
    outw(0xB004, 0x2000); /* Bochs / older QEMU */
    outw(0x4004, 0x3400); /* VirtualBox */
    apm_off();
    cputs(C_YELLOW, "\nNao foi possivel desligar por software.\nPode desligar o computador manualmente.\n");
    for (;;)
        asm volatile("cli; hlt");
}

void power_reboot(void)
{
    asm volatile("cli");
    for (u32 i = 0; i < 0x10000; i++)
        if (!(inb(0x64) & 2))
            break;
    outb(0x64, 0xFE); /* keyboard controller reset pulse */
    for (u32 i = 0; i < 1000000; i++)
        asm volatile("nop");
    asm volatile("ljmp $0xFFFF,$0x0000"); /* fallback: BIOS reset vector */
    for (;;)
        asm volatile("hlt");
}
