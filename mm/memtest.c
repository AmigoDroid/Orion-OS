/* Autoteste do gerenciador de memoria (comando `memtest`).
 *
 * Nao e um teste "feliz": ele PROVOCA de proposito erros que o gerenciador
 * precisa pegar (free duplo, overflow de 1 byte, escrita apos free,
 * liberar frame do kernel) e confirma que foram detectados, e depois
 * conserta o estrago para o sistema seguir de pe. */

#include "mm.h"
#include "kprintf.h"
#include "string.h"

static int failures, checks;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        kprintf("  [FALHOU] %s\n", what);
    }
}

static uint32_t rng_state = 0x1234ABCDu;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void fill(uint8_t *p, size_t n, uint8_t seed)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)(seed + i * 7);
}

static int verify(const uint8_t *p, size_t n, uint8_t seed)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] != (uint8_t)(seed + i * 7))
            return 0;
    }
    return 1;
}

/* ----------------------------- PMM ----------------------------- */

static void test_pmm(void)
{
    enum { N = 512 };
    static uint32_t frames[N];
    pmm_stats_t before, after;
    uint32_t run;

    kprintf(" PMM: frames...\n");
    pmm_get_stats(&before);

    for (int i = 0; i < N; i++) {
        frames[i] = pmm_alloc_frame();
        check(frames[i] != 0 && (frames[i] & 0xFFF) == 0, "frame valido e alinhado");
        if (!frames[i])
            break;
        for (int j = 0; j < i; j++) {
            if (frames[j] == frames[i]) {
                check(0, "frame entregue duas vezes");
                break;
            }
        }
    }

    for (int i = 0; i < N; i++) {
        if (frames[i]) {
            memset((void *)(uintptr_t)frames[i], 0xA5, PAGE_SIZE);
            check(pmm_free_frame(frames[i]) == PMM_OK, "liberar frame alocado");
        }
    }

    check(pmm_free_frame(frames[0]) == PMM_ERR_DOUBLE, "free duplo de frame detectado");
    check(pmm_free_frame(0x100000) == PMM_ERR_RESERVED, "liberar frame do kernel recusado");
    check(pmm_free_frame(0x1001) == PMM_ERR_ALIGN, "endereco desalinhado recusado");
    check(pmm_free_frame(0xFFFFF000u) == PMM_ERR_RANGE, "fora da RAM recusado");

    run = pmm_alloc_frames(64);
    check(run != 0, "alocar 64 frames contiguos");
    if (run) {
        memset((void *)(uintptr_t)run, 0x5A, 64 * PAGE_SIZE);
        check(pmm_free_frames(run, 64) == PMM_OK, "liberar 64 frames contiguos");
    }
    check(pmm_alloc_frames(0) == 0, "alocar 0 frames devolve 0");
    check(pmm_alloc_frames(0x7FFFFFFFu) == 0, "pedido gigante falha sem travar");

    pmm_get_stats(&after);
    check(after.free_frames == before.free_frames, "PMM sem vazamento de frames");
    check(pmm_check() == 0, "bitmaps consistentes (pmm_check)");
}

/* ----------------------------- HEAP ----------------------------- */

static void test_heap_basic(void)
{
    heap_stats_t b, a;
    uint8_t *p, *q;
    void *big;

    kprintf(" HEAP: basico...\n");
    heap_get_stats(&b);

    check(kmalloc(0) == NULL, "kmalloc(0) devolve NULL");
    kfree(NULL);

    p = kmalloc(1);
    check(p && ((uintptr_t)p & 15) == 0, "kmalloc(1) alinhado em 16");
    q = kmalloc(100);
    check(q && p != q, "blocos distintos");
    check(kmalloc_usable_size(q) >= 100, "tamanho utilizavel >= pedido");

    fill(q, 100, 3);
    check(verify(q, 100, 3), "dados escritos permanecem");

    p = krealloc(p, 5000);
    check(p != NULL, "krealloc para cima");
    q = krealloc(q, 40);
    check(q && verify(q, 40, 3), "krealloc para baixo preserva o conteudo");
    q = krealloc(q, 4000);
    check(q && verify(q, 40, 3), "krealloc para cima preserva o conteudo");

    {
        void *r = krealloc(NULL, 32);
        check(r != NULL, "krealloc(NULL) == kmalloc");
        kfree(r);
    }

    {
        uint8_t *z = kcalloc(10, 50);
        int allzero = z != NULL;
        for (int i = 0; z && i < 500; i++)
            allzero &= z[i] == 0;
        check(allzero, "kcalloc zera a memoria");
        kfree(z);
    }
    check(kcalloc(0x10000000u, 0x20) == NULL, "kcalloc detecta overflow da multiplicacao");

    big = kmalloc(0x7FFFFFF0u);
    check(big == NULL, "pedido maior que o heap devolve NULL");

    kfree(p);
    kfree(q);
    check(heap_check(1, 1) == 0, "heap_check profundo limpo");

    heap_get_stats(&a);
    check(a.blocks_used == b.blocks_used && a.used_bytes == b.used_bytes,
          "sem vazamento depois do teste basico");
}

