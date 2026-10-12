/* System messages in Portuguese, English and Spanish */
#include "nexus.h"

u8 lang = LANG_EN; /* English until the saved language is known */
u8 lang_set;
static u8 can_save;

/* Language names: Latin-1 for the graphical selector, CP437 for text mode */
const char *const LANG_NAME_GFX[LANG_COUNT] = {"PORTUGU\xCAS", "ENGLISH", "ESPA\xD1OL"};
const char *const LANG_NAME_TXT[LANG_COUNT] = {"Portugu\x88s", "English", "Espa\xA4ol"};

static const char *const STR[M_COUNT][LANG_COUNT] = {
    [M_BOOT_VIDEO_TEXT] = {"Video VGA texto 80x25", "VGA text video 80x25", "Video VGA texto 80x25"},
    [M_BOOT_VBE] = {"Video VBE", "VBE video", "Video VBE"},
    [M_BOOT_KBD] = {"Teclado (BIOS)", "Keyboard (BIOS)", "Teclado (BIOS)"},
    [M_BOOT_DISK] = {"Disco detectado via BIOS (LBA)", "Disk detected via BIOS (LBA)", "Disco detectado via BIOS (LBA)"},
    [M_BOOT_NODISK] = {"Disco/BIOS sem suporte a LBA ou disco muito pequeno",
                       "Disk/BIOS without LBA support or disk too small",
                       "Disco/BIOS sin soporte LBA o disco demasiado pequeno"},
    [M_BOOT_ISO] = {"Modo ISO: somente leitura, alteracoes valem ate desligar",
                    "ISO mode: read-only, changes last until power off",
                    "Modo ISO: solo lectura, los cambios duran hasta apagar"},
    [M_BOOT_MOUNT] = {"NXFS montado", "NXFS mounted", "NXFS montado"},
    [M_BOOT_NOFS] = {"Disco sem NXFS - formatando...", "Disk without NXFS - formatting...", "Disco sin NXFS - formateando..."},
    [M_BOOT_NEWFS] = {"NXFS criado e montado", "NXFS created and mounted", "NXFS creado y montado"},
    [M_BOOT_FSFAIL] = {"NXFS indisponivel", "NXFS unavailable", "NXFS no disponible"},
    [M_TAG_FAIL] = {"FALHA", "FAIL", "FALLO"},
    [M_SUBTITLE] = {"Sistema Operacional NEXUS.OS  -  v0.1", "NEXUS.OS Operating System  -  v0.1",
                    "Sistema Operativo NEXUS.OS  -  v0.1"},
    [M_STARTING] = {"Ligando", "Starting", "Iniciando"},
    [M_READY] = {" v0.1  -  pronto.\n", " v0.1  -  ready.\n", " v0.1  -  listo.\n"},

    [M_ST_INIT] = {"INICIALIZANDO SISTEMAS~", "INITIALIZING SYSTEMS~", "INICIALIZANDO SISTEMAS~"},
    [M_ST_ONLINE] = {"SISTEMA ONLINE", "SYSTEM ONLINE", "SISTEMA EN LINEA"},
    [M_ST_OFF] = {"ENCERRANDO SISTEMAS~", "SHUTTING DOWN SYSTEMS~", "APAGANDO SISTEMAS~"},
    [M_ST_SELECT] = {"SELECIONE O IDIOMA", "SELECT LANGUAGE", "SELECCIONE EL IDIOMA"},
    [M_SEL_HINT] = {"\030 \031 ESCOLHER    ENTER CONFIRMAR", "\030 \031 CHOOSE    ENTER CONFIRM",
                    "\030 \031 ELEGIR    ENTER CONFIRMAR"},

    [M_HINT] = {"Digite 'help' para ver os comandos.\n\n", "Type 'help' to see the commands.\n\n",
                "Escriba 'help' para ver los comandos.\n\n"},
    [M_ERR] = {"erro: ", "error: ", "error: "},
    [M_DIR_TAG] = {"  <PASTA>   ", "  <DIR>     ", "  <CARPETA> "},
    [M_FILE_TAG] = {"  <ARQ>     ", "  <FILE>    ", "  <ARCH>    "},
    [M_EMPTY] = {"  (vazio)\n", "  (empty)\n", "  (vacio)\n"},
    [M_NDIRS] = {" pasta(s), ", " folder(s), ", " carpeta(s), "},
    [M_NFILES] = {" arquivo(s)\n", " file(s)\n", " archivo(s)\n"},
    [M_STORAGE] = {"Armazenamento\n", "Storage\n", "Almacenamiento\n"},
    [M_INFO_DRIVE] = {"  Drive BIOS : 0x", "  BIOS drive : 0x", "  Unidad BIOS: 0x"},
    [M_INFO_SIZE] = {"  Tamanho    : ", "  Size       : ", "  Tamano     : "},
    [M_SECTORS] = {" setores (", " sectors (", " sectores ("},
    [M_INFO_ISO] = {"  Modo       : ISO (alteracoes nao persistem)\n", "  Mode       : ISO (changes are not saved)\n",
                    "  Modo       : ISO (los cambios no se guardan)\n"},
    [M_INFO_USED] = {" entradas usadas\n", " entries used\n", " entradas usadas\n"},
    [M_INFO_LANG] = {"  Idioma     : ", "  Language   : ", "  Idioma     : "},
    [M_HELP_TITLE] = {"Comandos do NEXUS.OS\n", "NEXUS.OS commands\n", "Comandos de NEXUS.OS\n"},
    [M_HELP] = {"  ls [caminho]          lista arquivos e pastas\n"
                "  tree                  lista tudo em arvore\n"
                "  cd <pasta> | pwd      navegar\n"
                "  mkdir <pasta>         cria pasta\n"
                "  rename <de> <para>    renomeia pasta/arquivo (alias: mv)\n"
                "  rmdir <pasta>         exclui pasta vazia\n"
                "  rm [-r] <item>        exclui arquivo ou pasta (-r: com conteudo)\n"
                "  touch <arquivo>       cria arquivo vazio\n"
                "  info | format         dados do disco / apaga o NXFS\n"
                "  idioma [pt|en|es]     troca o idioma\n"
                "  clear | reboot | shutdown\n",
                "  ls [path]             list files and folders\n"
                "  tree                  list everything as a tree\n"
                "  cd <folder> | pwd     navigate\n"
                "  mkdir <folder>        create folder\n"
                "  rename <from> <to>    rename folder/file (alias: mv)\n"
                "  rmdir <folder>        delete empty folder\n"
                "  rm [-r] <item>        delete file or folder (-r: with contents)\n"
                "  touch <file>          create empty file\n"
                "  info | format         disk info / erase NXFS\n"
                "  language [pt|en|es]   change language\n"
                "  clear | reboot | shutdown\n",
                "  ls [ruta]             lista archivos y carpetas\n"
                "  tree                  lista todo en arbol\n"
                "  cd <carpeta> | pwd    navegar\n"
                "  mkdir <carpeta>       crea carpeta\n"
                "  rename <de> <a>       renombra carpeta/archivo (alias: mv)\n"
                "  rmdir <carpeta>       borra carpeta vacia\n"
                "  rm [-r] <item>        borra archivo o carpeta (-r: con contenido)\n"
                "  touch <archivo>       crea archivo vacio\n"
                "  info | format         datos del disco / borra el NXFS\n"
                "  idioma [pt|en|es]     cambia el idioma\n"
                "  clear | reboot | shutdown\n"},
    [M_FMT_CONFIRM] = {"Isso APAGA todas as pastas e arquivos. Digite SIM para confirmar: ",
                       "This ERASES all folders and files. Type YES to confirm: ",
                       "Esto BORRA todas las carpetas y archivos. Escriba SI para confirmar: "},
    [M_FMT_WORD] = {"SIM", "YES", "SI"},
    [M_CANCELLED] = {"Cancelado.\n", "Cancelled.\n", "Cancelado.\n"},
    [M_FORMATTED] = {"NXFS formatado.\n", "NXFS formatted.\n", "NXFS formateado.\n"},
    [M_SHUTTING] = {"Desligando NEXUS.OS...\n", "Shutting down NEXUS.OS...\n", "Apagando NEXUS.OS...\n"},
    [M_REBOOTING] = {"Reiniciando...\n", "Rebooting...\n", "Reiniciando...\n"},
    [M_NOSTORAGE] = {"armazenamento indisponivel (disco sem suporte BIOS/LBA)\n",
                     "storage unavailable (disk without BIOS/LBA support)\n",
                     "almacenamiento no disponible (disco sin soporte BIOS/LBA)\n"},
    [M_USE_MKDIR] = {"uso: mkdir <pasta>\n", "usage: mkdir <folder>\n", "uso: mkdir <carpeta>\n"},
    [M_USE_TOUCH] = {"uso: touch <arquivo>\n", "usage: touch <file>\n", "uso: touch <archivo>\n"},
    [M_USE_RMDIR] = {"uso: rmdir <pasta>\n", "usage: rmdir <folder>\n", "uso: rmdir <carpeta>\n"},
    [M_USE_RM] = {"uso: rm [-r] <item>\n", "usage: rm [-r] <item>\n", "uso: rm [-r] <item>\n"},
    [M_USE_RENAME] = {"uso: rename <de> <para>\n", "usage: rename <from> <to>\n", "uso: rename <de> <a>\n"},
    [M_USE_LANG] = {"uso: idioma [pt|en|es]\n", "usage: language [pt|en|es]\n", "uso: idioma [pt|en|es]\n"},
    [M_UNKNOWN] = {"comando desconhecido: ", "unknown command: ", "comando desconocido: "},
    [M_TYPE_HELP] = {"  (digite help)\n", "  (type help)\n", "  (escriba help)\n"},
    [M_LANG_CHANGED] = {"Idioma: ", "Language: ", "Idioma: "},
    [M_POWER_FAIL] = {"\nNao foi possivel desligar por software.\nPode desligar o computador manualmente.\n",
                      "\nCould not power off by software.\nYou can turn off the computer manually.\n",
                      "\nNo se pudo apagar por software.\nPuede apagar el equipo manualmente.\n"},

    [M_OFF_IN] = {"NEXUS desligando em", "NEXUS shutting down in", "NEXUS apagandose en"},
    [M_OFF_HINT] = {"ESC cancela", "ESC cancels", "ESC cancela"},
    [M_SHUT_CANCEL] = {"Desligamento cancelado.\n", "Shutdown cancelled.\n", "Apagado cancelado.\n"},

    [M_FS_NOTFOUND] = {"nao encontrado", "not found", "no encontrado"},
    [M_FS_EXISTS] = {"ja existe um item com esse nome", "an item with this name already exists",
                     "ya existe un elemento con ese nombre"},
    [M_FS_FULL] = {"armazenamento cheio (limite de entradas)", "storage full (entry limit)",
                   "almacenamiento lleno (limite de entradas)"},
    [M_FS_NOTEMPTY] = {"a pasta nao esta vazia (use rm -r)", "folder is not empty (use rm -r)",
                       "la carpeta no esta vacia (use rm -r)"},
    [M_FS_INVALID] = {"nome ou caminho invalido", "invalid name or path", "nombre o ruta no valido"},
    [M_FS_IO] = {"erro de escrita/leitura no disco", "disk read/write error", "error de lectura/escritura en disco"},
    [M_FS_ROOT] = {"a raiz nao pode ser alterada", "the root cannot be changed", "la raiz no se puede modificar"},
    [M_FS_INUSE] = {"voce esta dentro dessa pasta (saia com cd)", "you are inside this folder (leave with cd)",
                    "esta dentro de esa carpeta (salga con cd)"},
    [M_FS_NOTDIR] = {"nao e uma pasta", "not a folder", "no es una carpeta"},
    [M_FS_UNKNOWN] = {"erro desconhecido", "unknown error", "error desconocido"},
};

