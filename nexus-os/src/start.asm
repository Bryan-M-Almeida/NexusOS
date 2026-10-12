; NEXUS.OS kernel entry (real mode, loaded at 1000:0000)
bits 16
section .text.start
global _start
global enter_unreal
global boot_base
extern kmain
extern __bss_start
extern __bss_end

_start:
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0xFFF0
    sti
    cld
    mov edi, __bss_start        ; clear .bss
    mov ecx, __bss_end
    sub ecx, edi
    xor al, al
    rep stosb
    xor eax, eax                ; hybrid ISO boot: image start LBA in EBX
    cmp esi, 0x3142584E         ; when ESI holds the 'NXB1' signature
    jne .nobase
    mov eax, ebx
.nobase:
    mov [boot_base], eax
    mov ax, 0xB800
    mov gs, ax
    xor ax, ax
    mov fs, ax
    movzx edx, dl               ; DL = boot drive
    push edx
    call dword kmain
.hang:
    cli
    hlt
    jmp .hang

; Unreal mode: give FS a 4 GB limit to access memory above 1 MB
enter_unreal:
    cli
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, gdt
    mov [gdtr + 2], eax
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short .pm
.pm:
    mov ax, 0x08
    mov fs, ax
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp short .rm
.rm:
    sti
    o32 ret

align 8
gdt:
    dq 0
    dq 0x00CF92000000FFFF
gdt_end:
gdtr:
    dw gdt_end - gdt - 1
    dd 0

section .bss
boot_base: resd 1
