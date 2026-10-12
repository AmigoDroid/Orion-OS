#include "log.h"
#include "io.h"

#define DEBUG_PORT 0xE9

void log_putc(char c)
{
    outb(DEBUG_PORT, (unsigned char)c);
}

void log_write(const char *message)
{
    while (*message)
        log_putc(*message++);
}

void log_init(void)
{
    log_write("[LOG] Sistema de logs iniciado\n");
}

void log_ok(const char *message)
{
    log_write("[ OK ] ");
    log_write(message);
    log_putc('\n');
}

void log_error(const char *message)
{
    log_write("[ERRO] ");
    log_write(message);
    log_putc('\n');
}

void log_hex(const char *label, unsigned int value)
{
    static const char digits[] = "0123456789ABCDEF";

    log_write(label);
    log_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        log_putc(digits[(value >> shift) & 0xF]);
    log_putc('\n');
}
