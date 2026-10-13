; =========================================================================
; Entrada UNICA do kernel Orion (i386).
;
; Chamada por qualquer bootloader que siga Multiboot 1:
;     EAX = 0x2BADB002, EBX = ponteiro para a estrutura multiboot_info.
; - GRUB: carrega o ELF e salta para _start.
; - Bootloader proprio (boot/stage2.asm): copia o kernel.bin para 1 MiB e
;   salta para _start com os mesmos registradores.
; =========================================================================

BITS 32

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000007          ; bit0 alinhar modulos, bit1 mapa de memoria,
                                    ; bit2 pedido de video
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
    ; Campos de endereco (header_addr ... entry_addr): ignorados porque o bit 16
    ; esta desligado (o kernel e ELF), mas o GRUB le o pedido de video no
    ; offset 32, entao estes 5 dwords precisam existir.
    dd 0, 0, 0, 0, 0
       ; Pedido de video: modo grafico linear 1280x720x32.
    dd 0
    dd 1280
    dd 720
    dd 32

section .text.entry
global _start
extern kernel_main
extern __bss_start
extern __bss_end
extern stack_top

_start:
    cli
    cld

    mov edx, eax                    ; guarda a magic do bootloader
    mov esi, ebx                    ; guarda o ponteiro multiboot_info

    ; GDT propria: a do GRUB/stage2 pode ficar em memoria que reaproveitamos.
    lgdt [early_gdt_desc]
    jmp 0x08:.reload_cs
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Zera o BSS (o GRUB ja zera, o stage2 nao: o .bss nao esta no arquivo).
    ; Nada em C usa BSS ate aqui e ainda estamos na pilha do bootloader.
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    shr ecx, 2
    rep stosd

    mov esp, stack_top
    xor ebp, ebp
    sub esp, 8                      ; mantem a pilha alinhada em 16 na chamada
    push esi                        ; arg 2: multiboot_info*
    push edx                        ; arg 1: magic
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .data
align 8
early_gdt:
    dq 0x0000000000000000           ; nulo
    dq 0x00CF9A000000FFFF           ; 0x08 codigo, base 0, limite 4 GiB
    dq 0x00CF92000000FFFF           ; 0x10 dados
early_gdt_end:

early_gdt_desc:
    dw early_gdt_end - early_gdt - 1
    dd early_gdt

section .note.GNU-stack noalloc noexec nowrite progbits
