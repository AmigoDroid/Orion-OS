
#ifndef ORION_ATA_H
#define ORION_ATA_H

int ata_initialize(void);

int ata_read_sector(unsigned int lba, unsigned char *buffer);
int ata_write_sector(unsigned int lba, const unsigned char *buffer);

#endif
