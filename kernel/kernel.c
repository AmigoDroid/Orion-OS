
#include "terminal.h"

void kernel_main(void)
{
    terminal_initialize();

    terminal_writestring("================================\n");
    terminal_writestring("          ORION OS 0.1.0\n");
    terminal_writestring("================================\n\n");

    terminal_writestring("Kernel iniciado com sucesso!\n");
    terminal_writestring("Terminal de texto inicializado.\n\n");

    terminal_writestring("Proximas etapas:\n");
    terminal_writestring("- Entrada pelo teclado\n");
    terminal_writestring("- Shell e comandos\n");
    terminal_writestring("- Gerenciamento de memoria\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
