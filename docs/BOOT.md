# Boot do Orion

Ha **um** kernel (`build/orion.elf`) e **duas** formas de dar boot nele. As duas entregam o mesmo contrato:

    EAX = 0x2BADB002        EBX = ponteiro para multiboot_info

Por isso existe uma unica entrada (`arch/i386/entry.asm`) e um unico linker script (`arch/i386/linker.ld`).

    make iso  -> build/orion.iso   GRUB (OrionGFD grafico)
    make img  -> build/orion.img   bootloader proprio (disquete 1,44 MB)

## Bootloader proprio

- `boot/stage1.asm` (512 B): le o stage2 (6 setores) para 0x8000.
- `boot/stage2.asm`: mede memoria (E820, com E801/88h de reserva), le o kernel para 0x10000,
  liga a A20, monta a `multiboot_info`, entra em modo protegido, copia o kernel para 1 MiB e salta para `_start`.
- Disquete usa CHS; HD/pendrive usa INT 13h estendido (LBA).
- O Makefile passa `KERNEL_BYTES` e `KERNEL_ENTRY` reais ao stage2 (`-D`). Nao ha mais "64 setores" fixos.
  Limite: kernel.bin <= 512 KiB (carga em modo real).
- `boot/layout.inc` guarda as constantes dos dois estagios.

## GRUB

`boot/grub/grub.cfg` tem uma entrada (OrionGFD). O header Multiboot esta em `arch/i386/entry.asm` e pede 800x600x32;
o GRUB obedece o header mesmo com `gfxpayload=text`, entao nao existe entrada de texto pelo GRUB.
O modo texto VGA e o do bootloader proprio (`make run BOOT=native`).

## Entrada do kernel

`entry.asm`: GDT propria -> zera o BSS (o stage2 nao zera) -> pilha -> `kernel_main(magic, mbi)`.
`kernel/main.c`: GDT/IDT -> checa magic -> video (GFD ou VGA) -> memoria -> dispositivos -> shell.

## Depuracao

Tudo que aparece no terminal tambem sai na porta 0xE9: `build/debug.log` quando roda pelo `make run`.