static void test_heap_errors(void)
{
    uint8_t *p, *q;
    kprintf(" HEAP: deteccao de erros...\n");

    p = kmalloc(64);
    check(p != NULL, "alloc 64");
    if (!p)
        return;

    check(kfree_checked(p) == HEAP_OK, "free valido");
    check(kfree_checked(p) == HEAP_ERR_DOUBLE, "free duplo detectado");
    check(kfree_checked(p + 1) != HEAP_OK, "ponteiro desalinhado recusado");
    check(kfree_checked((void *)0x1000) == HEAP_ERR_BADPTR, "ponteiro fora do heap recusado");

    /* overflow de 1 byte */
    p = kmalloc(32);
    p[32] ^= 0xFF;                           /* mexe no canario */
    check(kfree_checked(p) == HEAP_ERR_OVERFLOW, "overflow de 1 byte detectado");
    check(heap_check(0, 0) != 0, "heap_check tambem enxerga o canario");
    p[32] ^= 0xFF;                           /* conserta */
    check(heap_check(0, 0) == 0, "heap_check limpo apos conserto");
    check(kfree_checked(p) == HEAP_OK, "free apos conserto");

    /* escrita apos free */
    p = kmalloc(64);
    kfree(p);
    p[10] = 0x42;                            /* use-after-free */
    check(heap_check(1, 0) != 0, "use-after-free detectado pelo veneno");
    p[10] = 0xDD;                            /* conserta */
    check(heap_check(1, 0) == 0, "veneno restaurado");

    /* cabecalho destruido */
    p = kmalloc(48);
    q = kmalloc(48);
    {
        uint32_t *hdr = (uint32_t *)(p - 16);
        uint32_t saved = hdr[0];
        hdr[0] = 0;
        check(kfree_checked(p) == HEAP_ERR_CORRUPT, "cabecalho destruido detectado");
        check(heap_check(0, 0) != 0, "heap_check enxerga cabecalho destruido");
        hdr[0] = saved;
    }
    kfree(p);
    kfree(q);
    check(heap_check(1, 0) == 0, "heap limpo no fim");
}

