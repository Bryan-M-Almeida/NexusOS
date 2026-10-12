/* NEXUS.OS - animacao de abertura NEXUS.
 *
 * Video: VBE (INT 10h 4F0x) em 32 bpp com framebuffer linear, acessado pelo FS no modo
 * "unreal" (ver start.asm). Tudo em ponto fixo: o kernel compila com -mno-sse e nao
 * inicializa a FPU. Double buffering: cada quadro e desenhado no back buffer e so as
 * regioes que mudaram vao para o framebuffer. O tempo vem do PIT (canal 0 + tick da BIOS),
 * entao a duracao e real mesmo se o quadro atrasar.
 *
 * Os aros usam uma tabela polar pre-calculada: para cada pixel do anel guardamos raio e
 * angulo uma unica vez, agrupados por raio inteiro. A cada quadro so variamos escala,
 * rotacao e opacidade de cada aro, sem atan2/sqrt por pixel.
 *
 * Sem VBE 32 bpp, memoria ou linha A20, a intro e pulada sem mexer no modo texto.
 */
#include "nexus.h"

typedef signed char s8;
typedef short s16;
typedef int s32;
typedef long long s64;
typedef __seg_fs u8 hu8;   /* memoria "flat" acima de 1 MB, via FS */
typedef __seg_fs u32 hu32;

struct glyph
{
    u16 off;
    u8 w, h;
    s8 bx, by;
    u16 adv;
};
struct font
{
    const struct glyph *g;
    const u8 *data;
    const char *chars;
    u16 size16, asc16, desc16;
};
#include "assets.h" /* gerado por tools/mkassets.py */

/* ------------------------------------------------------------------ paleta */
#define COL_BG 0x030508
#define COL_BLUE 0x3D8BDB
#define COL_BRIGHT 0x6FB3FF
#define COL_RED 0xE5322D
#define COL_MUTED 0x8794A6
#define COL_FAINT 0x6FA0DC
#define COL_SILVER 0xC9D2DE
#define A(x) ((u8)((x) * 256 > 255 ? 255 : (x) * 256)) /* alfa 0..1 -> 0..255 */

/* ------------------------------------------------------------------ linha do tempo (ms) */
#define T_ONLINE 1700  /* nome entra, status muda */
#define T_ACRONYM 3300 /* linha de baixo */
#define T_FADE 5300    /* comeca o fade para preto */
#define T_END 6000
#define BOOT_MS 1800 /* entrada de cada aro */

/* ------------------------------------------------------------------ estado */
static u32 W, H, pitch, fbaddr, vbe_mode;
static s32 cx, cy;  /* centro da tela (fronteira entre pixels) */
static u32 D, S16;  /* diametro do anel em px; px por unidade do viewBox 600x600 (16.16) */
static hu32 *bb;    /* back buffer */
static hu32 *bgb;   /* fundo estatico (grade, nucleo, HUD depois de pronto) */
static hu32 *ptab;  /* tabela polar: [deslocamento do pixel, (raio16 << 16) | angulo] */
static u32 heap;

#define MAXBIN 340
static u32 bin0[MAXBIN + 2];   /* inicio de cada faixa de raio inteiro na ptab */
static u32 binfill[MAXBIN + 2];
static s32 rmin16, rmax16;     /* faixa de raio coberta pela tabela (1/16 px) */

static u8 vbeinfo[512];
static u8 modeinfo[256];

struct rect
{
    s32 x0, y0, x1, y1; /* x1/y1 exclusivos */
};

/* ------------------------------------------------------------------ utilitarios */
static u32 clamp256(s32 v) { return v < 0 ? 0 : v > 256 ? 256 : (u32)v; }
static s32 imin(s32 a, s32 b) { return a < b ? a : b; }
static s32 imax(s32 a, s32 b) { return a > b ? a : b; }

