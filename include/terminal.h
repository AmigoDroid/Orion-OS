#ifndef ORION_TERMINAL_H
#define ORION_TERMINAL_H

void terminal_initialize(void);
void terminal_clear(void);
void terminal_putchar(char c);
void terminal_writestring(const char *str);
void terminal_update_cursor(void);

/* Cor VGA (0xFB: fundo F, texto B). Sem efeito no console do GFD. */
void terminal_setcolor(unsigned char color);

#endif
