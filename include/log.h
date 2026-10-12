#ifndef ORION_LOG_H
#define ORION_LOG_H

/* Log de depuracao pela porta 0xE9 (QEMU: -debugcon file:log.txt ou
 * -debugcon stdio). Tudo que o terminal escreve tambem vai para c\xc3\xa1, o
 * que da um "transcript" completo do boot para testes automaticos. */

void log_init(void);
void log_putc(char c);
void log_write(const char *message);
void log_ok(const char *message);
void log_error(const char *message);
void log_hex(const char *label, unsigned int value);

#endif
