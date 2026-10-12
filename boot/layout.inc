; Constantes compartilhadas pelos dois estagios do bootloader proprio.
; (Os valores do KERNEL_* vem do Makefile via -D: tamanho e entrada reais.)

STAGE2_ADDR      equ 0x8000      ; onde o stage1 coloca o stage2
STAGE2_SECTORS   equ 6           ; tamanho reservado do stage2 (3 KiB)
KERNEL_FIRST_LBA equ 1 + STAGE2_SECTORS

MBINFO_ADDR      equ 0x5000      ; estrutura multiboot_info entregue ao kernel
MMAP_ADDR        equ 0x5100      ; mapa E820 em formato Multiboot
MMAP_MAX         equ 100         ; entradas de 24 bytes: ate 0x5100+2400

KERNEL_TMP_SEG   equ 0x1000      ; carga temporaria em 0x10000 (modo real)
KERNEL_TMP_ADDR  equ 0x10000
KERNEL_DEST      equ 0x100000    ; endereco final do kernel (1 MiB)
KERNEL_MAX_BYTES equ 0x80000     ; 0x10000 + 512 KiB ainda cabe abaixo de 0x9FC00

MULTIBOOT_MAGIC  equ 0x2BADB002
