#!/usr/bin/env python3
"""Monta nexus.iso hibrida: El Torito (CD, sem emulacao) + MBR no setor 0 (pendrive em modo DD).
Uso: mkiso.py boot_cd.bin boot_hyb.bin nexus.img out.iso"""
import struct, subprocess, sys, os, shutil, tempfile
bootcd, boothyb, img, out = sys.argv[1:5]
d = tempfile.mkdtemp()
def build(lba):
    b = bytearray(open(bootcd,'rb').read()); assert len(b) <= 2048
    struct.pack_into("<I", b, 0x40, lba)
    open(f"{d}/BOOT.BIN",'wb').write(bytes(b).ljust(2048, b"\0"))
    shutil.copy(img, f"{d}/NEXUS.IMG")
    subprocess.run(["xorriso","-as","mkisofs","-quiet","-V","NEXUS_OS","-b","BOOT.BIN","-no-emul-boot",
                    "-boot-load-size","4","-o",out,d], check=True)
def find_lba():
    iso = open(out,'rb').read()
    root = struct.unpack_from("<I", iso, 16*2048+156+2)[0]
    pos = root*2048
    while iso[pos]:
        ln = iso[pos]; nl = iso[pos+32]; name = iso[pos+33:pos+33+nl].decode()
        if name.startswith("NEXUS.IMG"): return struct.unpack_from("<I", iso, pos+2)[0]
        pos += ln
    raise SystemExit("NEXUS.IMG nao achado no ISO")
build(0); lba = find_lba(); build(lba); assert find_lba() == lba
shutil.rmtree(d)

# MBR hibrido: a area de sistema da ISO (primeiros 32 KiB) fica livre, o setor 0 vira o MBR
mbr = bytearray(open(boothyb,'rb').read()); assert len(mbr) == 512 and mbr[510:] == b"\x55\xaa"
base = lba * 4                                  # setor de 2048 -> LBA de 512
struct.pack_into("<I", mbr, 0x1B0, base)
struct.pack_into("<I", mbr, 446 + 8, base)
with open(out, 'r+b') as f:
    assert f.read(512) == bytes(512), "area de sistema da ISO nao esta vazia"
    f.seek(0); f.write(mbr)
    f.seek(0, 2); size = f.tell()
    f.truncate(max(size, (base + 2048) * 512))   # garante a imagem inteira dentro do arquivo
print(f"{out}: {os.path.getsize(out)//1024} KiB, imagem no setor {lba} (LBA {base}), hibrida (CD + USB)")
