/* NEXUS.OS - cabecalho comum (modo real 16-bit, compilado com gcc -m16) */
#ifndef NEXUS_H
#define NEXUS_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/* cores VGA */
#define C_NORMAL 0x07
#define C_GRAY 0x08
#define C_BLUE 0x09
#define C_GREEN 0x0A
#define C_CYAN 0x0B
#define C_RED 0x0C
#define C_MAGENTA 0x0D
#define C_YELLOW 0x0E
#define C_WHITE 0x0F

/* ---- portas de I/O ---- */
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" ::"a"(v), "Nd"(p)); }
static inline void outw(u16 p, u16 v) { asm volatile("outw %0,%1" ::"a"(v), "Nd"(p)); }
static inline u8 inb(u16 p)
{
    u8 v;
    asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline u16 inw(u16 p)
{
    u16 v;
    asm volatile("inw %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}

/* ---- memoria "flat" de 4GB via FS (modo unreal, ver start.asm) ---- */
static inline u8 rd8(u32 a)
{
    u8 v;
    asm volatile("movb %%fs:(%1),%0" : "=q"(v) : "r"(a));
    return v;
}
static inline u16 rd16(u32 a)
{
    u16 v;
    asm volatile("movw %%fs:(%1),%0" : "=r"(v) : "r"(a));
    return v;
}
static inline u32 rd32(u32 a)
{
    u32 v;
    asm volatile("movl %%fs:(%1),%0" : "=r"(v) : "r"(a));
    return v;
}

/* ---- string.c ---- */
u32 strlen(const char *s);
int strcmp(const char *a, const char *b);
void *memcpy(void *d, const void *s, u32 n);
void *memset(void *d, int c, u32 n);

/* ---- video.c ---- */
void vid_init(void);
void vid_clear(void);
void putc(char c);
void puts(const char *s);
void cputs(u8 color, const char *s);
void setcolor(u8 c);
void putu(u32 n);
void puthex(u32 n, int digits);

/* ---- kbd.c ---- */
u16 getkey(void); /* AH=scancode, AL=ascii */
void readline(char *buf, int max, char *last);

/* ---- disk.c (INT 13h estendido: funciona com pendrive e VM) ---- */
int disk_init(u8 drive);
int disk_read(u32 lba, void *buf);
int disk_write(u32 lba, const void *buf);
u32 disk_sectors(void);
u8 disk_drive(void);
u8 disk_is_cd(void);

/* ---- fs.c : NXFS ---- */
#define FS_LBA 128 /* setor onde o NXFS comeca no disco */
#define FS_MAX 224 /* maximo de entradas (pastas+arquivos) */
#define FS_NAMELEN 30
#define FS_DIR 1
#define FS_FILE 2

#define FS_E_NOTFOUND (-1)
#define FS_E_EXISTS (-2)
#define FS_E_FULL (-3)
#define FS_E_NOTEMPTY (-4)
#define FS_E_INVALID (-5)
#define FS_E_IO (-6)
#define FS_E_ROOT (-7)
#define FS_E_INUSE (-8)
#define FS_E_NOTDIR (-9)

struct fs_ent
{
    u8 type;
    u8 parent;
    char name[FS_NAMELEN];
};
extern struct fs_ent fs_tab[FS_MAX];

int fs_mount(void);
int fs_format(void);
int fs_walk(int cwd, const char *path);
int fs_split(int cwd, const char *path, int *parent, char *name);
int fs_valid_name(const char *name);
int fs_create(int parent, const char *name, u8 type);
int fs_remove(int idx, int recursive);
int fs_rename(int idx, const char *name);
int fs_within(int anc, int node);
int fs_used(void);
void fs_path(int idx, char *out, int max);
const char *fs_strerror(int e);

/* ---- power.c ---- */
void power_off(void);
void power_reboot(void);

/* ---- shell.c ---- */
void shell_run(int fs_ok);

/* ---- start.asm ---- */
extern void enter_unreal(void);

#endif
