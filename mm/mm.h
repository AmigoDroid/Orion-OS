#ifndef ORION_MM_H
#define ORION_MM_H

/* Gerenciador de memoria do Orion OS (ver docs/MEMORY.md).
 *
 *   PMM   mm/pmm.c   frames fisicos de 4 KiB (bitmap)
 *   VMM   mm/vmm.c   paginacao: protecao de NULL, de codigo e da pilha
 *   HEAP  mm/heap.c  kmalloc/kfree sobre frames contiguos do PMM
 *
 * Ordem de uso: mm_init() -> vmm_init(). Interrupcoes ficam desligadas no
 * Orion, entao nada aqui e reentrante nem precisa de trava. */

#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"

#define PAGE_SIZE   4096u
#define PAGE_ALIGN_DOWN(x) ((x) & ~(PAGE_SIZE - 1u))
#define PAGE_ALIGN_UP(x)   (((x) + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u))

/* ------------------------------ PMM ------------------------------ */

enum {
    PMM_OK = 0,
    PMM_ERR_ALIGN = -1,      /* endereco nao alinhado em 4 KiB            */
    PMM_ERR_RANGE = -2,      /* fora da RAM conhecida                     */
    PMM_ERR_RESERVED = -3,   /* frame reservado (kernel, BIOS, ...)       */
    PMM_ERR_DOUBLE = -4      /* frame ja estava livre (double free)       */
};

typedef struct {
    uint32_t total_frames;       /* RAM utilizavel segundo o firmware     */
    uint32_t free_frames;
    uint32_t reserved_frames;    /* retidos pelo kernel (1 MiB, imagem..) */
    uint32_t allocated_frames;   /* entregues por pmm_alloc_*             */
    uint32_t largest_free_run;   /* maior trecho contiguo livre (frames)  */
    uint32_t top_address;        /* fim da RAM utilizavel (bytes, <4 GiB) */
    uint32_t alloc_calls, free_calls, fail_calls, error_calls;
} pmm_stats_t;

void     pmm_init(const mb_info_t *mbi);
uint32_t pmm_alloc_frame(void);                 /* 0 = sem memoria        */
uint32_t pmm_alloc_frames(uint32_t count);      /* contiguos; 0 = falha   */
int      pmm_free_frame(uint32_t phys);
int      pmm_free_frames(uint32_t phys, uint32_t count);
void     pmm_get_stats(pmm_stats_t *out);
int      pmm_check(void);                       /* devolve nr de erros    */
void     pmm_print_map(const mb_info_t *mbi);   /* mapa E820 bruto        */

/* ------------------------------ HEAP ------------------------------ */

enum {
    HEAP_OK = 0,
    HEAP_ERR_BADPTR = -1,    /* ponteiro que nao e do heap / mal alinhado */
    HEAP_ERR_DOUBLE = -2,    /* free duplo                                */
    HEAP_ERR_OVERFLOW = -3,  /* canario depois do bloco foi sobrescrito   */
    HEAP_ERR_CORRUPT = -4    /* cabecalho do bloco destruido              */
};

typedef struct {
    uint32_t total_bytes;        /* tamanho da regiao do heap             */
    uint32_t used_bytes;         /* soma do payload dos blocos em uso     */
    uint32_t free_bytes;         /* soma do payload dos blocos livres     */
    uint32_t overhead_bytes;     /* cabecalhos                            */
    uint32_t largest_free;       /* maior bloco livre (payload)           */
    uint32_t blocks_used, blocks_free;
    uint32_t peak_used_bytes;
    uint32_t alloc_calls, free_calls, fail_calls;
} heap_stats_t;

void  heap_init(uint32_t base, uint32_t size);
void *kmalloc(size_t size);                     /* NULL se nao couber     */
void *kcalloc(size_t count, size_t size);       /* zerado, com overflow-check */
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);                         /* panic se detectar corrupcao */
int   kfree_checked(void *ptr);                 /* igual, mas devolve o erro   */
size_t kmalloc_usable_size(const void *ptr);
void  heap_get_stats(heap_stats_t *out);
/* Percorre TODOS os blocos. deep=1 tambem confere o veneno dos livres
 * (pega escrita apos free). Devolve o numero de erros; imprime se verbose. */
int   heap_check(int deep, int verbose);

/* ------------------------------ VMM ------------------------------ */

#define VMM_WRITE   0x002u
#define VMM_USER    0x004u
#define VMM_NOCACHE 0x018u      /* PWT | PCD: para MMIO */

void vmm_init(const mb_info_t *mbi);
int  vmm_map(uint32_t virt, uint32_t phys, uint32_t flags);   /* 1 pagina */
int  vmm_unmap(uint32_t virt);
int  vmm_translate(uint32_t virt, uint32_t *phys_out);        /* 0 = mapeado */
int  vmm_map_mmio(uint32_t phys, uint32_t size);              /* identidade, UC */
int  vmm_enabled(void);
void vmm_print_info(void);

/* ------------------------------ GERAL ------------------------------ */

void mm_init(const mb_info_t *mbi);      /* PMM + heap */
void mm_print_info(void);                /* comando `mem` */
void mm_print_map(void);                 /* comando `memmap` */
int  mm_selftest(void);                  /* comando `memtest`; devolve falhas */

#endif
