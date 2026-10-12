/* Linker script UNICO do kernel (antes eram dois: linker.ld e grub.ld).
 *
 * O kernel vive em 1 MiB, o endereco padrao de kernels Multiboot. Quem
 * carrega (GRUB, ou o stage2 do bootloader proprio) so precisa respeitar
 * este layout. Cada regiao comeca em pagina de 4 KiB para que o gerenciador
 * de memoria virtual possa proteger codigo (somente leitura/execucao),
 * constantes (somente leitura) e dados (leitura/escrita) separadamente. */

ENTRY(_start)

KERNEL_BASE = 0x100000;
STACK_SIZE  = 32768;

SECTIONS
{
    . = KERNEL_BASE;
    __kernel_start = .;

    __text_start = .;
    .text : {
        KEEP(*(.multiboot))      /* o header precisa estar nos primeiros 8 KiB */
        *(.text.entry)
        *(.text .text.*)
    }
    . = ALIGN(4096);
    __text_end = .;

    __rodata_start = .;
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4096);
    __rodata_end = .;

    __data_start = .;
    .data : { *(.data .data.*) }
    . = ALIGN(4096);
    __data_end = .;

    __bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(4096);
        stack_guard = .;         /* pagina SEM mapeamento: pega estouro de pilha */
        . += 4096;
        stack_bottom = .;
        . += STACK_SIZE;
        stack_top = .;
    }
    . = ALIGN(4096);
    __bss_end = .;
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}

/* A tabela de paginas inicial cobre os primeiros 4 MiB. */
ASSERT(__kernel_end <= 0x400000, "kernel maior que 3 MiB: aumente a cobertura em vmm.c")
