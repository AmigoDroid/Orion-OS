#ifndef ORION_FONT_H
#define ORION_FONT_H

#include <stdint.h>

#define ORION_FONT_WIDTH  8
#define ORION_FONT_HEIGHT 16

/*
 * Retorna o glifo Unicode usando a Orion Font.
 * Cada linha do glifo representa 8 pixels.
 */
const uint8_t *orion_font_glyph(uint32_t codepoint);

#endif
