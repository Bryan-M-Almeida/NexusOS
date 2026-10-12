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

static void okline(int m)
{
    cputs(C_GRAY, "  [");
    cputs(C_GREEN, " OK ");
    cputs(C_GRAY, "] ");
    puts(tr(m));
    putc('\n');
    delay(3);
}

static void failline(int m)
{
    cputs(C_GRAY, "  [");
    cputs(C_RED, tr(M_TAG_FAIL));
    cputs(C_GRAY, "] ");
    puts(tr(m));
    putc('\n');
    delay(6);
}

/* Runs one boot step per call (used by the intro and the text boot).
 * Returns BOOT_OK, BOOT_WARN, BOOT_FAIL or BOOT_END. */
static u32 boot_drive;
static int boot_stage, fs_ok;

static int boot_next(int *msg)
{
    for (;;)
        switch (boot_stage++)
        {
        case 0:
            *msg = M_BOOT_KBD;
            return BOOT_OK;
        case 1:
            if (disk_init((u8)boot_drive) == 0 && disk_sectors() >= FS_LBA + 16)
            {
                *msg = M_BOOT_DISK;
                return BOOT_OK;
            }
            boot_stage = 99;
            *msg = M_BOOT_NODISK;
            return BOOT_FAIL;
        case 2:
            if (disk_is_cd())
            {
                *msg = M_BOOT_ISO;
                return BOOT_WARN;
            }
            break;
        case 3:
            if (fs_mount() == 0)
            {
                fs_ok = 1;
                lang_attach_fs();
                boot_stage = 99;
                *msg = M_BOOT_MOUNT;
                return BOOT_OK;
            }
            *msg = M_BOOT_NOFS;
            return BOOT_WARN;
        case 4:
            if (fs_format() == 0 && fs_mount() == 0)
            {
                fs_ok = 1;
                lang_attach_fs();
                *msg = M_BOOT_NEWFS;
                return BOOT_OK;
            }
            *msg = M_BOOT_FSFAIL;
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
    for (int k = 0; k < (80 - (int)strlen(tr(M_SUBTITLE))) / 2; k++)
        putc(' ');
    cputs(C_GRAY, tr(M_SUBTITLE));
    puts("\n\n");

    puts("  ");
    puts(tr(M_STARTING));
    puts(" [");
    for (int i = 0; i < 40; i++)
    {
        cputs(C_CYAN, "\xDB");
        delay(1);
    }
    puts("]\n\n");

    okline(M_BOOT_VIDEO_TEXT);
    int msg;
    for (int st; (st = boot_next(&msg)) != BOOT_END;)
    {
        if (st == BOOT_OK)
            okline(msg);
        else if (st == BOOT_FAIL)
            failline(msg);
        else
        {
            cputs(C_YELLOW, "  [ !! ] ");
            cputs(C_YELLOW, tr(msg));
            putc('\n');
        }
    }
    if (!lang_set)
        lang_choose(lang_menu_text());
    delay(10);
}

void kmain(u32 drive)
{
    boot_drive = drive;
    vid_init();
    disk_init((u8)drive); /* early, so the intro can load its assets */
    if (!nexus_intro_play(boot_next))
        text_boot();
    vid_clear();
    cputs(C_CYAN, "NEXUS.OS");
    cputs(C_GRAY, tr(M_READY));
    shell_run(fs_ok);
}
