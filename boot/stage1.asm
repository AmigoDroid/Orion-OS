; ===========================================================================
; Orion bootloader - estagio 1 (setor de boot, 512 bytes).
; Unica tarefa: ler o estagio 2 (setores 2..7) para 0x8000 e saltar para la.
; ===========================================================================

bits 16
org 0x7C00

%include "layout.inc"

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov [boot_drive], dl

    mov si, msg_boot
    call print

    mov di, 3                       ; tentativas
.retry:
    mov ah, 0x02                    ; ler setores (CHS)
    mov al, STAGE2_SECTORS
    xor ch, ch                      ; cilindro 0
    mov cl, 2                       ; setor 2 (o 1 e este)
    xor dh, dh                      ; cabeca 0
    mov dl, [boot_drive]
    mov bx, STAGE2_ADDR
    int 0x13
    jnc .ok

    xor ax, ax                      ; reseta o controlador e tenta de novo
    mov dl, [boot_drive]
    int 0x13
    dec di
    jnz .retry

    mov si, msg_err
    call print
.halt:
    cli
    hlt
    jmp .halt

.ok:
    mov dl, [boot_drive]            ; o stage2 precisa saber de qual disco veio
    jmp 0x0000:STAGE2_ADDR

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

boot_drive db 0
msg_boot   db 'Orion: estagio 1', 13, 10, 0
msg_err    db 'Erro ao ler o estagio 2!', 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