static void test_heap_stress(void)
{
    enum { SLOTS = 256, ROUNDS = 4000 };
    static uint8_t *ptr[SLOTS];
    static uint32_t len[SLOTS];
    static uint8_t seed[SLOTS];
    heap_stats_t b, a;
    int bad = 0;

    kprintf(" HEAP: estresse (%d operacoes aleatorias)...\n", ROUNDS);
    heap_get_stats(&b);

    for (int i = 0; i < SLOTS; i++)
        ptr[i] = NULL;

    for (int r = 0; r < ROUNDS; r++) {
        int s = (int)(rng() % SLOTS);

        if (ptr[s]) {
            if (!verify(ptr[s], len[s], seed[s]))
                bad++;
            if (rng() % 3 == 0) {
                uint32_t nl = 1 + rng() % 3000;
                uint8_t *np = krealloc(ptr[s], nl);
                if (np) {
                    uint32_t keep = nl < len[s] ? nl : len[s];
                    if (!verify(np, keep, seed[s]))
                        bad++;
                    ptr[s] = np;
                    len[s] = nl;
                    seed[s] = (uint8_t)rng();
                    fill(np, nl, seed[s]);
                }
            } else {
                kfree(ptr[s]);
                ptr[s] = NULL;
            }
        } else {
            len[s] = 1 + rng() % (rng() % 8 == 0 ? 20000 : 600);
            ptr[s] = kmalloc(len[s]);
            if (ptr[s]) {
                seed[s] = (uint8_t)rng();
                fill(ptr[s], len[s], seed[s]);
            }
        }

        if (r % 500 == 0 && heap_check(0, 1) != 0)
            bad++;
    }

    for (int i = 0; i < SLOTS; i++) {
        if (ptr[i]) {
            if (!verify(ptr[i], len[i], seed[i]))
                bad++;
            kfree(ptr[i]);
        }
    }

    check(bad == 0, "nenhum dado corrompido durante o estresse");
    check(heap_check(1, 1) == 0, "heap_check profundo limpo apos o estresse");

    heap_get_stats(&a);
    check(a.blocks_used == b.blocks_used, "estresse sem vazamento de blocos");
    check(a.used_bytes == b.used_bytes, "estresse sem vazamento de bytes");
    check(a.blocks_free == b.blocks_free, "coalescencia: mesma contagem de blocos livres");
    check(a.largest_free == b.largest_free, "coalescencia: maior bloco livre restaurado");
}

static void test_heap_fragmentation(void)
{
    enum { N = 200 };
    static uint8_t *p[N];
    heap_stats_t b, a;

    kprintf(" HEAP: fragmentacao e coalescencia...\n");
    heap_get_stats(&b);

    for (int i = 0; i < N; i++)
        p[i] = kmalloc(64 + (i % 7) * 32);
    for (int i = 0; i < N; i += 2)
        kfree(p[i]);
    check(heap_check(0, 0) == 0, "heap valido com buracos alternados");
    for (int i = 1; i < N; i += 2)
        kfree(p[i]);

    heap_get_stats(&a);
    check(a.blocks_free == b.blocks_free && a.largest_free == b.largest_free,
          "tudo coalescido de volta ao estado inicial");
}

/* ----------------------------- VMM ----------------------------- */

static void test_vmm(void)
{
    uint32_t frame, virt = 0x70000000u, phys;
    volatile uint32_t *v, *id;

    kprintf(" VMM: mapeamento...\n");
    check(vmm_enabled(), "paginacao ligada");
    check(vmm_translate(0, &phys) != 0, "pagina 0 sem mapeamento (guarda do NULL)");
    check(vmm_translate(0x100000, &phys) == 0 && phys == 0x100000, "kernel mapeado por identidade");
    check(vmm_translate(virt, &phys) != 0, "endereco alto comeca sem mapeamento");

    frame = pmm_alloc_frame();
    check(frame != 0, "frame para o teste");
    if (!frame)
        return;

    check(vmm_map(virt, frame, VMM_WRITE) == 0, "vmm_map");
    check(vmm_map(virt, frame, VMM_WRITE) == -2, "mapear duas vezes e recusado");
    check(vmm_translate(virt, &phys) == 0 && phys == frame, "translate devolve o frame");

    v  = (volatile uint32_t *)(uintptr_t)virt;
    id = (volatile uint32_t *)(uintptr_t)frame;
    *v = 0xC0FFEE42u;
    check(*id == 0xC0FFEE42u, "escrita pelo virtual aparece no fisico");

    check(vmm_unmap(virt) == 0, "vmm_unmap");
    check(vmm_translate(virt, &phys) != 0, "desmapeado de fato");
    check(vmm_unmap(virt) != 0, "unmap duplo recusado");
    check(pmm_free_frame(frame) == PMM_OK, "devolve o frame");
}

int mm_selftest(void)
{
    failures = 0;
    checks = 0;

    kprintf("\n=== Autoteste do gerenciador de memoria ===\n");
    test_pmm();
    test_heap_basic();
    test_heap_errors();
    test_heap_stress();
    test_heap_fragmentation();
    test_vmm();

    if (failures)
        kprintf("=== %d de %d verificacoes FALHARAM ===\n\n", failures, checks);
    else
        kprintf("=== TODAS as %d verificacoes passaram ===\n\n", checks);
    return failures;
}
