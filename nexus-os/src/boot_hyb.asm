; NEXUS.OS hybrid ISO MBR (sector 0 of nexus.iso).
; Used when the ISO is written raw to a USB drive and booted as a disk. Loads the
; kernel from the NEXUS.IMG embedded in the ISO (starting at [imgbase], 512-byte LBA)
; and passes the offset to the kernel: ESI = 'NXB1', EBX = imgbase.
; CD boots ignore this sector and use El Torito instead.
bits 16
org 0x7C00

%ifndef KSECT
%define KSECT 40
%endif

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

    mov ah, 0x41                ; INT 13h extensions present?
    mov bx, 0x55AA
    mov dl, [drive]
    int 0x13
    jc  .noedd
    cmp bx, 0xAA55
    jne .noedd

    mov eax, [imgbase]          ; kernel = NEXUS.IMG sector 1
    inc eax
    mov [dap_lba], eax
    mov word [remain], KSECT
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
    jmp .err
.ok:
    mov ax, [chunk]
    sub [remain], ax
    add [dap_lba], ax
    adc word [dap_lba + 2], 0
    shl ax, 5
    add [dap_seg], ax
    jmp .next

.done:
    mov ebx, [imgbase]
    mov esi, 0x3142584E         ; 'NXB1'
    mov dl, [drive]
    jmp 0x1000:0x0000

.noedd:
    mov si, msg_noedd
    jmp .fail
.err:
    mov si, msg_err
.fail:
    call print
    cli
    hlt
    jmp $-2

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
msg_boot:  db "NEXUS.OS (USB): carregando kernel...", 13, 10, 0
msg_noedd: db "ERRO: BIOS sem LBA estendido", 13, 10, 0
msg_err:   db "ERRO: falha ao ler o disco", 13, 10, 0

dap:      db 0x10, 0
dap_cnt:  dw 0
dap_off:  dw 0
dap_seg:  dw 0x1000
dap_lba:  dq 0

times 0x1B0 - ($ - $$) db 0
imgbase:  dd 0                  ; NEXUS.IMG LBA, set by mkiso.py

times 446 - ($ - $$) db 0
; active partition covering NEXUS.IMG (start LBA set by mkiso.py)
db 0x80, 0x00, 0x02, 0x00, 0xDA, 0xFE, 0xFF, 0xFF
dd 0
dd 2048
times 510 - ($ - $$) db 0
dw 0xAA55
