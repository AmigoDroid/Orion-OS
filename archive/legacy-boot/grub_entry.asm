
BITS 32

section .multiboot
align 4
    dd 0x1BADB002
    dd 0x00000007
    dd -(0x1BADB002 + 0x00000007)

    ; Campos de endereco (header_addr ... entry_addr): ignorados porque o bit 16
    ; das flags esta desligado (o kernel e ELF), mas o GRUB le o pedido de video
    ; no offset 32 do header, entao esses 5 dwords precisam existir.
    dd 0, 0, 0, 0, 0

    ; Solicita modo gráfico 800x600, 32 bits
    dd 0
    dd 1280
    dd 720
    dd 32

section .text.entry
global _start
extern kernel_main
extern gfd_init
extern __bss_start
extern __bss_end
extern stack_top

_start:
    cli
    cld

    ; Preserva o ponteiro Multiboot recebido do GRUB
    mov esi, ebx

    ; Inicializa a seção BSS
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    rep stosb

    ; Prepara a pilha
    mov esp, stack_top
    and esp, 0xFFFFFFF0
    xor ebp, ebp

    ; Inicializa o OrionGFD (se nao houver framebuffer, o kernel segue em texto)
    push esi
    call gfd_init
    add esp, 4

    ; Inicia o kernel existente
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
