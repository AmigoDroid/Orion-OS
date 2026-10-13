/* OrionFS v3 - nucleo: volume, blocos, inodes, arquivos, diretorios, caminhos.
 *
 * Camadas (de baixo para cima):
 *   bcache.c   cache de setores (escrita direta)
 *   bitmaps    alocacao de blocos e inodes
 *   bmap       bloco logico de um arquivo -> bloco fisico (direto/indireto/duplo)
 *   inode_io   ler/escrever/truncar bytes de um inode
 *   diretorio  entradas de 32 bytes dentro de um inode do tipo DIR
 *   caminhos   "a/b/../c" -> (pai, nome, inode)
 *   API        mkdir, open, rename, ... (cada operacao que escreve e uma
 *              "transacao": begin_mut marca o disco sujo, end_mut o limpa)
 */

#include "fs_internal.h"
#include "ata.h"
#include "rtc.h"
#include "crc32.h"
#include "mm.h"
#include "string.h"
#include "kprintf.h"
#include "log.h"

sb_t fs_sb;
int  fs_is_mounted;

static int      disk_dirty;          /* o superbloco no disco esta marcado "sujo" */
static int      was_dirty;           /* o mount encontrou o volume sujo */
static uint32_t cwd_ino = ROOT_INO;
static uint8_t  cur_uid;
static uint32_t hint_block, hint_inode;

/* ======================================================================= */
/* Erros e utilitarios                                                     */
/* ======================================================================= */

const char *fs_strerror(int e)
{
    switch (e) {
    case FS_OK:              return "ok";
    case FS_ERR_IO:          return "erro de leitura/escrita no disco";
    case FS_ERR_NOTMOUNTED:  return "OrionFS nao montado (use: format)";
    case FS_ERR_NOENT:       return "nao existe";
    case FS_ERR_EXIST:       return "ja existe";
    case FS_ERR_NOTDIR:      return "nao e um diretorio";
    case FS_ERR_ISDIR:       return "e um diretorio";
    case FS_ERR_NOTEMPTY:    return "diretorio nao esta vazio";
    case FS_ERR_NOSPC:       return "sem espaco no disco";
    case FS_ERR_INVAL:       return "argumento invalido";
    case FS_ERR_NAMETOOLONG: return "nome ou caminho longo demais";
    case FS_ERR_TOOBIG:      return "arquivo grande demais";
    case FS_ERR_NOMEM:       return "sem memoria";
    case FS_ERR_CORRUPT:     return "estruturas corrompidas (rode: fsck repair)";
    case FS_ERR_BUSY:        return "em uso (diretorio atual)";
    case FS_ERR_PERM:        return "permissao negada";
    case FS_ERR_NOTFS:       return "o disco nao tem OrionFS v3 (use: format)";
    case FS_ERR_NODISK:      return "disco ATA nao encontrado";
    case FS_ERR_BADFD:       return "descritor invalido";
    case FS_ERR_MFILE:       return "arquivos abertos demais";
    default:                 return "erro desconhecido";
    }
}

static uint32_t now(void)
{
    return rtc_unix_time();
}

static uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

int fs_mounted(void) { return fs_is_mounted; }
void fs_set_uid(uint8_t uid) { cur_uid = uid; }

/* ======================================================================= */
/* Superbloco e "transacoes"                                               */
/* ======================================================================= */

int fs_super_write(void)
{
    fs_sb.crc = crc32(&fs_sb, BLOCK_SIZE - 4);
    return bc_write(SUPER_LBA, &fs_sb);
}

/* Marca o volume "sujo" no disco ANTES da primeira escrita de uma operacao.
 * Se faltar luz no meio, o proximo mount enxerga state == 0 e recontagem +
 * fsck sao recomendados. */
static int begin_mut(void)
{
    int r;

    if (!fs_is_mounted)
        return FS_ERR_NOTMOUNTED;
    if (disk_dirty)
        return FS_OK;

    fs_sb.state = 0;
    r = fs_super_write();
    if (r != FS_OK) {
        fs_sb.state = 1;
        return r;
    }
    disk_dirty = 1;
    return FS_OK;
}

/* Fecha a operacao: grava contadores, marca "limpo" e esvazia o cache do disco. */
static int end_mut(int result)
{
    int r;

    if (!disk_dirty)
        return result;

    fs_sb.state = 1;
    r = fs_super_write();
    if (r == FS_OK && ata_flush() != 0)
        r = FS_ERR_IO;
    if (r == FS_OK)
        disk_dirty = 0;
    return result < 0 ? result : r;
}

int fs_sync(void)
{
    if (!fs_is_mounted)
        return FS_ERR_NOTMOUNTED;
    return end_mut(begin_mut());
}

/* ======================================================================= */
/* Bitmaps e alocacao                                                      */
/* ======================================================================= */

int fs_bitmap_get(uint32_t start, uint32_t bit, int *value)
{
    uint8_t s[BLOCK_SIZE];
    int r = bc_read(start + bit / BITS_PER_BLOCK, s);

    if (r != FS_OK)
        return r;
    *value = (s[(bit % BITS_PER_BLOCK) / 8] >> (bit % 8)) & 1;
    return FS_OK;
}

int fs_bitmap_put(uint32_t start, uint32_t bit, int value)
{
    uint8_t s[BLOCK_SIZE];
    uint32_t lba = start + bit / BITS_PER_BLOCK;
    uint8_t mask = (uint8_t)(1u << (bit % 8));
    int r = bc_read(lba, s);

    if (r != FS_OK)
        return r;
    if (value)
        s[(bit % BITS_PER_BLOCK) / 8] |= mask;
    else
        s[(bit % BITS_PER_BLOCK) / 8] &= (uint8_t)~mask;
    return bc_write(lba, s);
}

/* Acha o primeiro bit 0 (a partir de `hint`, dando a volta). */
static int bitmap_find_zero(uint32_t start, uint32_t blocks, uint32_t nbits,
                            uint32_t hint, uint32_t *out)
{
    uint32_t first = (hint % nbits) / BITS_PER_BLOCK;

    for (uint32_t n = 0; n < blocks; n++) {
        uint32_t b = (first + n) % blocks;
        uint32_t s[BLOCK_SIZE / 4];
        int r = bc_read(start + b, s);

        if (r != FS_OK)
            return r;

        for (uint32_t w = 0; w < BLOCK_SIZE / 4; w++) {
            if (s[w] == 0xFFFFFFFFu)
                continue;
            for (uint32_t k = 0; k < 32; k++) {
                uint32_t bit = b * BITS_PER_BLOCK + w * 32 + k;

                if (bit >= nbits)
                    return FS_ERR_NOSPC;
                if (!((s[w] >> k) & 1)) {
                    *out = bit;
                    return FS_OK;
                }
            }
        }
    }
    return FS_ERR_NOSPC;
}

int fs_balloc(uint32_t *out)
{
    uint32_t b;
    int r;

    if (fs_sb.free_blocks == 0)
        return FS_ERR_NOSPC;

    r = bitmap_find_zero(fs_sb.bbitmap_start, fs_sb.bbitmap_blocks,
                         fs_sb.total_blocks, hint_block, &b);
    if (r != FS_OK)
        return r;
    if (b < fs_sb.data_start)
        return FS_ERR_CORRUPT;               /* metadado marcado como livre */

    r = fs_bitmap_put(fs_sb.bbitmap_start, b, 1);
    if (r != FS_OK)
        return r;

    fs_sb.free_blocks--;
    hint_block = b + 1;
    *out = b;
    return FS_OK;
}

int fs_bfree(uint32_t b)
{
    int bit, r;

    if (b < fs_sb.data_start || b >= fs_sb.total_blocks)
        return FS_ERR_CORRUPT;

    r = fs_bitmap_get(fs_sb.bbitmap_start, b, &bit);
    if (r != FS_OK)
        return r;
    if (!bit) {
        log_write("[FS] bfree de bloco ja livre\n");
        return FS_ERR_CORRUPT;
    }

    r = fs_bitmap_put(fs_sb.bbitmap_start, b, 0);
    if (r != FS_OK)
        return r;
    fs_sb.free_blocks++;
    if (b < hint_block)
        hint_block = b;
    return FS_OK;
}

int fs_ialloc(uint32_t *out)
{
    uint32_t bit;
    int r;

    if (fs_sb.free_inodes == 0)
        return FS_ERR_NOSPC;

    r = bitmap_find_zero(fs_sb.ibitmap_start, fs_sb.ibitmap_blocks,
                         fs_sb.inode_count, hint_inode, &bit);
    if (r != FS_OK)
        return r;
    r = fs_bitmap_put(fs_sb.ibitmap_start, bit, 1);
    if (r != FS_OK)
        return r;

    fs_sb.free_inodes--;
    hint_inode = bit + 1;
    *out = bit + 1;
    return FS_OK;
}

