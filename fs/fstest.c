/* Autoteste do OrionFS (comando `fstest`).
 *
 * Roda dentro de /.fstest, entao nao toca nos seus arquivos, e confere no
 * fim que blocos e inodes livres voltaram ao valor do inicio e que o fsck
 * nao acha nada. Tambem PROVOCA danos de proposito (bloco vazado, inode
 * orfao, nlink errado, entrada apontando para o nada) para provar que o
 * fsck os detecta e conserta. */

#include "fs_internal.h"
#include "kprintf.h"
#include "string.h"
#include "mm.h"

#define T "/.fstest"

static int failures, checks;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        kprintf("  [FALHOU] %s\n", what);
    }
}

#define EXPECT(call, want, what) check((call) == (want), what)


static uint8_t pat(uint32_t i)
{
    return (uint8_t)(i * 31u + (i >> 8) * 7u + 5u);
}

static int path_cwd_is(const char *want)
{
    char b[FS_PATH_MAX];

    return fs_getcwd(b, sizeof(b)) == FS_OK && strcmp(b, want) == 0;
}

static int read_equals(const char *path, const char *text)
{
    char b[256];
    uint32_t n = 0;

    return fs_read_file(path, b, sizeof(b), &n) == FS_OK &&
           n == strlen(text) && memcmp(b, text, n) == 0;
}

static uint32_t free_blocks(void)
{
    fs_statfs_t s;

    fs_statfs(&s);
    return s.free_blocks;
}

/* ---------------------------------------------------------------------- */

static void test_dirs(void)
{
    char name27[28], name28[29];

    kprintf(" FS: pastas e caminhos...\n");
    EXPECT(fs_mkdir(T), FS_OK, "mkdir /.fstest");
    EXPECT(fs_mkdir(T "/a"), FS_OK, "mkdir a");
    EXPECT(fs_mkdir(T "/a/b"), FS_OK, "mkdir a/b");
    EXPECT(fs_mkdir(T "/a"), FS_ERR_EXIST, "mkdir repetido -> EXIST");
    EXPECT(fs_mkdir(T "/x/y/z"), FS_ERR_NOENT, "mkdir sem pai -> NOENT");
    EXPECT(fs_chdir(T "/a/b"), FS_OK, "cd a/b");
    check(path_cwd_is(T "/a/b"), "pwd = /.fstest/a/b");
    EXPECT(fs_chdir("../.."), FS_OK, "cd ../..");
    check(path_cwd_is(T), "pwd = /.fstest");
    EXPECT(fs_chdir(T "//a///b/./.."), FS_OK, "cd com barras repetidas e pontos");
    check(path_cwd_is(T "/a"), "pwd = /.fstest/a depois de //a///b/./..");
    EXPECT(fs_chdir("/"), FS_OK, "cd /");
    EXPECT(fs_chdir(".."), FS_OK, "cd .. na raiz continua na raiz");
    check(path_cwd_is("/"), "pwd = /");
    EXPECT(fs_chdir(T "/nada"), FS_ERR_NOENT, "cd inexistente -> NOENT");

    memset(name27, 'n', 27); name27[27] = 0;
    memset(name28, 'n', 28); name28[28] = 0;
    {
        char p[64];
        ksnprintf(p, sizeof(p), T "/%s", name27);
        EXPECT(fs_mkdir(p), FS_OK, "nome de 27 caracteres");
        ksnprintf(p, sizeof(p), T "/%s", name28);
        EXPECT(fs_mkdir(p), FS_ERR_NAMETOOLONG, "nome de 28 caracteres -> NAMETOOLONG");
    }
    EXPECT(fs_mkdir(T "/com espaco"), FS_ERR_INVAL, "nome com espaco -> INVAL");
    EXPECT(fs_mkdir(T "/a/."), FS_ERR_EXIST, "mkdir '.' -> EXIST");
    EXPECT(fs_create(T "/a/b/.."), FS_ERR_EXIST, "create '..' -> EXIST");
}

