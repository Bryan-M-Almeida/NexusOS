CC      = gcc
CFLAGS  = -m16 -march=i386 -Os -Wall -Wextra -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-stack-protector -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns \
          -mno-sse -mno-mmx -fno-delete-null-pointer-checks -Isrc
OBJS    = build/start.o build/main.o build/shell.o build/fs.o build/disk.o build/video.o \
          build/kbd.o build/power.o build/string.o build/intro.o build/lang.o

all: nexus.iso

build:
	mkdir -p build

build/%.o: src/%.c src/nexus.h src/logo.h | build
	$(CC) $(CFLAGS) -c $< -o $@

# intro glyphs (Michroma / JetBrains Mono) and lookup tables
build/assets.h build/assets.bin &: tools/mkassets.py tools/fonts/Michroma-Regular.ttf tools/fonts/JetBrainsMono-Regular.ttf | build
	python3 tools/mkassets.py tools/fonts build/assets.h build/assets.bin

build/intro.o: src/intro.c src/nexus.h build/assets.h | build
	$(CC) $(CFLAGS) -Ibuild -c $< -o $@

build/start.o: src/start.asm | build
	nasm -f elf32 $< -o $@

build/kernel.bin: $(OBJS) link.ld
	ld -m elf_i386 -nostdlib -T link.ld -o $@ $(OBJS)

KSECT = $$(( ($$(stat -c%s build/kernel.bin) + 511) / 512 ))

build/boot.bin: src/boot.asm build/kernel.bin
	nasm -f bin -DKSECT=$(KSECT) $< -o $@

build/nexus.img: build/boot.bin build/kernel.bin build/assets.bin tools/mkimage.py
	python3 tools/mkimage.py build/boot.bin build/kernel.bin build/assets.bin $@

build/boot_cd.bin: src/boot_cd.asm build/kernel.bin
	nasm -f bin -DKSECT=$(KSECT) $< -o $@

build/boot_hyb.bin: src/boot_hyb.asm build/kernel.bin
	nasm -f bin -DKSECT=$(KSECT) $< -o $@

# the only output: hybrid ISO for VirtualBox (CD) and USB drives (DD mode)
nexus.iso: build/boot_cd.bin build/boot_hyb.bin build/nexus.img tools/mkiso.py
	python3 tools/mkiso.py build/boot_cd.bin build/boot_hyb.bin build/nexus.img $@

# QEMU: boot the ISO as a CD (read-only)
run-iso: nexus.iso
	qemu-system-i386 -m 64 -cdrom nexus.iso -boot d

# QEMU: boot a copy of the ISO as a USB drive (changes persist in build/usb.img)
run: nexus.iso
	[ -f build/usb.img ] && [ build/usb.img -nt nexus.iso ] || cp nexus.iso build/usb.img
	qemu-system-i386 -m 64 -drive format=raw,file=build/usb.img

clean:
	rm -rf build nexus.iso
.PHONY: all run run-iso clean
