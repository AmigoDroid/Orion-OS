/* fsck do OrionFS: confere o volume contra ele mesmo.
 *
 * 1. Caminha a arvore a partir da raiz (iterativo, sem recursao) e marca
 *    quais inodes e quais blocos sao ALCANCAVEIS.
 * 2. Compara com os bitmaps gravados em disco:
 *      bit ligado e nao alcancavel  -> vazamento / inode orfao
 *      bit desligado e alcancavel   -> o bitmap esqueceu um bloco em uso
 * 3. Confere links ("." / ".."), contagem de links e contadores do superbloco.
 *
 * Com repair=1 conserta o que da para decidir sozinho (bitmaps, orfaos,
 * entradas apontando para o nada, nlink, contadores). Bloco fora do
 * volume ou compartilhado por dois arquivos e so REPORTADO: escolher qual
 * dos donos esta certo exige um humano. */

#include "fs_internal.h"
#include "mm.h"
#include "string.h"
#include "kprintf.h"

#define MAX_DEPTH 256
#define MAX_MSGS  24

typedef struct {
    uint32_t ino, parent, idx, nsub;
    inode_t  d;
} frame_t;

typedef struct {
    uint8_t *used_b, *ref_i;
    uint32_t errors, fixed, shown;
    int      repair, verbose;
} ctx_t;

#define GET(m, i) (((m)[(i) / 8] >> ((i) % 8)) & 1)
#define SET(m, i) ((m)[(i) / 8] |= (uint8_t)(1u << ((i) % 8)))

static void problem(ctx_t *c, const char *fmt, uint32_t a, uint32_t b)
{
    c->errors++;
    if (c->verbose && c->shown < MAX_MSGS) {
        c->shown++;
        kprintf("  fsck: ");
        kprintf(fmt, a, b);
        kprintf("\n");
    }
}

static void fixed(ctx_t *c, const char *what)
{
    c->fixed++;
    if (c->verbose && c->shown < MAX_MSGS) {
        c->shown++;
        kprintf("        -> corrigido: %s\n", what);
    }
}

static int claim(ctx_t *c, uint32_t ino, uint32_t blk)
{
    if (blk < fs_sb.data_start || blk >= fs_sb.total_blocks) {
        problem(c, "inode %u aponta para bloco %u fora do volume", ino, blk);
        return 0;
    }
    if (GET(c->used_b, blk)) {
        problem(c, "bloco %u e usado por mais de um dono (inode %u)", blk, ino);
        return 0;
    }
    SET(c->used_b, blk);
    return 1;
}

static void claim_tree(ctx_t *c, uint32_t ino, uint32_t blk, int level)
{
    if (!blk || !claim(c, ino, blk) || level == 0)
        return;
    {
        uint32_t t[PTRS_PER_BLOCK];

        if (bc_read(blk, t) != FS_OK) {
            problem(c, "inode %u: nao consegui ler a tabela %u", ino, blk);
            return;
        }
        for (uint32_t i = 0; i < PTRS_PER_BLOCK; i++)
            claim_tree(c, ino, t[i], level - 1);
    }
}

static void claim_blocks(ctx_t *c, uint32_t ino, const inode_t *in)
{
    for (uint32_t i = 0; i < N_DIRECT; i++) {
        if (in->direct[i])
            claim(c, ino, in->direct[i]);
    }
    claim_tree(c, ino, in->indirect, 1);
    claim_tree(c, ino, in->dindirect, 2);
}

static int read_entry(const inode_t *d, uint32_t idx, dirent_t *e)
{
    inode_t t = *d;
    uint8_t s[BLOCK_SIZE];
    uint32_t phys;
    int fresh;

    if (fs_bmap(&t, idx / DIRENTS_PER_BLOCK, 0, &phys, &fresh) != FS_OK || !phys)
        return FS_ERR_CORRUPT;
    if (bc_read(phys, s) != FS_OK)
        return FS_ERR_IO;
    memcpy(e, s + (idx % DIRENTS_PER_BLOCK) * DIRENT_SIZE, DIRENT_SIZE);
    return FS_OK;
}

static int write_entry(const inode_t *d, uint32_t idx, const dirent_t *e)
{
    inode_t t = *d;
    uint8_t s[BLOCK_SIZE];
    uint32_t phys;
    int fresh;

    if (fs_bmap(&t, idx / DIRENTS_PER_BLOCK, 0, &phys, &fresh) != FS_OK || !phys)
        return FS_ERR_CORRUPT;
    if (bc_read(phys, s) != FS_OK)
        return FS_ERR_IO;
    memcpy(s + (idx % DIRENTS_PER_BLOCK) * DIRENT_SIZE, e, DIRENT_SIZE);
    return bc_write(phys, s);
}

static int ibit(uint32_t ino)
{
    int v = 0;

    fs_bitmap_get(fs_sb.ibitmap_start, ino - 1, &v);
    return v;
}

