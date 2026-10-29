from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "gfx/orion_system_fonts_data.h"

FONTS = [
    ("vcr", "VCR", ROOT / "font/vcr_fonte/vcr.ttf"),
    ("afterburner", "After Burner Neue",
     ROOT / "font/after_burner_neue/After Burner Neue.ttf"),
    ("shadow", "After Burner Neue Shadow",
     ROOT / "font/after_burner_neue/After Burner Neue Shadow.ttf"),
    ("minecraft", "Minecraft", ROOT / "font/minecraft/Minecraft.ttf"),
]

FIRST_CODE = 32
LAST_CODE = 255
WIDTH = 8
HEIGHT = 16
SCALE = 4


def make_glyphs(source):
    font = ImageFont.truetype(str(source), HEIGHT * SCALE)
    glyphs = []

    for code in range(FIRST_CODE, LAST_CODE + 1):
        ch = chr(code)
        image = Image.new("L", (WIDTH * SCALE, HEIGHT * SCALE), 0)
        draw = ImageDraw.Draw(image)

        bbox = draw.textbbox((0, 0), ch, font=font)
        left, top, right, bottom = bbox
        glyph_w = right - left
        glyph_h = bottom - top

        if glyph_w > 0 and glyph_h > 0:
            # Posiciona o desenho na imagem de alta resolucao.
            x = (image.width - glyph_w) // 2 - left
            y = (image.height - glyph_h) // 2 - top
            draw.text((x, y), ch, font=font, fill=255)

        # Reduz mantendo a proporcao, sem comprimir horizontalmente.
        image.thumbnail((WIDTH, HEIGHT), Image.Resampling.LANCZOS)

        cell = Image.new("L", (WIDTH, HEIGHT), 0)
        cell.paste(image, ((WIDTH - image.width) // 2,
                           (HEIGHT - image.height) // 2))

        rows = []
        for y in range(HEIGHT):
            bits = 0
            for x in range(WIDTH):
                if cell.getpixel((x, y)) >= 96:
                    bits |= 0x80 >> x
            rows.append(bits)

        glyphs.append(rows)

    return glyphs


for key, name, source in FONTS:
    if not source.is_file():
        raise SystemExit(f"Fonte nao encontrada: {source}")

with OUTPUT.open("w", encoding="utf-8") as f:
    count = len(FONTS)

    f.write("/* Gerado por tools/gen_system_fonts.py; nao editar. */\n")
    f.write("#ifndef ORION_SYSTEM_FONTS_DATA_H\n")
    f.write("#define ORION_SYSTEM_FONTS_DATA_H\n\n")
    f.write(f"#define ORION_SYSTEM_FONT_COUNT {count}u\n")
    f.write("#define ORION_SYSTEM_GLYPH_COUNT 224u\n")
    f.write("#define ORION_SYSTEM_FONT_FIRST_CODE 32u\n\n")

    f.write(f"static const char *const orion_system_font_names[{count}] = {{\n")
    for key, name, source in FONTS:
        f.write(f'    "{name}",\n')
    f.write("};\n\n")

    f.write(f"static const char *const orion_system_font_keys[{count}] = {{\n")
    for key, name, source in FONTS:
        f.write(f'    "{key}",\n')
    f.write("};\n\n")

    f.write("static const unsigned char ")
    f.write(f"orion_system_font_glyphs[{count}][224][16] = {{\n")

    for key, name, source in FONTS:
        print(f"Gerando: {name}")
        glyphs = make_glyphs(source)
        f.write("  {\n")
        for rows in glyphs:
            f.write("    { " + ", ".join(
                f"0x{b:02X}" for b in rows
            ) + " },\n")
        f.write("  },\n")

    f.write("};\n\n#endif\n")

print(f"\nArquivo gerado: {OUTPUT}")
print(f"Fontes: {len(FONTS)} | Caracteres por fonte: 224 | Glifo: 8x16")