int fs_ifree(uint32_t ino)
{
    int bit, r;

    if (ino < 1 || ino > fs_sb.inode_count)
        return FS_ERR_CORRUPT;
    r = fs_bitmap_get(fs_sb.ibitmap_start, ino - 1, &bit);
    if (r != FS_OK)
        return r;
    if (!bit)
        return FS_ERR_CORRUPT;
    r = fs_bitmap_put(fs_sb.ibitmap_start, ino - 1, 0);
    if (r != FS_OK)
        return r;
    fs_sb.free_inodes++;
    if (ino - 1 < hint_inode)
        hint_inode = ino - 1;
    return FS_OK;
}

/* Recalcula os contadores de livres contando bits (apos queda de energia). */
int fs_recount_free(void)
{
    uint32_t freeb = 0, freei = 0;

    for (uint32_t b = 0; b < fs_sb.bbitmap_blocks; b++) {
        uint8_t s[BLOCK_SIZE];
        int r = bc_read(fs_sb.bbitmap_start + b, s);

        if (r != FS_OK)
            return r;
        for (uint32_t k = 0; k < BITS_PER_BLOCK; k++) {
            uint32_t bit = b * BITS_PER_BLOCK + k;

            if (bit >= fs_sb.total_blocks)
                break;
            if (!((s[k / 8] >> (k % 8)) & 1))
                freeb++;
        }
    }
    for (uint32_t b = 0; b < fs_sb.ibitmap_blocks; b++) {
        uint8_t s[BLOCK_SIZE];
        int r = bc_read(fs_sb.ibitmap_start + b, s);

        if (r != FS_OK)
            return r;
        for (uint32_t k = 0; k < BITS_PER_BLOCK; k++) {
            uint32_t bit = b * BITS_PER_BLOCK + k;

            if (bit >= fs_sb.inode_count)
                break;
            if (!((s[k / 8] >> (k % 8)) & 1))
                freei++;
        }
    }
    fs_sb.free_blocks = freeb;
    fs_sb.free_inodes = freei;
    return FS_OK;
}

/* ======================================================================= */
/* Inodes                                                                  */
/* ======================================================================= */

int fs_iget(uint32_t ino, inode_t *out)
{
    uint8_t s[BLOCK_SIZE];
    int r;

    if (ino < 1 || ino > fs_sb.inode_count)
        return FS_ERR_CORRUPT;
    r = bc_read(fs_sb.itable_start + (ino - 1) / INODES_PER_BLOCK, s);
    if (r != FS_OK)
        return r;
    memcpy(out, s + ((ino - 1) % INODES_PER_BLOCK) * INODE_SIZE, INODE_SIZE);
    return FS_OK;
}

int fs_iput(uint32_t ino, const inode_t *in)
{
    uint8_t s[BLOCK_SIZE];
    uint32_t lba;
    int r;

    if (ino < 1 || ino > fs_sb.inode_count)
        return FS_ERR_CORRUPT;
    lba = fs_sb.itable_start + (ino - 1) / INODES_PER_BLOCK;
    r = bc_read(lba, s);
    if (r != FS_OK)
        return r;
    memcpy(s + ((ino - 1) % INODES_PER_BLOCK) * INODE_SIZE, in, INODE_SIZE);
    return bc_write(lba, s);
}

/* ======================================================================= */
/* Mapeamento: bloco logico do arquivo -> bloco fisico                     */
/*   0..8          direct[]                                                */
/*   9..136        indirect  (128 ponteiros)                               */
/*   137..16520    dindirect (128 x 128 ponteiros)                         */
/* ======================================================================= */

static int zero_block(uint32_t b)
{
    static const uint8_t zeros[BLOCK_SIZE];
    return bc_write(b, zeros);
}

/* *slot == 0 -> aloca (se alloc). zero: limpa o bloco novo (tabelas sempre). */
static int resolve_slot(uint32_t *slot, int alloc, int zero, uint32_t *phys, int *fresh)
{
    int r;

    if (*slot == 0) {
        uint32_t b;

        *phys = 0;
        if (!alloc)
            return FS_OK;
        r = fs_balloc(&b);
        if (r != FS_OK)
            return r;
        if (zero) {
            r = zero_block(b);
            if (r != FS_OK) {
                fs_bfree(b);
                return r;
            }
        }
        *slot = b;
        *fresh = 1;
    }
    *phys = *slot;
    return FS_OK;
}

/* Mesma coisa, mas o "slot" e a posicao idx dentro do bloco-tabela tblblk. */
static int table_slot(uint32_t tblblk, uint32_t idx, int alloc, int zero,
                      uint32_t *phys, int *fresh)
{
    uint32_t t[PTRS_PER_BLOCK];
    uint32_t before;
    int r = bc_read(tblblk, t);

    if (r != FS_OK)
        return r;
    before = t[idx];
    r = resolve_slot(&t[idx], alloc, zero, phys, fresh);
    if (r != FS_OK)
        return r;
    if (t[idx] != before)
        return bc_write(tblblk, t);
    return FS_OK;
}

int fs_bmap(inode_t *in, uint32_t fb, int alloc, uint32_t *phys, int *fresh)
{
    uint32_t tb;
    int f2 = 0, r;

    *phys = 0;
    *fresh = 0;

    if (fb < N_DIRECT)
        return resolve_slot(&in->direct[fb], alloc, 0, phys, fresh);
    fb -= N_DIRECT;

    if (fb < PTRS_PER_BLOCK) {
        r = resolve_slot(&in->indirect, alloc, 1, &tb, &f2);
        if (r != FS_OK || !tb)
            return r;
        return table_slot(tb, fb, alloc, 0, phys, fresh);
    }
    fb -= PTRS_PER_BLOCK;

    if (fb < PTRS_PER_BLOCK * PTRS_PER_BLOCK) {
        uint32_t mid;

        r = resolve_slot(&in->dindirect, alloc, 1, &tb, &f2);
        if (r != FS_OK || !tb)
            return r;
        r = table_slot(tb, fb / PTRS_PER_BLOCK, alloc, 1, &mid, &f2);
        if (r != FS_OK || !mid)
            return r;
        return table_slot(mid, fb % PTRS_PER_BLOCK, alloc, 0, phys, fresh);
    }

    return FS_ERR_TOOBIG;
}

/* Libera `blk` e, se level > 0, tudo o que ele aponta. */
static int free_tree(uint32_t blk, int level)
{
    int first_err = FS_OK, r;

    if (!blk)
        return FS_OK;

    if (level > 0) {
        uint32_t t[PTRS_PER_BLOCK];

        r = bc_read(blk, t);
        if (r != FS_OK)
            return r;
        for (uint32_t i = 0; i < PTRS_PER_BLOCK; i++) {
            if (t[i]) {
                r = free_tree(t[i], level - 1);
                if (r != FS_OK && first_err == FS_OK)
                    first_err = r;
            }
        }
    }
    r = fs_bfree(blk);
    return first_err != FS_OK ? first_err : r;
}

static int free_all_blocks(inode_t *in)
{
    int first_err = FS_OK, r;

    for (uint32_t i = 0; i < N_DIRECT; i++) {
        if (in->direct[i]) {
            r = fs_bfree(in->direct[i]);
            if (r != FS_OK && first_err == FS_OK)
                first_err = r;
            in->direct[i] = 0;
        }
    }
    r = free_tree(in->indirect, 1);
    if (r != FS_OK && first_err == FS_OK)
        first_err = r;
    r = free_tree(in->dindirect, 2);
    if (r != FS_OK && first_err == FS_OK)
        first_err = r;
    in->indirect = in->dindirect = 0;
    return first_err;
}

/* Libera so o bloco logico fb (se mapeado). */
static int release_block(inode_t *in, uint32_t fb)
{
    uint32_t t[PTRS_PER_BLOCK], b;
    int r;

    if (fb < N_DIRECT) {
        b = in->direct[fb];
        in->direct[fb] = 0;
        return b ? fs_bfree(b) : FS_OK;
    }
    fb -= N_DIRECT;

    if (fb < PTRS_PER_BLOCK) {
        if (!in->indirect)
            return FS_OK;
        r = bc_read(in->indirect, t);
        if (r != FS_OK)
            return r;
        b = t[fb];
        if (!b)
            return FS_OK;
        t[fb] = 0;
        r = bc_write(in->indirect, t);
        return r != FS_OK ? r : fs_bfree(b);
    }
    fb -= PTRS_PER_BLOCK;

    if (!in->dindirect)
        return FS_OK;
    r = bc_read(in->dindirect, t);
    if (r != FS_OK)
        return r;
    if (!t[fb / PTRS_PER_BLOCK])
        return FS_OK;
    {
        uint32_t mid = t[fb / PTRS_PER_BLOCK], u[PTRS_PER_BLOCK];

        r = bc_read(mid, u);
        if (r != FS_OK)
            return r;
        b = u[fb % PTRS_PER_BLOCK];
        if (!b)
            return FS_OK;
        u[fb % PTRS_PER_BLOCK] = 0;
        r = bc_write(mid, u);
        return r != FS_OK ? r : fs_bfree(b);
    }
}

