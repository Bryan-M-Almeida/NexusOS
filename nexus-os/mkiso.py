#!/usr/bin/env python3
"""Monta nexus.iso (El Torito, sem emulacao). Uso: mkiso.py boot_cd.bin nexus.img out.iso"""
import struct, subprocess, sys, os, shutil, tempfile
bootcd, img, out = sys.argv[1:4]
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
print(f"{out}: {os.path.getsize(out)//1024} KiB, imagem no setor {lba}")
shutil.rmtree(d)