static u32 isqrt(u32 n)
{
    u32 r = 0, b = 1u << 30;
    while (b > n)
        b >>= 2;
    while (b)
    {
        if (n >= r + b)
        {
            n -= r + b;
            r = (r >> 1) + b;
        }
        else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

/* a sobre d, alfa 0..256 */
static inline u32 blend(u32 d, u32 c, u32 a)
{
    u32 na = 256 - a;
    u32 rb = (((c & 0xFF00FF) * a + (d & 0xFF00FF) * na) >> 8) & 0xFF00FF;
    u32 g = (((c & 0x00FF00) * a + (d & 0x00FF00) * na) >> 8) & 0x00FF00;
    return rb | g;
}

/* progresso 0..4096 de [st, st+dur] */
static s32 prog(s32 t, s32 st, s32 dur)
{
    if (t <= st)
        return 0;
    if (t >= st + dur)
        return 4096;
    return (t - st) * 4096 / dur;
}

static s32 ease(const u16 *tab, s32 p)
{
    if (p <= 0)
        return 0;
    if (p >= 4096)
        return 4096;
    s32 i = p >> 4, f = p & 15;
    return tab[i] + ((((s32)tab[i + 1] - (s32)tab[i]) * f) >> 4);
}

static u32 hmalloc(u32 n) /* aloca na memoria alta (retorna endereco flat) */
{
    u32 a = heap;
    heap += (n + 15) & ~15u;
    return a;
}

/* ------------------------------------------------------------------ tempo (PIT) */
/* Canal 0 em modo 2 (contagem linear, mesma frequencia de IRQ da BIOS). */
static u32 pit_now(void)
{
    u32 t, c;
    u8 irr;
    asm volatile("cli");
    t = rd32(0x46C); /* ticks da BIOS (18,2 Hz) */
    outb(0x43, 0x00);
    c = inb(0x40);
    c |= (u32)inb(0x40) << 8;
    outb(0x20, 0x0A);
    irr = inb(0x20);
    asm volatile("sti");
    if (c == 0)
        c = 65536;
    if ((irr & 1) && c > 32768) /* IRQ0 pendente: o contador ja recarregou */
        t++;
    return (t << 16) + (65536 - c); /* unidades de 1/1193182 s */
}

/* ------------------------------------------------------------------ teclado */
static int key_hit(void)
{
    u16 ax = 0x0100;
    u8 zf;
    asm volatile("int $0x16" : "+a"(ax), "=@ccz"(zf)::"memory");
    return !zf;
}

/* ------------------------------------------------------------------ hardware */
static u32 ext_mem_kb(void)
{
    u16 ax = 0xE801, bx = 0, cx_ = 0, dx = 0;
    u8 cf;
    asm volatile("int $0x15" : "+a"(ax), "+b"(bx), "+c"(cx_), "+d"(dx), "=@ccc"(cf)::"memory", "si", "di");
    if (!cf)
    {
        if (!ax && !bx)
        {
            ax = cx_;
            bx = dx;
        }
        return (u32)ax + (u32)bx * 64;
    }
    ax = 0x8800;
    asm volatile("int $0x15" : "+a"(ax), "=@ccc"(cf)::"memory", "bx", "cx", "dx", "si", "di");
    return cf ? 0 : ax;
}

static int a20_on(void)
{
    /* enderecos via registrador: em .code16gcc um endereco absoluto vira 16 bits */
    u32 alo = 0x000510, ahi = 0x100510;
    asm("" : "+r"(alo), "+r"(ahi));
    volatile hu8 *lo = (volatile hu8 *)alo, *hi = (volatile hu8 *)ahi;
    u8 a = *lo, b = *hi;
    *hi = (u8)~a;
    int ok = (*lo == a);
    *hi = b;
    return ok;
}

static int a20_enable(void)
{
    if (a20_on())
        return 1;
    u16 ax = 0x2401;
    asm volatile("int $0x15" : "+a"(ax)::"memory", "cc", "bx", "cx", "dx", "si", "di");
    if (a20_on())
        return 1;
    u8 v = inb(0x92); /* "fast A20" */
    if (!(v & 2))
        outb(0x92, (u8)((v | 2) & 0xFE));
    return a20_on();
}

static int vbe(u16 ax_in, u16 bx_in, u16 cx_in, void *buf)
{
    u16 ax = ax_in, bx = bx_in, cx_ = cx_in, di = (u16)(u32)buf;
    asm volatile("int $0x10" : "+a"(ax), "+b"(bx), "+c"(cx_), "+D"(di)::"memory", "cc", "dx", "si");
    return ax == 0x004F;
}

static u16 mi16(int o) { return (u16)(modeinfo[o] | (modeinfo[o + 1] << 8)); }

/* modos aceitos, em ordem de preferencia (as fontes sao geradas para estas alturas) */
static const u16 MODES[3][2] = {{1024, 768}, {800, 600}, {640, 480}};

static int vbe_pick(void)
{
    memset(vbeinfo, 0, sizeof vbeinfo);
    memcpy(vbeinfo, "VBE2", 4);
    if (!vbe(0x4F00, 0, 0, vbeinfo) || vbeinfo[0] != 'V' || vbeinfo[1] != 'E')
        return 0;
    u16 ver = (u16)(vbeinfo[4] | (vbeinfo[5] << 8));
    u32 list = (u32)(vbeinfo[14] | (vbeinfo[15] << 8)) + ((u32)(vbeinfo[16] | (vbeinfo[17] << 8)) << 4);
    int best = 3;
    for (int i = 0; i < 512; i++)
    {
        u16 m = rd16(list + (u32)i * 2);
        if (m == 0xFFFF)
            break;
        if (!vbe(0x4F01, 0, m, modeinfo))
            continue;
        if ((mi16(0) & 0x99) != 0x99) /* suportado, cor, grafico, framebuffer linear */
            continue;
        if (modeinfo[0x19] != 32 || modeinfo[0x1B] != 6)
            continue;
        if (modeinfo[0x20] != 16 || modeinfo[0x22] != 8 || modeinfo[0x24] != 0)
            continue;
        for (int k = 0; k < best; k++)
            if (mi16(0x12) == MODES[k][0] && mi16(0x14) == MODES[k][1])
            {
                best = k;
                vbe_mode = m;
                W = MODES[k][0];
                H = MODES[k][1];
                pitch = (ver >= 0x300 && mi16(0x32)) ? mi16(0x32) : mi16(0x10);
                fbaddr = (u32)mi16(0x28) | ((u32)mi16(0x2A) << 16);
            }
    }
    return best < 3 && fbaddr;
}

/* ------------------------------------------------------------------ tabela polar */
/* angulo de (dx,dy) em 1/65536 de volta: 0 = direita, crescendo no sentido horario */
static u32 atan_ratio(u32 num, u32 den)
{
    u32 r = (num << 16) / den, i = r >> 8, f = r & 255;
    if (i >= 256)
        return atan_tab[256];
    return atan_tab[i] + (((atan_tab[i + 1] - atan_tab[i]) * f) >> 8);
}

static u16 atan2u(s32 dy, s32 dx)
{
    u32 ax = (u32)(dx < 0 ? -dx : dx), ay = (u32)(dy < 0 ? -dy : dy);
    u32 a = ax >= ay ? atan_ratio(ay, ax) : 16384 - atan_ratio(ax, ay);
    if (dx < 0)
        a = 32768 - a;
    if (dy < 0)
        a = 65536 - a;
    return (u16)a;
}

static struct rect ring_box(void)
{
    s32 r = (rmax16 >> 4) + 2;
    struct rect b = {imax(cx - r, 0), imax(cy - r, 0), imin(cx + r, (s32)W), imin(cy + r, (s32)H)};
    return b;
}

/* pass 0: conta pixels por faixa; pass 1: preenche */
static void polar_pass(int fill)
{
    struct rect b = ring_box();
    for (s32 y = b.y0; y < b.y1; y++)
    {
        s32 dy = (y - cy) * 16 + 8;
        for (s32 x = b.x0; x < b.x1; x++)
        {
            s32 dx = (x - cx) * 16 + 8;
            s32 r = (s32)isqrt((u32)(dx * dx + dy * dy));
            if (r < rmin16 || r > rmax16)
                continue;
            u32 bin = (u32)r >> 4;
            if (!fill)
                bin0[bin + 1]++;
            else
            {
                u32 i = binfill[bin]++;
                ptab[i * 2] = (u32)y * W + (u32)x;
                ptab[i * 2 + 1] = ((u32)r << 16) | atan2u(dy, dx);
            }
        }
    }
}

/* ------------------------------------------------------------------ aros */
#define RU(x) ((u16)((x) * 16))                 /* unidades do viewBox, x16 */
#define TURN(x) ((u16)(s32)((x) * 65536.0))     /* fracao de volta */
#define DASHOFF(o, r) TURN((o) / (2 * 3.14159265358979 * (r)))
#define K_ANG 411775                            /* 2*pi em 16.16 */
#define F_ARC 0
#define F_RAD 1

struct feat
{
    u8 grp, kind, glow, alpha;
    u16 r, r2, w; /* raio (ou raio inicial), raio final (radial), espessura */
    u16 n, fill;  /* n segmentos (0 = circulo cheio), fracao preenchida de cada um */
    u16 off;      /* deslocamento do padrao (arco) ou angulo (radial) */
    u32 col;
};

/* Mesma ordem de desenho do SVG da landing (HudRings.jsx) */
static const struct feat FEATS[] = {
    /* reticula */
    {0, F_RAD, 0, 255, RU(266), RU(300), RU(1.5), 0, 0, TURN(0.75), COL_RED},
    {0, F_RAD, 0, 255, RU(266), RU(300), RU(1.5), 0, 0, TURN(0.25), COL_RED},
    {0, F_RAD, 0, 255, RU(222), RU(238), RU(1.5), 0, 0, TURN(0.75), COL_BRIGHT},
    {0, F_RAD, 0, 255, RU(222), RU(238), RU(1.5), 0, 0, TURN(0.25), COL_RED},
    {0, F_ARC, 0, 255, RU(228), 0, RU(1.5), 1, 1279, TURN(0.25) + 640, COL_RED}, /* y=72 */
    {0, F_ARC, 0, 255, RU(228), 0, RU(1.5), 1, 1279, TURN(0.75) + 640, COL_RED}, /* y=528 */
    /* 1 - pontilhado externo */
    {1, F_ARC, 0, A(0.2), RU(292), 0, RU(1), 204, TURN(2.0 / 9), 0, COL_FAINT},
    /* 2 - faixas largas */
    {2, F_ARC, 0, A(0.26), RU(272), 0, RU(8), 4, TURN(0.72), 0, COL_BLUE},
    {2, F_ARC, 0, A(0.2), RU(279), 0, RU(1), 0, 0, 0, COL_FAINT},
    /* 3 - anel fino com arcos vermelhos */
    {3, F_ARC, 0, A(0.75), RU(258), 0, RU(1), 0, 0, 0, COL_BLUE},
    {3, F_ARC, 1, 255, RU(258), 0, RU(3), 1, TURN(0.06), 0, COL_RED},
    {3, F_ARC, 0, 255, RU(258), 0, RU(3), 1, TURN(0.02), DASHOFF(-810, 258), COL_RED},
    /* 4 - arcos azuis brilhantes */
    {4, F_ARC, 1, 255, RU(244), 0, RU(3), 2, TURN(0.17), 0, COL_BRIGHT},
    {4, F_ARC, 0, 255, RU(244), 0, RU(1.5), 2, TURN(0.045), DASHOFF(-140, 244), COL_RED},
    /* 5 - marcacoes */
    {5, F_ARC, 0, A(0.4), RU(230), 0, RU(7), 120, TURN(0.12), 0, COL_BRIGHT},
    {5, F_ARC, 0, A(0.2), RU(224), 0, RU(1), 0, 0, 0, COL_FAINT},
    /* 6 - colchetes vermelhos (oscilam) */
    {6, F_ARC, 1, 255, RU(210), 0, RU(3), 2, TURN(0.11), TURN(0.0275), COL_RED},
    /* 7 - anel interno */
    {7, F_ARC, 0, A(0.75), RU(196), 0, RU(1), 3, TURN(0.9), 0, COL_BLUE},
    /* 8 - parado */
    {8, F_ARC, 0, A(0.2), RU(168), 0, RU(1), 0, 0, 0, COL_FAINT},
};
#define NFEATS (sizeof FEATS / sizeof FEATS[0])

/* atraso de entrada, periodo de rotacao (ms), sentido (1 horario, -1 anti, 2 oscila) */
static const struct
{
    u16 delay;
    u32 period;
    s8 dir;
} GRP[9] = {
    {900, 0, 0}, {0, 140000, 1}, {150, 48000, -1}, {300, 20000, 1}, {450, 28000, -1},
    {600, 90000, 1}, {750, 7000, 2}, {900, 60000, -1}, {1000, 0, 0},
};

struct gstate
{
    u32 sc;  /* escala, 4096 = 1 */
    u16 rot; /* rotacao, 1/65536 volta */
    u32 op;  /* opacidade 0..256 */
};

static void group_state(s32 t, int g, struct gstate *s)
{
    s32 e = ease(ease_out_tab, prog(t, GRP[g].delay, BOOT_MS));
    s->sc = 2048 + (u32)e / 2;                              /* 0.5 -> 1 */
    u32 rot = (u32)(65536 - ((30948 * (4096 - e)) >> 12)); /* -170 graus -> 0 */
    s->op = (u32)(ease(ease_out_tab, prog(t, GRP[g].delay, BOOT_MS * 55 / 100)) * 256) >> 12;
    if (GRP[g].dir == 2)
    { /* oscila entre -28 e +28 graus, 7 s por ida, ease-in-out, vai e volta */
        u32 k = (u32)t / GRP[g].period, f = (u32)t % GRP[g].period;
        s32 p = (s32)(f * 4096 / GRP[g].period);
        if (k & 1)
            p = 4096 - p;
        rot += (u32)(-5097 + ((10194 * ease(ease_io_tab, p)) >> 12));
    }
    else if (GRP[g].period)
    {
        u32 spin = (u32)t * 65536 / GRP[g].period;
        rot += GRP[g].dir > 0 ? spin : (u32)-spin;
    }
    s->rot = (u16)rot;
}

static void draw_feat(const struct feat *f, const struct gstate *g)
{
    u32 opa = (u32)f->alpha * g->op; /* 0..65280 */
    if (!opa)
        return;
    s32 sg = (s32)((S16 * g->sc) >> 12); /* px por unidade, 16.16 */
    s32 hw = (s32)(((u32)f->w * (u32)sg) >> 17);
    s32 r1 = (s32)(((u32)f->r * (u32)sg) >> 16);
    s32 b0, b1;

    if (f->kind == F_RAD)
    {
        s32 r2 = (s32)(((u32)f->r2 * (u32)sg) >> 16);
        u16 th = (u16)(f->off + g->rot);
        b0 = (r1 - 8) >> 4;
        b1 = (r2 + 8) >> 4;
        b0 = imax(b0, rmin16 >> 4);
        b1 = imin(b1, rmax16 >> 4);
        for (s32 b = b0; b <= b1; b++)
            for (u32 i = bin0[b]; i < bin0[b + 1]; i++)
            {
                u32 e = ptab[i * 2 + 1];
                s32 rad = (s32)(e >> 16);
                s32 da = (s16)((u16)e - th);
                if (da < 0)
                    da = -da;
                if (da > 4096)
                    continue;
                s32 dpx = (s32)(((s64)(da * rad) * K_ANG) >> 28); /* distancia perpendicular, 1/256 px */
                u32 cp = clamp256(hw * 16 + 128 - dpx);
                u32 cr = (clamp256((rad - r1 + 8) * 16) * clamp256((r2 - rad + 8) * 16)) >> 8;
                u32 a = (((cp * cr) >> 8) * opa) >> 16;
                if (a)
                {
                    hu32 *p = bb + ptab[i * 2];
                    *p = blend(*p, f->col, a);
                }
            }
        return;
    }

    s32 sig = 0, ext = hw + 8, inv_sig = 0, s256 = 0, rs = 0;
    if (f->glow)
    { /* glow: gaussiana (feGaussianBlur stdDeviation=3) por baixo do traco */
        sig = imax((s32)((RU(3.1) * (u32)sg) >> 16), 8);
        ext = imax(ext, hw + sig * 3);
        inv_sig = (64 << 16) / sig;
        s256 = sig * 16;
        rs = (256 << 16) / (2 * s256);
    }
    b0 = imax((r1 - ext) >> 4, rmin16 >> 4);
    b1 = imin((r1 + ext) >> 4, rmax16 >> 4);
    s32 kn = f->n ? K_ANG / f->n : 0;
    u16 base = (u16)(f->off - g->rot);
    s32 fill = f->fill;
    for (s32 b = b0; b <= b1; b++)
        for (u32 i = bin0[b]; i < bin0[b + 1]; i++)
        {
            u32 e = ptab[i * 2 + 1];
            s32 rad = (s32)(e >> 16);
            s32 dist = rad - r1;
            if (dist < 0)
                dist = -dist;
            u32 cr = clamp256((hw + 8 - dist) * 16);
            u32 gl = 0;
            if (f->glow)
            {
                u32 gi = (u32)(dist * inv_sig) >> 16;
                gl = gi < 256 ? gauss_tab[gi] : 0;
            }
            if (!cr && !gl)
                continue;
            u32 ca = 256, cg = 256;
            if (f->n)
            {
                s32 p = (u16)((u16)((u16)e + base) * f->n);
                s32 d = p < fill ? imin(p, fill - p) : -imin(p - fill, 65536 - p);
                s32 dpx = (s32)(((s64)(d * rad) * kn) >> 28); /* distancia ate a borda, 1/256 px */
                ca = clamp256(dpx + 128);
                if (f->glow)
                { /* glow passa um pouco das pontas do arco */
                    s32 dg = dpx + s256;
                    cg = dg <= 0 ? 0 : dg >= 2 * s256 ? 256 : clamp256((dg * rs) >> 16);
                }
            }
            hu32 *px = bb + ptab[i * 2];
            u32 c = *px;
            if (gl && cg)
                c = blend(c, f->col, (((gl * cg) >> 8) * 102 >> 8) * opa >> 16);
            if (cr && ca)
                c = blend(c, f->col, (((cr * ca) >> 8) * opa) >> 16);
            *px = c;
        }
}

/* ------------------------------------------------------------------ fundo */
static u32 lerp_stops3(u32 c0, u32 c1, u32 c2, s32 mid, s32 t) /* t, mid em 0..4096 */
{
    if (t <= mid)
        return blend(c0, c1, (u32)(t * 256 / mid));
    return blend(c1, c2, (u32)((t - mid) * 256 / (4096 - mid)));
}

static void build_background(void)
{
    hu32 *col = (hu32 *)hmalloc(W * 4); /* termo horizontal das elipses, 1/4096 */
    for (u32 x = 0; x < W; x++)
    {
        s32 d = (s32)(2 * x + 1 - W);
        col[x] = (u32)(d * d) * 2048 / (W * W);
    }
    s32 rc16 = (s32)((250 * S16) >> 12); /* raio do nucleo */
    for (u32 y = 0; y < H; y++)
    {
        s32 d = (s32)(2 * y + 1 - H);
        u32 row = (u32)(d * d) * 2048 / (H * H);           /* elipse centralizada */
        u32 rowt = (2 * y + 1) * (2 * y + 1) * 512 / (H * H); /* elipse no topo */
        s32 dy16 = ((s32)y - cy) * 16 + 8;
        for (u32 x = 0; x < W; x++)
        {
            u32 c = COL_BG;
            /* mascara radial da grade: opaca ate 30%, some em 85% */
            s32 rho = (s32)isqrt((col[x] + row) * 4096);
            u32 m = clamp256((3482 - rho) * 256 / 2253);
            if (m)
            {
                if ((x & 63) == 0)
                    c = blend(c, COL_BLUE, (9 * m) >> 8);
                if ((y & 63) == 0)
                    c = blend(c, COL_BLUE, (9 * m) >> 8);
                s32 rt = (s32)isqrt((col[x] + rowt) * 4096);
                u32 gt = clamp256((2458 - rt) * 256 / 2458); /* brilho no topo, ate 60% */
                c = blend(c, COL_BLUE, (((gt * 20) >> 8) * m) >> 8);
            }
            /* nucleo: gradiente radial atras dos aros */
            s32 dx16 = ((s32)x - cx) * 16 + 8;
            if (dx16 > -rc16 && dx16 < rc16 && dy16 > -rc16 && dy16 < rc16)
            {
                s32 r = (s32)isqrt((u32)(dx16 * dx16 + dy16 * dy16));
                if (r < rc16)
                {
                    s32 u = r * 4096 / rc16;
                    u32 cc = lerp_stops3(0x0D1A2B, 0x060B13, 0x030508, 2458, u);
                    s32 al = u <= 2458 ? 230 - (77 * u) / 2458 : 153 - (153 * (u - 2458)) / 1638;
                    c = blend(c, cc, (u32)al);
                }
            }
            bgb[y * W + x] = c;
        }
    }
}

/* ------------------------------------------------------------------ HUD */
static void hline(s32 x0, s32 x1, s32 y, u32 col, u32 a, hu32 *buf) /* [x0, x1) */
{
    if (y < 0 || y >= (s32)H)
        return;
    x0 = imax(x0, 0);
    x1 = imin(x1, (s32)W);
    for (s32 x = x0; x < x1; x++)
        buf[(u32)y * W + (u32)x] = blend(buf[(u32)y * W + (u32)x], col, a);
}

static void vline(s32 x, s32 y0, s32 y1, u32 col, u32 a, hu32 *buf)
{
    if (x < 0 || x >= (s32)W)
        return;
    y0 = imax(y0, 0);
    y1 = imin(y1, (s32)H);
    for (s32 y = y0; y < y1; y++)
        buf[(u32)y * W + (u32)x] = blend(buf[(u32)y * W + (u32)x], col, a);
}

static void plot(s32 x, s32 y, u32 col, u32 a, hu32 *buf)
{
    if (x >= 0 && y >= 0 && x < (s32)W && y < (s32)H && a)
        buf[(u32)y * W + (u32)x] = blend(buf[(u32)y * W + (u32)x], col, a);
}

/* linha com antialiasing (Wu), so para |dx| >= |dy|; coordenadas em 1/16 px; recorta em [cx0, cx1) */
static void aaline(s32 x0, s32 y0, s32 x1, s32 y1, u32 col, u32 a, s32 cx0, s32 cx1, hu32 *buf)
{
    if (x0 > x1)
    {
        s32 t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
    }
    if (x1 == x0)
        return;
    s32 slope = ((y1 - y0) << 16) / (x1 - x0); /* 16.16 */
    for (s32 x = (x0 + 8) >> 4; x < (x1 + 8) >> 4; x++)
    {
        if (x < cx0 || x >= cx1)
            continue;
        s32 y = y0 + (s32)(((s64)(x * 16 + 8 - x0) * slope) >> 16) - 8; /* 1/16 px */
        s32 yi = y >> 4, f = (y & 15) * 16;
        plot(x, yi, col, (a * (u32)(256 - f)) >> 8, buf);
        plot(x, yi + 1, col, (a * (u32)f) >> 8, buf);
    }
}

static void draw_corners(u32 op, hu32 *buf)
{
    u32 a = (115 * op) >> 8, ar = op;
    s32 cw = imin((s32)W * 16 / 100, 220), ch = imin((s32)H * 18 / 100, 170);
    s32 l = 24, r = (s32)W - 25, t = 24, b = (s32)H - 25;
    s32 il = 39, ir = (s32)W - 40, it = 39, ib = (s32)H - 40;
    /* L azul externo */
    hline(l, l + cw, t, COL_BRIGHT, a, buf);
    vline(l, t + 1, t + ch, COL_BRIGHT, a, buf);
    hline(r - cw + 1, r + 1, t, COL_BRIGHT, a, buf);
    vline(r, t + 1, t + ch, COL_BRIGHT, a, buf);
    hline(l, l + cw, b, COL_BRIGHT, a, buf);
    vline(l, b - ch + 1, b, COL_BRIGHT, a, buf);
    hline(r - cw + 1, r + 1, b, COL_BRIGHT, a, buf);
    vline(r, b - ch + 1, b, COL_BRIGHT, a, buf);
    /* L vermelho interno, 28 px */
    hline(il, il + 28, it, COL_RED, ar, buf);
    vline(il, it + 1, it + 28, COL_RED, ar, buf);
    hline(ir - 27, ir + 1, it, COL_RED, ar, buf);
    vline(ir, it + 1, it + 28, COL_RED, ar, buf);
    hline(il, il + 28, ib, COL_RED, ar, buf);
    vline(il, ib - 27, ib, COL_RED, ar, buf);
    hline(ir - 27, ir + 1, ib, COL_RED, ar, buf);
    vline(ir, ib - 27, ib, COL_RED, ar, buf);
}

static void draw_sideticks(u32 op, hu32 *buf)
{
    u32 a = (128 * op) >> 8;
    s32 y0 = (s32)H * 28 / 100, y1 = (s32)H - y0;
    for (s32 y = y0; y < y1; y++)
        if ((y - y0) % 12 < 2)
        {
            hline(40, 42, y, COL_BRIGHT, a, buf);
            hline((s32)W - 42, (s32)W - 40, y, COL_BRIGHT, a, buf);
        }
}

/* Conectores laterais: path do SVG 100x40 esticado (50vw x 20% do anel), p = varredura 0..4096 */
static void draw_connectors(s32 p, hu32 *buf)
{
    if (p <= 0)
        return;
    s32 half = (s32)W * 8;                   /* 50vw em 1/16 px */
    s32 reach = (s32)(((s64)half * p) >> 12); /* parte ja revelada */
    for (int side = 0; side < 2; side++)
    {
        s32 dir = side ? 1 : -1; /* -1: esquerda (cresce para fora = x menor) */
        s32 x100 = cx * 16 + dir * ((s32)D * 8 - (s32)D * 16 * 4 / 100); /* ponta colada no anel */
#define CX(u) (x100 + dir * (100 - (u)) * half / 100)
#define CY(v) (cy * 16 + ((v) - 20) * (s32)D * 16 / 200)
        s32 lim = x100 + dir * reach;
        s32 c0 = side ? x100 >> 4 : lim >> 4, c1 = side ? (lim >> 4) + 1 : (x100 >> 4) + 1;
        for (int k = 0; k < 2; k++)
        {
            s32 vy = k ? 23 : 17, vo = k ? 34 : 6;
            s32 yn = (CY(vy) + 8) >> 4, yo = (CY(vo) + 8) >> 4;
            s32 a0 = CX(100) >> 4, a1 = CX(72) >> 4;
            hline(imax(imin(a0, a1), c0), imin(imax(a0, a1) + 1, c1), yn, COL_BRIGHT, 115, buf);
            aaline(CX(72), yn * 16 + 8, CX(62), yo * 16 + 8, COL_BRIGHT, 115, c0, c1, buf);
            a0 = CX(62) >> 4;
            a1 = CX(0) >> 4;
            hline(imax(imin(a0, a1), c0), imin(imax(a0, a1) + 1, c1), yo, COL_BRIGHT, 115, buf);
        }
        s32 a0 = CX(100) >> 4, a1 = CX(90) >> 4; /* trecho vermelho de 2 px */
        hline(imax(imin(a0, a1), c0), imin(imax(a0, a1) + 1, c1), cy - 1, COL_RED, 256, buf);
        hline(imax(imin(a0, a1), c0), imin(imax(a0, a1) + 1, c1), cy, COL_RED, 256, buf);
#undef CX
#undef CY
    }
}

/* ------------------------------------------------------------------ texto */
static const struct glyph *glyph_of(const struct font *f, char ch)
{
    for (int i = 0; f->chars[i]; i++)
        if (f->chars[i] == ch)
            return &f->g[i];
    return 0;
}

static s32 text_width(const struct font *f, const char *s, s32 ls16)
{
    s32 w = 0;
    for (; *s; s++)
    {
        const struct glyph *g = glyph_of(f, *s);
        w += (g ? g->adv : f->g[0].adv) + ls16;
    }
    return w;
}

/* texto mono sem escala, alfa de 4 bits direto da fonte */
static void draw_text(const struct font *f, const char *s, s32 x16, s32 base, s32 ls16, u32 col, u32 opa)
{
    for (; *s; s++)
    {
        const struct glyph *g = glyph_of(f, *s);
        if (!g)
        {
            x16 += f->g[0].adv + ls16;
            continue;
        }
        s32 gx = ((x16 + 8) >> 4) + g->bx, gy = base + g->by, rb = (g->w + 1) / 2;
        for (s32 y = 0; y < g->h; y++)
            for (s32 x = 0; x < g->w; x++)
            {
                u8 v = f->data[g->off + y * rb + x / 2];
                v = (x & 1) ? v >> 4 : v & 15;
                if (v)
                    plot(gx + x, gy + y, col, (v * 17 * opa) >> 8, bb);
            }
        x16 += g->adv + ls16;
    }
}

/* Glifo como mapa de alfa de 8 bits na memoria alta, com margem para o blur */
struct gmap
{
    hu8 *a, *b, *g; /* nitido, blur forte (12 px), blur medio (glow final) */
    s32 w, h;
    s32 x16, y16; /* canto do mapa em relacao ao pen/baseline */
};

static void make_map(struct gmap *m, const struct font *f, char ch, s32 pad)
{
    const struct glyph *g = glyph_of(f, ch);
    m->w = g->w + 2 * pad;
    m->h = g->h + 2 * pad;
    m->x16 = (g->bx - pad) * 16;
    m->y16 = (g->by - pad) * 16;
    m->a = (hu8 *)hmalloc((u32)(m->w * m->h));
    m->b = m->g = 0;
    for (s32 i = 0; i < m->w * m->h; i++)
        m->a[i] = 0;
    s32 rb = (g->w + 1) / 2;
    for (s32 y = 0; y < g->h; y++)
        for (s32 x = 0; x < g->w; x++)
        {
            u8 v = f->data[g->off + y * rb + x / 2];
            m->a[(y + pad) * m->w + x + pad] = (u8)(((x & 1) ? v >> 4 : v & 15) * 17);
        }
}

/* blur gaussiano aproximado: 3 passadas de caixa (raio ~ sigma) */
static hu8 *blur_map(const struct gmap *m, s32 r)
{
    s32 n = m->w * m->h;
    hu8 *d = (hu8 *)hmalloc((u32)n), *t = (hu8 *)hmalloc((u32)n);
    for (s32 i = 0; i < n; i++)
        d[i] = m->a[i];
    u32 rec = 65536 / (u32)(2 * r + 1) + 1;
    for (int pass = 0; pass < 3; pass++)
        for (int dir = 0; dir < 2; dir++)
        {
            hu8 *src = dir ? t : d, *dst = dir ? d : t;
            s32 len = dir ? m->h : m->w, lines = dir ? m->w : m->h;
            s32 step = dir ? m->w : 1, lstep = dir ? 1 : m->w;
            for (s32 l = 0; l < lines; l++)
            {
                hu8 *s = src + l * lstep, *o = dst + l * lstep;
                u32 sum = 0;
                for (s32 i = 0; i < r && i < len; i++)
                    sum += s[i * step];
                for (s32 i = 0; i < len; i++)
                {
                    if (i + r < len)
                        sum += s[(i + r) * step];
                    o[i * step] = (u8)((sum * rec) >> 16);
                    if (i - r >= 0)
                        sum -= s[(i - r) * step];
                }
            }
        }
    return d;
}

struct fx
{
    u32 opa;        /* 0..256 */
    u32 mixb;       /* nitido -> blur (0..256) */
    u32 gmix;       /* glow: blur forte -> medio (0..256) */
    u32 galpha;     /* intensidade do glow (0..256) */
    u32 gcol, col;  /* cor do glow e cor solida */
    int grad;       /* 1 = gradiente prata vertical */
    s32 gtop, gh;   /* faixa do gradiente (local, 1/16 px) */
};

/* Desenha um mapa com escala k (4096 = 1) em torno do pivo (local); origem local em (ox, oy) na tela */
static void draw_map(const struct gmap *m, s32 ox, s32 oy, s32 pvx, s32 pvy, s32 k, const struct fx *e)
{
    if (k < 64 || !e->opa)
        return;
    s32 sx0 = (ox + pvx + (s32)(((s64)(m->x16 - pvx) * k) >> 12)) >> 4;
    s32 sy0 = (oy + pvy + (s32)(((s64)(m->y16 - pvy) * k) >> 12)) >> 4;
    s32 sx1 = ((ox + pvx + (s32)(((s64)(m->x16 + m->w * 16 - pvx) * k) >> 12)) >> 4) + 2;
    s32 sy1 = ((oy + pvy + (s32)(((s64)(m->y16 + m->h * 16 - pvy) * k) >> 12)) >> 4) + 2;
    sx0 = imax(sx0, 0);
    sy0 = imax(sy0, 0);
    sx1 = imin(sx1, (s32)W);
    sy1 = imin(sy1, (s32)H);
    s32 invk = (s32)((1u << 28) / (u32)k); /* 16.16 */
    s32 step = invk >> 8;                   /* texels (1/256) por pixel de tela */
    s32 u0 = (s32)(((s64)(sx0 * 16 + 8 - ox - pvx) * invk) >> 16) + pvx - m->x16; /* 1/16 */
    u0 = u0 * 16 - 128;
    for (s32 sy = sy0; sy < sy1; sy++)
    {
        s32 lv = (s32)(((s64)(sy * 16 + 8 - oy - pvy) * invk) >> 16) + pvy; /* y local, 1/16 */
        s32 v = (lv - m->y16) * 16 - 128;
        s32 iv = v >> 8, fv = v & 255;
        if (iv < 0 || iv >= m->h - 1)
            continue;
        u32 col = e->col;
        if (e->grad)
            col = lerp_stops3(0xFFFFFF, 0xD7DFE9, 0x8D9BB0, 1843, (s32)clamp256((lv - e->gtop) * 256 / e->gh) * 16);
        s32 u = u0;
        hu32 *row = bb + (u32)sy * W;
        for (s32 sx = sx0; sx < sx1; sx++, u += step)
        {
            s32 iu = u >> 8, fu = u & 255;
            if (iu < 0 || iu >= m->w - 1)
                continue;
            s32 o = iv * m->w + iu;
            u32 w00 = (u32)((256 - fu) * (256 - fv)), w10 = (u32)(fu * (256 - fv));
            u32 w01 = (u32)((256 - fu) * fv), w11 = (u32)(fu * fv);
#define SAMPLE(mp) (((mp)[o] * w00 + (mp)[o + 1] * w10 + (mp)[o + m->w] * w01 + (mp)[o + m->w + 1] * w11) >> 16)
            s32 sa = (s32)SAMPLE(m->a), sb = m->b ? (s32)SAMPLE(m->b) : 0, sg = sb;
            if (!m->b)
                sg = m->g ? (s32)SAMPLE(m->g) : 0;
            else if (m->g && e->gmix)
                sg = sb + (((s32)SAMPLE(m->g) - sb) * (s32)e->gmix >> 8);
#undef SAMPLE
            u32 body = (u32)(sa + (((sb - sa) * (s32)e->mixb) >> 8));
            u32 c = row[sx];
            u32 ga = ((((u32)sg * e->galpha) >> 8) * e->opa) >> 8;
            if (ga)
                c = blend(c, e->gcol, ga);
            u32 ba = (body * e->opa) >> 8;
            if (ba)
                c = blend(c, col, ba + (ba >> 7));
            row[sx] = c;
        }
    }
}

/* ------------------------------------------------------------------ layout do texto */
static const struct font *ft_title, *ft_team;
static struct gmap m_letter[5], m_dot, m_team[4];
static s32 lx[5], lw[5], dx_[4], dw_; /* caixas das letras e pontos (1/16 px) */
static s32 word_top, word_base;      /* caixa do N.E.X.U.S (topo e baseline, 1/16 px) */
static s32 team_x, team_base;
static s32 acr_base[2], acr_len[2];
static const char *acr_str[2];
static s32 status_y;                 /* centro da linha de status (px) */

static const char ACRONYM[] = "NETWORKED EXPERIENTIAL INTELLIGENCE & USER SYSTEM";
static const char WORD[] = "NEXUS";

static void layout(void)
{
    s32 F = ft_title->size16, fT = ft_team->size16;
    s32 teamlh = fT * 155 / 100, acrlh = fnt_mono_small.size16 * 155 / 100;
    s32 ls_acr = fnt_mono_small.size16 * 12 / 100;

    /* sigla: quebra em ate 2 linhas dentro de 78% do anel */
    s32 maxw = (s32)D * 16 * 78 / 100, n = (s32)strlen(ACRONYM);
    acr_str[0] = ACRONYM;
    acr_len[0] = n;
    acr_len[1] = 0;
    if (text_width(&fnt_mono_small, ACRONYM, ls_acr) > maxw)
    {
        s32 cut = 0;
        static char tmp[64];
        for (s32 i = 0; i < n; i++)
            if (ACRONYM[i] == ' ')
            {
                memcpy(tmp, ACRONYM, (u32)i);
                tmp[i] = 0;
                if (text_width(&fnt_mono_small, tmp, ls_acr) <= maxw)
                    cut = i;
            }
        acr_len[0] = cut;
        acr_str[1] = ACRONYM + cut + 1;
        acr_len[1] = n - cut - 1;
    }
    s32 lines = acr_len[1] ? 2 : 1;

    s32 mb = (s32)D * 16 * 2 / 100, mt = (s32)D * 16 * 3 / 100;
    s32 total = teamlh + mb + F + mt + acrlh * lines;
    s32 top = cy * 16 - total / 2;

    team_base = top + (teamlh - ft_team->asc16 - ft_team->desc16) / 2 + ft_team->asc16;
    word_top = top + teamlh + mb;
    word_base = word_top + (F - ft_title->asc16 - ft_title->desc16) / 2 + ft_title->asc16;
    for (s32 l = 0; l < lines; l++)
    {
        s32 lt = word_top + F + mt + acrlh * l;
        acr_base[l] = (lt + (acrlh - fnt_mono_small.asc16 - fnt_mono_small.desc16) / 2 + fnt_mono_small.asc16 + 8) >> 4;
    }

    /* N.E.X.U.S: letter-spacing 0.04em, pontos com margem 0.12em */
    s32 ls = F * 4 / 100, mg = F * 12 / 100, x = 0;
    const struct glyph *gd = glyph_of(ft_title, '.');
    dw_ = gd->adv + ls;
    for (int i = 0; i < 5; i++)
    {
        lx[i] = x;
        lw[i] = glyph_of(ft_title, WORD[i])->adv + ls;
        x += lw[i];
        if (i < 4)
        {
            dx_[i] = x + mg;
            x += mg + dw_ + mg;
        }
    }
    s32 x0 = cx * 16 - x / 2;
    for (int i = 0; i < 5; i++)
        lx[i] += x0;
    for (int i = 0; i < 4; i++)
        dx_[i] += x0;

    /* TEAM: letter-spacing 0.7em, margin-right -0.7em */
    team_x = cx * 16 - (text_width(ft_team, "TEAM", fT * 70 / 100) - fT * 70 / 100) / 2;

    status_y = cy + (s32)D / 2 + 16;
}

static void draw_title(s32 t)
{
    s32 F = ft_title->size16;
    struct fx e;

    /* TEAM: fade + sobe 8 px */
    s32 p = prog(t, T_ONLINE, 800);
    if (p)
    {
        e.opa = (u32)(ease(ease_tab, p) * 256) >> 12;
        e.mixb = e.gmix = e.galpha = 0;
        e.gcol = 0;
        e.col = COL_SILVER;
        e.grad = 0;
        s32 ty = (8 * 16 * (4096 - ease(ease_out_tab, p))) >> 12;
        s32 x = team_x, ls = ft_team->size16 * 70 / 100;
        for (int i = 0; i < 4; i++)
        {
            draw_map(&m_team[i], x, team_base + ty, 0, 0, 4096, &e);
            x += glyph_of(ft_team, "TEAM"[i])->adv + ls;
        }
    }

    /* letras: escala 1.25 -> 1, sobe 0.25em, blur 12px -> 0, pico de brilho azul aos 60% */
    for (int i = 0; i < 5; i++)
    {
        s32 st = T_ONLINE + i * 100;
        if (t < st)
            continue;
        s32 u = prog(t, st, 900), tr = ease(ease_out_tab, u);
        s32 k = 5120 - ((1024 * tr) >> 12);
        s32 ty = ((F / 4) * (4096 - tr)) >> 12;
        e.gcol = COL_BRIGHT;
        e.grad = 1;
        e.gtop = -(word_base - word_top);
        e.gh = F;
        if (u < 2458)
        {
            s32 q = ease(ease_out_tab, u * 4096 / 2458);
            e.opa = (u32)(q * 256) >> 12;
            e.mixb = (u32)((4096 - q) * 256) >> 12;
            e.galpha = (u32)(230 * q) >> 12;
            e.gmix = 0;
        }
        else
        {
            s32 q = ease(ease_out_tab, (u - 2458) * 4096 / 1638);
            e.opa = 256;
            e.mixb = 0;
            e.galpha = (u32)(230 - ((166 * q) >> 12));
            e.gmix = (u32)(q * 256) >> 12;
        }
        draw_map(&m_letter[i], lx[i], word_base + ty, lw[i] / 2, -(word_base - word_top) + F / 2, k, &e);
    }

    /* pontos: escala 0 -> 1.6 -> 1 com glow vermelho */
    for (int i = 0; i < 4; i++)
    {
        s32 st = T_ONLINE + 550 + i * 80;
        if (t < st)
            continue;
        s32 u = prog(t, st, 500), k, q;
        if (u < 2867)
        {
            q = ease(ease_out_tab, u * 4096 / 2867);
            e.opa = (u32)(q * 256) >> 12;
            k = (q * 6554) >> 12;
        }
        else
        {
            q = ease(ease_out_tab, (u - 2867) * 4096 / 1229);
            e.opa = 256;
            k = 6554 - ((2458 * q) >> 12);
        }
        e.mixb = e.gmix = 0;
        e.galpha = 205;
        e.gcol = e.col = COL_RED;
        e.grad = 0;
        draw_map(&m_dot, dx_[i], word_base, dw_ / 2, -(word_base - word_top) + F / 2, k, &e);
    }

    /* sigla */
    p = prog(t, T_ACRONYM, 1000);
    if (p)
    {
        u32 op = (u32)(ease(ease_tab, p) * 256) >> 12;
        s32 ls = fnt_mono_small.size16 * 12 / 100;
        for (int l = 0; l < 2 && acr_len[l]; l++)
        {
            static char line[64];
            memcpy(line, acr_str[l], (u32)acr_len[l]);
            line[acr_len[l]] = 0;
            s32 w = text_width(&fnt_mono_small, line, ls);
            draw_text(&fnt_mono_small, line, cx * 16 - w / 2, acr_base[l], ls, COL_MUTED, op);
        }
    }
}

static void draw_status(s32 t)
{
    const char *s = t < T_ONLINE ? "INICIALIZANDO SISTEMAS~" : "SISTEMA ONLINE";
    const struct font *f = &fnt_mono_status;
    s32 ls = f->size16 * 4 / 100;
    s32 tw = text_width(f, s, ls), total = tw + (6 + 8) * 16;
    s32 x = cx * 16 - total / 2;
    s32 base = status_y + ((f->asc16 - f->desc16) >> 5);
    draw_text(f, s, x + 14 * 16, base, ls, COL_MUTED, 256);

    /* ponto: vermelho piscando (steps) ou azul pulsando devagar */
    u32 op, col;
    if (t < T_ONLINE)
    {
        s32 c = t % 900;
        op = c < 225 ? 256 : c < 450 ? 160 : c < 675 ? 64 : 160;
        col = COL_RED;
    }
    else
    {
        s32 c = (t - T_ONLINE) % 2400;
        s32 p = c < 1200 ? c * 4096 / 1200 : (2400 - c) * 4096 / 1200;
        op = (u32)(256 - ((192 * ease(ease_io_tab, p)) >> 12));
        col = COL_BRIGHT;
    }
    s32 dcx = x + 3 * 16, dcy = status_y * 16;
    for (s32 y = (dcy >> 4) - 16; y <= (dcy >> 4) + 16; y++)
        for (s32 xx = (dcx >> 4) - 16; xx <= (dcx >> 4) + 16; xx++)
        {
            s32 ddx = xx * 16 + 8 - dcx, ddy = y * 16 + 8 - dcy;
            s32 d = (s32)isqrt((u32)(ddx * ddx + ddy * ddy));
            s32 gi = imax(d - 48, 0) * 64 / 80; /* box-shadow 10px: sigma 5 */
            u32 sh = gi < 256 ? (gauss_tab[gi] * 150) >> 8 : 0;
            u32 a = clamp256((56 - d) * 16);
            u32 aa = a > sh ? a : sh;
            plot(xx, y, col, (aa * op) >> 8, bb);
        }
}

/* ------------------------------------------------------------------ apresentacao */
static void rect_copy(hu32 *dst, const hu32 *src, struct rect r)
{
    for (s32 y = r.y0; y < r.y1; y++)
    {
        u32 o = (u32)y * W;
        for (s32 x = r.x0; x < r.x1; x++)
            dst[o + (u32)x] = src[o + (u32)x];
    }
}

static void present(struct rect r, u32 fade)
{
    for (s32 y = r.y0; y < r.y1; y++)
    {
        hu32 *s = bb + (u32)y * W;
        hu32 *d = (hu32 *)(fbaddr + (u32)y * pitch);
        if (fade >= 256)
            for (s32 x = r.x0; x < r.x1; x++)
                d[x] = s[x];
        else
            for (s32 x = r.x0; x < r.x1; x++)
            {
                u32 p = s[x];
                d[x] = ((((p & 0xFF00FF) * fade) >> 8) & 0xFF00FF) | ((((p & 0xFF00) * fade) >> 8) & 0xFF00);
            }
    }
}

static void render(s32 t, int full, int baked, u32 fade)
{
    struct rect rs[8];
    int n = 0;
    struct rect scr = {0, 0, (s32)W, (s32)H};
    struct rect rb = ring_box();
    rb.y1 = imin(status_y + 20, (s32)H);
    if (full)
        rs[n++] = scr;
    else
    {
        rs[n++] = rb;
        if (!baked)
        {
            s32 cw = imin((s32)W * 16 / 100, 220) + 26, ch = imin((s32)H * 18 / 100, 170) + 26;
            rs[n++] = (struct rect){0, 0, (s32)W, ch};                 /* cantos de cima */
            rs[n++] = (struct rect){0, (s32)H - ch, (s32)W, (s32)H};   /* cantos de baixo */
            rs[n++] = (struct rect){0, ch, cw, (s32)H - ch};           /* laterais */
            rs[n++] = (struct rect){(s32)W - cw, ch, (s32)W, (s32)H - ch};
            s32 hh = (s32)D / 10 + 3; /* faixa dos conectores */
            rs[n++] = (struct rect){0, cy - hh, (s32)W, cy + hh};
        }
    }
    for (int i = 0; i < n; i++)
        rect_copy(bb, bgb, rs[i]);

    if (!baked)
    {
        draw_corners((u32)(ease(ease_tab, prog(t, 400, 1000)) * 256) >> 12, bb);
        draw_sideticks((u32)(ease(ease_tab, prog(t, 800, 1000)) * 256) >> 12, bb);
        draw_connectors(ease(ease_out_tab, prog(t, 1100, 1400)), bb);
    }

    struct gstate gs[9];
    for (int g = 0; g < 9; g++)
        group_state(t, g, &gs[g]);
    for (u32 i = 0; i < NFEATS; i++)
        draw_feat(&FEATS[i], &gs[FEATS[i].grp]);

    draw_title(t);
    draw_status(t);

    if (fade < 256)
        present(scr, fade);
    else
        for (int i = 0; i < n; i++)
            present(rs[i], 256);
}

/* ------------------------------------------------------------------ entrada */
static void build_tables(void)
{
    memcpy(binfill, bin0, sizeof binfill);
    polar_pass(1);
    build_background();
    layout();
    for (int i = 0; i < 5; i++)
    {
        make_map(&m_letter[i], ft_title, WORD[i], 36);
        m_letter[i].b = blur_map(&m_letter[i], 12);
        m_letter[i].g = blur_map(&m_letter[i], 7);
    }
    make_map(&m_dot, ft_title, '.', 18);
    m_dot.g = blur_map(&m_dot, 6);
    for (int i = 0; i < 4; i++)
        make_map(&m_team[i], ft_team, "TEAM"[i], 2);
}

static int setup(void)
{
    enter_unreal();
    if (!a20_enable())
        return 0;
    if (!vbe_pick())
        return 0;

    if (H == 768)
    {
        ft_title = &fnt_title_768;
        ft_team = &fnt_team_768;
    }
    else if (H == 600)
    {
        ft_title = &fnt_title_600;
        ft_team = &fnt_team_600;
    }
    else
    {
        ft_title = &fnt_title_480;
        ft_team = &fnt_team_480;
    }
    D = H * 64 / 100;
    S16 = D * 65536 / 600;
    cx = (s32)W / 2;
    cy = (s32)H / 2;
    rmin16 = (s32)((80 * S16) >> 12);
    rmax16 = (s32)((303 * S16) >> 12);
    if ((rmax16 >> 4) >= MAXBIN)
        return 0;

    /* memoria alta: back buffer, fundo, tabela polar, glifos */
    heap = 0x200000;
    bb = (hu32 *)hmalloc(W * H * 4);
    bgb = (hu32 *)hmalloc(W * H * 4);
    memset(bin0, 0, sizeof bin0);
    polar_pass(0);
    for (u32 b = 0; b <= MAXBIN; b++)
        bin0[b + 1] += bin0[b];
    ptab = (hu32 *)hmalloc(bin0[MAXBIN + 1] * 8);
    if (heap + 0x100000 > 0x100000 + ext_mem_kb() * 1024) /* + ~1 MB para glifos e blur */
        return 0;

    build_tables(); /* ainda no modo texto: a tela de boot continua visivel enquanto calcula */

    /* tudo certo: troca para o modo grafico */
    if (!vbe(0x4F02, (u16)(vbe_mode | 0x4000), 0, 0))
        return 0;
    enter_unreal();
    return 1;
}
void nexus_intro_play(void)
{
    if (!setup())
        return;
    outb(0x43, 0x34); /* PIT canal 0: modo 2, divisor 65536 (mesma taxa de IRQ) */
    outb(0x40, 0);
    outb(0x40, 0);
    while (key_hit())
        getkey();

    u32 t0 = pit_now();
    int baked = 0, first = 1;
    for (;;)
    {
        u32 fs = pit_now();
        s32 t = (s32)((fs - t0) / 1193);
        if (t >= T_END)
            break;
        if (key_hit())
        {
            getkey(); /* qualquer tecla pula */
            break;
        }
        enter_unreal(); /* garante o FS de 4 GB mesmo se alguma rotina da BIOS mexer nele */
        int full = first;
        if (!baked && t >= 2600)
        { /* HUD terminou: vira parte do fundo, so o anel continua sendo redesenhado */
            draw_corners(256, bgb);
            draw_sideticks(256, bgb);
            draw_connectors(4096, bgb);
            baked = full = 1;
        }
        u32 fade = t < T_FADE ? 256 : 256 - (u32)(ease(ease_tab, prog(t, T_FADE, T_END - T_FADE)) * 256 >> 12);
        render(t, full, baked, fade);
        first = 0;
        while (pit_now() - fs < 19886) /* ~60 fps */
            asm volatile("pause");
    }

    outb(0x43, 0x36); /* PIT de volta ao modo 3 padrao da BIOS */
    outb(0x40, 0);
    outb(0x40, 0);
    vid_init(); /* volta ao modo texto 80x25 */
}
