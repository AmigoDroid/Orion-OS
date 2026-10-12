#include "gdt.h"
#include "idt.h"

typedef struct {
    uint16_t limit_lo, base_lo;
    uint8_t  base_mid, access, gran, base_hi;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

#define DF_STACK_SIZE 4096

static gdt_entry_t gdt[5];
static gdt_ptr_t   gdt_ptr;
static tss_t       tss_main;
static tss_t       tss_df;
static uint8_t     df_stack[DF_STACK_SIZE] __attribute__((aligned(16)));

static void gdt_set(int i, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t gran)
{
    gdt[i].limit_lo = limit & 0xFFFF;
    gdt[i].base_lo  = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].access   = access;
    gdt[i].gran     = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].base_hi  = (base >> 24) & 0xFF;
}

void gdt_init(void)
{
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);                  /* codigo ring 0 */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);                  /* dados  ring 0 */

    /* Tarefa do #DF: roda double_fault_task() numa pilha separada, entao
     * funciona mesmo quando o estouro de pilha impede de entregar a #PF. */
    tss_df.eip    = (uint32_t)(uintptr_t)double_fault_task;
    tss_df.eflags = 0x2;
    tss_df.esp    = (uint32_t)(uintptr_t)(df_stack + DF_STACK_SIZE);
    tss_df.cs     = GDT_KCODE;
    tss_df.ss     = tss_df.ds = tss_df.es = tss_df.fs = tss_df.gs = GDT_KDATA;
    tss_df.iomap  = sizeof(tss_t);

    tss_main.ss0   = GDT_KDATA;
    tss_main.iomap = sizeof(tss_t);

    gdt_set(3, (uint32_t)(uintptr_t)&tss_df,   sizeof(tss_t) - 1, 0x89, 0x00);
    gdt_set(4, (uint32_t)(uintptr_t)&tss_main, sizeof(tss_t) - 1, 0x89, 0x00);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)(uintptr_t)gdt;

    __asm__ volatile (
        "lgdt %0\n\t"
        "ljmp %1, $1f\n"
        "1:\n\t"
        "mov %2, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %3, %%ax\n\t"
        "ltr %%ax"
        :
        : "m"(gdt_ptr), "i"(GDT_KCODE), "i"(GDT_KDATA), "i"(GDT_TSS_MAIN)
        : "eax", "memory");
}

void gdt_set_df_cr3(uint32_t cr3)
{
    tss_df.cr3 = cr3;
}

const tss_t *gdt_main_tss(void)
{
    return &tss_main;
}