const char *tr(int id) { return STR[id][lang]; }

/* Called once NXFS is mounted: load the saved language and allow saving */
void lang_attach_fs(void)
{
    u8 v = fs_get_lang();
    can_save = 1;
    if (v >= 1 && v <= LANG_COUNT)
    {
        lang = (u8)(v - 1);
        lang_set = 1;
    }
}

void lang_choose(int l)
{
    lang = (u8)l;
    lang_set = 1;
    if (can_save)
        fs_set_lang((u8)(l + 1));
}

int lang_parse(const char *s)
{
    if (!strcmp(s, "pt"))
        return LANG_PT;
    if (!strcmp(s, "en"))
        return LANG_EN;
    if (!strcmp(s, "es"))
        return LANG_ES;
    return -1;
}

/* Text-mode language menu, used without VBE graphics */
int lang_menu_text(void)
{
    cputs(C_CYAN, "\n  Selecione o idioma / Select language / Seleccione el idioma\n\n");
    for (int i = 0; i < LANG_COUNT; i++)
    {
        cputs(C_GRAY, "    ");
        putc((char)('1' + i));
        cputs(C_GRAY, ") ");
        puts(LANG_NAME_TXT[i]);
        putc('\n');
    }
    putc('\n');
    for (;;)
    {
        u8 ch = (u8)(getkey() & 0xFF);
        if (ch >= '1' && ch < '1' + LANG_COUNT)
            return ch - '1';
    }
}
