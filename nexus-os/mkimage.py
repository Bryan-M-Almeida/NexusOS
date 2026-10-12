#!/usr/bin/env python3
"""Monta nexus.img: boot (LBA 0) + kernel (LBA 1..) + NXFS (LBA 128) com pastas de exemplo."""
import struct, sys
FS_LBA, FS_MAX, IMG_SECT = 128, 224, 2048
boot, kernel, out = (open(sys.argv[1],'rb').read(), open(sys.argv[2],'rb').read(), sys.argv[3])
assert len(boot) == 512
assert len(kernel) <= (FS_LBA-1)*512, "kernel passou de 63KB"

ents = [(1,0,b"/")]                       # raiz
def add(t, parent, name): ents.append((t,parent,name.encode())); return len(ents)-1
sis = add(1,0,"sistema"); doc = add(1,0,"documentos"); proj = add(1,0,"projetos")
add(2,sis,"nexus.cfg"); add(2,doc,"leiame.txt"); add(1,proj,"web"); add(1,proj,"c"); add(2,0,"bem-vindo.txt")
table = b"".join(struct.pack("<BB30s",t,p,n) for t,p,n in ents)
table += b"\0" * (FS_MAX*32 - len(table))
sb = (b"NEXUSFS1" + struct.pack("<H", FS_MAX)).ljust(512, b"\0")

img = bytearray(IMG_SECT*512)
img[0:512] = boot
img[512:512+len(kernel)] = kernel
img[FS_LBA*512:FS_LBA*512+512] = sb
img[(FS_LBA+1)*512:(FS_LBA+1)*512+len(table)] = table
open(out,'wb').write(img)
print(f"{out}: {len(img)//1024} KiB, kernel {len(kernel)} bytes")
