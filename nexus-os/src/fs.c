/* NXFS, the NEXUS.OS file system
 *   LBA FS_LBA       : superblock ("NEXUSFS1")
 *   LBA FS_LBA+1 ... : table of FS_MAX 32-byte entries
 * Entry: type(1) parent(1) name(30). Entry 0 is the root "/".
 */
#include "nexus.h"

struct fs_ent fs_tab[FS_MAX];
static u8 sect[512];
static const char MAGIC[8] = {'N', 'E', 'X', 'U', 'S', 'F', 'S', '1'};

#define TAB_SECTORS ((FS_MAX * 32) / 512)

static int flush_ent(int i)
{
    int s = i / 16;
    return disk_write(FS_LBA + 1 + s, (u8 *)fs_tab + s * 512) ? FS_E_IO : 0;
}

int fs_mount(void)
{
    if (disk_read(FS_LBA, sect))
        return -1;
    for (int i = 0; i < 8; i++)
        if (sect[i] != (u8)MAGIC[i])
            return -1;
    if (sect[8] != (u8)(FS_MAX & 0xFF) || sect[9] != (u8)(FS_MAX >> 8))
        return -1;
    for (int s = 0; s < TAB_SECTORS; s++)
        if (disk_read(FS_LBA + 1 + s, (u8 *)fs_tab + s * 512))
            return -1;
    if (fs_tab[0].type != FS_DIR)
        return -1;
    return 0;
}

int fs_format(void)
{
    memset(fs_tab, 0, sizeof fs_tab);
    fs_tab[0].type = FS_DIR;
    fs_tab[0].parent = 0;
    fs_tab[0].name[0] = '/';
    memset(sect, 0, 512);
    memcpy(sect, MAGIC, 8);
    sect[8] = (u8)(FS_MAX & 0xFF);
    sect[9] = (u8)(FS_MAX >> 8);
    if (disk_write(FS_LBA, sect))
        return FS_E_IO;
    for (int s = 0; s < TAB_SECTORS; s++)
        if (disk_write(FS_LBA + 1 + s, (u8 *)fs_tab + s * 512))
            return FS_E_IO;
    return 0;
}

static int find(int parent, const char *name)
{
    for (int i = 1; i < FS_MAX; i++)
        if (fs_tab[i].type && fs_tab[i].parent == parent && !strcmp(fs_tab[i].name, name))
            return i;
    return -1;
}

int fs_valid_name(const char *n)
{
    u32 len = strlen(n);
    if (len == 0 || len >= FS_NAMELEN)
        return 0;
    if (!strcmp(n, ".") || !strcmp(n, ".."))
        return 0;
    for (u32 i = 0; i < len; i++)
        if ((u8)n[i] <= 32 || (u8)n[i] >= 127 || n[i] == '/')
            return 0;
    return 1;
}

int fs_walk(int cwd, const char *path)
{
    char comp[FS_NAMELEN];
    int cur = (path[0] == '/') ? 0 : cwd;
    if (strlen(path) > 120)
        return FS_E_INVALID;
    while (*path)
    {
        while (*path == '/')
            path++;
        if (!*path)
            break;
        u32 n = 0;
        while (*path && *path != '/')
        {
            if (n < FS_NAMELEN - 1)
                comp[n] = *path;
            n++;
            path++;
        }
        if (n >= FS_NAMELEN)
            return FS_E_NOTFOUND;
        comp[n] = 0;
        if (!strcmp(comp, "."))
            continue;
        if (fs_tab[cur].type != FS_DIR)
            return FS_E_NOTDIR;
        if (!strcmp(comp, ".."))
        {
            cur = fs_tab[cur].parent;
            continue;
        }
        int f = find(cur, comp);
        if (f < 0)
            return FS_E_NOTFOUND;
        cur = f;
    }
    return cur;
}

