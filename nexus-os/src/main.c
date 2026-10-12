/* NEXUS.OS - ponto de entrada do kernel */
#include "nexus.h"
#include "logo.h"

static u32 ticks(void)
{
    u16 cx, dx, ax = 0;
    asm volatile("int $0x1A" : "+a"(ax), "=c"(cx), "=d"(dx)::"memory", "cc", "bx", "si", "di");
    return ((u32)cx << 16) | dx;
}

static void delay(u32 t)
{
    u32 s = ticks();
    while (ticks() - s < t)
    {
    }
}

static void okline(const char *m)
{
    cputs(C_GRAY, "  [");
    cputs(C_GREEN, " OK ");
    cputs(C_GRAY, "] ");
    puts(m);
    putc('\n');
    delay(3);
}

static void failline(const char *m)
{
    cputs(C_GRAY, "  [");
    cputs(C_RED, "FALHA");
    cputs(C_GRAY, "] ");
    puts(m);
    putc('\n');
    delay(6);
}

void kmain(u32 drive)
{
    int fs_ok = 0;
    vid_init();
    vid_clear();
    putc('\n');
    putc('\n');
    for (int i = 0; i < 5; i++)
    {
        for (int k = 0; k < (80 - LOGO_W) / 2; k++)
            putc(' ');
        cputs(C_CYAN, LOGO[i]);
        putc('\n');
    }
    putc('\n');
    cputs(C_GRAY, "                 Sistema Operacional NEXUS.OS  -  v0.1\n\n");

    puts("  Ligando [");
    for (int i = 0; i < 40; i++)
    {
        cputs(C_CYAN, "\xDB");
        delay(1);
    }
    puts("]\n\n");

    okline("Video VGA texto 80x25");
    okline("Teclado (BIOS)");

    if (disk_init((u8)drive) == 0 && disk_sectors() >= FS_LBA + 16)
    {
        okline("Disco detectado via BIOS (LBA)");
        if (disk_is_cd())
            cputs(C_YELLOW, "  [ !! ] Modo ISO: somente leitura, alteracoes valem ate desligar\n");
        if (fs_mount() == 0)
        {
            okline("NXFS montado");
            fs_ok = 1;
        }
        else
        {
            cputs(C_YELLOW, "  [ !! ] Disco sem NXFS - formatando...\n");
            if (fs_format() == 0 && fs_mount() == 0)
            {
                okline("NXFS criado e montado");
                fs_ok = 1;
            }
            else
                failline("NXFS indisponivel");
        }
    }
    else
        failline("Disco/BIOS sem suporte a LBA ou disco muito pequeno");

    delay(10);
    vid_clear();
    cputs(C_CYAN, "NEXUS.OS");
    cputs(C_GRAY, " v0.1  -  pronto.\n");
    shell_run(fs_ok);
}
