/* Shell do NEXUS.OS */
#include "nexus.h"

static int cwd = 0;
static int fs_ok = 0;

static void err(int code)
{
    cputs(C_RED, "erro: ");
    cputs(C_RED, fs_strerror(code));
    putc('\n');
}

static void prompt(void)
{
    char p[96];
    fs_path(cwd, p, sizeof p);
    cputs(C_GREEN, "nexus");
    cputs(C_GRAY, ":");
    cputs(C_CYAN, p);
    cputs(C_WHITE, "> ");
}

static int split(char *line, char **argv, int max)
{
    int n = 0;
    while (*line)
    {
        while (*line == ' ')
            line++;
        if (!*line)
            break;
        if (n < max)
            argv[n++] = line;
        while (*line && *line != ' ')
            line++;
        if (*line)
            *line++ = 0;
    }
    return n;
}

static void print_entry(int i)
{
    if (fs_tab[i].type == FS_DIR)
    {
        cputs(C_CYAN, "  <PASTA>  ");
        cputs(C_CYAN, fs_tab[i].name);
    }
    else
    {
        cputs(C_NORMAL, "  <ARQ>    ");
        cputs(C_NORMAL, fs_tab[i].name);
    }
    putc('\n');
}

static void cmd_ls(const char *arg)
{
    int d = arg ? fs_walk(cwd, arg) : cwd;
    if (d < 0)
    {
        err(d);
        return;
    }
    if (fs_tab[d].type != FS_DIR)
    {
        print_entry(d);
        return;
    }
    int nd = 0, nf = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 1; i < FS_MAX; i++)
        {
            if (!fs_tab[i].type || fs_tab[i].parent != d)
                continue;
            if ((pass == 0) != (fs_tab[i].type == FS_DIR))
                continue;
            print_entry(i);
            if (pass == 0)
                nd++;
            else
                nf++;
        }
    if (!nd && !nf)
        cputs(C_GRAY, "  (vazio)\n");
    cputs(C_GRAY, "  ");
    putu((u32)nd);
    cputs(C_GRAY, " pasta(s), ");
    putu((u32)nf);
    cputs(C_GRAY, " arquivo(s)\n");
}

static void tree(int d, int depth)
{
    if (depth > 12)
        return;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 1; i < FS_MAX; i++)
        {
            if (!fs_tab[i].type || fs_tab[i].parent != d)
                continue;
            if ((pass == 0) != (fs_tab[i].type == FS_DIR))
                continue;
            for (int k = 0; k < depth; k++)
                puts("|  ");
            puts("+-- ");
            cputs(fs_tab[i].type == FS_DIR ? C_CYAN : C_NORMAL, fs_tab[i].name);
            putc('\n');
            if (fs_tab[i].type == FS_DIR)
                tree(i, depth + 1);
        }
}

static void cmd_create(const char *path, u8 type)
{
    int parent;
    char name[FS_NAMELEN];
    int r = fs_split(cwd, path, &parent, name);
    if (r)
    {
        err(r);
        return;
    }
    r = fs_create(parent, name, type);
    if (r < 0)
        err(r);
}

static void cmd_rm(const char *path, int recursive, int only_dir)
{
    int idx = fs_walk(cwd, path);
    if (idx < 0)
    {
        err(idx);
        return;
    }
    if (only_dir && fs_tab[idx].type != FS_DIR)
    {
        err(FS_E_NOTDIR);
        return;
    }
    if (fs_within(idx, cwd))
    {
        err(idx == 0 ? FS_E_ROOT : FS_E_INUSE);
        return;
    }
    int r = fs_remove(idx, recursive);
    if (r)
        err(r);
}

static void cmd_rename(const char *old, const char *nw)
{
    int idx = fs_walk(cwd, old);
    if (idx < 0)
    {
        err(idx);
        return;
    }
    int r = fs_rename(idx, nw);
    if (r)
        err(r);
}

static void cmd_info(void)
{
    u32 s = disk_sectors();
    cputs(C_YELLOW, "Armazenamento\n");
    puts("  Drive BIOS : 0x");
    puthex(disk_drive(), 2);
    putc('\n');
    puts("  Tamanho    : ");
    putu(s);
    puts(" setores (");
    putu(s / 2048);
    puts(" MiB)\n");
    if (disk_is_cd())
        cputs(C_YELLOW, "  Modo       : ISO (alteracoes nao persistem)\n");
    puts("  NXFS       : ");
    putu((u32)fs_used());
    putc('/');
    putu(FS_MAX);
    puts(" entradas usadas\n");
}

