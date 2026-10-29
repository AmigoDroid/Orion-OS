#include "orion_fonts.h"
#include "orion_fonts_data.h"

uint32_t orion_fonts_count(void)
{
    return ORION_FONT_ASSET_COUNT;
}

int orion_fonts_get(uint32_t index, orion_font_info_t *out)
{
    if (out == 0 || index >= ORION_FONT_ASSET_COUNT)
        return -1;

    out->name = orion_font_assets[index].name;
    out->path = orion_font_assets[index].path;
    out->size = orion_font_assets[index].size;

    return 0;
}

const uint8_t *orion_fonts_data(uint32_t index, uint32_t *size_out)
{
    if (size_out != 0)
        *size_out = 0;

    if (index >= ORION_FONT_ASSET_COUNT)
        return 0;

    if (size_out != 0)
        *size_out = orion_font_assets[index].size;

    return orion_font_assets[index].data;
}