static int is_dot_name(const char *n)
{
    return (n[0] == '.' && !n[1]) || (n[0] == '.' && n[1] == '.' && !n[2]);
}

/* Passo 1: arvore. Devolve 0 se conseguiu percorrer, <0 se abortou. */
static int walk(ctx_t *c, frame_t *st)
{
    uint32_t sp = 0;
    inode_t root;

    if (fs_iget(ROOT_INO, &root) != FS_OK || root.type != T_DIR) {
        problem(c, "a raiz nao e um diretorio valido", 0, 0);
        return FS_ERR_CORRUPT;
    }
    SET(c->ref_i, ROOT_INO - 1);
    claim_blocks(c, ROOT_INO, &root);
    st[0].ino = ROOT_INO;
    st[0].parent = ROOT_INO;
    st[0].idx = 0;
    st[0].nsub = 0;
    st[0].d = root;
    sp = 1;

    while (sp > 0) {
        frame_t *f = &st[sp - 1];
        dirent_t e;
        inode_t ch;
        uint32_t idx;

        if (f->idx >= f->d.size / DIRENT_SIZE) {
            uint16_t want = (uint16_t)(2 + f->nsub);

            if (f->d.nlink != want) {
                problem(c, "diretorio %u: nlink incorreto (%u)", f->ino, f->d.nlink);
                if (c->repair) {
                    f->d.nlink = want;
                    if (fs_iput(f->ino, &f->d) == FS_OK)
                        fixed(c, "nlink do diretorio");
                }
            }
            sp--;
            continue;
        }

        idx = f->idx++;
        if (read_entry(&f->d, idx, &e) != FS_OK) {
            problem(c, "diretorio %u: bloco da entrada %u ilegivel", f->ino, idx);
            continue;
        }
        if (e.ino == 0)
            continue;
        if (e.name[sizeof(e.name) - 1] != '\0' || e.name[0] == '\0') {
            problem(c, "diretorio %u: entrada %u com nome invalido", f->ino, idx);
            if (c->repair) {
                memset(&e, 0, sizeof(e));
                if (write_entry(&f->d, idx, &e) == FS_OK)
                    fixed(c, "entrada de nome invalido removida");
            }
            continue;
        }

        if (idx == 0 || idx == 1) {                 /* "." e ".." ficam sempre nos 2 primeiros slots */
            uint32_t want = idx == 0 ? f->ino : f->parent;
            const char *nm = idx == 0 ? "." : "..";

            if (strcmp(e.name, nm) != 0 || e.ino != want) {
                problem(c, "diretorio %u: entrada %u deveria ser '.'/'..' correta", f->ino, idx);
                if (c->repair) {
                    memset(&e, 0, sizeof(e));
                    e.ino = want;
                    e.name[0] = '.';
                    if (idx == 1)
                        e.name[1] = '.';
                    if (write_entry(&f->d, idx, &e) == FS_OK)
                        fixed(c, "'.' / '..' refeitos");
                }
            }
            continue;
        }
        if (is_dot_name(e.name)) {
            problem(c, "diretorio %u: '.' ou '..' repetido na entrada %u", f->ino, idx);
            continue;
        }

        if (e.ino > fs_sb.inode_count || !ibit(e.ino) ||
            fs_iget(e.ino, &ch) != FS_OK ||
            (ch.type != T_FILE && ch.type != T_DIR)) {
            problem(c, "diretorio %u: entrada %u aponta para inode invalido", f->ino, idx);
            if (c->repair) {
                memset(&e, 0, sizeof(e));
                if (write_entry(&f->d, idx, &e) == FS_OK)
                    fixed(c, "entrada removida");
            }
            continue;
        }
        if (GET(c->ref_i, e.ino - 1)) {
            problem(c, "inode %u e referenciado mais de uma vez (diretorio %u)", e.ino, f->ino);
            continue;
        }
        SET(c->ref_i, e.ino - 1);

        if (ch.size > MAX_FILE_BYTES || (ch.type == T_DIR && ch.size % DIRENT_SIZE))
            problem(c, "inode %u com tamanho invalido (%u)", e.ino, ch.size);
        claim_blocks(c, e.ino, &ch);

        if (ch.type == T_DIR) {
            f->nsub++;
            if (sp >= MAX_DEPTH) {
                problem(c, "arvore mais funda que %u niveis", MAX_DEPTH, 0);
                continue;
            }
            st[sp].ino = e.ino;
            st[sp].parent = f->ino;
            st[sp].idx = 0;
            st[sp].nsub = 0;
            st[sp].d = ch;
            sp++;
        } else if (ch.nlink != 1) {
            problem(c, "arquivo %u: nlink=%u, esperado 1", e.ino, ch.nlink);
            if (c->repair) {
                ch.nlink = 1;
                if (fs_iput(e.ino, &ch) == FS_OK)
                    fixed(c, "nlink do arquivo");
            }
        }
    }
    return FS_OK;
}