int fs_split(int cwd, const char *path, int *parent, char *name)
{
    char tmp[64];
    int n = (int)strlen(path), slash = -1;
    if (n == 0 || n >= 64)
        return FS_E_INVALID;
    memcpy(tmp, path, (u32)n + 1);
    while (n > 1 && tmp[n - 1] == '/')
        tmp[--n] = 0;
    for (int i = 0; i < n; i++)
        if (tmp[i] == '/')
            slash = i;
    const char *base;
    if (slash < 0)
    {
        *parent = cwd;
        base = tmp;
    }
    else
    {
        if (slash == 0)
            *parent = 0;
        else
        {
            tmp[slash] = 0;
            *parent = fs_walk(cwd, tmp);
            if (*parent < 0)
                return FS_E_NOTFOUND;
            if (fs_tab[*parent].type != FS_DIR)
                return FS_E_NOTDIR;
        }
        base = tmp + slash + 1;
    }
    if (!fs_valid_name(base))
        return FS_E_INVALID;
    memcpy(name, base, strlen(base) + 1);
    return 0;
}

int fs_create(int parent, const char *name, u8 type)
{
    if (fs_tab[parent].type != FS_DIR)
        return FS_E_NOTDIR;
    if (!fs_valid_name(name))
        return FS_E_INVALID;
    if (find(parent, name) >= 0)
        return FS_E_EXISTS;
    int i;
    for (i = 1; i < FS_MAX; i++)
        if (!fs_tab[i].type)
            break;
    if (i == FS_MAX)
        return FS_E_FULL;
    memset(&fs_tab[i], 0, sizeof fs_tab[i]);
    fs_tab[i].type = type;
    fs_tab[i].parent = (u8)parent;
    memcpy(fs_tab[i].name, name, strlen(name) + 1);
    int r = flush_ent(i);
    if (r)
    {
        fs_tab[i].type = 0;
        return r;
    }
    return i;
}

int fs_remove(int idx, int recursive)
{
    if (idx <= 0)
        return FS_E_ROOT;
    if (fs_tab[idx].type == FS_DIR)
    {
        for (int i = 1; i < FS_MAX; i++)
        {
            if (fs_tab[i].type && fs_tab[i].parent == idx)
            {
                if (!recursive)
                    return FS_E_NOTEMPTY;
                int r = fs_remove(i, 1);
                if (r)
                    return r;
            }
        }
    }
    memset(&fs_tab[idx], 0, sizeof fs_tab[idx]);
    return flush_ent(idx);
}

int fs_rename(int idx, const char *name)
{
    if (idx <= 0)
        return FS_E_ROOT;
    if (!fs_valid_name(name))
        return FS_E_INVALID;
    int f = find(fs_tab[idx].parent, name);
    if (f >= 0 && f != idx)
        return FS_E_EXISTS;
    memset(fs_tab[idx].name, 0, FS_NAMELEN);
    memcpy(fs_tab[idx].name, name, strlen(name) + 1);
    return flush_ent(idx);
}

/* 1 if 'node' is 'anc' or one of its descendants */
int fs_within(int anc, int node)
{
    for (int guard = 0; guard < FS_MAX; guard++)
    {
        if (node == anc)
            return 1;
        if (node == 0)
            return 0;
        node = fs_tab[node].parent;
    }
    return 0;
}

int fs_used(void)
{
    int n = 0;
    for (int i = 0; i < FS_MAX; i++)
        if (fs_tab[i].type)
            n++;
    return n;
}

void fs_path(int idx, char *out, int max)
{
    int chain[40], n = 0, o = 0;
    while (idx != 0 && n < 40)
    {
        chain[n++] = idx;
        idx = fs_tab[idx].parent;
    }
    if (n == 0)
    {
        out[0] = '/';
        out[1] = 0;
        return;
    }
    while (n > 0)
    {
        const char *nm = fs_tab[chain[--n]].name;
        if (o < max - 1)
            out[o++] = '/';
        while (*nm && o < max - 1)
            out[o++] = *nm++;
    }
    out[o] = 0;
}

const char *fs_strerror(int e)
{
    switch (e)
    {
    case FS_E_NOTFOUND:
        return "nao encontrado";
    case FS_E_EXISTS:
        return "ja existe um item com esse nome";
    case FS_E_FULL:
        return "armazenamento cheio (limite de entradas)";
    case FS_E_NOTEMPTY:
        return "a pasta nao esta vazia (use rm -r)";
    case FS_E_INVALID:
        return "nome ou caminho invalido";
    case FS_E_IO:
        return "erro de escrita/leitura no disco";
    case FS_E_ROOT:
        return "a raiz nao pode ser alterada";
    case FS_E_INUSE:
        return "voce esta dentro dessa pasta (saia com cd)";
    case FS_E_NOTDIR:
        return "nao e uma pasta";
    }
    return "erro desconhecido";
}