static int table_empty(const uint32_t *t)
{
    for (uint32_t i = 0; i < PTRS_PER_BLOCK; i++) {
        if (t[i])
            return 0;
    }
    return 1;
}

/* Depois de liberar blocos soltos: devolve as tabelas que ficaram vazias. */
static int prune_tables(inode_t *in)
{
    uint32_t t[PTRS_PER_BLOCK];
    int r;

    if (in->indirect) {
        r = bc_read(in->indirect, t);
        if (r != FS_OK)
            return r;
        if (table_empty(t)) {
            r = fs_bfree(in->indirect);
            if (r != FS_OK)
                return r;
            in->indirect = 0;
        }
    }

    if (in->dindirect) {
        int changed = 0;

        r = bc_read(in->dindirect, t);
        if (r != FS_OK)
            return r;
        for (uint32_t i = 0; i < PTRS_PER_BLOCK; i++) {
            uint32_t u[PTRS_PER_BLOCK];

            if (!t[i])
                continue;
            r = bc_read(t[i], u);
            if (r != FS_OK)
                return r;
            if (table_empty(u)) {
                r = fs_bfree(t[i]);
                if (r != FS_OK)
                    return r;
                t[i] = 0;
                changed = 1;
            }
        }
        if (table_empty(t)) {
            r = fs_bfree(in->dindirect);
            if (r != FS_OK)
                return r;
            in->dindirect = 0;
        } else if (changed) {
            r = bc_write(in->dindirect, t);
            if (r != FS_OK)
                return r;
        }
    }
    return FS_OK;
}

/* ======================================================================= */
/* Ler / escrever / truncar bytes de um inode                              */
/* ======================================================================= */

static int inode_read(const inode_t *cin, uint32_t off, void *buf, uint32_t len)
{
    inode_t in = *cin;
    uint8_t *dst = buf;
    uint32_t done = 0;

    if (off >= in.size)
        return 0;
    len = min_u32(len, in.size - off);

    while (done < len) {
        uint8_t s[BLOCK_SIZE];
        uint32_t pos = off + done, within = pos % BLOCK_SIZE;
        uint32_t chunk = min_u32(len - done, BLOCK_SIZE - within);
        uint32_t phys;
        int fresh, r;

        r = fs_bmap(&in, pos / BLOCK_SIZE, 0, &phys, &fresh);
        if (r != FS_OK)
            return done ? (int)done : r;

        if (phys == 0) {
            memset(dst + done, 0, chunk);              /* buraco: zeros */
        } else {
            r = bc_read(phys, s);
            if (r != FS_OK)
                return done ? (int)done : r;
            memcpy(dst + done, s + within, chunk);
        }
        done += chunk;
    }
    return (int)done;
}

/* Escreve e grava o inode (mesmo em erro parcial, para nao vazar blocos). */
static int inode_write(uint32_t ino, inode_t *in, uint32_t off,
                       const void *buf, uint32_t len)
{
    const uint8_t *src = buf;
    uint32_t done = 0;
    int err = FS_OK, r;

    if (len > MAX_FILE_BYTES || off > MAX_FILE_BYTES - len)
        return FS_ERR_TOOBIG;

    while (done < len) {
        uint8_t s[BLOCK_SIZE];
        uint32_t pos = off + done, within = pos % BLOCK_SIZE;
        uint32_t chunk = min_u32(len - done, BLOCK_SIZE - within);
        uint32_t phys;
        int fresh;

        err = fs_bmap(in, pos / BLOCK_SIZE, 1, &phys, &fresh);
        if (err != FS_OK)
            break;

        if (chunk == BLOCK_SIZE) {
            memcpy(s, src + done, BLOCK_SIZE);
        } else {
            if (fresh)
                memset(s, 0, BLOCK_SIZE);
            else if ((err = bc_read(phys, s)) != FS_OK)
                break;
            memcpy(s + within, src + done, chunk);
        }
        err = bc_write(phys, s);
        if (err != FS_OK)
            break;
        done += chunk;
    }

    if (off + done > in->size)
        in->size = off + done;
    if (done)
        in->mtime = now();
    r = fs_iput(ino, in);

    if (done)
        return (int)done;
    return err != FS_OK ? err : r;
}

static int inode_truncate(uint32_t ino, inode_t *in, uint32_t newsize)
{
    int r = FS_OK;

    if (newsize > MAX_FILE_BYTES)
        return FS_ERR_TOOBIG;

    if (newsize < in->size) {
        if (newsize == 0) {
            r = free_all_blocks(in);
        } else {
            uint32_t first = (newsize + BLOCK_SIZE - 1) / BLOCK_SIZE;
            uint32_t last = (in->size + BLOCK_SIZE - 1) / BLOCK_SIZE;

            for (uint32_t fb = first; fb < last && r == FS_OK; fb++)
                r = release_block(in, fb);
            if (r == FS_OK)
                r = prune_tables(in);

            /* zera o resto do ultimo bloco para nao "ressuscitar" dados */
            if (r == FS_OK && newsize % BLOCK_SIZE) {
                uint8_t s[BLOCK_SIZE];
                uint32_t phys;
                int fresh;

                r = fs_bmap(in, newsize / BLOCK_SIZE, 0, &phys, &fresh);
                if (r == FS_OK && phys) {
                    r = bc_read(phys, s);
                    if (r == FS_OK) {
                        memset(s + newsize % BLOCK_SIZE, 0,
                               BLOCK_SIZE - newsize % BLOCK_SIZE);
                        r = bc_write(phys, s);
                    }
                }
            }
        }
    }

    in->size = newsize;
    in->mtime = now();
    {
        int w = fs_iput(ino, in);
        return r != FS_OK ? r : w;
    }
}

/* ======================================================================= */
/* Diretorios: um inode T_DIR cujo conteudo e um vetor de dirent_t         */
/* ======================================================================= */

#define NONE 0xFFFFFFFFu

typedef struct {
    uint32_t ino;                /* 0 = nao achou */
    uint32_t slot;
} dlook_t;

static int is_dot(const char *n)
{
    return (n[0] == '.' && n[1] == '\0') || (n[0] == '.' && n[1] == '.' && n[2] == '\0');
}

static int valid_name(const char *n)
{
    uint32_t len = 0;

    for (; n[len]; len++) {
        unsigned char c = (unsigned char)n[len];

        if (len >= FS_NAME_MAX)
            return FS_ERR_NAMETOOLONG;
        if (c <= 32 || c == '/' || c == 127)
            return FS_ERR_INVAL;
    }
    if (len == 0 || is_dot(n))
        return FS_ERR_INVAL;
    return FS_OK;
}

static int dir_get(const inode_t *d, uint32_t idx, dirent_t *e)
{
    inode_t t = *d;
    uint8_t s[BLOCK_SIZE];
    uint32_t phys;
    int fresh, r;

    r = fs_bmap(&t, idx / DIRENTS_PER_BLOCK, 0, &phys, &fresh);
    if (r != FS_OK)
        return r;
    if (!phys)
        return FS_ERR_CORRUPT;                   /* diretorio nunca tem buracos */
    r = bc_read(phys, s);
    if (r != FS_OK)
        return r;
    memcpy(e, s + (idx % DIRENTS_PER_BLOCK) * DIRENT_SIZE, DIRENT_SIZE);
    return FS_OK;
}

/* Pode alocar um bloco novo em *d: o chamador grava o inode depois. */
static int dir_put(inode_t *d, uint32_t idx, const dirent_t *e)
{
    uint8_t s[BLOCK_SIZE];
    uint32_t phys;
    int fresh, r;

    r = fs_bmap(d, idx / DIRENTS_PER_BLOCK, 1, &phys, &fresh);
    if (r != FS_OK)
        return r;
    if (fresh)
        memset(s, 0, BLOCK_SIZE);
    else if ((r = bc_read(phys, s)) != FS_OK)
        return r;
    memcpy(s + (idx % DIRENTS_PER_BLOCK) * DIRENT_SIZE, e, DIRENT_SIZE);
    return bc_write(phys, s);
}

