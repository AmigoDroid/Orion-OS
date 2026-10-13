#ifndef ORION_FS_H
#define ORION_FS_H

/* OrionFS v3 - sistema de arquivos hierarquico do Orion OS.
 * Formato e funcionamento: docs/ORIONFS.md.
 *
 * Caminhos: absolutos ("/docs/a.txt") ou relativos ao diretorio atual
 * ("a.txt", "../b"), com "." e "..". Nomes: 1 a 27 bytes, sem '/'.
 * Toda funcao devolve >= 0 em sucesso ou um FS_ERR_* (< 0). */

#include <stdint.h>

#define FS_NAME_MAX  27
#define FS_PATH_MAX  256
#define FS_MAX_DEPTH 40          /* limite de recursao (rmtree, copy) */

enum {
    FS_OK              =  0,
    FS_ERR_IO          = -1,     /* falha de leitura/escrita no disco       */
    FS_ERR_NOTMOUNTED  = -2,
    FS_ERR_NOENT       = -3,     /* nao existe                              */
    FS_ERR_EXIST       = -4,     /* ja existe                               */
    FS_ERR_NOTDIR      = -5,
    FS_ERR_ISDIR       = -6,
    FS_ERR_NOTEMPTY    = -7,
    FS_ERR_NOSPC       = -8,     /* sem blocos ou sem inodes                */
    FS_ERR_INVAL       = -9,
    FS_ERR_NAMETOOLONG = -10,
    FS_ERR_TOOBIG      = -11,    /* maior que o limite de um arquivo        */
    FS_ERR_NOMEM       = -12,
    FS_ERR_CORRUPT     = -13,    /* estruturas inconsistentes: rode fsck    */
    FS_ERR_BUSY        = -14,    /* diretorio atual / em uso                */
    FS_ERR_PERM        = -15,
    FS_ERR_NOTFS       = -16,    /* o disco nao tem OrionFS v3              */
    FS_ERR_NODISK      = -17,
    FS_ERR_BADFD       = -18,
    FS_ERR_MFILE       = -19     /* descritores esgotados                   */
};

#define FS_TYPE_FILE 1
#define FS_TYPE_DIR  2

typedef struct {
    uint32_t ino;
    uint8_t  type;               /* FS_TYPE_* */
    uint8_t  uid;
    uint16_t mode;               /* rwx do dono (0700) e dos outros (0007) */
    uint32_t size;               /* bytes */
    uint32_t blocks;             /* blocos de 512 B realmente alocados */
    uint32_t ctime, mtime;       /* segundos desde 1970 (UTC); 0 = desconhecido */
    uint16_t nlink;
} fs_stat_t;

typedef struct {
    char     name[FS_NAME_MAX + 1];
    uint32_t ino;
    uint8_t  type;
    uint32_t size;
} fs_dirent_t;

typedef struct {
    uint32_t block_size;
    uint32_t total_blocks, free_blocks;
    uint32_t total_inodes, free_inodes;
    uint32_t cache_hits, cache_misses;
    int      was_dirty;          /* o ultimo mount encontrou o disco "sujo" */
} fs_statfs_t;

#define FS_O_READ    0x01
#define FS_O_WRITE   0x02
#define FS_O_CREATE  0x04
#define FS_O_TRUNC   0x08
#define FS_O_APPEND  0x10

#define FS_SEEK_SET 0
#define FS_SEEK_CUR 1
#define FS_SEEK_END 2

/* ---- volume ---- */
int  fs_mount(void);                 /* FS_ERR_NOTFS se o disco nao foi formatado */
int  fs_format(void);                /* APAGA tudo e cria um volume novo */
int  fs_sync(void);                  /* garante tudo no disco (flush) */
int  fs_mounted(void);
int  fs_statfs(fs_statfs_t *out);
int  fs_fsck(int repair, int verbose);   /* devolve problemas NAO resolvidos (<0 = erro) */
const char *fs_strerror(int err);

/* ---- identidade (o kernel ainda nao tem login: padrao uid 0 = root) ---- */
void fs_set_uid(uint8_t uid);

/* ---- diretorio atual ---- */
int  fs_chdir(const char *path);
int  fs_getcwd(char *buf, uint32_t size);

/* ---- diretorios ---- */
int  fs_mkdir(const char *path);
int  fs_rmdir(const char *path);                 /* so se vazio */
int  fs_rmtree(const char *path);                /* remove tudo dentro, recursivo */
/* Itera: *cursor comeca em 0; devolve 1 e preenche `out`, 0 no fim, <0 erro. Pula "." e "..". */
int  fs_readdir(const char *path, uint32_t *cursor, fs_dirent_t *out);

/* ---- arquivos por caminho ---- */
int  fs_create(const char *path);                /* arquivo vazio; FS_ERR_EXIST se ja existe */
int  fs_unlink(const char *path);                /* remove arquivo */
int  fs_rename(const char *from, const char *to);/* arquivo ou diretorio */
int  fs_copy(const char *from, const char *to, int recursive);
int  fs_truncate(const char *path, uint32_t size);
int  fs_stat(const char *path, fs_stat_t *out);
int  fs_chmod(const char *path, uint16_t mode);
int  fs_chown(const char *path, uint8_t uid);    /* so root */

/* ---- descritores ---- */
int  fs_open(const char *path, int flags);       /* fd >= 0 */
int  fs_read(int fd, void *buf, uint32_t len);   /* bytes lidos (0 = fim) */
int  fs_write(int fd, const void *buf, uint32_t len);
int  fs_seek(int fd, int32_t offset, int whence);/* nova posicao */
int  fs_close(int fd);

/* ---- atalhos ---- */
int  fs_read_file(const char *path, void *buf, uint32_t cap, uint32_t *out_len);
int  fs_write_file(const char *path, const void *data, uint32_t len, int append);

/* ---- autoteste (comando `fstest`): devolve o numero de falhas ---- */
int  fs_selftest(void);

#endif
