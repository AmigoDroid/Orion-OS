/* Gerenciador de memoria FISICA: um bit por frame de 4 KiB.
 *
 * Dois bitmaps (128 KiB cada, cobrem 4 GiB):
 *   used_map  1 = frame ocupado (reservado OU alocado)
 *   rsvd_map  1 = frame reservado para sempre (nunca pode ser liberado)
 * Comecam TODOS em 1: so vira livre o que o firmware declara disponivel
 * (tipo 1 do mapa E820) E que o kernel nao reservou. Isso e o oposto do
 * ingenuo "tudo livre ate prova em contrario" e evita entregar memoria de
 * BIOS/ACPI/framebuffer por engano. */

#include "mm.h"
#include "panic.h"
#include "kprintf.h"
#include "string.h"

#define MAX_FRAMES (1u << 20)               /* 4 GiB / 4 KiB */
#define MAP_WORDS  (MAX_FRAMES / 32)

extern char __kernel_start[], __kernel_end[];

static uint32_t used_map[MAP_WORDS];
static uint32_t rsvd_map[MAP_WORDS];
static uint32_t frame_limit;                /* 1 + ultimo frame de RAM */
static uint32_t hint;                       /* proxima busca (next-fit) */

static uint32_t st_total, st_free, st_reserved;
static uint32_t n_alloc, n_free, n_fail, n_err;

static inline int  bit_get(const uint32_t *m, uint32_t i) { return (m[i >> 5] >> (i & 31)) & 1; }
static inline void bit_set(uint32_t *m, uint32_t i)       { m[i >> 5] |= 1u << (i & 31); }
static inline void bit_clr(uint32_t *m, uint32_t i)       { m[i >> 5] &= ~(1u << (i & 31)); }