static int dir_find(uint32_t dir_ino, const char *name, dlook_t *o)
{
    inode_t d;
    uint32_t nent;
    int r = fs_iget(dir_ino, &d);

    o->ino = 0;
    o->slot = NONE;
    if (r != FS_OK)
        return r;
    if (d.type != T_DIR)
        return FS_ERR_NOTDIR;

    nent = d.size / DIRENT_SIZE;
    for (uint32_t fb = 0; fb * DIRENTS_PER_BLOCK < nent; fb++) {
        uint8_t s[BLOCK_SIZE];
        const dirent_t *e = (const dirent_t *)s;
        uint32_t phys;
        int fresh;

        r = fs_bmap(&d, fb, 0, &phys, &fresh);
        if (r != FS_OK)
            return r;
        if (!phys)
            return FS_ERR_CORRUPT;
        r = bc_read(phys, s);
        if (r != FS_OK)
            return r;

        for (uint32_t i = 0; i < DIRENTS_PER_BLOCK; i++) {
            uint32_t idx = fb * DIRENTS_PER_BLOCK + i;

            if (idx >= nent)
                break;
            if (e[i].ino && strncmp(e[i].name, name, sizeof(e[i].name)) == 0) {
                o->ino = e[i].ino;
                o->slot = idx;
                return FS_OK;
            }
        }
    }
    return FS_OK;
}

static int dir_add(uint32_t dir_ino, const char *name, uint32_t ino)
{
    inode_t d;
    dirent_t e;
    uint32_t nent, slot = NONE, old_size;
    int r = fs_iget(dir_ino, &d);

    if (r != FS_OK)
        return r;
    if (d.type != T_DIR)
        return FS_ERR_NOTDIR;

    old_size = d.size;
    nent = d.size / DIRENT_SIZE;
    for (uint32_t i = 0; i < nent; i++) {
        r = dir_get(&d, i, &e);
        if (r != FS_OK)
            return r;
        if (e.ino == 0) {
            slot = i;
            break;
        }
    }
    if (slot == NONE) {
        slot = nent;
        if ((nent + 1) * DIRENT_SIZE > MAX_FILE_BYTES)
            return FS_ERR_TOOBIG;
        d.size = (nent + 1) * DIRENT_SIZE;
    }

    memset(&e, 0, sizeof(e));
    e.ino = ino;
    for (uint32_t i = 0; i < sizeof(e.name) - 1 && name[i]; i++)
        e.name[i] = name[i];

    r = dir_put(&d, slot, &e);
    if (r != FS_OK) {
        d.size = old_size;
        fs_iput(dir_ino, &d);                    /* guarda o bloco que possa ter sido alocado */
        return r;
    }
    d.mtime = now();
    return fs_iput(dir_ino, &d);
}

static int dir_remove_slot(uint32_t dir_ino, uint32_t slot)
{
    inode_t d;
    dirent_t e;
    int r = fs_iget(dir_ino, &d);

    if (r != FS_OK)
        return r;
    memset(&e, 0, sizeof(e));
    r = dir_put(&d, slot, &e);
    if (r != FS_OK)
        return r;
    d.mtime = now();
    return fs_iput(dir_ino, &d);
}

static int dir_is_empty(uint32_t dir_ino, int *empty)
{
    inode_t d;
    int r = fs_iget(dir_ino, &d);

    if (r != FS_OK)
        return r;
    *empty = 1;
    for (uint32_t i = 0; i < d.size / DIRENT_SIZE; i++) {
        dirent_t e;

        r = dir_get(&d, i, &e);
        if (r != FS_OK)
            return r;
        if (e.ino && !is_dot(e.name)) {
            *empty = 0;
            return FS_OK;
        }
    }
    return FS_OK;
}

/* Nome com que `child` aparece dentro de `parent`. */
static int dir_name_of(uint32_t parent, uint32_t child, char *out)
{
    inode_t d;
    int r = fs_iget(parent, &d);

    if (r != FS_OK)
        return r;
    for (uint32_t i = 0; i < d.size / DIRENT_SIZE; i++) {
        dirent_t e;

        r = dir_get(&d, i, &e);
        if (r != FS_OK)
            return r;
        if (e.ino == child && !is_dot(e.name)) {
            memcpy(out, e.name, sizeof(e.name));
            return FS_OK;
        }
    }
    return FS_ERR_NOENT;
}

/* dir == anc, ou anc e um ancestral de dir? (sobe por "..") */
static int is_within(uint32_t dir, uint32_t anc)
{
    uint32_t cur = dir;

    for (int i = 0; i < 2 * FS_PATH_MAX; i++) {
        dlook_t l;

        if (cur == anc)
            return 1;
        if (cur == ROOT_INO)
            return 0;
        if (dir_find(cur, "..", &l) != FS_OK || !l.ino)
            return 0;
        cur = l.ino;
    }
    return 0;
}

/* ======================================================================= */
/* Caminhos e permissoes                                                   */
/* ======================================================================= */

typedef struct {
    uint32_t parent;                 /* diretorio que contem (ou conteria) o ultimo nome */
    uint32_t ino;                    /* 0 se o ultimo componente nao existe */
    uint32_t slot;
    char     leaf[FS_NAME_MAX + 1];  /* ultimo componente ("" para "/") */
} res_t;

static int resolve(const char *path, res_t *r)
{
    uint32_t cur;
    const char *p;

    if (!fs_is_mounted)
        return FS_ERR_NOTMOUNTED;
    if (!path || !path[0])
        return FS_ERR_INVAL;
    if (strlen(path) >= FS_PATH_MAX)
        return FS_ERR_NAMETOOLONG;

    cur = path[0] == '/' ? ROOT_INO : cwd_ino;
    r->parent = cur;
    r->ino = cur;
    r->slot = NONE;
    r->leaf[0] = '\0';

    for (p = path;;) {
        char comp[FS_NAME_MAX + 1];
        uint32_t n = 0;
        dlook_t l;
        int rc;

        while (*p == '/')
            p++;
        if (!*p)
            break;

        while (*p && *p != '/') {
            if (n >= FS_NAME_MAX)
                return FS_ERR_NAMETOOLONG;
            comp[n++] = *p++;
        }
        comp[n] = '\0';

        if (r->ino == 0)
            return FS_ERR_NOENT;              /* pasta do meio nao existe */

        rc = dir_find(r->ino, comp, &l);
        if (rc != FS_OK)
            return rc;

        r->parent = r->ino;
        memcpy(r->leaf, comp, n + 1);
        r->slot = l.slot;
        r->ino = l.ino;
    }
    return FS_OK;
}

/* want: 4 = ler, 2 = escrever. root (uid 0) pode tudo. */
static int can(const inode_t *in, int want)
{
    int bits;

    if (cur_uid == 0)
        return 1;
    bits = in->uid == cur_uid ? (in->mode >> 6) & 7 : in->mode & 7;
    return (bits & want) == want;
}

/* ======================================================================= */
/* Criar e remover nos                                                     */
/* ======================================================================= */

static int release_node(uint32_t ino)
{
    inode_t n;
    int r = fs_iget(ino, &n), r2;

    if (r != FS_OK)
        return r;
    r = free_all_blocks(&n);
    memset(&n, 0, sizeof(n));
    r2 = fs_iput(ino, &n);
    if (r2 != FS_OK && r == FS_OK)
        r = r2;
    r2 = fs_ifree(ino);
    return r != FS_OK ? r : r2;
}

static int make_node(uint32_t parent, const char *name, int type, uint16_t mode,
                     uint32_t *out)
{
    inode_t p, n;
    uint32_t ino;
    int r = valid_name(name);

    if (r != FS_OK)
        return r;
    r = fs_iget(parent, &p);
    if (r != FS_OK)
        return r;
    if (p.type != T_DIR)
        return FS_ERR_NOTDIR;
    if (!can(&p, 2))
        return FS_ERR_PERM;

    r = fs_ialloc(&ino);
    if (r != FS_OK)
        return r;

    memset(&n, 0, sizeof(n));
    n.type = (uint8_t)type;
    n.uid = cur_uid;
    n.mode = mode;
    n.ctime = n.mtime = now();
    n.nlink = type == T_DIR ? 2 : 1;

    if (type == T_DIR) {
        dirent_t e;

        memset(&e, 0, sizeof(e));
        e.ino = ino;
        e.name[0] = '.';
        r = dir_put(&n, 0, &e);
        if (r == FS_OK) {
            e.ino = parent;
            e.name[1] = '.';
            r = dir_put(&n, 1, &e);
        }
        n.size = 2 * DIRENT_SIZE;
        if (r != FS_OK) {
            free_all_blocks(&n);
            fs_ifree(ino);
            return r;
        }
    }

    r = fs_iput(ino, &n);
    if (r == FS_OK)
        r = dir_add(parent, name, ino);
    if (r != FS_OK) {
        free_all_blocks(&n);
        memset(&n, 0, sizeof(n));
        fs_iput(ino, &n);
        fs_ifree(ino);
        return r;
    }

    if (type == T_DIR) {
        r = fs_iget(parent, &p);                  /* dir_add pode ter mudado o pai */
        if (r == FS_OK) {
            p.nlink++;
            r = fs_iput(parent, &p);
        }
        if (r != FS_OK)
            return r;
    }

    if (out)
        *out = ino;
    return FS_OK;
}

