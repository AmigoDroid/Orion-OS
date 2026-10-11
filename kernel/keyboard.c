
#include "keyboard.h"
#include "gfd.h"

typedef unsigned char u8;

static inline u8 inb(unsigned short port)
{
    u8 value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

void keyboard_initialize(void)
{
    /* Nesta versão, o teclado usa leitura por consulta. */
}

int keyboard_getchar(void)
{
    /* Bit 0 indica que existe um dado disponível. */
    u8 status = inb(0x64);

    if ((status & 1) == 0) {
        return -1;
    }

    u8 scancode = inb(0x60);

    /* Bit 5 do status: o byte veio do mouse (porta auxiliar), nao do teclado. */
    if (status & 0x20) {
        if (gfd_mouse_feed)
            gfd_mouse_feed(scancode);
        return -1;
    }

    /* Ignora eventos de soltura da tecla. */
    if (scancode & 0x80) {
        return -1;
    }

    switch (scancode) {
        case 0x01: return 27;  /* Esc */
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        case 0x0C: return '-';
        case 0x0D: return '=';
        case 0x0E: return '\b';
        case 0x0F: return '\t';

        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';

        case 0x1C: return '\n';
        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';

        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';

        case 0x27: return ';';
        case 0x28: return '\'';
        case 0x29: return '`';
        case 0x2B: return '\\';
        case 0x33: return ',';
        case 0x34: return '.';
        case 0x35: return '/';
        case 0x39: return ' ';
        default: return -1;
    }
}