/* Declara [base, base+len) como RAM utilizavel (so frames inteiros). */
static void add_available(uint32_t base_lo, uint32_t base_hi,
                          uint32_t len_lo, uint32_t len_hi)
{
    uint64_t base, end;

    if (base_hi)
        return;                                     /* acima de 4 GiB */

    base = base_lo;
    end  = base + (((uint64_t)len_hi << 32) | len_lo);
    if (end > 0xFFFFF000ull)
        end = 0xFFFFF000ull;

    base = (base + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    end &= ~(uint64_t)(PAGE_SIZE - 1);
    if (base >= end)
        return;

    for (uint32_t f = (uint32_t)(base >> 12); f < (uint32_t)(end >> 12); f++) {
        if (bit_get(rsvd_map, f)) {                 /* ainda nao contado */
            bit_clr(rsvd_map, f);
            bit_clr(used_map, f);
            st_total++;
            st_free++;
        }
    }
    if ((uint32_t)(end >> 12) > frame_limit)
        frame_limit = (uint32_t)(end >> 12);
}

/* Retira [start, end) do uso do PMM, para sempre. */
static void reserve_range(uint32_t start, uint32_t end)
{
    uint32_t first = start >> 12;
    uint32_t last  = (uint32_t)(((uint64_t)end + PAGE_SIZE - 1) >> 12);

    if (last > MAX_FRAMES)
        last = MAX_FRAMES;

    for (uint32_t f = first; f < last; f++) {
        if (bit_get(rsvd_map, f))
            continue;
        bit_set(rsvd_map, f);
        if (!bit_get(used_map, f)) {
            bit_set(used_map, f);
            st_free--;
        }
        st_reserved++;
    }
}

static uint32_t bounded_strlen(const char *s)
{
    uint32_t n = 0;
    while (n < 256 && s[n])
        n++;
    return n + 1;
}

void pmm_init(const mb_info_t *mbi)
{
    memset(used_map, 0xFF, sizeof(used_map));
    memset(rsvd_map, 0xFF, sizeof(rsvd_map));
    frame_limit = 0;

    if (mbi->flags & MB_FLAG_MMAP) {
        uint32_t p = mbi->mmap_addr, end = mbi->mmap_addr + mbi->mmap_length;

        while (p + sizeof(mb_mmap_entry_t) <= end) {
            const mb_mmap_entry_t *e = (const mb_mmap_entry_t *)(uintptr_t)p;

            if (e->type == MB_MMAP_AVAILABLE)
                add_available(e->base_lo, e->base_hi, e->len_lo, e->len_hi);

            if (e->size < 20)                       /* entrada invalida */
                break;
            p += e->size + 4;
        }
    } else if (mbi->flags & MB_FLAG_MEM) {
        /* Sem mapa E820: so a memoria alta (a partir de 1 MiB) e confiavel. */
        add_available(0x100000, 0, mbi->mem_upper * 1024u, 0);
    } else {
        panic("PMM: o bootloader nao informou o mapa de memoria");
    }

    if (st_total == 0)
        panic("PMM: nenhuma memoria RAM utilizavel encontrada");

    /* O que o kernel NAO pode entregar a ninguem: */
    reserve_range(0, 0x100000);                     /* BIOS, VGA, EBDA, stage2 */
    reserve_range((uint32_t)(uintptr_t)__kernel_start,
                  (uint32_t)(uintptr_t)__kernel_end); /* imagem + BSS + pilha */

    /* Estruturas que o bootloader deixou em memoria. */
    reserve_range((uint32_t)(uintptr_t)mbi, (uint32_t)(uintptr_t)mbi + sizeof(*mbi));
    if (mbi->flags & MB_FLAG_MMAP)
        reserve_range(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);
    if (mbi->flags & MB_FLAG_LOADER_NAME)
        reserve_range(mbi->boot_loader_name,
                      mbi->boot_loader_name +
                      bounded_strlen((const char *)(uintptr_t)mbi->boot_loader_name));
    if (mbi->flags & MB_FLAG_CMDLINE)
        reserve_range(mbi->cmdline,
                      mbi->cmdline +
                      bounded_strlen((const char *)(uintptr_t)mbi->cmdline));
    if (mbi->flags & MB_FLAG_MODS) {
        const mb_module_t *m = (const mb_module_t *)(uintptr_t)mbi->mods_addr;

        reserve_range(mbi->mods_addr,
                      mbi->mods_addr + mbi->mods_count * (uint32_t)sizeof(*m));
        for (uint32_t i = 0; i < mbi->mods_count; i++)
            reserve_range(m[i].start, m[i].end);
    }

    hint = 0;
}

/* Procura `n` frames livres seguidos em [lo, hi). -1 se nao houver. */
static int32_t find_run(uint32_t n, uint32_t lo, uint32_t hi)
{
    uint32_t run = 0, start = 0, f = lo;

    while (f < hi) {
        if ((f & 31) == 0 && used_map[f >> 5] == 0xFFFFFFFFu) {
            run = 0;
            f += 32;
            continue;
        }
        if (!bit_get(used_map, f)) {
            if (run == 0)
                start = f;
            if (++run == n)
                return (int32_t)start;
        } else {
            run = 0;
        }
        f++;
    }
    return -1;
}

uint32_t pmm_alloc_frames(uint32_t count)
{
    int32_t first;

    if (count == 0 || count > st_free) {
        n_fail++;
        return 0;
    }

    first = find_run(count, hint, frame_limit);
    if (first < 0) {
        uint32_t stop = hint + count;
        first = find_run(count, 0, stop < frame_limit ? stop : frame_limit);
    }
    if (first < 0) {
        n_fail++;
        return 0;
    }

    for (uint32_t i = 0; i < count; i++)
        bit_set(used_map, (uint32_t)first + i);

    st_free -= count;
    n_alloc++;
    hint = (uint32_t)first + count;
    if (hint >= frame_limit)
        hint = 0;

    return (uint32_t)first << 12;
}

uint32_t pmm_alloc_frame(void)
{
    return pmm_alloc_frames(1);
}

int pmm_free_frames(uint32_t phys, uint32_t count)
{
    uint32_t first = phys >> 12;

    n_free++;

    if (phys & (PAGE_SIZE - 1) || count == 0) {
        n_err++;
        return PMM_ERR_ALIGN;
    }
    if (first >= frame_limit || count > frame_limit - first) {
        n_err++;
        return PMM_ERR_RANGE;
    }

    /* Valida TUDO antes de mexer em qualquer bit: ou libera o bloco
     * inteiro, ou nao toca em nada. */
    for (uint32_t i = 0; i < count; i++) {
        if (bit_get(rsvd_map, first + i)) {
            n_err++;
            return PMM_ERR_RESERVED;
        }
        if (!bit_get(used_map, first + i)) {
            n_err++;
            return PMM_ERR_DOUBLE;
        }
    }

    for (uint32_t i = 0; i < count; i++)
        bit_clr(used_map, first + i);

    st_free += count;
    if (first < hint)
        hint = first;
    return PMM_OK;
}

int pmm_free_frame(uint32_t phys)
{
    return pmm_free_frames(phys, 1);
}

void pmm_get_stats(pmm_stats_t *out)
{
    uint32_t best = 0, run = 0;

    out->total_frames     = st_total;
    out->free_frames      = st_free;
    out->reserved_frames  = st_reserved;
    out->allocated_frames = st_total - st_free - st_reserved;
    out->top_address      = frame_limit << 12;
    out->alloc_calls = n_alloc;
    out->free_calls  = n_free;
    out->fail_calls  = n_fail;
    out->error_calls = n_err;

    for (uint32_t f = 0; f < frame_limit; f++) {
        if ((f & 31) == 0 && used_map[f >> 5] == 0xFFFFFFFFu) {
            run = 0;
            f += 31;
            continue;
        }
        if (!bit_get(used_map, f)) {
            if (++run > best)
                best = run;
        } else {
            run = 0;
        }
    }
    out->largest_free_run = best;
}

/* Reconta tudo a partir dos bitmaps e compara com os contadores. */
int pmm_check(void)
{
    uint32_t free_count = 0, bad = 0;

    for (uint32_t f = 0; f < frame_limit; f++) {
        int used = bit_get(used_map, f), rsvd = bit_get(rsvd_map, f);

        if (rsvd && !used)
            bad++;                          /* reservado tem de estar usado */
        if (!used)
            free_count++;
    }

    if (free_count != st_free)
        bad++;
    return (int)bad;
}

void pmm_print_map(const mb_info_t *mbi)
{
    static const char *const names[] = {
        "?", "disponivel", "reservado", "ACPI recuperavel", "ACPI NVS", "defeituoso"
    };

    if (!(mbi->flags & MB_FLAG_MMAP)) {
        kprintf("  (o bootloader nao enviou mapa de memoria)\n");
        return;
    }

    uint32_t p = mbi->mmap_addr, end = mbi->mmap_addr + mbi->mmap_length;

    while (p + sizeof(mb_mmap_entry_t) <= end) {
        const mb_mmap_entry_t *e = (const mb_mmap_entry_t *)(uintptr_t)p;
        uint64_t last = (((uint64_t)e->base_hi << 32) | e->base_lo) +
                        (((uint64_t)e->len_hi << 32) | e->len_lo) - 1;

        kprintf("  %08x%08x - %08x%08x  %s\n",
                e->base_hi, e->base_lo,
                (uint32_t)(last >> 32), (uint32_t)last,
                e->type < 6 ? names[e->type] : names[0]);

        if (e->size < 20)
            break;
        p += e->size + 4;
    }
}
