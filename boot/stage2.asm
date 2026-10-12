; ===========================================================================
; Orion bootloader - estagio 2.
;
; Faz o que o GRUB faria, para o kernel nao saber a diferenca:
;   1. mede a memoria (INT 15h E820, com E801/88h de reserva);
;   2. le o kernel do disco para 0x10000 (modo real);
;   3. liga a linha A20;
;   4. monta uma estrutura multiboot_info (mapa de memoria + nome);
;   5. entra em modo protegido, copia o kernel para 1 MiB e salta para a
;      entrada com EAX = 0x2BADB002 e EBX = &multiboot_info.
;
; Parametros do Makefile (-D): KERNEL_ENTRY (endereco de _start) e
; KERNEL_BYTES (tamanho de kernel.bin). Nada aqui e "numero magico" do kernel.
; ===========================================================================

bits 16
org 0x8000

%include "layout.inc"

%ifndef KERNEL_ENTRY
%error "defina KERNEL_ENTRY (o Makefile passa -DKERNEL_ENTRY=0x...)"
%endif
%ifndef KERNEL_BYTES
%error "defina KERNEL_BYTES (o Makefile passa -DKERNEL_BYTES=...)"
%endif
%if KERNEL_BYTES > KERNEL_MAX_BYTES
%error "kernel.bin maior que KERNEL_MAX_BYTES: nao cabe na carga pelo modo real"
%endif

KERNEL_SECTORS equ (KERNEL_BYTES + 511) / 512

stage2:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov [boot_drive], dl
    mov si, msg_stage2
    call print

    call detect_edd
    call get_memory_info
    call load_kernel
    call enable_a20
    call build_mbinfo

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:pm_entry

; --------------------------------------------------------------------------
; Saidas de erro
; --------------------------------------------------------------------------
fatal:                              ; SI = mensagem
    call print
.halt:
    cli
    hlt
    jmp .halt

; --------------------------------------------------------------------------
; Texto
; --------------------------------------------------------------------------
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

; --------------------------------------------------------------------------
; INT 13h estendido (LBA) para HD/pendrive; disquete usa CHS.
; --------------------------------------------------------------------------
detect_edd:
    mov byte [use_edd], 0
    mov dl, [boot_drive]
    cmp dl, 0x80
    jb .done
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .done
    cmp bx, 0xAA55
    jne .done
    test cx, 1
    jz .done
    mov byte [use_edd], 1
.done:
    ret

; EAX = LBA, ES:BX = destino (1 setor). CF=1 se falhar apos 3 tentativas.
read_sector:
    pusha
    mov [rs_lba], eax
    mov byte [rs_tries], 3
.try:
    cmp byte [use_edd], 0
    jne .edd

    mov ax, [rs_lba]                ; CHS de disquete 1,44 MB: 18 setores, 2 cabecas
    xor dx, dx
    mov si, 18
    div si                          ; AX = trilha, DX = setor-1
    inc dl
    mov cl, dl
    xor dx, dx
    mov si, 2
    div si                          ; AX = cilindro, DX = cabeca
    mov dh, dl
    mov ch, al
    mov dl, [boot_drive]
    mov ax, 0x0201
    int 0x13
    jnc .ok
    jmp .fail

.edd:
    mov word [dap + 2], 1
    mov [dap + 4], bx
    mov [dap + 6], es
    mov eax, [rs_lba]
    mov [dap + 8], eax
    mov dword [dap + 12], 0
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .ok

.fail:
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13                        ; reset
    dec byte [rs_tries]
    jnz .try
    stc
    jmp .out
.ok:
    clc
.out:
    popa                            ; popa nao altera CF
    ret

load_kernel:
    mov si, msg_load
    call print

    mov dword [cur_lba], KERNEL_FIRST_LBA
    mov word [sectors_left], KERNEL_SECTORS
    mov ax, KERNEL_TMP_SEG
    mov es, ax
    xor bx, bx
.next:
    mov eax, [cur_lba]
    call read_sector
    jc .error
    add bx, 512
    jnz .same_segment               ; bx deu a volta (64 KiB): avanca o segmento
    mov ax, es
    add ax, 0x1000
    mov es, ax
.same_segment:
    inc dword [cur_lba]
    dec word [sectors_left]
    jnz .next
    xor ax, ax
    mov es, ax
    ret
.error:
    xor ax, ax
    mov es, ax
    mov si, msg_disk
    jmp fatal

; --------------------------------------------------------------------------
; Memoria: mapa E820 no formato Multiboot + mem_lower/mem_upper.
; Cada entrada: size(4)=20, base(8), length(8), type(4)  -> 24 bytes.
; --------------------------------------------------------------------------
get_memory_info:
    xor ax, ax
    mov es, ax
    mov di, MMAP_ADDR + 4           ; deixa 4 bytes antes de cada entrada p/ o `size`
    xor ebx, ebx
    xor bp, bp
.e820_next:
    mov eax, 0xE820
    mov edx, 0x534D4150             ; 'SMAP'
    mov ecx, 24
    mov dword [es:di + 20], 1       ; atributo estendido "valido" por padrao
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    cmp cl, 20
    jb .e820_skip
    je .e820_accept
    test byte [es:di + 20], 1       ; 24 bytes: so vale se o bit 0 estiver ligado
    jz .e820_skip
