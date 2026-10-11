
#include "fs.h"
#include "ata.h"
#include "terminal.h"

/*
 * OrionFS v2
 *   LBA 0        reservado
 *   LBA 1        superbloco ("ORIONFS2", versao 2)
 *   LBA 2..5     diretorio: 64 entradas de 32 bytes
 *   LBA 16...    dados: cada entrada i tem um slot fixo de 8 setores
 *                (LBA 16 + i*8), ou seja, ate 4096 bytes por arquivo
 *
 * Entrada (32 bytes): nome[16] | tamanho u32 LE | usado | uid | reservado
 */
#define FS_MAGIC        "ORIONFS2"
#define FS_VERSION      2
#define FS_SUPER_LBA    1
#define FS_DIR_LBA      2
#define FS_DIR_SECTORS  4
#define FS_DATA_START   16
#define FS_SLOT_SECTORS 8
#define FS_PER_SECTOR   16
#define FS_ENTRIES      (FS_DIR_SECTORS * FS_PER_SECTOR)
#define FS_ENTRY_SIZE   32
#define FS_NAME_SIZE    16
#define FS_MAX_CONTENT  (FS_SLOT_SECTORS * 512)

#define OFF_SIZE 16
#define OFF_USED 20
#define OFF_UID  21

static unsigned char sector[512];
static int mounted = 0;

static void zero_bytes(unsigned char *p, unsigned int n)
{
    for (unsigned int i = 0; i < n; i++)
        p[i] = 0;
}

static int same_name(const unsigned char *stored, const char *name)
{
    for (unsigned int i = 0; i < FS_NAME_SIZE; i++) {
        if (stored[i] != (unsigned char)name[i])
            return 0;
        if (name[i] == '\0')
            return 1;
    }

    return 0;
}

static int valid_name(const char *name)
{
    unsigned int i = 0;

    if (!name || !name[0])
        return 0;

    while (name[i]) {
        if (i >= FS_NAME_SIZE - 1)
            return 0;

        if (name[i] == '/' || name[i] == ' ')
            return 0;

        i++;
    }

    return 1;
}

static unsigned int get_size(const unsigned char *e)
{
    return (unsigned int)e[OFF_SIZE] |
           ((unsigned int)e[OFF_SIZE + 1] << 8) |
           ((unsigned int)e[OFF_SIZE + 2] << 16) |
           ((unsigned int)e[OFF_SIZE + 3] << 24);
}

static void set_size(unsigned char *e, unsigned int size)
{
    e[OFF_SIZE]     = size & 0xFF;
    e[OFF_SIZE + 1] = (size >> 8) & 0xFF;
    e[OFF_SIZE + 2] = (size >> 16) & 0xFF;
    e[OFF_SIZE + 3] = (size >> 24) & 0xFF;
}

/* Carrega o setor do diretorio que contem a entrada e devolve-a. */
static unsigned char *dir_load(int index)
{
    if (ata_read_sector(FS_DIR_LBA + index / FS_PER_SECTOR, sector) != 0)
        return 0;

    return &sector[(index % FS_PER_SECTOR) * FS_ENTRY_SIZE];
}

static int dir_store(int index)
{
    return ata_write_sector(FS_DIR_LBA + index / FS_PER_SECTOR, sector);
}

static int find_entry(const char *name)
{
    for (int s = 0; s < FS_DIR_SECTORS; s++) {
        if (ata_read_sector(FS_DIR_LBA + s, sector) != 0)
            return -1;

        for (int i = 0; i < FS_PER_SECTOR; i++) {
            unsigned char *e = &sector[i * FS_ENTRY_SIZE];

            if (e[OFF_USED] == 1 && same_name(e, name))
                return s * FS_PER_SECTOR + i;
        }
    }

    return -1;
}

static void print_number(unsigned int value)
{
    char digits[10];
    int count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < 10);

    while (count > 0)
        terminal_putchar(digits[--count]);
}

int fs_mount(void)
{
    unsigned char super[512];

    mounted = 0;

    if (ata_read_sector(FS_SUPER_LBA, super) != 0)
        return -1;

    for (int i = 0; i < 8; i++) {
        if (super[i] != (unsigned char)FS_MAGIC[i])
            return -1;
    }

    if (super[8] != FS_VERSION)
        return -1;

    mounted = 1;
    return 0;
}

