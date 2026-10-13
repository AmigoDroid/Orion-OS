# Estrutura de pastas

    arch/i386/   codigo dependente de CPU: entry.asm, linker.ld, gdt, idt, isr, io.h
    boot/        bootloader proprio (stage1, stage2, layout.inc) e grub/grub.cfg
    kernel/      main.c, shell.c, terminal.c, log.c, panic.c
    mm/          pmm.c, heap.c, vmm.c, mm.c, memtest.c, mm.h
    lib/         string.c (mem*/str*), kprintf.c, crc32.c
    gfx/         OrionGFD: gfd.c, gfd.h, gfd_font.h
    drivers/     ata, keyboard, mouse, pci, rtc, usb/, core/ (placeholders vazios)
    fs/          OrionFS v3: fs.c (nucleo), bcache.c, fsck.c, fstest.c, fs.h (API), fs_internal.h (formato)
    include/     cabecalhos usados por varios modulos (multiboot, log, terminal, kprintf, panic, string, version)
    tools/       test_boot.py (testes automaticos no QEMU)
    docs/        documentacao e VERSION (fonte unica da versao)
    archive/     arquivos antigos (.bak, gui.c, linker scripts e Makefile anteriores)
    build/       saidas (nao versionar); orion-data.img e preservado pelo `make clean`

Regras:
- Codigo novo vai dentro de uma das pastas de `SRC_DIRS` do Makefile; ele e descoberto sozinho.
- Cada pasta com `.h` entra automaticamente no `-I`; use `#include "nome.h"`.
- Quem muda versao muda so `docs/VERSION`.
