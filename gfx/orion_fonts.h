#ifndef ORION_FONTS_H
#define ORION_FONTS_H

#include <stdint.h>

typedef struct {
    const char *name;
    const char *path;
    uint32_t size;
} orion_font_info_t;

/* Quantidade de fontes descobertas na compilacao. */
uint32_t orion_fonts_count(void);

/* Consulta os dados de uma fonte pelo indice. Retorna 0 se funcionar. */
int orion_fonts_get(uint32_t index, orion_font_info_t *out);

/* Acesso aos bytes originais da fonte TTF incorporada. */
const uint8_t *orion_fonts_data(uint32_t index, uint32_t *size_out);

#endif