static void test_small_files(void)
{
    fs_stat_t st;

    kprintf(" FS: arquivos pequenos...\n");
    EXPECT(fs_write_file(T "/hello.txt", "Ola, Orion!", 11, 0), FS_OK, "write_file");
    check(read_equals(T "/hello.txt", "Ola, Orion!"), "read_file devolve o mesmo texto");
    EXPECT(fs_write_file(T "/hello.txt", " Tudo bem?", 10, 1), FS_OK, "append");
    check(read_equals(T "/hello.txt", "Ola, Orion! Tudo bem?"), "append concatenou");
    check(fs_stat(T "/hello.txt", &st) == FS_OK && st.size == 21 &&
          st.type == FS_TYPE_FILE && st.mode == 0644 && st.nlink == 1, "stat do arquivo");
    EXPECT(fs_create(T "/hello.txt"), FS_ERR_EXIST, "create existente -> EXIST");
    EXPECT(fs_write_file(T "/hello.txt", "x", 1, 0), FS_OK, "sobrescrever (trunca)");
    check(read_equals(T "/hello.txt", "x"), "ficou so o novo conteudo");
    EXPECT(fs_create(T "/vazio"), FS_OK, "create arquivo vazio");
    check(fs_stat(T "/vazio", &st) == FS_OK && st.size == 0 && st.blocks == 0, "vazio nao gasta bloco");
    EXPECT(fs_unlink(T "/hello.txt"), FS_OK, "unlink");
    EXPECT(fs_stat(T "/hello.txt", &st), FS_ERR_NOENT, "stat apos unlink -> NOENT");
    EXPECT(fs_unlink(T "/a"), FS_ERR_ISDIR, "unlink de pasta -> ISDIR");
    EXPECT(fs_unlink(T "/hello.txt"), FS_ERR_NOENT, "unlink inexistente -> NOENT");
    EXPECT(fs_unlink(T "/vazio"), FS_OK, "unlink do vazio");
}