.e820_accept:
    mov eax, [es:di + 8]
    or  eax, [es:di + 12]
    jz .e820_skip                   ; tamanho zero: ignora
    mov dword [es:di - 4], 20
    add di, 24
    inc bp
    cmp bp, MMAP_MAX
    jae .e820_done
.e820_skip:
    test ebx, ebx
    jnz .e820_next
.e820_done:
    mov [mmap_count], bp
    mov ax, bp
    mov cx, 24
    mul cx
    mov [mmap_bytes], ax

    int 0x12                        ; memoria convencional em KiB
    movzx eax, ax
    mov [mem_lower], eax

    mov dword [mem_upper], 0
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov ax, 0xE801
    int 0x15
    jc .try88
    test ax, ax
    jnz .e801_have
    mov ax, cx
    mov bx, dx
.e801_have:
    movzx eax, ax                   ; KiB entre 1 e 16 MiB
    movzx ebx, bx                   ; blocos de 64 KiB acima de 16 MiB
    shl ebx, 6
    add eax, ebx
    mov [mem_upper], eax
    ret
.try88:
    mov ah, 0x88
    int 0x15
    jc .done
    movzx eax, ax
    mov [mem_upper], eax
.done:
    ret

; --------------------------------------------------------------------------
; A20
; --------------------------------------------------------------------------
; ZF=1 se a A20 esta DESLIGADA (o endereco 1 MiB+0x500 "da a volta").
a20_is_on:
    push ds
    push es
    push si
    push di
    xor ax, ax
    mov es, ax
    not ax
    mov ds, ax
    mov di, 0x0500
    mov si, 0x0510
    mov al, [es:di]
    push ax
    mov al, [ds:si]
    push ax
    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF
    cmp byte [es:di], 0xFF
    pop ax
    mov [ds:si], al
    pop ax
    mov [es:di], al
    pop di
    pop si
    pop es
    pop ds
    ret

kbc_wait:
    in al, 0x64
    test al, 2
    jnz kbc_wait
    ret

enable_a20:
    call a20_is_on
    jnz .done
    mov ax, 0x2401                  ; BIOS
    int 0x15
    call a20_is_on
    jnz .done
    in al, 0x92                     ; porta rapida
    or al, 2
    and al, 0xFE
    out 0x92, al
    call a20_is_on
    jnz .done
    call kbc_wait                   ; controlador de teclado
    mov al, 0xD1
    out 0x64, al
    call kbc_wait
    mov al, 0xDF
    out 0x60, al
    call kbc_wait
    mov cx, 0xFFFF
.poll:
    call a20_is_on
    jnz .done
    loop .poll
    mov si, msg_a20
    jmp fatal
.done:
    ret

; --------------------------------------------------------------------------
; multiboot_info
; --------------------------------------------------------------------------
build_mbinfo:
    xor ax, ax
    mov es, ax
    mov di, MBINFO_ADDR
    mov cx, 32
    xor eax, eax
    rep stosd                       ; zera 128 bytes

    mov eax, 1 << 9                 ; bit 9: nome do bootloader
    cmp dword [mem_upper], 0
    je .no_mem
    or eax, 1                       ; bit 0: mem_lower/mem_upper
.no_mem:
    cmp word [mmap_count], 0
    je .no_mmap
    or eax, 1 << 6                  ; bit 6: mapa de memoria
.no_mmap:
    mov [MBINFO_ADDR + 0], eax

    mov eax, [mem_lower]
    mov [MBINFO_ADDR + 4], eax
    mov eax, [mem_upper]
    mov [MBINFO_ADDR + 8], eax
    movzx eax, word [mmap_bytes]
    mov [MBINFO_ADDR + 44], eax
    mov dword [MBINFO_ADDR + 48], MMAP_ADDR
    mov dword [MBINFO_ADDR + 64], loader_name
    ret

; --------------------------------------------------------------------------
; Dados
; --------------------------------------------------------------------------
boot_drive   db 0
use_edd      db 0
rs_tries     db 0
rs_lba       dd 0
cur_lba      dd 0
sectors_left dw 0
mmap_count   dw 0
mmap_bytes   dw 0
mem_lower    dd 0
mem_upper    dd 0

dap:         db 0x10, 0
             dw 1
             dw 0, 0
             dd 0, 0

loader_name  db 'Orion Boot', 0
msg_stage2   db 'Orion: estagio 2', 13, 10, 0
msg_load     db 'Orion: carregando kernel...', 13, 10, 0
msg_disk     db 'Orion: erro ao ler o kernel!', 13, 10, 0
msg_a20      db 'Orion: nao consegui ligar a A20!', 13, 10, 0

align 8
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF           ; 0x08 codigo 32 bits
    dq 0x00CF92000000FFFF           ; 0x10 dados 32 bits
gdt_end:

gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

; --------------------------------------------------------------------------
; Modo protegido: copia o kernel para 1 MiB e entrega o controle.
; --------------------------------------------------------------------------
bits 32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7C00

    mov esi, KERNEL_TMP_ADDR
    mov edi, KERNEL_DEST
    mov ecx, (KERNEL_BYTES + 3) / 4
    cld
    rep movsd

    mov eax, MULTIBOOT_MAGIC
    mov ebx, MBINFO_ADDR
    mov ecx, KERNEL_ENTRY
    jmp ecx

times (STAGE2_SECTORS * 512) - ($ - $$) db 0
