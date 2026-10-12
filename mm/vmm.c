/* Memoria virtual (paginacao i386, sem PAE).
 *
 * Mapeamento por IDENTIDADE (virtual == fisico), entao nenhum ponteiro do
 * kernel muda de valor ao ligar a paginacao. O que a paginacao acrescenta
 * e PROTECAO:
 *   - pagina 0 sem mapeamento      -> NULL deref vira page fault legivel;
 *   - .text e .rodata somente leitura (CR0.WP=1) -> escrever em codigo ou
 *     em constante vira page fault, nao corrupcao silenciosa;
 *   - pagina de guarda abaixo da pilha -> estouro de pilha e pego;
 *   - tudo acima da RAM e do framebuffer fica SEM mapeamento: ponteiro
 *     solto nao "le 0xFF" de um endereco qualquer, ele falha.
 *
 * Para economizar tabelas, a RAM e mapeada com paginas de 4 MiB (PSE). So
 * os primeiros 4 MiB (onde moram o kernel e o VGA) usam paginas de 4 KiB.
 * vmm_map/vmm_unmap "quebram" uma pagina de 4 MiB em 1024 de 4 KiB quando
 * precisam mexer numa so. */

#include "mm.h"
#include "gdt.h"
#include "io.h"
#include "panic.h"
#include "kprintf.h"
#include "string.h"

#define P_PRESENT 0x001u
#define P_WRITE   0x002u
#define P_FLAGS   0x01Fu         /* P, W, U, PWT, PCD */
#define P_PS      0x080u         /* PDE de 4 MiB */
#define LARGE     0x400000u

extern char __text_start[], __text_end[], __rodata_start[], __rodata_end[];
extern char stack_guard[];

static uint32_t *page_dir;
static int paging_on;
static uint32_t n_tables, n_splits;

static inline uint32_t pdi(uint32_t v) { return v >> 22; }
static inline uint32_t pti(uint32_t v) { return (v >> 12) & 1023u; }

static uint32_t *new_table(void)
{
    uint32_t *t = (uint32_t *)(uintptr_t)pmm_alloc_frame();

    if (t) {
        memset(t, 0, PAGE_SIZE);
        n_tables++;
    }
    return t;
}

static void flush_tlb(void)
{
    write_cr3(read_cr3());
}

static int cpu_has_pse(void)
{
    uint32_t a = 1, b, c, d;

    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return (d >> 3) & 1;
}

void vmm_init(const mb_info_t *mbi)
{
    pmm_stats_t st;
    uint32_t *low;
    uint32_t top;

    if (!cpu_has_pse())
        panic("VMM: a CPU nao suporta paginas de 4 MiB (PSE)");

    page_dir = new_table();
    if (!page_dir)
        panic("VMM: sem memoria para o diretorio de paginas");

    /* RAM inteira, identidade, paginas de 4 MiB. */
    pmm_get_stats(&st);
    top = st.top_address;
    for (uint32_t i = 0; i < 1024 && ((uint64_t)i << 22) < top; i++)
        page_dir[i] = (i << 22) | P_PRESENT | P_WRITE | P_PS;

    /* Framebuffer do GFD (fica fora da RAM). */
    if ((mbi->flags & MB_FLAG_FRAMEBUFFER) && mbi->fb_addr_hi == 0 &&
        mbi->fb_addr_lo != 0) {
        uint32_t fb = mbi->fb_addr_lo;
        uint32_t last = fb + mbi->fb_pitch * mbi->fb_height - 1;

        for (uint32_t i = pdi(fb); i <= pdi(last); i++)
            page_dir[i] = (i << 22) | P_PRESENT | P_WRITE | P_PS;
    }

    /* Primeiros 4 MiB com granularidade de 4 KiB. */
    low = new_table();
    if (!low)
        panic("VMM: sem memoria para a tabela inicial");

    for (uint32_t p = 0; p < 1024; p++)
        low[p] = (p << 12) | P_PRESENT | P_WRITE;

    low[0] = 0;                                         /* guarda do NULL */

    for (uint32_t a = (uint32_t)(uintptr_t)__text_start;
         a < (uint32_t)(uintptr_t)__text_end; a += PAGE_SIZE)
        low[a >> 12] &= ~P_WRITE;
    for (uint32_t a = (uint32_t)(uintptr_t)__rodata_start;
         a < (uint32_t)(uintptr_t)__rodata_end; a += PAGE_SIZE)
        low[a >> 12] &= ~P_WRITE;

    low[(uint32_t)(uintptr_t)stack_guard >> 12] = 0;    /* guarda da pilha */

    page_dir[0] = (uint32_t)(uintptr_t)low | P_PRESENT | P_WRITE;

    write_cr4(read_cr4() | 0x10u);                      /* CR4.PSE */
    write_cr3((uint32_t)(uintptr_t)page_dir);
    write_cr0(read_cr0() | 0x80000000u | 0x10000u);     /* PG | WP */

    gdt_set_df_cr3((uint32_t)(uintptr_t)page_dir);
    paging_on = 1;
}