static int do_mkdir(const char *path)
{
    res_t rs;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (rs.ino)
        return FS_ERR_EXIST;
    return make_node(rs.parent, rs.leaf, T_DIR, MODE_DIR_DEFAULT, NULL);
}

static int do_create(const char *path, uint32_t *ino)
{
    res_t rs;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (rs.ino)
        return FS_ERR_EXIST;
    return make_node(rs.parent, rs.leaf, T_FILE, MODE_FILE_DEFAULT, ino);
}

static int do_unlink(const char *path)
{
    res_t rs;
    inode_t n, p;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (!rs.ino)
        return FS_ERR_NOENT;
    r = fs_iget(rs.ino, &n);
    if (r == FS_OK)
        r = fs_iget(rs.parent, &p);
    if (r != FS_OK)
        return r;
    if (n.type == T_DIR)
        return FS_ERR_ISDIR;
    if (!can(&p, 2))
        return FS_ERR_PERM;

    r = dir_remove_slot(rs.parent, rs.slot);      /* primeiro some do diretorio... */
    if (r != FS_OK)
        return r;
    return release_node(rs.ino);                  /* ...depois devolve os blocos */
}

static int do_rmdir(const char *path)
{
    res_t rs;
    inode_t n, p;
    int r = resolve(path, &rs), empty;

    if (r != FS_OK)
        return r;
    if (!rs.ino)
        return FS_ERR_NOENT;
    if (is_dot(rs.leaf) || rs.ino == ROOT_INO)
        return FS_ERR_INVAL;
    r = fs_iget(rs.ino, &n);
    if (r == FS_OK)
        r = fs_iget(rs.parent, &p);
    if (r != FS_OK)
        return r;
    if (n.type != T_DIR)
        return FS_ERR_NOTDIR;
    if (is_within(cwd_ino, rs.ino))
        return FS_ERR_BUSY;
    if (!can(&p, 2))
        return FS_ERR_PERM;
    r = dir_is_empty(rs.ino, &empty);
    if (r != FS_OK)
        return r;
    if (!empty)
        return FS_ERR_NOTEMPTY;

    r = dir_remove_slot(rs.parent, rs.slot);
    if (r != FS_OK)
        return r;
    r = fs_iget(rs.parent, &p);
    if (r == FS_OK) {
        if (p.nlink > 2)
            p.nlink--;
        r = fs_iput(rs.parent, &p);
    }
    {
        int r2 = release_node(rs.ino);
        return r != FS_OK ? r : r2;
    }
}

/* Apaga TUDO dentro de `dir` (nao o proprio dir). */
static int rm_contents(uint32_t dir, int depth)
{
    inode_t d;
    uint32_t nent;
    int r = fs_iget(dir, &d);

    if (r != FS_OK)
        return r;
    if (depth > FS_MAX_DEPTH)
        return FS_ERR_NAMETOOLONG;

    nent = d.size / DIRENT_SIZE;
    for (uint32_t i = 0; i < nent; i++) {
        dirent_t e;
        inode_t c;

        r = dir_get(&d, i, &e);
        if (r != FS_OK)
            return r;
        if (!e.ino || is_dot(e.name))
            continue;
        r = fs_iget(e.ino, &c);
        if (r != FS_OK)
            return r;
        if (c.type == T_DIR) {
            r = rm_contents(e.ino, depth + 1);
            if (r != FS_OK)
                return r;
        }
        r = dir_remove_slot(dir, i);
        if (r != FS_OK)
            return r;
        r = release_node(e.ino);
        if (r != FS_OK)
            return r;
    }

    r = fs_iget(dir, &d);                         /* recarrega: tamanho/mtime mudaram */
    if (r != FS_OK)
        return r;
    d.nlink = 2;
    return fs_iput(dir, &d);
}

static int do_rmtree(const char *path)
{
    res_t rs;
    inode_t n, p;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (!rs.ino)
        return FS_ERR_NOENT;
    if (is_dot(rs.leaf) || rs.ino == ROOT_INO)
        return FS_ERR_INVAL;
    r = fs_iget(rs.ino, &n);
    if (r == FS_OK)
        r = fs_iget(rs.parent, &p);
    if (r != FS_OK)
        return r;
    if (!can(&p, 2))
        return FS_ERR_PERM;

    if (n.type == T_FILE) {
        r = dir_remove_slot(rs.parent, rs.slot);
        return r != FS_OK ? r : release_node(rs.ino);
    }

    if (is_within(cwd_ino, rs.ino))
        return FS_ERR_BUSY;
    r = rm_contents(rs.ino, 0);
    if (r != FS_OK)
        return r;
    r = dir_remove_slot(rs.parent, rs.slot);
    if (r != FS_OK)
        return r;
    r = fs_iget(rs.parent, &p);
    if (r == FS_OK) {
        if (p.nlink > 2)
            p.nlink--;
        r = fs_iput(rs.parent, &p);
    }
    {
        int r2 = release_node(rs.ino);
        return r != FS_OK ? r : r2;
    }
}

static int do_rename(const char *from, const char *to)
{
    res_t a, b;
    inode_t src, pa, pb;
    int r = resolve(from, &a);

    if (r == FS_OK)
        r = resolve(to, &b);
    if (r != FS_OK)
        return r;
    if (!a.ino)
        return FS_ERR_NOENT;
    if (a.ino == ROOT_INO || is_dot(a.leaf) || !b.leaf[0] || is_dot(b.leaf))
        return FS_ERR_INVAL;
    if (b.ino == a.ino)
        return FS_OK;

    r = valid_name(b.leaf);
    if (r != FS_OK)
        return r;
    r = fs_iget(a.ino, &src);
    if (r == FS_OK)
        r = fs_iget(a.parent, &pa);
    if (r == FS_OK)
        r = fs_iget(b.parent, &pb);
    if (r != FS_OK)
        return r;
    if (pb.type != T_DIR)
        return FS_ERR_NOTDIR;
    if (!can(&pa, 2) || !can(&pb, 2))
        return FS_ERR_PERM;
    if (src.type == T_DIR && is_within(b.parent, a.ino))
        return FS_ERR_INVAL;                      /* mover pasta para dentro de si mesma */

    if (b.ino) {                                  /* destino existe: substitui */
        inode_t dst;

        r = fs_iget(b.ino, &dst);
        if (r != FS_OK)
            return r;
        if (src.type == T_DIR && dst.type != T_DIR)
            return FS_ERR_NOTDIR;
        if (src.type != T_DIR && dst.type == T_DIR)
            return FS_ERR_ISDIR;
        if (dst.type == T_DIR) {
            int empty;

            if (is_within(cwd_ino, b.ino))
                return FS_ERR_BUSY;
            r = dir_is_empty(b.ino, &empty);
            if (r != FS_OK)
                return r;
            if (!empty)
                return FS_ERR_NOTEMPTY;
        }
        r = dir_remove_slot(b.parent, b.slot);
        if (r != FS_OK)
            return r;
        if (dst.type == T_DIR) {
            r = fs_iget(b.parent, &pb);
            if (r == FS_OK) {
                if (pb.nlink > 2)
                    pb.nlink--;
                r = fs_iput(b.parent, &pb);
            }
            if (r != FS_OK)
                return r;
        }
        r = release_node(b.ino);
        if (r != FS_OK)
            return r;
    }

    r = dir_add(b.parent, b.leaf, a.ino);         /* novo nome primeiro... */
    if (r != FS_OK)
        return r;
    r = dir_remove_slot(a.parent, a.slot);        /* ...depois tira o antigo */
    if (r != FS_OK)
        return r;

    if (src.type == T_DIR && a.parent != b.parent) {
        dlook_t l;
        inode_t s2, op, np;
        dirent_t e;

        r = dir_find(a.ino, "..", &l);            /* ".." do diretorio movido */
        if (r != FS_OK || !l.ino)
            return r != FS_OK ? r : FS_ERR_CORRUPT;
        r = fs_iget(a.ino, &s2);
        if (r != FS_OK)
            return r;
        memset(&e, 0, sizeof(e));
        e.ino = b.parent;
        e.name[0] = e.name[1] = '.';
        r = dir_put(&s2, l.slot, &e);
        if (r != FS_OK)
            return r;

        r = fs_iget(a.parent, &op);
        if (r == FS_OK) {
            if (op.nlink > 2)
                op.nlink--;
            r = fs_iput(a.parent, &op);
        }
        if (r == FS_OK)
            r = fs_iget(b.parent, &np);
        if (r == FS_OK) {
            np.nlink++;
            r = fs_iput(b.parent, &np);
        }
    }
    return r;
}

