#!/usr/bin/env python3
"""Gera build/assets.h para a intro NEXUS (src/intro.c).

Uso: mkassets.py <fontes_dir> <saida.h>

- Glifos (alfa de 4 bits, antialiasing por supersampling 4x) da Michroma (titulo/TEAM,
  um tamanho por modo de video) e da JetBrains Mono (textos pequenos). Ambas OFL.
- Tabelas em ponto fixo: atan, curvas de easing (cubic-bezier) e gaussiana.
Precisa de Pillow (pip install pillow / apt install python3-pil).
"""
import math, sys, os
from PIL import Image, ImageDraw, ImageFont

fdir, out = sys.argv[1:3]
MICHROMA = os.path.join(fdir, "Michroma-Regular.ttf")
MONO = os.path.join(fdir, "JetBrainsMono-Regular.ttf")
SS = 4  # supersampling

# Alturas de tela suportadas pela intro (precisa bater com MODES em intro.c)
HEIGHTS = (768, 600, 480)

# Textos mono usados na intro ('~' = reticencias)
MONO_TEXT = ("INICIALIZANDO SISTEMAS~", "SISTEMA ONLINE", "ENCERRANDO SISTEMAS~",
             "NETWORKED EXPERIENTIAL INTELLIGENCE & USER SYSTEM")
MONO_CHARS = "".join(sorted(set("".join(MONO_TEXT)) - {" "}))


def render_font(path, size, chars, name):
    """Renderiza `chars` em `size` px (float). Retorna codigo C."""
    f = ImageFont.truetype(path, round(size * SS))
    asc, desc = f.getmetrics()
    data = bytearray()
    glyphs = []
    for ch in chars:
        real = "…" if ch == "~" else ch
        pad = SS * 8
        w4 = int(f.getlength(real)) + 2 * pad
        h4 = asc + desc + 2 * pad
        w4 += -w4 % SS
        h4 += -h4 % SS
        im = Image.new("L", (w4, h4), 0)
        # pen e baseline em multiplos de SS: alinhamento exato apos reduzir
        base4 = pad + asc - asc % SS
        ImageDraw.Draw(im).text((pad, base4), real, font=f, fill=255, anchor="ls")
        sm = im.reduce(SS)
        bb = sm.getbbox()
        adv16 = round(f.getlength(real) / SS * 16)
        if bb is None:
            glyphs.append((len(data), 0, 0, 0, 0, adv16))
            continue
        x0, y0, x1, y1 = bb
        g = sm.crop(bb)
        w, h = g.size
        px = g.load()
        off = len(data)
        for y in range(h):
            row = [(px[x, y] * 15 + 127) // 255 for x in range(w)]
            if w % 2:
                row.append(0)
            for i in range(0, len(row), 2):
                data.append(row[i] | (row[i + 1] << 4))
        bx = x0 - pad // SS
        by = y0 - base4 // SS  # topo em relacao a baseline (negativo = acima)
        glyphs.append((off, w, h, bx, by, adv16))
    c = [f"static const u8 {name}_data[{len(data)}] = {{"]
    for i in range(0, len(data), 24):
        c.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    c.append("};")
    c.append(f"static const struct glyph {name}_g[{len(glyphs)}] = {{")
    for gl in glyphs:
        c.append("    {%d, %d, %d, %d, %d, %d}," % gl)
    c.append("};")
    chars_c = chars.replace("\\", "\\\\").replace('"', '\\"')
    c.append(f'static const struct font {name} = {{{name}_g, {name}_data, "{chars_c}", '
             f"{round(size * 16)}, {round(asc / SS * 16)}, {round(desc / SS * 16)}}};")
    return "\n".join(c)


def bezier_table(x1, y1, x2, y2, n=256, scale=4096):
    def bx(t): return 3 * x1 * t * (1 - t) ** 2 + 3 * x2 * t * t * (1 - t) + t ** 3
    def by(t): return 3 * y1 * t * (1 - t) ** 2 + 3 * y2 * t * t * (1 - t) + t ** 3
    vals = []
    for i in range(n + 1):
        x = i / n
        lo, hi = 0.0, 1.0
        for _ in range(60):
            mid = (lo + hi) / 2
            if bx(mid) < x:
                lo = mid
            else:
                hi = mid
        vals.append(round(by((lo + hi) / 2) * scale))
    return vals


def table(name, ctype, vals):
    lines = [f"static const {ctype} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), 16):
        lines.append("    " + ",".join(str(v) for v in vals[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


parts = ["/* gerado por tools/mkassets.py - nao editar */",
         "/* Fontes: Michroma e JetBrains Mono, SIL Open Font License 1.1 (tools/fonts/OFL-*.txt) */"]

# atan(i/256) em unidades de 1/65536 de volta
parts.append(table("atan_tab", "u16", [round(math.atan(i / 256) / (2 * math.pi) * 65536) for i in range(257)]))
# easing: entrada 0..256 (interpolar), saida 0..4096
parts.append(table("ease_out_tab", "u16", bezier_table(0.16, 0.84, 0.24, 1)))   # var(--ease-out)
parts.append(table("ease_io_tab", "u16", bezier_table(0.42, 0, 0.58, 1)))       # ease-in-out
parts.append(table("ease_tab", "u16", bezier_table(0.25, 0.1, 0.25, 1)))        # ease (padrao CSS)
# gaussiana exp(-x^2/2), x = i/64, 0..255
parts.append(table("gauss_tab", "u8", [round(255 * math.exp(-((i / 64) ** 2) / 2)) for i in range(256)]))

parts.append(render_font(MONO, 11.2, MONO_CHARS, "fnt_mono_status"))   # 0.7rem
parts.append(render_font(MONO, 10.0, MONO_CHARS, "fnt_mono_small"))    # max(10px, 1.9% do anel)
for H in HEIGHTS:
    D = H * 64 // 100  # diametro do anel = 64% da altura (mesma conta do C)
    parts.append(render_font(MICHROMA, D * 0.078, "NEXUS.", f"fnt_title_{H}"))
    parts.append(render_font(MICHROMA, D * 0.03, "TEAM", f"fnt_team_{H}"))

open(out, "w", newline="\n").write("\n\n".join(parts) + "\n")
print(f"{out}: {os.path.getsize(out) // 1024} KiB de fonte C")
