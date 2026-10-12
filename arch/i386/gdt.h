#ifndef ORION_GDT_H
#define ORION_GDT_H

#include <stdint.h>

#define GDT_KCODE   0x08
#define GDT_KDATA   0x10
#define GDT_TSS_DF  0x18        /* tarefa do double fault (pilha propria) */
#define GDT_TSS_MAIN 0x20       /* tarefa principal (exigida pelo ltr) */

typedef struct {
    uint32_t link, esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap;
} __attribute__((packed)) tss_t;

void gdt_init(void);

/* O task switch do double fault recarrega CR3; o VMM avisa o valor certo. */
void gdt_set_df_cr3(uint32_t cr3);

/* Estado da tarefa principal salvo pela CPU quando o #DF disparou. */
const tss_t *gdt_main_tss(void);

#endif
