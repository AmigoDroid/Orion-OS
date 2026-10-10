
bits 16
org 0x8000

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld

    mov [boot_drive], dl

    ; Exibe mensagem
    mov si, mensagem
    call print

    ; Carrega o kernel do setor 4 para 0x10000
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 4
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Restaura segmentos antes de preparar a GDT
    xor ax, ax
    mov es, ax
    mov ds, ax

    ; Carrega a GDT
    lgdt [gdt_descriptor]

    ; Ativa o modo protegido de 32 bits
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Salto longo para o segmento de código
    jmp dword 0x08:protected_mode_start

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

disk_error:
    mov si, erro_msg
    call print
    cli
.halt_error:
    hlt
    jmp .halt_error

mensagem db 'ORION: carregando kernel...', 13, 10, 0
erro_msg db 'ORION: erro ao ler kernel!', 13, 10, 0
boot_drive db 0

align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

bits 32
protected_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, 0x90000

    ; Executa kernel_main em 0x10000
    mov eax, 0x10000
    jmp eax

    cli
.halt:
    hlt
    jmp .halt

times 1024 - ($ - $$) db 0
