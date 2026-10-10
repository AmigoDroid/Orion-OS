
#include "terminal.h"
#include "keyboard.h"

#define PROMPT "  orion> "

void kernel_main(void)
{
    unsigned int input_length = 0;

    terminal_initialize();
    keyboard_initialize();

    terminal_writestring("================================\n");
    terminal_writestring("          ORION OS 0.1.0\n");
    terminal_writestring("================================\n\n");

    terminal_writestring(" Kernel iniciado com sucesso!\n");
    terminal_writestring(" Teclado inicializado.\n\n");
    terminal_writestring(" Digite alguma coisa abaixo:\n\n");

    terminal_writestring(PROMPT);

    for (;;) {
        int c = keyboard_getchar();

        if (c < 0) {
            continue;
        }

        if (c == '\n') {
            terminal_putchar('\n');
            input_length = 0;
            terminal_writestring(PROMPT);
        } else if (c == '\b') {
            if (input_length > 0) {
                terminal_putchar('\b');
                input_length--;
            }
        } else if (c == '\t') {
            for (int i = 0; i < 4; i++) {
                terminal_putchar(' ');
                input_length++;
            }
        } else if (c >= 32 && c <= 126) {
            terminal_putchar((char)c);
            input_length++;
        }
    }
}