int vmm_enabled(void)
{
    return paging_on;
}

/* Troca a pagina de 4 MiB do indice `i` por uma tabela equivalente. */
static int split_large(uint32_t i)
{
    uint32_t pde = page_dir[i];
    uint32_t *t = new_table();

    if (!t)
        return -1;

    for (uint32_t p = 0; p < 1024; p++)
        t[p] = ((pde & 0xFFC00000u) + (p << 12)) | (pde & P_FLAGS);

    page_dir[i] = (uint32_t)(uintptr_t)t | (pde & P_FLAGS);
    n_splits++;
    flush_tlb();
    return 0;
}

/* Devolve a tabela de 4 KiB do indice (criando/quebrando se `create`). */
static uint32_t *table_for(uint32_t virt, int create)
{
    uint32_t i = pdi(virt), pde = page_dir[i];

    if (!(pde & P_PRESENT)) {
        uint32_t *t;

        if (!create)
            return NULL;
        t = new_table();
        if (!t)
            return NULL;
        page_dir[i] = (uint32_t)(uintptr_t)t | P_PRESENT | P_WRITE;
        return t;
    }

    if (pde & P_PS) {
        if (split_large(i) != 0)
            return NULL;
        pde = page_dir[i];
    }
    return (uint32_t *)(uintptr_t)(pde & 0xFFFFF000u);
}

int vmm_map(uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t *t;

    if (!paging_on || (virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1)))
        return -1;

    t = table_for(virt, 1);
    if (!t)
        return -1;
    if (t[pti(virt)] & P_PRESENT)
        return -2;                          /* ja mapeado: faca unmap antes */

    t[pti(virt)] = phys | (flags & P_FLAGS) | P_PRESENT;
    invlpg(virt);
    return 0;
}

int vmm_unmap(uint32_t virt)
{
    uint32_t *t;

    if (!paging_on || (virt & (PAGE_SIZE - 1)))
        return -1;
    if (!(page_dir[pdi(virt)] & P_PRESENT))
        return -1;

    t = table_for(virt, 0);
    if (!t || !(t[pti(virt)] & P_PRESENT))
        return -1;

    t[pti(virt)] = 0;
    invlpg(virt);
    return 0;
}

int vmm_translate(uint32_t virt, uint32_t *phys_out)
{
    uint32_t pde, pte;

    if (!paging_on) {                       /* sem paginacao: identidade */
        if (phys_out)
            *phys_out = virt;
        return 0;
    }

    pde = page_dir[pdi(virt)];
    if (!(pde & P_PRESENT))
        return -1;

    if (pde & P_PS) {
        if (phys_out)
            *phys_out = (pde & 0xFFC00000u) | (virt & 0x3FFFFFu);
        return 0;
    }

    pte = ((uint32_t *)(uintptr_t)(pde & 0xFFFFF000u))[pti(virt)];
    if (!(pte & P_PRESENT))
        return -1;
    if (phys_out)
        *phys_out = (pte & 0xFFFFF000u) | (virt & 0xFFFu);
    return 0;
}

int vmm_map_mmio(uint32_t phys, uint32_t size)
{
    uint32_t a = PAGE_ALIGN_DOWN(phys);
    uint32_t end = PAGE_ALIGN_UP(phys + size);

    for (; a < end; a += PAGE_SIZE) {
        uint32_t cur;

        if (vmm_translate(a, &cur) == 0 && cur == a)
            continue;
        if (vmm_map(a, a, VMM_WRITE | VMM_NOCACHE) != 0)
            return -1;
    }
    return 0;
}

void vmm_print_info(void)
{
    uint32_t large = 0, tables = 0;

    if (!paging_on) {
        kprintf("Paginacao desligada.\n");
        return;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        if (!(page_dir[i] & P_PRESENT))
            continue;
        if (page_dir[i] & P_PS)
            large++;
        else
            tables++;
    }

    kprintf("Paginacao LIGADA (CR3=%08x, CR0=%08x)\n", read_cr3(), read_cr0());
    kprintf("  paginas de 4 MiB: %u   tabelas de 4 KiB: %u   (quebras: %u)\n",
            large, tables, n_splits);
    kprintf("  .text   %08x-%08x  somente leitura\n",
            (uint32_t)(uintptr_t)__text_start, (uint32_t)(uintptr_t)__text_end);
    kprintf("  .rodata %08x-%08x  somente leitura\n",
            (uint32_t)(uintptr_t)__rodata_start, (uint32_t)(uintptr_t)__rodata_end);
    kprintf("  pagina 0 e guarda da pilha (%08x): sem mapeamento\n",
            (uint32_t)(uintptr_t)stack_guard);
}
