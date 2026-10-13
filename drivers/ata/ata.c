
#include "ata.h"

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_CONTROL    0x3F6

#define ATA_BSY  0x80
#define ATA_DRQ  0x08
#define ATA_ERR  0x01
#define ATA_DF   0x20

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_TIMEOUT 1000000

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline unsigned short inw(unsigned short port)
{
    unsigned short value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(unsigned short port, unsigned short value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void ata_delay(void)
{
    for (int i = 0; i < 4; i++) {
        (void)inb(ATA_CONTROL);
    }
}

/* Retorna 0 quando o dispositivo está pronto para transferir dados. */
static int ata_wait(int require_drq)
{
    for (unsigned int i = 0; i < ATA_TIMEOUT; i++) {
        unsigned char status = inb(ATA_STATUS);

        if (status == 0 || status == 0xFF)
            return -1;

        if (status & (ATA_ERR | ATA_DF))
            return -1;

        if (!(status & ATA_BSY)) {
            if (!require_drq || (status & ATA_DRQ))
                return 0;
        }
    }

    return -1;
}

static int present;
static unsigned int total_sectors;
static char model[41];

int ata_present(void)            { return present; }
unsigned int ata_sector_count(void) { return total_sectors; }
const char *ata_model(void)      { return model; }

int ata_initialize(void)
{
    unsigned short id[256];
    unsigned char status;

    present = 0;
    total_sectors = 0;
    model[0] = '\0';

    outb(ATA_CONTROL, 0x02);          /* nIEN: sem interrupcoes, so polling */
    outb(ATA_DRIVE, 0xA0);            /* disco primario master */
    ata_delay();

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    status = inb(ATA_STATUS);
    if (status == 0 || status == 0xFF)
        return -1;                    /* nao ha disco */

    for (unsigned int i = 0; i < ATA_TIMEOUT; i++) {
        status = inb(ATA_STATUS);

        if (status == 0xFF || (status & ATA_ERR))
            return -1;                /* ATAPI/SATA: nao e um disco ATA simples */

        if (!(status & ATA_BSY) && (status & ATA_DRQ)) {
            for (int j = 0; j < 256; j++)
                id[j] = inw(ATA_DATA);

            /* palavras 27-46: modelo, 2 caracteres por palavra, invertidos */
            for (int j = 0; j < 20; j++) {
                model[j * 2]     = (char)(id[27 + j] >> 8);
                model[j * 2 + 1] = (char)(id[27 + j] & 0xFF);
            }
            model[40] = '\0';
            for (int j = 39; j >= 0 && model[j] == ' '; j--)
                model[j] = '\0';

            /* palavras 60-61: total de setores LBA28 */
            total_sectors = ((unsigned int)id[61] << 16) | id[60];
            if (total_sectors > 0x0FFFFFFFu)
                total_sectors = 0x0FFFFFFFu;

            present = total_sectors != 0;
            return present ? 0 : -1;
        }
    }

    return -1;
}

int ata_flush(void)
{
    if (!present)
        return -1;

    outb(ATA_DRIVE, 0xE0);
    ata_delay();
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    return ata_wait(0);
}

int ata_read_sector(unsigned int lba, unsigned char *buffer)
{
    if (buffer == 0 || lba >= 0x10000000U || (present && lba >= total_sectors))
        return -1;

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay();

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, lba & 0xFF);
    outb(ATA_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_LBA2, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_READ);

    if (ata_wait(1) != 0)
        return -1;

    for (int i = 0; i < 256; i++) {
        unsigned short word = inw(ATA_DATA);
        buffer[i * 2] = word & 0xFF;
        buffer[i * 2 + 1] = (word >> 8) & 0xFF;
    }

    ata_delay();
    return 0;
}

int ata_write_sector(unsigned int lba, const unsigned char *buffer)
{
    if (buffer == 0 || lba >= 0x10000000U || (present && lba >= total_sectors))
        return -1;

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay();

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, lba & 0xFF);
    outb(ATA_LBA1, (lba >> 8) & 0xFF);
    outb(ATA_LBA2, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    if (ata_wait(1) != 0)
        return -1;

    for (int i = 0; i < 256; i++) {
        unsigned short word =
            (unsigned short)buffer[i * 2] |
            ((unsigned short)buffer[i * 2 + 1] << 8);

        outw(ATA_DATA, word);
    }

    ata_delay();

    /* Aguarda o disco concluir a gravação. */
    if (ata_wait(0) != 0)
        return -1;

    return 0;
}
