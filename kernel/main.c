/* Ponto de entrada em C. Chamado por arch/i386/entry.asm, igual para o
 * boot pelo GRUB e pelo bootloader proprio. */

#include "multiboot.h"
#include "version.h"
#include "kprintf.h"
#include "terminal.h"
#include "panic.h"
#include "log.h"
#include "gdt.h"
#include "idt.h"
#include "gfd.h"
#include "mm.h"
#include "fs.h"
#include "pci.h"
#include "usb.h"
#include "keyboard.h"
#include "mouse.h"
#include "shell.h"

static void banner(void)
{
    kprintf("       ___  ____  _              \n");
    kprintf("      / _ \\|  _ \\(_) ___  _ __   \n");
    kprintf("     | | | | |_) | |/ _ \\| '_ \\  \n");
    kprintf("     | |_| |  _ <| | (_) | | | | \n");
    kprintf("      \\___/|_| \\_\\_|\\___/|_| |_| \n");
    kprintf("                                 \n");
    kprintf("       -- ORION OS %s --\n", ORION_VERSION);
    kprintf("---------------------------------\n\n");
}

void kernel_main(uint32_t magic, const mb_info_t *mbi)
{
    log_init();

    /* 1. CPU: tabelas proprias, para que qualquer falha dali em diante
     *    apareca como mensagem e nao como reinicio silencioso. */
    gdt_init();
    idt_init();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        terminal_initialize();
        panic("o bootloader nao e Multiboot (EAX=%08x)", magic);
    }

    /* 2. Video: GFD se houver framebuffer, senao texto VGA. */
    gfd_init((unsigned int)(uintptr_t)mbi);
    terminal_initialize();
    banner();

    /* 3. Memoria: frames fisicos, heap e depois paginacao. */
    mm_init(mbi);
    vmm_init(mbi);
    kprintf(" Paginacao ligada (NULL, codigo e pilha protegidos).\n");
    if (gfd_active())
        kprintf(gfd_enable_backbuffer() ? " GFD: buffer duplo ativo.\n"
                                        : " GFD: sem memoria para buffer duplo (a tela pode piscar).\n");

    /* 4. Dispositivos. */
    keyboard_initialize();
    mouse_initialize();
    pci_initialize();
    usb_initialize();
    kprintf(" Teclado e mouse inicializados.\n");

    kprintf(" Inicializando OrionFS...\n\n");
    fs_mount();

    log_ok("kernel pronto");
    shell_run(mbi);
}