int fs_format(void)
{
    unsigned char block[512];

    mounted = 0;
    zero_bytes(block, sizeof(block));

    for (int i = 0; i < 8; i++)
        block[i] = (unsigned char)FS_MAGIC[i];

    block[8] = FS_VERSION;

    if (ata_write_sector(FS_SUPER_LBA, block) != 0)
        return -1;

    zero_bytes(block, sizeof(block));

    for (int s = 0; s < FS_DIR_SECTORS; s++) {
        if (ata_write_sector(FS_DIR_LBA + s, block) != 0)
            return -1;
    }

    mounted = 1;
    return 0;
}

void fs_list(void)
{
    int found = 0;

    if (!mounted) {
        terminal_writestring("OrionFS nao montado. Use: format\n");
        return;
    }

    terminal_writestring("Arquivos:\n");

    for (int s = 0; s < FS_DIR_SECTORS; s++) {
        if (ata_read_sector(FS_DIR_LBA + s, sector) != 0) {
            terminal_writestring("Erro ao ler diretorio.\n");
            return;
        }

        for (int i = 0; i < FS_PER_SECTOR; i++) {
            unsigned char *e = &sector[i * FS_ENTRY_SIZE];

            if (e[OFF_USED] != 1)
                continue;

            terminal_writestring("  ");
            for (int j = 0; j < FS_NAME_SIZE && e[j]; j++)
                terminal_putchar((char)e[j]);

            terminal_writestring(" (");
            print_number(get_size(e));
            terminal_writestring(" bytes, uid ");
            print_number(e[OFF_UID]);
            terminal_writestring(")\n");
            found = 1;
        }
    }

    if (!found)
        terminal_writestring("  (vazio)\n");
}

int fs_create(const char *name, unsigned char uid)
{
    if (!mounted || !valid_name(name))
        return -1;

    if (find_entry(name) >= 0)
        return -1;

    for (int s = 0; s < FS_DIR_SECTORS; s++) {
        if (ata_read_sector(FS_DIR_LBA + s, sector) != 0)
            return -1;

        for (int i = 0; i < FS_PER_SECTOR; i++) {
            unsigned char *e = &sector[i * FS_ENTRY_SIZE];

            if (e[OFF_USED] == 1)
                continue;

            zero_bytes(e, FS_ENTRY_SIZE);

            for (int j = 0; name[j]; j++)
                e[j] = (unsigned char)name[j];

            e[OFF_USED] = 1;
            e[OFF_UID] = uid;

            return ata_write_sector(FS_DIR_LBA + s, sector);
        }
    }

    return -1;
}

int fs_write(const char *name, const char *content)
{
    unsigned char data[512];
    unsigned int size = 0;
    unsigned int done = 0;
    unsigned int lba;
    unsigned char *e;
    int entry;

    if (!mounted || !content)
        return -1;

    entry = find_entry(name);
    if (entry < 0)
        return -1;

    while (content[size] && size < FS_MAX_CONTENT)
        size++;

    if (content[size] != '\0')
        return -1;

    lba = FS_DATA_START + (unsigned int)entry * FS_SLOT_SECTORS;

    while (done < size) {
        unsigned int chunk = size - done;

        if (chunk > 512)
            chunk = 512;

        zero_bytes(data, sizeof(data));

        for (unsigned int i = 0; i < chunk; i++)
            data[i] = (unsigned char)content[done + i];

        if (ata_write_sector(lba++, data) != 0)
            return -1;

        done += chunk;
    }

    e = dir_load(entry);
    if (!e)
        return -1;

    set_size(e, size);
    return dir_store(entry);
}

int fs_read(const char *name)
{
    unsigned char data[512];
    unsigned int size;
    unsigned int lba;
    unsigned char *e;
    int entry;

    if (!mounted) {
        terminal_writestring("OrionFS nao montado. Use: format\n");
        return -1;
    }

    entry = find_entry(name);
    if (entry < 0) {
        terminal_writestring("Arquivo nao encontrado.\n");
        return -1;
    }

    e = dir_load(entry);
    if (!e)
        return -1;

    size = get_size(e);
    if (size > FS_MAX_CONTENT)
        return -1;

    lba = FS_DATA_START + (unsigned int)entry * FS_SLOT_SECTORS;

    while (size > 0) {
        unsigned int chunk = size > 512 ? 512 : size;

        if (ata_read_sector(lba++, data) != 0)
            return -1;

        for (unsigned int i = 0; i < chunk; i++)
            terminal_putchar((char)data[i]);

        size -= chunk;
    }

    terminal_putchar('\n');
    return 0;
}

int fs_remove(const char *name)
{
    unsigned char *e;
    int entry;

    if (!mounted)
        return -1;

    entry = find_entry(name);
    if (entry < 0)
        return -1;

    e = dir_load(entry);
    if (!e)
        return -1;

    zero_bytes(e, FS_ENTRY_SIZE);
    return dir_store(entry);
}
