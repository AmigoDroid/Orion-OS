#ifndef ORION_ATA_H
#define ORION_ATA_H

/* Driver ATA PIO (LBA28), disco primario master (portas 0x1F0/0x3F6).
 * ata_initialize() pode ser chamado mais de uma vez. */

int ata_initialize(void);                   /* 0 = disco encontrado e identificado */
int ata_present(void);

unsigned int ata_sector_count(void);        /* setores de 512 bytes (limitado a LBA28) */
const char  *ata_model(void);               /* texto do modelo, ex.: "QEMU HARDDISK" */

int ata_read_sector(unsigned int lba, unsigned char *buffer);
int ata_write_sector(unsigned int lba, const unsigned char *buffer);

/* FLUSH CACHE: so devolve quando o disco gravou tudo de verdade. */
int ata_flush(void);

#endif
