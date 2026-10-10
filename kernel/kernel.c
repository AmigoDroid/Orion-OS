#include "terminal.h"
#include "keyboard.h"

void kernel_main(void)
{
terminal_initialize();
keyboard_initialize();
terminal_writestring("================================\n");
terminal_writestring("          ORION OS 0.1.0\n");
terminal_writestring("================================\n\n");
terminal_writestring("Kernel iniciado com sucesso!\n");
terminal_writestring("Teclado inicializado.\n\n");
terminal_writestring("Digite alguma coisa abaixo:\n\n");
terminal_writestring("orion> ");
for (;;) {
int c = keyboard_getchar();
if (c < 0) {
continue;
}
if (c == '\n') {
terminal_putchar('\n');
terminal_writestring("orion> ");
} else if (c == '\b') {
terminal_putchar('\b');
} else if (c == '\t') {
terminal_putchar(' ');
terminal_putchar(' ');
terminal_putchar(' ');
terminal_putchar(' ');
} else if (c >= 32 && c <= 126) {
terminal_putchar((char)c);
}
}
}
