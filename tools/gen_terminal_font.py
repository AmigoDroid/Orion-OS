#!/usr/bin/env python3
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "gfx/orion_terminal_fonts_data.h"

FONTS = [
    ("vcr", "VCR", ROOT / "font/vcr_fonte/vcr.ttf"),
    ("afterburner", "After Burner Neue",
     ROOT / "font/after_burner_neue/After Burner Neue.ttf"),
    ("shadow", "After Burner Neue Shadow",
     ROOT / "font/after_burner_neue/After Burner Neue Shadow.ttf"),
    ("minecraft", "Minecraft", ROOT / "font/minecraft/Minecraft.ttf"),
]

WIDTH = 16
HEIGHT = 24
FIRST_CODE = 32
LAST_CODE = 255
SCALE = 4


def make_glyphs(source):
    font = ImageFont.truetype(str(source), HEIGHT * SCALE)
    glyphs = []

    for code in range(FIRST_CODE, LAST_CODE + 1):
        # O terminal recebe bytes CP437; convertemos cada byte
        # para o caractere Unicode correspondente antes de rasterizar.
        ch = bytes([code]).decode("cp437")
        image = Image.new("L", (WIDTH * SCALE, HEIGHT * SCALE), 0)
        draw = ImageDraw.Draw(image)

        bbox = draw.textbbox((0, 0), ch, font=font)
        left, top, right, bottom = bbox

        if right > left and bottom > top:
            x = (image.width - (right - left)) // 2 - left
            y = (image.height - (bottom - top)) // 2 - top
            draw.text((x, y), ch, font=font, fill=255)

        image.thumbnail((WIDTH, HEIGHT), Image.Resampling.LANCZOS)

        cell = Image.new("L", (WIDTH, HEIGHT), 0)
        cell.paste(
            image,
            ((WIDTH - image.width) // 2,
             (HEIGHT - image.height) // 2)
        )

        rows = []
        for y in range(HEIGHT):
            bits = 0
            for x in range(WIDTH):
                if cell.getpixel((x, y)) >= 96:
                    bits |= 1 << (15 - x)
            rows.append(bits)

        glyphs.append(rows)

    return glyphs


for _, name, source in FONTS:
    if not source.is_file():
        raise SystemExit(f"Fonte não encontrada: {source}")

with OUTPUT.open("w", encoding="utf-8") as f:
    f.write("/* Gerado por tools/gen_terminal_font.py; não editar. */\n")
    f.write("#ifndef ORION_TERMINAL_FONTS_DATA_H\n")
    f.write("#define ORION_TERMINAL_FONTS_DATA_H\n\n")
    f.write("#define ORION_TERMINAL_FONT_WIDTH 16u\n")
    f.write("#define ORION_TERMINAL_FONT_HEIGHT 24u\n")
    f.write("#define ORION_TERMINAL_FONT_FIRST_CODE 32u\n")
    f.write("#define ORION_TERMINAL_FONT_COUNT 4u\n")
    f.write("#define ORION_TERMINAL_GLYPH_COUNT 224u\n\n")

    f.write("static const unsigned short ")
    f.write("orion_terminal_font_glyphs[4][224][24] = {\n")

    for key, name, source in FONTS:
        print(f"Gerando fonte do terminal: {name}")
        f.write("  {\n")
        for rows in make_glyphs(source):
            f.write("    { " + ", ".join(
                f"0x{b:04X}" for b in rows
            ) + " },\n")
        f.write("  },\n")

    f.write("};\n\n#endif\n")

print(f"Arquivo gerado: {OUTPUT}")
print("Resolução: 16x24 | Fontes: 4 | Caracteres por fonte: 224")
