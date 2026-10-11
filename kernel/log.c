
#include "log.h"

#define DEBUG_PORT 0xE9

static void debug_putc(char c)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(c), "Nd"(DEBUG_PORT)
    );
}

void log_init(void)
{
    log_write("\n[LOG] Sistema de logs iniciado\n");
}

void log_write(const char *message)
{
    while (*message) {
        if (*message == '\n')
            debug_putc('\r');

        debug_putc(*message++);
    }
}

void log_ok(const char *message)
{
    log_write("[ OK ] ");
    log_write(message);
    log_write("\n");
}

void log_error(const char *message)
{
    log_write("[ERRO] ");
    log_write(message);
    log_write("\n");
}

void log_hex(const char *label, unsigned int value)
{
    static const char digits[] = "0123456789ABCDEF";

    log_write(label);
    log_write("0x");

    for (int shift = 28; shift >= 0; shift -= 4)
        debug_putc(digits[(value >> shift) & 0xF]);

    log_write("\n");
}
