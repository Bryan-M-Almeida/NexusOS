/* NEXUS.OS kernel entry point */
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

/* Runs one boot step per call (used by the intro and the text boot).
 * Returns BOOT_OK, BOOT_WARN, BOOT_FAIL or BOOT_END. */
static u32 boot_drive;
static int boot_stage, fs_ok;

static int boot_next(const char **msg)
{
    for (;;)
        switch (boot_stage++)
        {
        case 0:
            *msg = "Teclado (BIOS)";
            return BOOT_OK;
        case 1:
            if (disk_init((u8)boot_drive) == 0 && disk_sectors() >= FS_LBA + 16)
            {
                *msg = "Disco detectado via BIOS (LBA)";
                return BOOT_OK;
            }
            boot_stage = 99;
            *msg = "Disco/BIOS sem suporte a LBA ou disco muito pequeno";
            return BOOT_FAIL;
        case 2:
            if (disk_is_cd())
            {
                *msg = "Modo ISO: somente leitura, alteracoes valem ate desligar";
                return BOOT_WARN;
            }
            break;
        case 3:
            if (fs_mount() == 0)
            {
                fs_ok = 1;
                boot_stage = 99;
                *msg = "NXFS montado";
                return BOOT_OK;
            }
            *msg = "Disco sem NXFS - formatando...";
            return BOOT_WARN;
        case 4:
            if (fs_format() == 0 && fs_mount() == 0)
            {
                fs_ok = 1;
                *msg = "NXFS criado e montado";
                return BOOT_OK;
            }
            *msg = "NXFS indisponivel";
            return BOOT_FAIL;
        default:
            return BOOT_END;
        }
}

/* Text-mode boot, used when the intro is unavailable */
static void text_boot(void)
{
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
    const char *msg;
    for (int st; (st = boot_next(&msg)) != BOOT_END;)
    {
        if (st == BOOT_OK)
            okline(msg);
        else if (st == BOOT_FAIL)
            failline(msg);
        else
        {
            cputs(C_YELLOW, "  [ !! ] ");
            cputs(C_YELLOW, msg);
            putc('\n');
        }
    }
    delay(10);
}

void kmain(u32 drive)
{
    boot_drive = drive;
    vid_init();
    if (!nexus_intro_play(boot_next))
        text_boot();
    vid_clear();
    cputs(C_CYAN, "NEXUS.OS");
    cputs(C_GRAY, " v0.1  -  pronto.\n");
    shell_run(fs_ok);
}
