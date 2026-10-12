; NEXUS.OS CD boot sector (El Torito, no emulation).
; Reads NEXUS.IMG (2048-byte sectors) from [imglba] to 0FE0:0000, so the kernel,
; 512 bytes into the image, lands at 1000:0000.
bits 16
org 0x7C00

%ifndef KSECT
%define KSECT 40
%endif
%define CDSECT ((KSECT + 4) / 4)        ; (512 + KSECT*512) / 2048, rounded up

    jmp short start
    nop
    times 0x40 - ($ - $$) db 0          ; 8..63 reserved (boot info table)
imglba: dd 0                            ; 0x40: set by mkiso.py

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [drive], dl
    mov si, msg_boot
    call print

    mov eax, [imglba]
    mov [dap_lba], eax
    mov word [remain], CDSECT
.next:
    cmp word [remain], 0
    je  .done
    mov ax, 16
    cmp [remain], ax
    jae .set
    mov ax, [remain]
.set:
    mov [dap_cnt], ax
    mov [chunk], ax
    mov di, 3
.retry:
    mov si, dap
    mov dl, [drive]
    mov ah, 0x42
    int 0x13
    jnc .ok
    xor ax, ax
    mov dl, [drive]
    int 0x13
    dec di
    jnz .retry
    mov si, msg_err
    call print
    cli
    hlt
    jmp $-2
.ok:
    mov ax, [chunk]
    sub [remain], ax
    add [dap_lba], ax
    shl ax, 7                   ; 2048-byte sectors / 16 = 128 paragraphs
    add [dap_seg], ax
    jmp .next
.done:
    mov dl, [drive]
    jmp 0x1000:0x0000

print:
    lodsb
    test al, al
    jz .r
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print
.r: ret

drive:    db 0
remain:   dw 0
chunk:    dw 0
msg_boot: db "NEXUS.OS (ISO): carregando...", 13, 10, 0
msg_err:  db "ERRO: falha ao ler o CD", 13, 10, 0

dap:      db 0x10, 0
dap_cnt:  dw 0
dap_off:  dw 0
dap_seg:  dw 0x0FE0
dap_lba:  dq 0