/* Passo 2a: bitmap de inodes contra o que a arvore alcancou. */
static void compare_inodes(ctx_t *c)
{
    for (uint32_t ino = 1; ino <= fs_sb.inode_count; ino++) {
        int bit = ibit(ino), ref = GET(c->ref_i, ino - 1);

        if (bit && !ref) {
            problem(c, "inode %u esta alocado mas nenhum diretorio o referencia (orfao)", ino, 0);
            if (c->repair) {
                inode_t z;

                memset(&z, 0, sizeof(z));
                fs_iput(ino, &z);
                if (fs_ifree(ino) == FS_OK)
                    fixed(c, "inode orfao liberado");
            }
        } else if (!bit && ref) {
            problem(c, "inode %u esta em uso mas o bitmap diz livre", ino, 0);
            if (c->repair && fs_bitmap_put(fs_sb.ibitmap_start, ino - 1, 1) == FS_OK)
                fixed(c, "bitmap de inodes");
        }
    }
}

/* Passo 2b: bitmap de blocos contra os blocos alcancados (por setor do bitmap). */
static void compare_blocks(ctx_t *c)
{
    for (uint32_t b = 0; b < fs_sb.bbitmap_blocks; b++) {
        uint8_t s[BLOCK_SIZE];
        int dirty = 0;

        if (bc_read(fs_sb.bbitmap_start + b, s) != FS_OK) {
            problem(c, "bitmap de blocos %u ilegivel", b, 0);
            continue;
        }
        for (uint32_t k = 0; k < BITS_PER_BLOCK; k++) {
            uint32_t blk = b * BITS_PER_BLOCK + k;
            int bit = GET(s, k), want;

            if (blk >= fs_sb.total_blocks)
                want = 1;                           /* padding: sempre "usado" */
            else
                want = blk < fs_sb.data_start || GET(c->used_b, blk);

            if (bit == want)
                continue;
            problem(c, bit ? "bloco %u marcado como usado mas ninguem o usa (vazamento)"
                           : "bloco %u em uso mas o bitmap diz livre", blk, 0);
            if (c->repair) {
                if (want) s[k / 8] |= (uint8_t)(1u << (k % 8));
                else      s[k / 8] &= (uint8_t)~(1u << (k % 8));
                dirty = 1;
                c->fixed++;
            }
        }
        if (dirty && bc_write(fs_sb.bbitmap_start + b, s) != FS_OK)
            problem(c, "falha ao gravar o bitmap de blocos %u", b, 0);
    }
}

int fs_fsck(int repair, int verbose)
{
    ctx_t c;
    frame_t *stack;
    uint32_t fb, fi;
    fs_statfs_t st;
    int r, w;

    if (!fs_is_mounted)
        return FS_ERR_NOTMOUNTED;

    memset(&c, 0, sizeof(c));
    c.repair = repair;
    c.verbose = verbose;
    c.used_b = kcalloc((fs_sb.total_blocks + 7) / 8, 1);
    c.ref_i = kcalloc((fs_sb.inode_count + 7) / 8, 1);
    stack = kcalloc(MAX_DEPTH, sizeof(frame_t));
    if (!c.used_b || !c.ref_i || !stack) {
        kfree(c.used_b);
        kfree(c.ref_i);
        kfree(stack);
        return FS_ERR_NOMEM;
    }

    fs_statfs(&st);
    if (repair && (r = fs_txn_begin()) != FS_OK)
        goto out;

    for (uint32_t b = 0; b < fs_sb.data_start; b++)
        SET(c.used_b, b);                           /* metadados */

    r = walk(&c, stack);
    if (r == FS_OK) {
        compare_inodes(&c);
        compare_blocks(&c);

        fb = fs_sb.free_blocks;
        fi = fs_sb.free_inodes;
        if (fs_recount_free() != FS_OK) {
            problem(&c, "nao consegui recontar os livres", 0, 0);
        } else if (fb != fs_sb.free_blocks || fi != fs_sb.free_inodes) {
            problem(&c, "contadores do superbloco desatualizados (blocos %u, inodes %u)", fb, fi);
            if (repair)
                fixed(&c, "contadores recalculados");
            else {
                fs_sb.free_blocks = fb;             /* nao mexe sem permissao */
                fs_sb.free_inodes = fi;
            }
        }
        if (st.was_dirty && repair)
            fixed(&c, "volume marcado como limpo");
    }

    if (repair) {
        w = fs_txn_end(r);
        if (r == FS_OK && w < 0)
            r = w;
    }

out:
    if (verbose)
        kprintf("fsck: %u problema(s) encontrado(s), %u corrigido(s)\n", c.errors, c.fixed);
    kfree(c.used_b);
    kfree(c.ref_i);
    kfree(stack);
    if (r != FS_OK)
        return r;
    return (int)(c.errors > c.fixed ? c.errors - c.fixed : 0);
}
