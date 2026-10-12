/* Cola do gerenciador de memoria: inicializacao e relatorio (`mem`). */

#include "mm.h"
#include "panic.h"
#include "kprintf.h"
#include "string.h"

#define HEAP_MIN (256u * 1024u)
#define HEAP_MAX (32u * 1024u * 1024u)

static const mb_info_t *boot_info;

static uint32_t kib(uint32_t frames) { return frames * (PAGE_SIZE / 1024u); }

void mm_init(const mb_info_t *mbi)
{
    pmm_stats_t st;
    uint32_t bytes, base;

    boot_info = mbi;
    pmm_init(mbi);
    pmm_get_stats(&st);

    /* Heap = 1/4 da RAM livre (entre 256 KiB e 32 MiB), num bloco contiguo. */
    bytes = st.free_frames / 4 * PAGE_SIZE;
    if (bytes > HEAP_MAX)
        bytes = HEAP_MAX;
    if (bytes < HEAP_MIN)
        bytes = HEAP_MIN;
    if (bytes > st.largest_free_run * PAGE_SIZE)
        bytes = st.largest_free_run * PAGE_SIZE;
    if (bytes < PAGE_SIZE * 16)
        panic("memoria insuficiente para o heap (%u KiB livres)", kib(st.free_frames));

    base = pmm_alloc_frames(bytes / PAGE_SIZE);
    if (!base)
        panic("PMM nao entregou %u KiB contiguos para o heap", bytes / 1024);

    heap_init(base, bytes);
    kprintf(" Memoria: %u KiB utilizaveis, heap de %u KiB em %08x.\n",
            kib(st.total_frames), bytes / 1024, base);
}

void mm_print_info(void)
{
    pmm_stats_t p;
    heap_stats_t h;

    pmm_get_stats(&p);
    heap_get_stats(&h);

    kprintf("\nMemoria fisica (PMM, frames de 4 KiB):\n");
    kprintf("  utilizavel  %8u KiB  (topo da RAM em %08x)\n",
            kib(p.total_frames), p.top_address);
    kprintf("  livre       %8u KiB  (maior trecho contiguo: %u KiB)\n",
            kib(p.free_frames), kib(p.largest_free_run));
    kprintf("  kernel/BIOS %8u KiB  (reservado, nunca liberado)\n", kib(p.reserved_frames));
    kprintf("  alocado     %8u KiB  (heap e tabelas de pagina)\n", kib(p.allocated_frames));
    kprintf("  chamadas: %u alloc, %u free, %u falhas, %u erros\n",
            p.alloc_calls, p.free_calls, p.fail_calls, p.error_calls);

    kprintf("\nHeap do kernel:\n");
    kprintf("  total       %8u KiB\n", h.total_bytes / 1024);
    kprintf("  em uso      %8u bytes em %u blocos (pico: %u bytes)\n",
            h.used_bytes, h.blocks_used, h.peak_used_bytes);
    kprintf("  livre       %8u bytes em %u blocos (maior: %u bytes)\n",
            h.free_bytes, h.blocks_free, h.largest_free);
    kprintf("  cabecalhos  %8u bytes\n", h.overhead_bytes);
    kprintf("  chamadas: %u kmalloc, %u kfree, %u falhas\n",
            h.alloc_calls, h.free_calls, h.fail_calls);
    kprintf("  vazamento:  %u blocos ainda alocados\n\n", h.blocks_used);

    (void)boot_info;
}

void mm_print_map(void)
{
    kprintf("\nMapa de memoria do firmware (E820):\n");
    pmm_print_map(boot_info);
    kprintf("\n");
}
