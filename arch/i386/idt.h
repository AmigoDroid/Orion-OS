#ifndef ORION_IDT_H
#define ORION_IDT_H

#include <stdint.h>

/* Quadro salvo pelos stubs de isr.asm (de baixo para cima na pilha). */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;   /* pusha */
    uint32_t vector, error;
    uint32_t eip, cs, eflags;
} regs_t;

void idt_init(void);

/* Chamada pelos stubs. Excecoes fatais nao retornam (panic). */
void exception_dispatch(regs_t *r);

/* Corpo da tarefa do double fault (GDT_TSS_DF). Nao retorna. */
void double_fault_task(void) __attribute__((noreturn));

#endif
