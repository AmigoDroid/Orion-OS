BITS 32

section .multiboot
align 4
    dd 0x1BADB002
    dd 0x00000003
    dd -(0x1BADB002 + 0x00000003)

section .text.entry
global _start
extern kernel_main
extern __bss_start
extern __bss_end
extern stack_top

_start:
    cli
    cld

    ; Inicializa a secao BSS do kernel
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    rep stosb

    ; Prepara a pilha
    mov esp, stack_top
    and esp, 0xFFFFFFF0
    xor ebp, ebp

    ; Inicia o kernel C
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
