/* Disco via BIOS INT 13h extendido (LBA). Funciona em HD virtual e pendrive. */
#include "nexus.h"

struct dap
{
    u8 size, zero;
    u16 count, off, seg;
    u32 lba, lba_hi;
} __attribute__((packed));
struct edd
{
    u16 size, flags;
    u32 cyl, heads, spt, sec_lo, sec_hi;
    u16 bps;
} __attribute__((packed));

static struct dap dap;
static struct edd edd;
static u8 drive;

/* Modo CD/ISO: BIOS entrega setores de 2048 bytes e o midia e somente leitura.
 * Leituras: convertemos LBA de 512 -> setor de 2048. Escritas: ficam so na RAM. */
static u8 cd_mode;
static u32 cd_base; /* setor do NEXUS.IMG dentro do ISO (gravado pelo bootloader) */
static u8 cdbuf[2048];
static u32 cd_cached = 0xFFFFFFFF;

static u16 cur_seg(void)
{
    u16 s;
    asm volatile("movw %%ds,%0" : "=r"(s));
    return s;
}

static int bios13(u16 ax_in, void *p)
{
    u16 ax = ax_in;
    u8 cf;
    u16 si = (u16)(u32)p;
    asm volatile("int $0x13" : "+a"(ax), "=@ccc"(cf) : "d"((u16)drive), "S"(si)
                 : "memory", "bx", "cx", "di");
    return cf ? 1 : 0;
}

static void bios_reset(void)
{
    u16 ax = 0;
    asm volatile("int $0x13" : "+a"(ax) : "d"((u16)drive) : "memory", "cc", "bx", "cx", "si", "di");
}

int disk_init(u8 drv)
{
    u16 bx = 0x55AA, ax = 0x4100;
    u8 cf;
    drive = drv;
    asm volatile("int $0x13" : "+a"(ax), "+b"(bx), "=@ccc"(cf) : "d"((u16)drv)
                 : "memory", "cx", "si", "di");
    if (cf || bx != 0xAA55)
        return -1; /* BIOS sem LBA estendido */
    memset(&edd, 0, sizeof edd);
    edd.size = 0x1E;
    if (bios13(0x4800, &edd))
        edd.sec_lo = 0;
    if (edd.bps == 2048 || drv >= 0xE0)
    { /* boot por CD/ISO */
        cd_mode = 1;
        cd_base = rd32(0x7C40); /* campo gravado em boot_cd.asm */
        edd.sec_lo = 2048;      /* tamanho da imagem em setores de 512 */
    }
    return 0;
}

u32 disk_sectors(void) { return edd.sec_lo; }
u8 disk_is_cd(void) { return cd_mode; }
u8 disk_drive(void) { return drive; }

static int disk_rw(int write, u32 lba, void *buf)
{
    if (cd_mode)
    {
        if (write)
            return 0; /* volatil: so memoria */
        u32 sec = cd_base + lba / 4;
        for (int t = 0; sec != cd_cached; t++)
        {
            if (t == 3)
                return -1;
            dap.size = 16;
            dap.zero = 0;
            dap.count = 1;
            dap.off = (u16)(u32)cdbuf;
            dap.seg = cur_seg();
            dap.lba = sec;
            dap.lba_hi = 0;
            if (!bios13(0x4200, &dap))
                cd_cached = sec;
            else
                bios_reset();
        }
        memcpy(buf, cdbuf + (lba % 4) * 512, 512);
        return 0;
    }
    for (int t = 0; t < 3; t++)
    {
        dap.size = 16;
        dap.zero = 0;
        dap.count = 1;
        dap.off = (u16)(u32)buf;
        dap.seg = cur_seg();
        dap.lba = lba;
        dap.lba_hi = 0;
        if (!bios13(write ? 0x4300 : 0x4200, &dap))
            return 0;
        bios_reset();
    }
    return -1;
}

int disk_read(u32 lba, void *buf) { return disk_rw(0, lba, buf); }
int disk_write(u32 lba, const void *buf) { return disk_rw(1, lba, (void *)buf); }
