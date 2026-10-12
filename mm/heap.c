/* Heap do kernel: kmalloc / kfree / krealloc / kcalloc.
 *
 * Uma unica regiao contigua de frames do PMM, dividida em blocos em ordem
 * de endereco. Cada bloco tem um cabecalho de 16 bytes:
 *
 *      +--------+--------+--------+--------+---------------------+--------+
 *      | magic  | size   | prev   | req    | payload (size bytes)| canary |
 *      +--------+--------+--------+--------+---------------------+--------+
 *
 * Defesas (todas ligadas sempre; o custo e aceitavel para um kernel pequeno):
 *   - magic distinto para USADO e LIVRE: pega free duplo e ponteiro invalido;
 *   - canario de 4 bytes colado ao fim dos `req` bytes pedidos: pega
 *     escrita 1 byte alem do buffer, checado em cada free/realloc;
 *   - veneno 0xDD em tudo que e livre: se alguem escreveu num bloco ja
 *     liberado, o veneno muda e o proximo kmalloc que reaproveita o bloco
 *     (ou heap_check(deep)) acusa "use-after-free";
 *   - memoria nova vem preenchida com 0xCD, para leitura de memoria nao
 *     inicializada dar lixo reconhecivel, nao "zero por sorte";
 *   - lista duplamente encadeada em ordem de endereco, conferida a cada
 *     passo; coalescencia imediata de vizinhos livres (sem fragmentacao
 *     por blocos livres adjacentes);
 *   - best-fit: escolhe o menor bloco livre que serve.
 *
 * Politica de falha: pedido que nao cabe devolve NULL; corrupcao detectada
 * em kfree/kmalloc/krealloc e fatal (panic) com o endereco do bloco. */

#include "mm.h"
#include "panic.h"
#include "kprintf.h"
#include "string.h"

#define MAGIC_USED  0xA110C8EDu
#define MAGIC_FREE  0xF4EEB10Cu
#define HDR         16u
#define MIN_PAYLOAD 16u
#define POISON_FREE 0xDD
#define POISON_NEW  0xCD
static const uint8_t CANARY[4] = { 0xC4, 0xA7, 0x5E, 0xED };

typedef struct block {
    uint32_t      magic;
    uint32_t      size;     /* bytes de payload (multiplo de 16, >= 16) */
    struct block *prev;     /* bloco anterior em memoria, NULL no primeiro */
    uint32_t      req;      /* bytes pedidos pelo usuario (0 se livre) */
} block_t;

_Static_assert(sizeof(block_t) == HDR, "cabecalho do heap deve ter 16 bytes");

static uint8_t *heap_start, *heap_end;
static uint32_t st_used, st_peak, st_calls_alloc, st_calls_free, st_fail;

static inline uint32_t align16(uint32_t n)          { return (n + 15u) & ~15u; }
static inline uint8_t *payload(block_t *b)          { return (uint8_t *)b + HDR; }
static inline block_t *next_of(block_t *b)
{
    uint8_t *n = (uint8_t *)b + HDR + b->size;
    return n < heap_end ? (block_t *)n : NULL;
}

static void write_canary(block_t *b)
{
    memcpy(payload(b) + b->req, CANARY, sizeof(CANARY));
}

static int canary_ok(const block_t *b)
{
    return memcmp(payload((block_t *)b) + b->req, CANARY, sizeof(CANARY)) == 0;
}

/* Cabecalho plausivel? (usado antes de confiar em qualquer campo) */
static int header_sane(const block_t *b)
{
    uint8_t *p = (uint8_t *)b;

    if (p < heap_start || p + HDR > heap_end)
        return 0;
    if (((uintptr_t)p & 15u) != 0)
        return 0;
    if (b->magic != MAGIC_USED && b->magic != MAGIC_FREE)
        return 0;
    if (b->size < MIN_PAYLOAD || (b->size & 15u) != 0)
        return 0;
    if (p + HDR + b->size > heap_end)
        return 0;
    if (b->magic == MAGIC_USED && b->req + sizeof(CANARY) > b->size)
        return 0;
    return 1;
}

static block_t *must_block(block_t *b, const char *where)
{
    if (!header_sane(b))
        panic("heap corrompido (%s) no bloco %p: magic=%08x size=%u",
              where, (void *)b, b->magic, b->size);
    return b;
}

void heap_init(uint32_t base, uint32_t size)
{
    block_t *b;

    if ((base & (PAGE_SIZE - 1)) || size < PAGE_SIZE)
        panic("heap_init: regiao invalida (%08x, %u)", base, size);

    heap_start = (uint8_t *)(uintptr_t)base;
    heap_end   = heap_start + size;

    b = (block_t *)heap_start;
    b->magic = MAGIC_FREE;
    b->size  = size - HDR;
    b->prev  = NULL;
    b->req   = 0;
    memset(payload(b), POISON_FREE, b->size);

    st_used = st_peak = st_calls_alloc = st_calls_free = st_fail = 0;
}

