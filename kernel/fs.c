
#include "fs.h"
#include "ata.h"
#include "terminal.h"

#define FS_MAGIC       "ORIONFS1"
#define FS_SUPER_LBA   1
#define FS_DIR_LBA     2
#define FS_DATA_START  16
#define FS_ENTRIES     16
#define FS_ENTRY_SIZE  32
#define FS_NAME_SIZE   16
#define FS_MAX_CONTENT 511

static unsigned char sector[512];
static int mounted = 0;

static void zero_bytes(unsigned char *p, unsigned int n)
{
    for (unsigned int i = 0; i < n; i++)
        p[i] = 0;
}

static int same_name(const unsigned char *stored, const char *name)
{
    unsigned int i = 0;

    while (i < FS_NAME_SIZE && name[i] && stored[i] == (unsigned char)name[i])
        i++;

    if (i == FS_NAME_SIZE)
        return name[i] == '\0';

    return stored[i] == 0 && name[i] == '\0';
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

static int read_directory(void)
{
    return ata_read_sector(FS_DIR_LBA, sector);
}

static int find_entry(const char *name)
{
    if (read_directory() != 0)
        return -1;

    for (int i = 0; i < FS_ENTRIES; i++) {
        unsigned int offset = i * FS_ENTRY_SIZE;

        if (sector[offset + 20] == 1 &&
            same_name(&sector[offset], name))
            return i;
    }

    return -1;
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

    if (super[8] != 1)
        return -1;

    mounted = 1;
    return 0;
}

int fs_format(void)
{
    unsigned char super[512];
    unsigned char directory[512];

    zero_bytes(super, sizeof(super));
    zero_bytes(directory, sizeof(directory));

    for (int i = 0; i < 8; i++)
        super[i] = (unsigned char)FS_MAGIC[i];

    super[8] = 1;

    if (ata_write_sector(FS_SUPER_LBA, super) != 0)
        return -1;

    if (ata_write_sector(FS_DIR_LBA, directory) != 0) {
        mounted = 0;
        return -1;
    }

    mounted = 1;
    return 0;
}

void fs_list(void)
{
    if (!mounted) {
        terminal_writestring("OrionFS nao montado. Use: format\n");
        return;
    }

    if (read_directory() != 0) {
        terminal_writestring("Erro ao ler diretorio.\n");
        return;
    }

    terminal_writestring("Arquivos:\n");

    int found = 0;

    for (int i = 0; i < FS_ENTRIES; i++) {
        unsigned int offset = i * FS_ENTRY_SIZE;

        if (sector[offset + 20] != 1)
            continue;

        terminal_writestring("  ");
        for (int j = 0; j < FS_NAME_SIZE && sector[offset + j]; j++)
            terminal_putchar((char)sector[offset + j]);

        terminal_writestring(" (");
        unsigned int size =
            (unsigned int)sector[offset + 16] |
            ((unsigned int)sector[offset + 17] << 8);

        char digits[10];
        int count = 0;

        do {
            digits[count++] = (char)('0' + size % 10);
            size /= 10;
        } while (size && count < 10);

        while (count > 0)
            terminal_putchar(digits[--count]);

        terminal_writestring(" bytes)\n");
        found = 1;
    }

    if (!found)
        terminal_writestring("  (vazio)\n");
}

int fs_create(const char *name)
{
    if (!mounted || !valid_name(name))
        return -1;

    if (find_entry(name) >= 0)
        return -1;

    if (read_directory() != 0)
        return -1;

    for (int i = 0; i < FS_ENTRIES; i++) {
        unsigned int offset = i * FS_ENTRY_SIZE;

        if (sector[offset + 20] != 1) {
            for (int j = 0; j < FS_ENTRY_SIZE; j++)
                sector[offset + j] = 0;

            for (int j = 0; name[j]; j++)
                sector[offset + j] = (unsigned char)name[j];

            sector[offset + 20] = 1;

            return ata_write_sector(FS_DIR_LBA, sector);
        }
    }

    return -1;
}

int fs_write(const char *name, const char *content)
{
    unsigned char data[512];
    unsigned int size = 0;
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

    zero_bytes(data, sizeof(data));

    for (unsigned int i = 0; i < size; i++)
        data[i] = (unsigned char)content[i];

    if (ata_write_sector(FS_DATA_START + (unsigned int)entry, data) != 0)
        return -1;

    if (read_directory() != 0)
        return -1;

    unsigned int offset = (unsigned int)entry * FS_ENTRY_SIZE;
    sector[offset + 16] = size & 0xFF;
    sector[offset + 17] = (size >> 8) & 0xFF;
    sector[offset + 18] = 0;
    sector[offset + 19] = 0;

    return ata_write_sector(FS_DIR_LBA, sector);
}

int fs_read(const char *name)
{
    unsigned char data[512];
    unsigned int size;
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

    if (read_directory() != 0)
        return -1;

    unsigned int offset = (unsigned int)entry * FS_ENTRY_SIZE;

    size = (unsigned int)sector[offset + 16] |
           ((unsigned int)sector[offset + 17] << 8);

    if (size > FS_MAX_CONTENT)
        return -1;

    if (ata_read_sector(FS_DATA_START + (unsigned int)entry, data) != 0)
        return -1;

    for (unsigned int i = 0; i < size; i++)
        terminal_putchar((char)data[i]);

    terminal_putchar('\n');
    return 0;
}

int fs_remove(const char *name)
{
    int entry;

    if (!mounted)
        return -1;

    entry = find_entry(name);
    if (entry < 0)
        return -1;

    if (read_directory() != 0)
        return -1;

    unsigned int offset = (unsigned int)entry * FS_ENTRY_SIZE;

    for (int i = 0; i < FS_ENTRY_SIZE; i++)
        sector[offset + i] = 0;

    return ata_write_sector(FS_DIR_LBA, sector);
}