/* ======================================================================= */
/* Copia, truncate, stat, permissoes                                       */
/* ======================================================================= */

static int copy_data(uint32_t src_ino, uint32_t dst_ino)
{
    inode_t s, d;
    uint8_t buf[BLOCK_SIZE];
    int r = fs_iget(src_ino, &s);

    if (r == FS_OK)
        r = fs_iget(dst_ino, &d);
    if (r != FS_OK)
        return r;

    for (uint32_t off = 0; off < s.size; off += BLOCK_SIZE) {
        uint32_t want = min_u32(BLOCK_SIZE, s.size - off);
        int n = inode_read(&s, off, buf, want), w;

        if (n < 0)
            return n;
        if ((uint32_t)n != want)
            return FS_ERR_CORRUPT;
        w = inode_write(dst_ino, &d, off, buf, want);
        if (w < 0)
            return w;
        if ((uint32_t)w != want)
            return FS_ERR_NOSPC;
    }
    return FS_OK;
}

static int copy_node(uint32_t src_ino, uint32_t dst_parent, const char *name,
                     int recursive, int depth)
{
    inode_t s;
    uint32_t created;
    int r;

    if (depth > FS_MAX_DEPTH)
        return FS_ERR_NAMETOOLONG;
    r = fs_iget(src_ino, &s);
    if (r != FS_OK)
        return r;
    if (!can(&s, 4))
        return FS_ERR_PERM;

    if (s.type == T_FILE) {
        r = make_node(dst_parent, name, T_FILE, s.mode, &created);
        return r != FS_OK ? r : copy_data(src_ino, created);
    }

    if (!recursive)
        return FS_ERR_ISDIR;
    r = make_node(dst_parent, name, T_DIR, s.mode, &created);
    if (r != FS_OK)
        return r;

    for (uint32_t i = 0; i < s.size / DIRENT_SIZE; i++) {
        dirent_t e;

        r = dir_get(&s, i, &e);
        if (r != FS_OK)
            return r;
        if (!e.ino || is_dot(e.name))
            continue;
        r = copy_node(e.ino, created, e.name, 1, depth + 1);
        if (r != FS_OK)
            return r;
    }
    return FS_OK;
}

static int do_copy(const char *from, const char *to, int recursive)
{
    res_t a, b;
    inode_t src;
    uint32_t parent;
    char name[FS_NAME_MAX + 1];
    dlook_t l;
    int r = resolve(from, &a);

    if (r == FS_OK)
        r = resolve(to, &b);
    if (r != FS_OK)
        return r;
    if (!a.ino)
        return FS_ERR_NOENT;
    r = fs_iget(a.ino, &src);
    if (r != FS_OK)
        return r;

    if (b.ino) {
        inode_t dst;

        if (b.ino == a.ino)
            return FS_ERR_INVAL;
        r = fs_iget(b.ino, &dst);
        if (r != FS_OK)
            return r;
        if (dst.type == T_DIR) {                  /* cp x pasta/  -> pasta/x */
            if (!a.leaf[0] || is_dot(a.leaf))
                return FS_ERR_INVAL;
            parent = b.ino;
            memcpy(name, a.leaf, sizeof(name));
        } else {
            if (src.type == T_DIR)
                return FS_ERR_NOTDIR;
            parent = b.parent;
            memcpy(name, b.leaf, sizeof(name));
        }
    } else {
        parent = b.parent;
        memcpy(name, b.leaf, sizeof(name));
    }

    r = valid_name(name);
    if (r != FS_OK)
        return r;
    if (src.type == T_DIR && is_within(parent, a.ino))
        return FS_ERR_INVAL;                      /* copiar pasta para dentro de si */

    r = dir_find(parent, name, &l);               /* ja existe algo la? */
    if (r != FS_OK)
        return r;
    if (l.ino) {
        inode_t old;

        r = fs_iget(l.ino, &old);
        if (r != FS_OK)
            return r;
        if (old.type == T_DIR || src.type == T_DIR)
            return FS_ERR_EXIST;
        if (l.ino == a.ino)
            return FS_ERR_INVAL;
        r = dir_remove_slot(parent, l.slot);      /* arquivo sobre arquivo: substitui */
        if (r == FS_OK)
            r = release_node(l.ino);
        if (r != FS_OK)
            return r;
    }
    return copy_node(a.ino, parent, name, recursive, 0);
}

static int do_truncate(const char *path, uint32_t size)
{
    res_t rs;
    inode_t n;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (!rs.ino)
        return FS_ERR_NOENT;
    r = fs_iget(rs.ino, &n);
    if (r != FS_OK)
        return r;
    if (n.type != T_FILE)
        return FS_ERR_ISDIR;
    if (!can(&n, 2))
        return FS_ERR_PERM;
    return inode_truncate(rs.ino, &n, size);
}

static uint32_t count_nonzero(uint32_t blk)
{
    uint32_t t[PTRS_PER_BLOCK], n = 0;

    if (bc_read(blk, t) != FS_OK)
        return 0;
    for (uint32_t i = 0; i < PTRS_PER_BLOCK; i++)
        n += t[i] != 0;
    return n;
}

static uint32_t count_blocks(const inode_t *in)
{
    uint32_t n = 0;

    for (uint32_t i = 0; i < N_DIRECT; i++)
        n += in->direct[i] != 0;
    if (in->indirect)
        n += 1 + count_nonzero(in->indirect);
    if (in->dindirect) {
        uint32_t t[PTRS_PER_BLOCK];

        n++;
        if (bc_read(in->dindirect, t) == FS_OK) {
            for (uint32_t i = 0; i < PTRS_PER_BLOCK; i++) {
                if (t[i])
                    n += 1 + count_nonzero(t[i]);
            }
        }
    }
    return n;
}

static void fill_stat(uint32_t ino, const inode_t *n, fs_stat_t *o)
{
    o->ino = ino;
    o->type = n->type;
    o->uid = n->uid;
    o->mode = n->mode;
    o->size = n->size;
    o->blocks = count_blocks(n);
    o->ctime = n->ctime;
    o->mtime = n->mtime;
    o->nlink = n->nlink;
}

int fs_stat(const char *path, fs_stat_t *out)
{
    res_t rs;
    inode_t n;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (!rs.ino)
        return FS_ERR_NOENT;
    r = fs_iget(rs.ino, &n);
    if (r != FS_OK)
        return r;
    fill_stat(rs.ino, &n, out);
    return FS_OK;
}

static int do_chmod(const char *path, uint16_t mode)
{
    res_t rs;
    inode_t n;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (!rs.ino)
        return FS_ERR_NOENT;
    r = fs_iget(rs.ino, &n);
    if (r != FS_OK)
        return r;
    if (cur_uid != 0 && n.uid != cur_uid)
        return FS_ERR_PERM;
    n.mode = mode & 0777;
    return fs_iput(rs.ino, &n);
}

static int do_chown(const char *path, uint8_t uid)
{
    res_t rs;
    inode_t n;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (!rs.ino)
        return FS_ERR_NOENT;
    if (cur_uid != 0)
        return FS_ERR_PERM;
    r = fs_iget(rs.ino, &n);
    if (r != FS_OK)
        return r;
    n.uid = uid;
    return fs_iput(rs.ino, &n);
}

int fs_readdir(const char *path, uint32_t *cursor, fs_dirent_t *out)
{
    res_t rs;
    inode_t d;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (!rs.ino)
        return FS_ERR_NOENT;
    r = fs_iget(rs.ino, &d);
    if (r != FS_OK)
        return r;
    if (d.type != T_DIR)
        return FS_ERR_NOTDIR;
    if (!can(&d, 4))
        return FS_ERR_PERM;

    for (uint32_t i = *cursor; i < d.size / DIRENT_SIZE; i++) {
        dirent_t e;
        inode_t c;

        r = dir_get(&d, i, &e);
        if (r != FS_OK)
            return r;
        if (!e.ino || is_dot(e.name))
            continue;
        r = fs_iget(e.ino, &c);
        if (r != FS_OK)
            return r;

        memcpy(out->name, e.name, sizeof(e.name));
        out->name[FS_NAME_MAX] = '\0';
        out->ino = e.ino;
        out->type = c.type;
        out->size = c.size;
        *cursor = i + 1;
        return 1;
    }
    return 0;
}

