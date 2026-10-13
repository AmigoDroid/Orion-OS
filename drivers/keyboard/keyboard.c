
#include "keyboard.h"

typedef unsigned char u8;

static int shift_left;
static int shift_right;
static int caps_lock;
static int extended;
static int altgr;

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
    shift_left = 0;
    shift_right = 0;
    caps_lock = 0;
    extended = 0;
    altgr = 0;
}

/*
 * Caracteres especiais:
 * 0x87 = ç em CP437
 * 0x80 = Ç em CP437
 *
 * Esses códigos são úteis para o VGA e serão
 * reconhecidos também pelo console gráfico.
 */
static int map_scancode(u8 sc, int shift)
{
    int c = -1;

    /* ABNT2: AltGr+Q = '/'  AltGr+W = '?'  AltGr+E = '°'.
     * E o jeito de digitar '/' e '?' em notebooks sem a tecla extra. */
    if (altgr) {
        switch (sc) {
            case 0x10: return '/';
            case 0x11: return '?';
            case 0x12: return 0xF8;                 /* ° em CP437 */
            default: break;
        }
    }

    switch (sc) {
        case 0x01: return 27;

        /* Tecla a esquerda do 1: aspas simples e duplas no ABNT2 */
        case 0x29: c = shift ? '"' : '\''; break;
        case 0x02: c = shift ? '!' : '1'; break;
        case 0x03: c = shift ? '@' : '2'; break;
        case 0x04: c = shift ? '#' : '3'; break;
        case 0x05: c = shift ? '$' : '4'; break;
        case 0x06: c = shift ? '%' : '5'; break;
        case 0x07: c = shift ? '^' : '6'; break;
        case 0x08: c = shift ? '&' : '7'; break;
        case 0x09: c = shift ? '*' : '8'; break;
        case 0x0A: c = shift ? '(' : '9'; break;
        case 0x0B: c = shift ? ')' : '0'; break;
        case 0x0C: c = shift ? '_' : '-'; break;
        case 0x0D: c = shift ? '+' : '='; break;
        case 0x0E: return '\b';
        case 0x0F: return '\t';

        case 0x10: c = 'q'; break;
        case 0x11: c = 'w'; break;
        case 0x12: c = 'e'; break;
        case 0x13: c = 'r'; break;
        case 0x14: c = 't'; break;
        case 0x15: c = 'y'; break;
        case 0x16: c = 'u'; break;
        case 0x17: c = 'i'; break;
        case 0x18: c = 'o'; break;
        case 0x19: c = 'p'; break;

        /* ABNT2: acentos e colchetes à direita do P */
        case 0x1A: c = shift ? '`' : '\''; break;
        case 0x1B: c = shift ? '{' : '['; break;

        case 0x1C: return '\n';
        case 0x1E: c = 'a'; break;
        case 0x1F: c = 's'; break;
        case 0x20: c = 'd'; break;
        case 0x21: c = 'f'; break;
        case 0x22: c = 'g'; break;
        case 0x23: c = 'h'; break;
        case 0x24: c = 'j'; break;
        case 0x25: c = 'k'; break;
        case 0x26: c = 'l'; break;

        /* Tecla física Ç do ABNT2 */
        case 0x27: return shift ? 0x80 : 0x87;

        /* Tecla de til/circunflexo no ABNT2 */
        case 0x28: c = shift ? '^' : '~'; break;

        case 0x2B: c = shift ? '}' : ']'; break;

        /* Teclas extras do teclado brasileiro */
        case 0x56: c = shift ? '|' : '\\'; break;
        case 0x73: c = shift ? '?' : '/'; break;     /* a que fica antes do Shift direito */

        case 0x2C: c = 'z'; break;
        case 0x2D: c = 'x'; break;
        case 0x2E: c = 'c'; break;
        case 0x2F: c = 'v'; break;
        case 0x30: c = 'b'; break;
        case 0x31: c = 'n'; break;
        case 0x32: c = 'm'; break;

        case 0x33: c = shift ? '<' : ','; break;
        case 0x34: c = shift ? '>' : '.'; break;
        case 0x35: c = shift ? ':' : ';'; break;
        case 0x39: return ' ';

        default: return -1;
    }

    if (c >= 'a' && c <= 'z') {
        if (caps_lock != shift)
            c -= 'a' - 'A';
    }

    return c;
}

int keyboard_getchar(void)
{
    for (int tries = 0; tries < 16; tries++) {
        u8 status = inb(0x64);

        if (!(status & 1))
            return -1;

        /* Deixa o driver do mouse cuidar dos bytes dele. */
        if (status & 0x20)
            return -1;

        u8 sc = inb(0x60);

        if (sc == 0xE0 || sc == 0xE1) {
            extended = 1;
            continue;
        }

        int released = (sc & 0x80) != 0;
        sc &= 0x7F;

        if (extended) {
            extended = 0;

            if (sc == 0x38) {                       /* AltGr (Alt direito) */
                altgr = !released;
                continue;
            }
            if (released)
                continue;

            switch (sc) {
                case 0x1C: return '\n';
                case 0x35: return '/';
                default: continue;
            }
        }

        if (sc == 0x2A) {
            shift_left = !released;
            continue;
        }

        if (sc == 0x36) {
            shift_right = !released;
            continue;
        }

        if (sc == 0x3A) {
            if (!released)
                caps_lock = !caps_lock;
            continue;
        }

        if (released)
            continue;

        return map_scancode(
            sc,
            shift_left || shift_right
        );
    }

    return -1;
}