/* Corta `b` em [b: need] + [novo livre: resto], se sobrar espaco util.
 * O resto precisa ja estar envenenado (ou ser envenenado pelo chamador). */
static void split(block_t *b, uint32_t need)
{
    uint32_t rest;
    block_t *n, *after;

    if (b->size < need + HDR + MIN_PAYLOAD)
        return;

    rest = b->size - need - HDR;
    n = (block_t *)(payload(b) + need);
    n->magic = MAGIC_FREE;
    n->size  = rest;
    n->prev  = b;
    n->req   = 0;
    b->size  = need;

    after = next_of(n);
    if (after)
        after->prev = n;
}

/* Junta `b` (livre) com o vizinho seguinte se tambem for livre. */
static block_t *merge_next(block_t *b)
{
    block_t *n = next_of(b), *after;

    if (!n || must_block(n, "vizinho")->magic != MAGIC_FREE)
        return b;

    b->size += HDR + n->size;               /* ler o size ANTES de envenenar */
    memset(n, POISON_FREE, HDR);            /* cabecalho absorvido vira veneno */
    after = next_of(b);
    if (after)
        after->prev = b;
    return b;
}

/* Os primeiros `len` bytes (multiplo de 4) do payload ainda sao veneno? */
static int poison_ok(const block_t *b, uint32_t len)
{
    const uint32_t *w = (const uint32_t *)payload((block_t *)b);

    for (uint32_t i = 0; i < len / 4; i++) {
        if (w[i] != 0xDDDDDDDDu)
            return 0;
    }
    return 1;
}

void *kmalloc(size_t size)
{
    block_t *b, *best = NULL;
    uint32_t need;

    if (!heap_start)
        panic("kmalloc antes de heap_init");
    if (size == 0)
        return NULL;
    if (size > (uint32_t)(heap_end - heap_start) - HDR - sizeof(CANARY)) {
        st_fail++;
        return NULL;
    }

    need = align16((uint32_t)size + (uint32_t)sizeof(CANARY));
    st_calls_alloc++;

    for (b = (block_t *)heap_start; b; b = next_of(b)) {
        must_block(b, "busca");
        if (b->magic != MAGIC_FREE || b->size < need)
            continue;
        if (!best || b->size < best->size) {
            best = b;
            if (b->size == need)
                break;
        }
    }

    if (!best) {
        st_fail++;
        return NULL;
    }

    /* So confere o trecho que vai ser entregue: conferir o bloco inteiro
     * (o resto do heap pode ter MiB) tornaria cada kmalloc lento. O resto
     * e conferido quando for entregue, ou por heap_check(deep). */
    if (!poison_ok(best, need))
        panic("heap: escrita em memoria ja liberada (use-after-free) no bloco %p",
              (void *)best);

    split(best, need);
    best->magic = MAGIC_USED;
    best->req   = (uint32_t)size;
    memset(payload(best), POISON_NEW, best->size);
    write_canary(best);

    st_used += best->size;
    if (st_used > st_peak)
        st_peak = st_used;

    return payload(best);
}

