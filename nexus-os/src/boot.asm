; NEXUS.OS - bootloader (setor 0). Carrega o kernel via INT 13h estendido (LBA)
; para 1000:0000 e salta para ele. Funciona em HD virtual e pendrive (USB-HDD).
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

    mov ah, 0x41                ; BIOS suporta LBA estendido?
    mov bx, 0x55AA
    mov dl, [drive]
    int 0x13
    jc  .noedd
    cmp bx, 0xAA55
    jne .noedd

    mov word [remain], KSECT
.next:
    cmp word [remain], 0
    je  .done
    mov ax, 16                  ; le em blocos de 16 setores
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
    shl ax, 5                   ; setores * 512 / 16 = paragrafos
    add [dap_seg], ax
    jmp .next

.done:
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
msg_boot:  db "NEXUS.OS: carregando kernel...", 13, 10, 0
msg_noedd: db "ERRO: BIOS sem LBA estendido", 13, 10, 0
msg_err:   db "ERRO: falha ao ler o disco", 13, 10, 0

dap:      db 0x10, 0
dap_cnt:  dw 0
dap_off:  dw 0
dap_seg:  dw 0x1000
dap_lba:  dq 1

times 446 - ($ - $$) db 0
; tabela de particoes: uma entrada ativa (alguns BIOS exigem para boot USB)
db 0x80, 0x00, 0x02, 0x00, 0xDA, 0xFE, 0xFF, 0xFF
dd 1
dd 2047
times 510 - ($ - $$) db 0
dw 0xAA55