int fs_chdir(const char *path)
{
    res_t rs;
    inode_t n;
    int r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (!rs.ino)
        return FS_ERR_NOENT;
    r = fs_iget(rs.ino, &n);
    if (r != FS_OK)
        return r;
    if (n.type != T_DIR)
        return FS_ERR_NOTDIR;
    cwd_ino = rs.ino;
    return FS_OK;
}

int fs_getcwd(char *buf, uint32_t size)
{
    char names[64][FS_NAME_MAX + 1];
    uint32_t cur = cwd_ino, depth = 0, pos = 0;

    if (!fs_is_mounted || size < 2) {
        if (size >= 2) {
            buf[0] = '/';
            buf[1] = '\0';
        }
        return fs_is_mounted ? FS_ERR_NAMETOOLONG : FS_ERR_NOTMOUNTED;
    }

    while (cur != ROOT_INO) {
        dlook_t l;
        int r;

        if (depth >= 64)
            return FS_ERR_NAMETOOLONG;
        r = dir_find(cur, "..", &l);
        if (r != FS_OK || !l.ino)
            return r != FS_OK ? r : FS_ERR_CORRUPT;
        r = dir_name_of(l.ino, cur, names[depth]);
        if (r != FS_OK)
            return r;
        depth++;
        cur = l.ino;
    }

    if (depth == 0) {
        buf[0] = '/';
        buf[1] = '\0';
        return FS_OK;
    }
    while (depth) {
        uint32_t len = strlen(names[--depth]);

        if (pos + 1 + len + 1 > size)
            return FS_ERR_NAMETOOLONG;
        buf[pos++] = '/';
        memcpy(buf + pos, names[depth], len);
        pos += len;
    }
    buf[pos] = '\0';
    return FS_OK;
}

/* ======================================================================= */
/* Descritores de arquivo                                                  */
/* ======================================================================= */

#define MAX_FDS 16

static struct {
    uint8_t  used, flags;
    uint32_t ino, pos;
} fds[MAX_FDS];

static int do_open(const char *path, int flags)
{
    res_t rs;
    inode_t n;
    uint32_t ino;
    int fd, r = resolve(path, &rs);

    if (r != FS_OK)
        return r;
    if (!(flags & (FS_O_READ | FS_O_WRITE)))
        flags |= FS_O_READ;

    for (fd = 0; fd < MAX_FDS && fds[fd].used; fd++) { }
    if (fd == MAX_FDS)
        return FS_ERR_MFILE;

    ino = rs.ino;
    if (!ino) {
        if (!(flags & FS_O_CREATE))
            return FS_ERR_NOENT;
        r = make_node(rs.parent, rs.leaf, T_FILE, MODE_FILE_DEFAULT, &ino);
        if (r != FS_OK)
            return r;
    }

    r = fs_iget(ino, &n);
    if (r != FS_OK)
        return r;
    if (n.type == T_DIR)
        return FS_ERR_ISDIR;
    if (((flags & FS_O_READ) && !can(&n, 4)) || ((flags & FS_O_WRITE) && !can(&n, 2)))
        return FS_ERR_PERM;
    if ((flags & FS_O_TRUNC) && (flags & FS_O_WRITE)) {
        r = inode_truncate(ino, &n, 0);
        if (r != FS_OK)
            return r;
    }

    fds[fd].used = 1;
    fds[fd].flags = (uint8_t)flags;
    fds[fd].ino = ino;
    fds[fd].pos = 0;
    return fd;
}

int fs_open(const char *path, int flags)
{
    int fd, e;

    if (!fs_is_mounted)
        return FS_ERR_NOTMOUNTED;
    if (!(flags & (FS_O_CREATE | FS_O_TRUNC)))
        return do_open(path, flags);

    e = begin_mut();
    if (e != FS_OK)
        return e;
    fd = do_open(path, flags);
    e = end_mut(fd < 0 ? fd : 0);
    if (e < 0 && fd >= 0) {
        fds[fd].used = 0;
        return e;
    }
    return fd;
}

int fs_close(int fd)
{
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used)
        return FS_ERR_BADFD;
    fds[fd].used = 0;
    return FS_OK;
}

int fs_read(int fd, void *buf, uint32_t len)
{
    inode_t n;
    int r;

    if (!fs_is_mounted)
        return FS_ERR_NOTMOUNTED;
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used || !(fds[fd].flags & FS_O_READ))
        return FS_ERR_BADFD;
    r = fs_iget(fds[fd].ino, &n);
    if (r != FS_OK)
        return r;
    if (n.type != T_FILE)
        return FS_ERR_NOENT;                      /* o arquivo foi apagado */
    r = inode_read(&n, fds[fd].pos, buf, len);
    if (r > 0)
        fds[fd].pos += (uint32_t)r;
    return r;
}

int fs_write(int fd, const void *buf, uint32_t len)
{
    inode_t n;
    int r, w;

    if (!fs_is_mounted)
        return FS_ERR_NOTMOUNTED;
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used || !(fds[fd].flags & FS_O_WRITE))
        return FS_ERR_BADFD;
    if (len == 0)
        return 0;

    r = begin_mut();
    if (r != FS_OK)
        return r;
    w = fs_iget(fds[fd].ino, &n);
    if (w == FS_OK && n.type != T_FILE)
        w = FS_ERR_NOENT;
    if (w == FS_OK) {
        if (fds[fd].flags & FS_O_APPEND)
            fds[fd].pos = n.size;
        w = inode_write(fds[fd].ino, &n, fds[fd].pos, buf, len);
        if (w > 0)
            fds[fd].pos += (uint32_t)w;
    }
    r = end_mut(w < 0 ? w : 0);
    return r < 0 ? r : w;
}

int fs_seek(int fd, int32_t offset, int whence)
{
    inode_t n;
    int64_t base, target;
    int r;

    if (!fs_is_mounted)
        return FS_ERR_NOTMOUNTED;
    if (fd < 0 || fd >= MAX_FDS || !fds[fd].used)
        return FS_ERR_BADFD;
    r = fs_iget(fds[fd].ino, &n);
    if (r != FS_OK)
        return r;

    if (whence == FS_SEEK_SET)      base = 0;
    else if (whence == FS_SEEK_CUR) base = fds[fd].pos;
    else if (whence == FS_SEEK_END) base = n.size;
    else return FS_ERR_INVAL;

    target = base + offset;
    if (target < 0 || target > (int64_t)MAX_FILE_BYTES)
        return FS_ERR_INVAL;
    fds[fd].pos = (uint32_t)target;
    return (int)target;
}

int fs_read_file(const char *path, void *buf, uint32_t cap, uint32_t *out_len)
{
    uint8_t *p = buf;
    uint32_t got = 0;
    int fd = fs_open(path, FS_O_READ), r = FS_OK;

    if (fd < 0)
        return fd;
    while (got < cap) {
        int n = fs_read(fd, p + got, cap - got);

        if (n < 0) { r = n; break; }
        if (n == 0)
            break;
        got += (uint32_t)n;
    }
    if (r == FS_OK && got == cap) {                /* sobrou arquivo? */
        uint8_t extra;
        if (fs_read(fd, &extra, 1) > 0)
            r = FS_ERR_TOOBIG;
    }
    fs_close(fd);
    if (out_len)
        *out_len = got;
    return r;
}

int fs_write_file(const char *path, const void *data, uint32_t len, int append)
{
    const uint8_t *p = data;
    uint32_t done = 0;
    int fd = fs_open(path, FS_O_WRITE | FS_O_CREATE | (append ? FS_O_APPEND : FS_O_TRUNC));
    int r = FS_OK;

    if (fd < 0)
        return fd;
    while (done < len) {
        int n = fs_write(fd, p + done, len - done);

        if (n < 0) { r = n; break; }
        if (n == 0) { r = FS_ERR_NOSPC; break; }
        done += (uint32_t)n;
    }
    fs_close(fd);
    return r;
}

/* ======================================================================= */
/* API publica: cada operacao que escreve vira uma transacao               */
/* ======================================================================= */

#define MUT(call) do { int _r = begin_mut(); if (_r != FS_OK) return _r; \
                       _r = (call); return end_mut(_r); } while (0)

