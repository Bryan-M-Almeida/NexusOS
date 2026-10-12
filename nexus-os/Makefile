CC      = gcc
CFLAGS  = -m16 -march=i386 -Os -Wall -Wextra -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-stack-protector -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns \
          -mno-sse -mno-mmx -fno-delete-null-pointer-checks -Isrc
OBJS    = build/start.o build/main.o build/shell.o build/fs.o build/disk.o build/video.o \
          build/kbd.o build/power.o build/string.o

all: nexus.img nexus.iso

build:
	mkdir -p build

build/%.o: src/%.c src/nexus.h src/logo.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/start.o: src/start.asm | build
	nasm -f elf32 $< -o $@

build/kernel.bin: $(OBJS) link.ld
	ld -m elf_i386 -nostdlib -T link.ld -o $@ $(OBJS)

build/boot.bin: src/boot.asm build/kernel.bin
	nasm -f bin -DKSECT=$$(( ($$(stat -c%s build/kernel.bin) + 511) / 512 )) $< -o $@

nexus.img: build/boot.bin build/kernel.bin mkimage.py
	python3 mkimage.py build/boot.bin build/kernel.bin $@

build/boot_cd.bin: src/boot_cd.asm build/kernel.bin
	nasm -f bin -DKSECT=$$(( ($$(stat -c%s build/kernel.bin) + 511) / 512 )) $< -o $@

nexus.iso: build/boot_cd.bin nexus.img mkiso.py
	python3 mkiso.py build/boot_cd.bin nexus.img $@

run-iso: nexus.iso
	qemu-system-i386 -cdrom nexus.iso -boot d

run: nexus.img
	qemu-system-i386 -drive format=raw,file=nexus.img

clean:
	rm -rf build nexus.img nexus.iso
.PHONY: all run run-iso clean