static void test_big_file(void)
{
    enum { SIZE = 100 * 1000 };                  /* 196 blocos: passa do indireto (137) */
    static uint8_t buf[1000];
    uint32_t base = free_blocks(), before_trunc;
    fs_stat_t st;
    int fd, ok = 1;

    kprintf(" FS: arquivo grande (%d bytes, usa duplo indireto)...\n", SIZE);
    fd = fs_open(T "/big", FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
    check(fd >= 0, "open big");
    for (uint32_t off = 0; off < SIZE && fd >= 0; off += sizeof(buf)) {
        for (uint32_t i = 0; i < sizeof(buf); i++)
            buf[i] = pat(off + i);
        if (fs_write(fd, buf, sizeof(buf)) != (int)sizeof(buf))
            ok = 0;
    }
    check(ok, "escreveu tudo em pedacos de 1000 bytes");
    fs_close(fd);

    check(fs_stat(T "/big", &st) == FS_OK && st.size == SIZE, "tamanho correto");
    check(st.blocks >= 196 + 2, "blocos de dados + tabelas alocados");
    check(base - free_blocks() == st.blocks, "blocos livres cairam exatamente o alocado");

    ok = 1;
    fd = fs_open(T "/big", FS_O_READ);
    {
        static uint8_t rb[777];
        uint32_t off = 0;

        for (;;) {
            int n = fs_read(fd, rb, sizeof(rb));

            if (n <= 0) { ok &= n == 0; break; }
            for (int i = 0; i < n; i++)
                ok &= rb[i] == pat(off + (uint32_t)i);
            off += (uint32_t)n;
        }
        ok &= off == SIZE;
    }
    check(ok, "leitura em pedacos de 777 bytes confere byte a byte");
    fs_close(fd);

    before_trunc = free_blocks();
    EXPECT(fs_truncate(T "/big", 50000), FS_OK, "truncate para 50000");
    check(free_blocks() > before_trunc, "truncate devolveu blocos");
    ok = 1;
    fd = fs_open(T "/big", FS_O_READ);
    for (uint32_t off = 0; off < 50000; off += sizeof(buf)) {
        uint32_t want = 50000 - off < sizeof(buf) ? 50000 - off : sizeof(buf);

        if (fs_read(fd, buf, want) != (int)want)
            ok = 0;
        for (uint32_t i = 0; i < want; i++)
            ok &= buf[i] == pat(off + i);
    }
    check(ok && fs_read(fd, buf, 1) == 0, "o inicio sobreviveu ao truncate e acaba em 50000");
    fs_close(fd);

    EXPECT(fs_truncate(T "/big", 30000), FS_OK, "truncate para 30000 (meio de bloco)");
    EXPECT(fs_truncate(T "/big", 40000), FS_OK, "crescer para 40000");
    fd = fs_open(T "/big", FS_O_READ);
    fs_seek(fd, 30000, FS_SEEK_SET);
    ok = fs_read(fd, buf, 1000) == 1000;
    for (int i = 0; i < 1000; i++)
        ok &= buf[i] == 0;
    check(ok, "area criada ao crescer vem zerada (nao ressuscita dados)");
    fs_close(fd);

    EXPECT(fs_truncate(T "/big", 0), FS_OK, "truncate para 0");
    check(free_blocks() == base, "blocos livres voltaram ao valor inicial");

    /* arquivo esparso: so o ultimo pedaco existe */
    fd = fs_open(T "/sparse", FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
    fs_seek(fd, 60000, FS_SEEK_SET);
    check(fs_write(fd, "FIM", 3) == 3, "escreve alem do fim (buraco)");
    fs_close(fd);
    check(fs_stat(T "/sparse", &st) == FS_OK && st.size == 60003 && st.blocks < 12,
          "arquivo esparso: tamanho 60003 mas poucos blocos");
    fd = fs_open(T "/sparse", FS_O_READ);
    ok = fs_read(fd, buf, 1000) == 1000;
    for (int i = 0; i < 1000; i++)
        ok &= buf[i] == 0;
    check(ok, "o buraco le como zeros");
    fs_close(fd);
    EXPECT(fs_unlink(T "/sparse"), FS_OK, "remove o esparso");
    EXPECT(fs_unlink(T "/big"), FS_OK, "remove big");
    check(free_blocks() == base, "sem vazamento de blocos");
}

static void test_fd(void)
{
    char b[16];
    int fd;

    kprintf(" FS: descritores...\n");
    fd = fs_open(T "/fd", FS_O_READ | FS_O_WRITE | FS_O_CREATE);
    check(fd >= 0, "open RW + CREATE");
    check(fs_write(fd, "ABCDEF", 6) == 6, "write");
    check(fs_seek(fd, 2, FS_SEEK_SET) == 2, "seek SET");
    check(fs_read(fd, b, 3) == 3 && memcmp(b, "CDE", 3) == 0, "read depois do seek");
    check(fs_seek(fd, -1, FS_SEEK_END) == 5, "seek END");
    check(fs_read(fd, b, 4) == 1 && b[0] == 'F', "read no fim devolve 1 byte");
    check(fs_read(fd, b, 4) == 0, "read no fim devolve 0");
    check(fs_seek(fd, -100, FS_SEEK_CUR) == FS_ERR_INVAL, "seek negativo -> INVAL");
    fs_close(fd);
    EXPECT(fs_close(fd), FS_ERR_BADFD, "close duplo -> BADFD");
    EXPECT(fs_read(99, b, 1), FS_ERR_BADFD, "read em fd invalido");
    EXPECT(fs_open(T "/a", FS_O_READ), FS_ERR_ISDIR, "open de pasta -> ISDIR");
    EXPECT(fs_open(T "/nao-existe", FS_O_READ), FS_ERR_NOENT, "open inexistente -> NOENT");

    fd = fs_open(T "/fd", FS_O_WRITE | FS_O_APPEND);
    fs_write(fd, "GH", 2);
    fs_close(fd);
    check(read_equals(T "/fd", "ABCDEFGH"), "O_APPEND");
    fd = fs_open(T "/fd", FS_O_READ);
    EXPECT(fs_write(fd, "x", 1), FS_ERR_BADFD, "write em fd somente-leitura -> BADFD");
    fs_close(fd);
    {
        int fds[20], n = 0;

        for (int i = 0; i < 20; i++) {
            fds[i] = fs_open(T "/fd", FS_O_READ);
            if (fds[i] >= 0) n++;
        }
        check(n == 16 && fds[16] == FS_ERR_MFILE, "limite de 16 descritores -> MFILE");
        for (int i = 0; i < 20; i++)
            if (fds[i] >= 0) fs_close(fds[i]);
    }
    EXPECT(fs_unlink(T "/fd"), FS_OK, "remove fd");
}

static void test_rename(void)
{
    kprintf(" FS: rename...\n");
    EXPECT(fs_write_file(T "/r1", "um", 2, 0), FS_OK, "cria r1");
    EXPECT(fs_rename(T "/r1", T "/r2"), FS_OK, "renomeia arquivo");
    check(read_equals(T "/r2", "um"), "conteudo preservado");
    EXPECT(fs_rename(T "/r2", T "/a/b/r3"), FS_OK, "move para outra pasta");
    check(read_equals(T "/a/b/r3", "um"), "chegou ao destino");
    EXPECT(fs_write_file(T "/s", "dois", 4, 0), FS_OK, "cria s");
    EXPECT(fs_rename(T "/s", T "/a/b/r3"), FS_OK, "rename sobre arquivo existente substitui");
    check(read_equals(T "/a/b/r3", "dois"), "substituido");
    EXPECT(fs_rename(T "/nada", T "/x"), FS_ERR_NOENT, "origem inexistente -> NOENT");
    EXPECT(fs_rename(T "/a", T "/a/b/a"), FS_ERR_INVAL, "pasta para dentro de si -> INVAL");
    EXPECT(fs_rename(T "/a/b/r3", T "/a"), FS_ERR_ISDIR, "arquivo sobre pasta -> ISDIR");

    EXPECT(fs_mkdir(T "/m"), FS_OK, "mkdir m");
    EXPECT(fs_rename(T "/a/b", T "/m/b2"), FS_OK, "move pasta com conteudo");
    check(read_equals(T "/m/b2/r3", "dois"), "filhos foram junto");
    EXPECT(fs_chdir(T "/m/b2"), FS_OK, "entra na pasta movida");
    EXPECT(fs_chdir(".."), FS_OK, "'..' da pasta movida aponta para o novo pai");
    check(path_cwd_is(T "/m"), "pwd correto apos mover");
    EXPECT(fs_write_file(T "/a/x", "x", 1, 0), FS_OK, "a deixa de ser vazia");
    EXPECT(fs_rename(T "/m", T "/a"), FS_ERR_NOTEMPTY, "pasta sobre pasta nao vazia -> NOTEMPTY");
    EXPECT(fs_unlink(T "/a/x"), FS_OK, "esvazia a de novo");
    EXPECT(fs_mkdir(T "/vazia"), FS_OK, "mkdir vazia");
    EXPECT(fs_rename(T "/vazia", T "/a"), FS_OK, "pasta vazia sobre pasta vazia substitui");
    EXPECT(fs_chdir("/"), FS_OK, "volta para a raiz");
    EXPECT(fs_rename(T "/m/b2", T "/a/b"), FS_OK, "devolve a pasta");
}

static void test_remove(void)
{
    kprintf(" FS: rmdir / BUSY / rmtree...\n");
    EXPECT(fs_rmdir(T "/a"), FS_ERR_NOTEMPTY, "rmdir de pasta cheia -> NOTEMPTY");
    EXPECT(fs_rmdir(T "/a/b/r3"), FS_ERR_NOTDIR, "rmdir de arquivo -> NOTDIR");
    EXPECT(fs_chdir(T "/m"), FS_OK, "entra em m");
    EXPECT(fs_rmdir(T "/m"), FS_ERR_BUSY, "rmdir da pasta atual -> BUSY");
    EXPECT(fs_chdir("/"), FS_OK, "sai");
    EXPECT(fs_rmdir(T "/m"), FS_OK, "rmdir de pasta vazia");
    EXPECT(fs_rmdir("/"), FS_ERR_INVAL, "rmdir da raiz -> INVAL");
}

static void test_copy(void)
{
    uint32_t base = free_blocks();

    kprintf(" FS: copy e rmtree...\n");
    EXPECT(fs_mkdir(T "/src"), FS_OK, "mkdir src");
    EXPECT(fs_mkdir(T "/src/d1"), FS_OK, "mkdir src/d1");
    EXPECT(fs_mkdir(T "/src/d1/d2"), FS_OK, "mkdir src/d1/d2");
    EXPECT(fs_write_file(T "/src/f1", "arquivo 1", 9, 0), FS_OK, "src/f1");
    EXPECT(fs_write_file(T "/src/d1/f2", "arquivo 2", 9, 0), FS_OK, "src/d1/f2");
    EXPECT(fs_write_file(T "/src/d1/d2/f3", "arquivo 3", 9, 0), FS_OK, "src/d1/d2/f3");

    EXPECT(fs_copy(T "/src/f1", T "/copia", 0), FS_OK, "cp arquivo");
    check(read_equals(T "/copia", "arquivo 1"), "copia identica");
    EXPECT(fs_copy(T "/src/d1/f2", T "/src", 0), FS_OK, "cp arquivo para dentro de pasta");
    check(read_equals(T "/src/f2", "arquivo 2"), "foi para src/f2");
    EXPECT(fs_copy(T "/src", T "/dst", 0), FS_ERR_ISDIR, "cp pasta sem -r -> ISDIR");
    EXPECT(fs_copy(T "/src", T "/dst", 1), FS_OK, "cp -r");
    check(read_equals(T "/dst/d1/d2/f3", "arquivo 3"), "arvore copiada ate o fundo");
    EXPECT(fs_copy(T "/src", T "/src/d1/dentro", 1), FS_ERR_INVAL, "cp -r para dentro de si -> INVAL");
    EXPECT(fs_copy(T "/nada", T "/x", 0), FS_ERR_NOENT, "cp inexistente -> NOENT");

    EXPECT(fs_rmtree(T "/src"), FS_OK, "rmtree src");
    EXPECT(fs_rmtree(T "/dst"), FS_OK, "rmtree dst");
    EXPECT(fs_unlink(T "/copia"), FS_OK, "remove copia");
    {
        fs_stat_t st;
        EXPECT(fs_stat(T "/dst/d1", &st), FS_ERR_NOENT, "tudo foi embora");
    }
    check(free_blocks() == base, "copy + rmtree sem vazamento de blocos");
}

static void test_perms(void)
{
    fs_stat_t st;

    kprintf(" FS: permissoes...\n");
    EXPECT(fs_write_file(T "/p", "so do root", 10, 0), FS_OK, "arquivo do root");
    fs_set_uid(7);
    check(read_equals(T "/p", "so do root"), "outro usuario pode ler (0644)");
    EXPECT(fs_write_file(T "/p", "x", 1, 0), FS_ERR_PERM, "outro usuario nao escreve -> PERM");
    EXPECT(fs_mkdir(T "/proibido"), FS_ERR_PERM, "nao cria em pasta do root -> PERM");
    EXPECT(fs_unlink(T "/p"), FS_ERR_PERM, "nao remove em pasta do root -> PERM");
    EXPECT(fs_chmod(T "/p", 0777), FS_ERR_PERM, "chmod por nao-dono -> PERM");
    fs_set_uid(0);
    EXPECT(fs_chmod(T, 0777), FS_OK, "root abre a pasta (0777)");
    fs_set_uid(7);
    EXPECT(fs_write_file(T "/do7", "meu", 3, 0), FS_OK, "usuario 7 cria arquivo");
    check(fs_stat(T "/do7", &st) == FS_OK && st.uid == 7, "dono gravado = 7");
    EXPECT(fs_chmod(T "/do7", 0600), FS_OK, "dono faz chmod 600");
    fs_set_uid(8);
    EXPECT(fs_open(T "/do7", FS_O_READ), FS_ERR_PERM, "terceiro nao le arquivo 600");
    EXPECT(fs_chown(T "/do7", 8), FS_ERR_PERM, "chown so root");
    fs_set_uid(0);
    check(read_equals(T "/do7", "meu"), "root le qualquer coisa");
    EXPECT(fs_chown(T "/do7", 9), FS_OK, "root faz chown");
    EXPECT(fs_chmod(T, 0755), FS_OK, "restaura 0755");
    EXPECT(fs_unlink(T "/p"), FS_OK, "limpa p");
    EXPECT(fs_unlink(T "/do7"), FS_OK, "limpa do7");
}

static void test_full_disk(void)
{
    static uint8_t chunk[BLOCK_SIZE * 8];
    uint32_t base = free_blocks(), total = 0;
    int fd, n, last = 0;

    kprintf(" FS: disco cheio (ENOSPC)...\n");
    if (base > 12000) {
        kprintf("  (pulado: disco grande demais para encher no teste)\n");
        return;
    }
    memset(chunk, 0xAB, sizeof(chunk));
    fd = fs_open(T "/fill", FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
    check(fd >= 0, "open fill");
    for (;;) {
        n = fs_write(fd, chunk, sizeof(chunk));
        if (n < 0) { last = n; break; }
        if (n == 0) { last = FS_ERR_NOSPC; break; }
        total += (uint32_t)n;
        if (total > base * BLOCK_SIZE)
            break;
    }
    fs_close(fd);
    check(last == FS_ERR_NOSPC, "escrever ate acabar o disco devolve NOSPC");
    check(free_blocks() < 8, "praticamente nao sobrou bloco livre");
    EXPECT(fs_mkdir(T "/sem-espaco"), FS_ERR_NOSPC, "mkdir sem espaco -> NOSPC (sem corromper)");
    check(fs_fsck(0, 0) == 0, "fsck limpo mesmo com o disco cheio");
    EXPECT(fs_unlink(T "/fill"), FS_OK, "remove o arquivao");
    check(free_blocks() == base, "todo o espaco voltou");
    EXPECT(fs_write_file(T "/depois", "ok", 2, 0), FS_OK, "volta a gravar normalmente");
    EXPECT(fs_unlink(T "/depois"), FS_OK, "limpa");
}

static void test_persistence(void)
{
    kprintf(" FS: persistencia (sync + remontagem)...\n");
    EXPECT(fs_write_file(T "/a/persiste", "continua aqui", 13, 0), FS_OK, "grava");
    EXPECT(fs_sync(), FS_OK, "sync");
    EXPECT(fs_mount(), FS_OK, "remonta (cache descartado, tudo vem do disco)");
    check(read_equals(T "/a/persiste", "continua aqui"), "arquivo sobreviveu a remontagem");
    check(path_cwd_is("/"), "diretorio atual volta para a raiz");
    {
        fs_statfs_t s;
        check(fs_statfs(&s) == FS_OK && !s.was_dirty, "volume foi encontrado limpo");
    }
    EXPECT(fs_unlink(T "/a/persiste"), FS_OK, "limpa");
}

static void test_fsck(void)
{
    uint32_t base_b, base_i, leaked, ino;
    fs_statfs_t s;
    inode_t n;
    fs_stat_t st;

    kprintf(" FS: fsck detecta e conserta danos provocados...\n");
    check(fs_fsck(0, 0) == 0, "fsck limpo antes dos danos");
    fs_statfs(&s);
    base_b = s.free_blocks;
    base_i = s.free_inodes;

    /* 1. bloco vazado: bitmap marcado, ninguem usa */
    fs_txn_begin();
    check(fs_balloc(&leaked) == FS_OK, "aloca um bloco sem dono");
    fs_txn_end(0);

    /* 2. inode orfao com um bloco: alocado, nenhum diretorio o referencia */
    fs_txn_begin();
    if (fs_ialloc(&ino) == FS_OK) {
        uint32_t phys;
        int fresh;

        memset(&n, 0, sizeof(n));
        n.type = FS_TYPE_FILE;
        n.mode = 0644;
        n.nlink = 1;
        n.size = 100;
        fs_bmap(&n, 0, 1, &phys, &fresh);
        fs_iput(ino, &n);
    }
    fs_txn_end(0);

    /* 3. nlink errado numa pasta */
    EXPECT(fs_mkdir(T "/nl"), FS_OK, "mkdir nl");
    fs_stat(T "/nl", &st);
    fs_txn_begin();
    fs_iget(st.ino, &n);
    n.nlink = 99;
    fs_iput(st.ino, &n);
    fs_txn_end(0);

    /* 4. entrada apontando para inode que virou "livre" */
    EXPECT(fs_create(T "/fantasma"), FS_OK, "cria fantasma");
    fs_stat(T "/fantasma", &st);
    fs_txn_begin();
    memset(&n, 0, sizeof(n));
    fs_iput(st.ino, &n);                          /* apaga o inode, mas deixa a entrada */
    fs_txn_end(0);

    check(fs_fsck(0, 0) >= 4, "fsck (so verificar) acha os danos");
    check(fs_fsck(1, 0) == 0, "fsck repair conserta tudo");
    check(fs_fsck(0, 0) == 0, "segundo fsck: limpo");
    EXPECT(fs_stat(T "/fantasma", &st), FS_ERR_NOENT, "entrada fantasma foi removida");
    check(fs_stat(T "/nl", &st) == FS_OK && st.nlink == 2, "nlink da pasta voltou a 2");
    EXPECT(fs_rmdir(T "/nl"), FS_OK, "limpa nl");

    fs_statfs(&s);
    check(s.free_blocks == base_b && s.free_inodes == base_i,
          "blocos e inodes livres voltaram ao valor de antes dos danos");
}

int fs_selftest(void)
{
    char saved_cwd[FS_PATH_MAX];
    fs_statfs_t before, after;
    fs_stat_t st;

    failures = 0;
    checks = 0;

    if (!fs_mounted()) {
        kprintf("OrionFS nao montado. Use: format\n");
        return -1;
    }

    kprintf("\n=== Autoteste do OrionFS ===\n");
    if (fs_getcwd(saved_cwd, sizeof(saved_cwd)) != FS_OK)
        saved_cwd[0] = '\0';
    fs_set_uid(0);
    fs_chdir("/");
    if (fs_stat(T, &st) == FS_OK)
        fs_rmtree(T);                              /* sobra de um teste interrompido */

    fs_statfs(&before);

    test_dirs();
    test_small_files();
    test_big_file();
    test_fd();
    test_rename();
    test_remove();
    test_copy();
    test_perms();
    test_full_disk();
    test_persistence();
    test_fsck();

    fs_set_uid(0);
    fs_chdir("/");
    EXPECT(fs_rmtree(T), FS_OK, "remove /.fstest inteira");
    fs_statfs(&after);
    check(after.free_blocks == before.free_blocks, "no fim: mesmos blocos livres do inicio");
    check(after.free_inodes == before.free_inodes, "no fim: mesmos inodes livres do inicio");
    check(fs_fsck(0, 0) == 0, "fsck final limpo");

    if (saved_cwd[0])
        fs_chdir(saved_cwd);

    if (failures)
        kprintf("=== %d de %d verificacoes do FS FALHARAM ===\n\n", failures, checks);
    else
        kprintf("=== TODAS as %d verificacoes do FS passaram ===\n\n", checks);
    return failures;
}