int fs_mkdir(const char *p)                  { MUT(do_mkdir(p)); }
int fs_rmdir(const char *p)                  { MUT(do_rmdir(p)); }
int fs_rmtree(const char *p)                 { MUT(do_rmtree(p)); }
int fs_create(const char *p)                 { MUT(do_create(p, NULL)); }
int fs_unlink(const char *p)                 { MUT(do_unlink(p)); }
int fs_rename(const char *a, const char *b)  { MUT(do_rename(a, b)); }
int fs_copy(const char *a, const char *b, int rec) { MUT(do_copy(a, b, rec)); }
int fs_truncate(const char *p, uint32_t sz)  { MUT(do_truncate(p, sz)); }
int fs_chmod(const char *p, uint16_t m)      { MUT(do_chmod(p, m)); }
int fs_chown(const char *p, uint8_t uid)     { MUT(do_chown(p, uid)); }

int fs_txn_begin(void)        { return begin_mut(); }
int fs_txn_end(int result)    { return end_mut(result); }

/* ======================================================================= */
/* Montar, formatar, estatisticas                                          */
/* ======================================================================= */

int fs_statfs(fs_statfs_t *o)
{
    if (!fs_is_mounted)
        return FS_ERR_NOTMOUNTED;
    o->block_size = BLOCK_SIZE;
    o->total_blocks = fs_sb.total_blocks;
    o->free_blocks = fs_sb.free_blocks;
    o->total_inodes = fs_sb.inode_count;
    o->free_inodes = fs_sb.free_inodes;
    o->was_dirty = was_dirty;
    bc_stats(&o->cache_hits, &o->cache_misses);
    return FS_OK;
}

static void reset_state(void)
{
    fs_is_mounted = 0;
    disk_dirty = 0;
    was_dirty = 0;
    cwd_ino = ROOT_INO;
    hint_block = hint_inode = 0;
    memset(fds, 0, sizeof(fds));
}

int fs_mount(void)
{
    sb_t s;
    inode_t root;
    uint32_t total;
    int r;

    reset_state();
    if (ata_initialize() != 0)
        return FS_ERR_NODISK;
    total = ata_sector_count();

    r = bc_init(total);
    if (r != FS_OK)
        return r;
    r = bc_read(SUPER_LBA, &s);
    if (r != FS_OK) {
        bc_exit();
        return r;
    }

    if (memcmp(s.magic, FS_MAGIC, 8) != 0) {
        bc_exit();
        return FS_ERR_NOTFS;                      /* inclusive discos OrionFS v2 */
    }
    if (s.crc != crc32(&s, BLOCK_SIZE - 4) || s.version != FS_VERSION ||
        s.block_size != BLOCK_SIZE || s.root_ino != ROOT_INO ||
        s.total_blocks > total || s.inode_count == 0 ||
        s.ibitmap_start < 2 ||
        s.bbitmap_start != s.ibitmap_start + s.ibitmap_blocks ||
        s.itable_start != s.bbitmap_start + s.bbitmap_blocks ||
        s.data_start != s.itable_start + s.itable_blocks ||
        s.data_start >= s.total_blocks ||
        s.ibitmap_blocks * BITS_PER_BLOCK < s.inode_count ||
        s.bbitmap_blocks * BITS_PER_BLOCK < s.total_blocks ||
        s.itable_blocks * INODES_PER_BLOCK < s.inode_count) {
        bc_exit();
        return FS_ERR_CORRUPT;
    }

    fs_sb = s;
    fs_is_mounted = 1;
    was_dirty = (s.state == 0);

    if (was_dirty) {
        log_write("[FS] volume sujo: recontando blocos livres\n");
        if (fs_recount_free() != FS_OK) {
            fs_is_mounted = 0;
            bc_exit();
            return FS_ERR_IO;
        }
    }

    r = fs_iget(ROOT_INO, &root);
    if (r != FS_OK || root.type != T_DIR) {
        fs_is_mounted = 0;
        bc_exit();
        return r != FS_OK ? r : FS_ERR_CORRUPT;
    }

    fs_sb.mount_count++;
    return FS_OK;
}

int fs_format(void)
{
    static const uint8_t zeros[BLOCK_SIZE];
    uint32_t total, inodes, ibb, bbb, itb, ib, bb, it, ds;
    inode_t root;
    dirent_t e;
    uint32_t ino, blk;
    int r;

    reset_state();
    if (ata_initialize() != 0)
        return FS_ERR_NODISK;
    total = ata_sector_count();
    if (total < 64)
        return FS_ERR_NOSPC;

    r = bc_init(total);
    if (r != FS_OK)
        return r;

    inodes = total / 8;
    if (inodes < 32)    inodes = 32;
    if (inodes > 65536) inodes = 65536;
    inodes = (inodes + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK * INODES_PER_BLOCK;

    ibb = (inodes + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
    bbb = (total + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
    itb = inodes / INODES_PER_BLOCK;
    ib = 2;
    bb = ib + ibb;
    it = bb + bbb;
    ds = it + itb;
    if (ds + 16 > total) {
        bc_exit();
        return FS_ERR_NOSPC;
    }

    /* tabela de inodes zerada (tipo 0 = livre) */
    for (uint32_t b = it; b < ds; b++) {
        if ((r = bc_write(b, zeros)) != FS_OK)
            goto fail;
    }
    /* bitmap de blocos: metadados (0..ds-1) e o que passa do fim ficam "usados" */
    for (uint32_t b = 0; b < bbb; b++) {
        uint8_t s[BLOCK_SIZE];

        memset(s, 0, BLOCK_SIZE);
        for (uint32_t k = 0; k < BITS_PER_BLOCK; k++) {
            uint32_t bit = b * BITS_PER_BLOCK + k;

            if (bit < ds || bit >= total)
                s[k / 8] |= (uint8_t)(1u << (k % 8));
        }
        if ((r = bc_write(bb + b, s)) != FS_OK)
            goto fail;
    }
    /* bitmap de inodes: so o padding apos o ultimo inode fica "usado" */
    for (uint32_t b = 0; b < ibb; b++) {
        uint8_t s[BLOCK_SIZE];

        memset(s, 0, BLOCK_SIZE);
        for (uint32_t k = 0; k < BITS_PER_BLOCK; k++) {
            if (b * BITS_PER_BLOCK + k >= inodes)
                s[k / 8] |= (uint8_t)(1u << (k % 8));
        }
        if ((r = bc_write(ib + b, s)) != FS_OK)
            goto fail;
    }

    memset(&fs_sb, 0, sizeof(fs_sb));
    memcpy(fs_sb.magic, FS_MAGIC, 8);
    fs_sb.version = FS_VERSION;
    fs_sb.block_size = BLOCK_SIZE;
    fs_sb.total_blocks = total;
    fs_sb.inode_count = inodes;
    fs_sb.ibitmap_start = ib;  fs_sb.ibitmap_blocks = ibb;
    fs_sb.bbitmap_start = bb;  fs_sb.bbitmap_blocks = bbb;
    fs_sb.itable_start = it;   fs_sb.itable_blocks = itb;
    fs_sb.data_start = ds;
    fs_sb.root_ino = ROOT_INO;
    fs_sb.free_blocks = total - ds;
    fs_sb.free_inodes = inodes;
    fs_sb.state = 1;
    fs_sb.create_time = now();
    fs_is_mounted = 1;

    /* raiz: inode 1, com "." e ".." apontando para ela mesma */
    if ((r = fs_ialloc(&ino)) != FS_OK)
        goto fail;
    if (ino != ROOT_INO) {
        r = FS_ERR_CORRUPT;
        goto fail;
    }
    if ((r = fs_balloc(&blk)) != FS_OK)
        goto fail;

    memset(&root, 0, sizeof(root));
    root.type = T_DIR;
    root.mode = MODE_DIR_DEFAULT;
    root.nlink = 2;
    root.size = 2 * DIRENT_SIZE;
    root.ctime = root.mtime = fs_sb.create_time;
    root.direct[0] = blk;

    {
        uint8_t s[BLOCK_SIZE];

        memset(s, 0, BLOCK_SIZE);
        memset(&e, 0, sizeof(e));
        e.ino = ROOT_INO;
        e.name[0] = '.';
        memcpy(s, &e, DIRENT_SIZE);
        e.name[1] = '.';
        memcpy(s + DIRENT_SIZE, &e, DIRENT_SIZE);
        if ((r = bc_write(blk, s)) != FS_OK)
            goto fail;
    }
    if ((r = fs_iput(ROOT_INO, &root)) != FS_OK)
        goto fail;
    if ((r = fs_super_write()) != FS_OK)
        goto fail;
    if (ata_flush() != 0) {
        r = FS_ERR_IO;
        goto fail;
    }
    return FS_OK;

fail:
    fs_is_mounted = 0;
    bc_exit();
    return r;
}
