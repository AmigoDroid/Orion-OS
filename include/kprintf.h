#ifndef ORION_KPRINTF_H
#define ORION_KPRINTF_H

#include <stdarg.h>

/* printf minimo do kernel. Suporta: %s %c %d %i %u %x %X %p %% com largura
 * e '0' (ex.: %08x, %5u, %-10s). Nao suporta 64 bits nem ponto flutuante
 * (isso exigiria divisao 64 bits e, portanto, libgcc). */

void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void kvprintf(const char *fmt, va_list ap);

/* Igual ao kprintf, mas so para o log de depuracao (porta 0xE9), nunca
 * para a tela. Usado em situacoes em que a tela nao e confiavel (panic). */
void klog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Escreve em buffer; devolve quantos caracteres coube (sem o '\0'). */
int ksnprintf(char *buf, unsigned int size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#endif
