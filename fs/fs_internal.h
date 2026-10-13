#ifndef ORION_FS_INTERNAL_H
#define ORION_FS_INTERNAL_H

/* Detalhes do formato em disco. Usado por fs.c, fsck.c, bcache.c e fstest.c. */

#include <stdint.h>
#include "fs.h"

#define BLOCK_SIZE        512u
#define INODE_SIZE        64u
#define INODES_PER_BLOCK  8u
#define DIRENT_SIZE       32u
#define DIRENTS_PER_BLOCK 16u
#define N_DIRECT          9u
#define PTRS_PER_BLOCK    128u
#define BITS_PER_BLOCK    4096u
#define MAX_FILE_BLOCKS   (N_DIRECT + PTRS_PER_BLOCK + PTRS_PER_BLOCK * PTRS_PER_BLOCK)
#define MAX_FILE_BYTES    (MAX_FILE_BLOCKS * BLOCK_SIZE)

#define ROOT_INO   1u
#define T_FREE     0
#define T_FILE     FS_TYPE_FILE
#define T_DIR      FS_TYPE_DIR

#define FS_MAGIC   "ORIONFS3"
#define FS_VERSION 3u
#define SUPER_LBA  1u

#define MODE_FILE_DEFAULT 0644u
#define MODE_DIR_DEFAULT  0755u

typedef struct {
    uint8_t  type;
    uint8_t  uid;
    uint16_t mode;
    uint32_t size;
    uint32_t ctime, mtime;
    uint16_t nlink, reserved;
    uint32_t direct[N_DIRECT];
    uint32_t indirect;
    uint32_t dindirect;
} inode_t;

typedef struct {
    uint32_t ino;                  /* 0 = slot livre */
    char     name[28];             /* terminado em '\0' */
} dirent_t;

typedef struct {
    char     magic[8];
    uint32_t version, block_size, total_blocks, inode_count;
    uint32_t ibitmap_start, ibitmap_blocks;
    uint32_t bbitmap_start, bbitmap_blocks;
    uint32_t itable_start, itable_blocks;
    uint32_t data_start, root_ino;
    uint32_t free_blocks, free_inodes;
    uint32_t state;                /* 1 = limpo, 0 = sujo (operacao em andamento) */
    uint32_t mount_count, create_time;
    uint8_t  pad[432];
    uint32_t crc;                  /* CRC-32 dos 508 primeiros bytes */
} sb_t;

_Static_assert(sizeof(inode_t) == INODE_SIZE, "inode_t deve ter 64 bytes");
_Static_assert(sizeof(dirent_t) == DIRENT_SIZE, "dirent_t deve ter 32 bytes");
_Static_assert(sizeof(sb_t) == BLOCK_SIZE, "sb_t deve ter 512 bytes");

/* estado global do volume (fs.c) */
extern sb_t fs_sb;
extern int  fs_is_mounted;

/* cache de blocos (bcache.c) - escrita direta, entao o disco nunca fica atras */
int  bc_init(uint32_t disk_sectors);
void bc_exit(void);
void bc_invalidate(void);
int  bc_read(uint32_t lba, void *out512);
int  bc_write(uint32_t lba, const void *in512);
void bc_stats(uint32_t *hits, uint32_t *misses);

/* primitivas (fs.c), usadas tambem pelo fsck e pelo autoteste */
int  fs_iget(uint32_t ino, inode_t *out);
int  fs_iput(uint32_t ino, const inode_t *in);
int  fs_balloc(uint32_t *out);
int  fs_bfree(uint32_t blk);
int  fs_ialloc(uint32_t *out);
int  fs_ifree(uint32_t ino);
int  fs_bitmap_get(uint32_t start, uint32_t bit, int *value);
int  fs_bitmap_put(uint32_t start, uint32_t bit, int value);
int  fs_bmap(inode_t *in, uint32_t fileblock, int alloc, uint32_t *phys, int *fresh);
int  fs_super_write(void);
int  fs_recount_free(void);
int  fs_txn_begin(void);              /* marca o disco "sujo" */
int  fs_txn_end(int result);         /* grava contadores, marca "limpo", flush */

#endif