void *kcalloc(size_t count, size_t size)
{
    size_t total;
    void *p;

    if (__builtin_mul_overflow(count, size, &total))
        return NULL;
    p = kmalloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

/* Valida um ponteiro vindo do usuario. Devolve o bloco ou um HEAP_ERR_*. */
static int locate(const void *ptr, block_t **out)
{
    const uint8_t *p = ptr;
    block_t *b;

    if (p < heap_start + HDR || p >= heap_end || ((uintptr_t)p & 15u))
        return HEAP_ERR_BADPTR;

    b = (block_t *)(p - HDR);
    if (b->magic == MAGIC_FREE && header_sane(b))
        return HEAP_ERR_DOUBLE;
    if (!header_sane(b) || b->magic != MAGIC_USED)
        return HEAP_ERR_CORRUPT;
    if (!canary_ok(b)) {
        *out = b;
        return HEAP_ERR_OVERFLOW;
    }
    *out = b;
    return HEAP_OK;
}

int kfree_checked(void *ptr)
{
    block_t *b = NULL;
    int err;

    if (!ptr)
        return HEAP_OK;

    err = locate(ptr, &b);
    if (err != HEAP_OK)
        return err;

    st_calls_free++;
    st_used -= b->size;

    b->magic = MAGIC_FREE;
    b->req   = 0;
    memset(payload(b), POISON_FREE, b->size);

    merge_next(b);
    if (b->prev && must_block(b->prev, "anterior")->magic == MAGIC_FREE)
        merge_next(b->prev);

    return HEAP_OK;
}

static const char *err_text(int e)
{
    switch (e) {
    case HEAP_ERR_BADPTR:    return "ponteiro nao pertence ao heap";
    case HEAP_ERR_DOUBLE:    return "free duplo";
    case HEAP_ERR_OVERFLOW:  return "escrita alem do fim do bloco (canario destruido)";
    default:                 return "cabecalho do bloco destruido";
    }
}

void kfree(void *ptr)
{
    int err = kfree_checked(ptr);

    if (err != HEAP_OK)
        panic("kfree(%p): %s", ptr, err_text(err));
}

size_t kmalloc_usable_size(const void *ptr)
{
    block_t *b = NULL;

    if (!ptr || locate(ptr, &b) == HEAP_ERR_BADPTR || !b)
        return 0;
    return b->size - sizeof(CANARY);
}

void *krealloc(void *ptr, size_t size)
{
    block_t *b = NULL, *n;
    uint32_t need;
    int err;
    void *fresh;

    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    if (size > (uint32_t)(heap_end - heap_start) - HDR - sizeof(CANARY)) {
        st_fail++;
        return NULL;
    }

    err = locate(ptr, &b);
    if (err != HEAP_OK)
        panic("krealloc(%p): %s", ptr, err_text(err));

    need = align16((uint32_t)size + (uint32_t)sizeof(CANARY));

    /* Tenta crescer no lugar absorvendo o vizinho livre. */
    if (need > b->size) {
        n = next_of(b);
        if (n && must_block(n, "vizinho")->magic == MAGIC_FREE &&
            b->size + HDR + n->size >= need) {
            uint32_t before = b->size;
            b->size += HDR + n->size;
            memset(n, POISON_FREE, HDR);
            n = next_of(b);
            if (n)
                n->prev = b;
            st_used += b->size - before;
        }
    }

    if (need <= b->size) {
        uint32_t old = b->size;
        block_t *rest;

        split(b, need);
        rest = next_of(b);
        if (b->size < old && rest) {            /* houve corte: o resto e livre */
            st_used -= old - b->size;
            memset(payload(rest), POISON_FREE, rest->size);
            merge_next(rest);
        }
        b->req = (uint32_t)size;
        write_canary(b);
        if (st_used > st_peak)
            st_peak = st_used;
        return ptr;
    }

    fresh = kmalloc(size);
    if (!fresh)
        return NULL;
    memcpy(fresh, ptr, b->req < size ? b->req : size);
    kfree(ptr);
    return fresh;
}

void heap_get_stats(heap_stats_t *out)
{
    block_t *b;

    memset(out, 0, sizeof(*out));
    out->total_bytes = (uint32_t)(heap_end - heap_start);

    for (b = (block_t *)heap_start; b; b = next_of(b)) {
        must_block(b, "estatisticas");
        out->overhead_bytes += HDR;
        if (b->magic == MAGIC_USED) {
            out->used_bytes += b->size;
            out->blocks_used++;
        } else {
            out->free_bytes += b->size;
            out->blocks_free++;
            if (b->size > out->largest_free)
                out->largest_free = b->size;
        }
    }

    out->peak_used_bytes = st_peak;
    out->alloc_calls = st_calls_alloc;
    out->free_calls  = st_calls_free;
    out->fail_calls  = st_fail;
}

int heap_check(int deep, int verbose)
{
    block_t *b, *prev = NULL;
    uint32_t errors = 0, sum = 0, used = 0;
    int last_free = 0;

#define HC_ERR(...) do { errors++; if (verbose && errors <= 8) kprintf(__VA_ARGS__); } while (0)

    if (!heap_start)
        return 1;

    for (b = (block_t *)heap_start; b; b = next_of(b)) {
        if (!header_sane(b)) {
            HC_ERR("  bloco %p: cabecalho invalido (magic=%08x size=%u)\n",
                   (void *)b, b->magic, b->size);
            return (int)errors;     /* sem tamanho confiavel nao da pra seguir */
        }
        if (b->prev != prev)
            HC_ERR("  bloco %p: prev=%p, esperado %p\n",
                   (void *)b, (void *)b->prev, (void *)prev);

        sum += HDR + b->size;

        if (b->magic == MAGIC_USED) {
            used += b->size;
            if (!canary_ok(b))
                HC_ERR("  bloco %p: canario destruido (overflow de %u bytes pedidos)\n",
                       (void *)b, b->req);
            last_free = 0;
        } else {
            if (last_free)
                HC_ERR("  bloco %p: dois blocos livres seguidos (sem coalescencia)\n",
                       (void *)b);
            if (deep && !poison_ok(b, b->size))
                HC_ERR("  bloco %p: veneno alterado (escrita apos free)\n", (void *)b);
            last_free = 1;
        }
        prev = b;
    }

    if (sum != (uint32_t)(heap_end - heap_start))
        HC_ERR("  soma dos blocos (%u) difere do heap (%u)\n",
               sum, (uint32_t)(heap_end - heap_start));
    if (used != st_used)
        HC_ERR("  contador de bytes usados (%u) difere do real (%u)\n", st_used, used);

#undef HC_ERR
    return (int)errors;
}
