
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

    mov si, mensagem
    call print

    ; Destino inicial: endereço físico 0x10000
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    ; O kernel começa no setor 4 do disquete.
    ; Como o kernel atual tem 8640 bytes (~17 setores), carregamos 20 setores.
    mov byte [sector], 4
    mov byte [head], 0
    mov byte [cylinder], 0
    mov word [sectors_left], 20

.load_sector:
    mov ax, 0x0201
    mov ch, [cylinder]
    mov cl, [sector]
    mov dh, [head]
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Próximo endereço de destino: 512 bytes adiante.
    add bx, 512

    ; Avança para o próximo setor da trilha.
    inc byte [sector]
    cmp byte [sector], 19
    jb .next_sector

    ; Disquete de 1,44 MB: 18 setores por trilha.
    mov byte [sector], 1
    xor byte [head], 1
    cmp byte [head], 0
    jne .next_sector

    inc byte [cylinder]

.next_sector:
    dec word [sectors_left]
    jnz .load_sector

    ; Prepara a GDT.
    xor ax, ax
    mov ds, ax
    mov es, ax
    lgdt [gdt_descriptor]

    ; Entra no modo protegido.
    cli
    mov eax, cr0
    or eax, 1
    mov cr0, eax
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
    xor ax, ax
    mov ds, ax
    mov si, erro_msg
    call print
    cli
.halt_error:
    hlt
    jmp .halt_error

mensagem db 'ORION: carregando kernel...', 13, 10, 0
erro_msg db 'ORION: erro ao ler kernel!', 13, 10, 0
boot_drive db 0
sector db 0
head db 0
cylinder db 0
sectors_left dw 0

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

    ; Ponto de entrada do kernel.
    mov eax, 0x10000
    jmp eax

    cli
.halt:
    hlt
    jmp .halt

times 1024 - ($ - $$) db 0
