/* Cache de blocos com escrita direta (write-through).
 *
 * Leitura: se o setor ja esta no cache, nao vai ao disco (o ATA por PIO e
 * lento). Escrita: vai SEMPRE ao disco primeiro e so entao atualiza o
 * cache; se a gravacao falhar, o setor sai do cache. Assim o disco nunca
 * fica "atras" do que o cache mostra e um desligamento brusco nao perde
 * dados que ja foram confirmados. Substituicao: LRU. */

#include "fs_internal.h"
#include "ata.h"
#include "mm.h"
#include "string.h"

#define BC_SLOTS 64

typedef struct {
    uint32_t lba, stamp;
    uint8_t  valid;
    uint8_t  pad[3];
    uint8_t  data[BLOCK_SIZE];
} slot_t;

static slot_t *slots;
static uint32_t clock_tick, hits, misses, limit;

int bc_init(uint32_t disk_sectors)
{
    bc_exit();
    slots = kcalloc(BC_SLOTS, sizeof(slot_t));
    if (!slots)
        return FS_ERR_NOMEM;
    limit = disk_sectors;
    clock_tick = hits = misses = 0;
    return FS_OK;
}

void bc_exit(void)
{
    if (slots)
        kfree(slots);
    slots = NULL;
}

void bc_invalidate(void)
{
    if (slots)
        memset(slots, 0, BC_SLOTS * sizeof(slot_t));
}

void bc_stats(uint32_t *h, uint32_t *m)
{
    *h = hits;
    *m = misses;
}

static slot_t *find(uint32_t lba)
{
    for (int i = 0; i < BC_SLOTS; i++) {
        if (slots[i].valid && slots[i].lba == lba)
            return &slots[i];
    }
    return NULL;
}

static slot_t *victim(void)
{
    slot_t *v = &slots[0];

    for (int i = 0; i < BC_SLOTS; i++) {
        if (!slots[i].valid)
            return &slots[i];
        if (slots[i].stamp < v->stamp)
            v = &slots[i];
    }
    return v;
}

int bc_read(uint32_t lba, void *out)
{
    slot_t *s;

    if (!slots || lba >= limit)
        return FS_ERR_IO;

    s = find(lba);
    if (s) {
        hits++;
    } else {
        misses++;
        s = victim();
        s->valid = 0;
        if (ata_read_sector(lba, s->data) != 0)
            return FS_ERR_IO;
        s->lba = lba;
        s->valid = 1;
    }

    s->stamp = ++clock_tick;
    memcpy(out, s->data, BLOCK_SIZE);
    return FS_OK;
}

int bc_write(uint32_t lba, const void *in)
{
    slot_t *s;

    if (!slots || lba >= limit)
        return FS_ERR_IO;

    s = find(lba);
    if (ata_write_sector(lba, in) != 0) {
        if (s)
            s->valid = 0;               /* nao confia no que tinha */
        return FS_ERR_IO;
    }

    if (!s)
        s = victim();
    memcpy(s->data, in, BLOCK_SIZE);
    s->lba = lba;
    s->valid = 1;
    s->stamp = ++clock_tick;
    return FS_OK;
}