static void cmd_help(void)
{
    cputs(C_YELLOW, "Comandos do NEXUS.OS\n");
    puts("  ls [caminho]          lista arquivos e pastas\n");
    puts("  tree                  lista tudo em arvore\n");
    puts("  cd <pasta> | pwd      navegar\n");
    puts("  mkdir <pasta>         cria pasta\n");
    puts("  rename <de> <para>    renomeia pasta/arquivo (alias: mv)\n");
    puts("  rmdir <pasta>         exclui pasta vazia\n");
    puts("  rm [-r] <item>        exclui arquivo ou pasta (-r: com conteudo)\n");
    puts("  touch <arquivo>       cria arquivo vazio\n");
    puts("  info | format         dados do disco / apaga o NXFS\n");
    puts("  clear | reboot | shutdown\n");
}

static void cmd_format(void)
{
    char c[8];
    cputs(C_RED, "Isso APAGA todas as pastas e arquivos. Digite SIM para confirmar: ");
    readline(c, sizeof c, 0);
    if (strcmp(c, "SIM"))
    {
        puts("Cancelado.\n");
        return;
    }
    int r = fs_format();
    cwd = 0;
    if (r)
        err(r);
    else
        puts("NXFS formatado.\n");
}

void shell_run(int ok)
{
    char line[80], last[80], *av[4];
    fs_ok = ok;
    last[0] = 0;
    cputs(C_GRAY, "Digite 'help' para ver os comandos.\n\n");
    for (;;)
    {
        prompt();
        readline(line, sizeof line, last);
        int n = split(line, av, 4);
        if (!n)
            continue;
        {
            int i = 0;
            while (line[i] || i == 0)
            {
                last[i] = line[i];
                if (!line[i])
                    break;
                i++;
            }
        }
        /* 'line' foi cortado em tokens; refaz 'last' juntando os argumentos */
        {
            int o = 0;
            for (int a = 0; a < n; a++)
            {
                const char *t = av[a];
                if (a)
                    last[o++] = ' ';
                while (*t && o < 78)
                    last[o++] = *t++;
            }
            last[o] = 0;
        }

        const char *c = av[0];
        if (!strcmp(c, "help") || !strcmp(c, "ajuda"))
            cmd_help();
        else if (!strcmp(c, "clear") || !strcmp(c, "cls") || !strcmp(c, "limpar"))
            vid_clear();
        else if (!strcmp(c, "shutdown") || !strcmp(c, "poweroff") || !strcmp(c, "desligar"))
        {
            cputs(C_YELLOW, "Desligando NEXUS.OS...\n");
            nexus_outro_play();
            power_off();
        }
        else if (!strcmp(c, "reboot") || !strcmp(c, "reiniciar"))
        {
            cputs(C_YELLOW, "Reiniciando...\n");
            power_reboot();
        }
        else if (!fs_ok)
            cputs(C_RED, "armazenamento indisponivel (disco sem suporte BIOS/LBA)\n");
        else if (!strcmp(c, "ls") || !strcmp(c, "dir"))
            cmd_ls(n > 1 ? av[1] : 0);
        else if (!strcmp(c, "tree"))
        {
            int d = n > 1 ? fs_walk(cwd, av[1]) : cwd;
            if (d < 0)
                err(d);
            else
                tree(d, 0);
        }
        else if (!strcmp(c, "pwd"))
        {
            char p[96];
            fs_path(cwd, p, sizeof p);
            puts(p);
            putc('\n');
        }
        else if (!strcmp(c, "cd"))
        {
            int d = n > 1 ? fs_walk(cwd, av[1]) : 0;
            if (d < 0)
                err(d);
            else if (fs_tab[d].type != FS_DIR)
                err(FS_E_NOTDIR);
            else
                cwd = d;
        }
        else if (!strcmp(c, "mkdir"))
        {
            if (n < 2)
                puts("uso: mkdir <pasta>\n");
            else
                cmd_create(av[1], FS_DIR);
        }
        else if (!strcmp(c, "touch"))
        {
            if (n < 2)
                puts("uso: touch <arquivo>\n");
            else
                cmd_create(av[1], FS_FILE);
        }
        else if (!strcmp(c, "rmdir"))
        {
            if (n < 2)
                puts("uso: rmdir <pasta>\n");
            else
                cmd_rm(av[1], 0, 1);
        }
        else if (!strcmp(c, "rm") || !strcmp(c, "del"))
        {
            if (n >= 3 && !strcmp(av[1], "-r"))
                cmd_rm(av[2], 1, 0);
            else if (n >= 2)
                cmd_rm(av[1], 0, 0);
            else
                puts("uso: rm [-r] <item>\n");
        }
        else if (!strcmp(c, "rename") || !strcmp(c, "mv") || !strcmp(c, "renomear"))
        {
            if (n < 3)
                puts("uso: rename <de> <para>\n");
            else
                cmd_rename(av[1], av[2]);
        }
        else if (!strcmp(c, "info"))
            cmd_info();
        else if (!strcmp(c, "format"))
            cmd_format();
        else
        {
            cputs(C_RED, "comando desconhecido: ");
            cputs(C_RED, c);
            cputs(C_GRAY, "  (digite help)\n");
        }
    }
}
