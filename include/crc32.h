#ifndef ORION_CRC32_H
#define ORION_CRC32_H

#include <stdint.h>
#include <stddef.h>

/* CRC-32 (IEEE 802.3, polinomio 0xEDB88320). Usado no superbloco do OrionFS. */
uint32_t crc32(const void *data, size_t len);

#endif
