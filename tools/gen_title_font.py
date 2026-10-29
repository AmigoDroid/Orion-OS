from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parent.parent
source = root / "font/after_burner_neue/After Burner Neue.ttf"
output = root / "gfx/orion_title_font_data.h"

font = ImageFont.truetype(str(source), 16)
glyphs = []

for code in range(32, 256):
    ch = chr(code)
    image = Image.new("L", (16, 16), 0)
    draw = ImageDraw.Draw(image)

    bbox = draw.textbbox((0, 0), ch, font=font)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]

    x = (16 - width) // 2 - bbox[0]
    y = (16 - height) // 2 - bbox[1]
    draw.text((x, y), ch, font=font, fill=255)

    # Ajusta a fonte para a célula de 8x16 pixels do Orion.
    image = image.resize((8, 16), Image.Resampling.LANCZOS)

    rows = []
    for y in range(16):
        bits = 0
        for x in range(8):
            if image.getpixel((x, y)) >= 96:
                bits |= 0x80 >> x
        rows.append(bits)

    glyphs.append(rows)

with output.open("w", encoding="utf-8") as f:
    f.write("/* Gerado automaticamente por tools/gen_title_font.py */\n")
    f.write("#ifndef ORION_TITLE_FONT_DATA_H\n")
    f.write("#define ORION_TITLE_FONT_DATA_H\n\n")
    f.write("static const unsigned char orion_system_glyphs[224][16] = {\n")

    for rows in glyphs:
        f.write("    { " + ", ".join(f"0x{b:02X}" for b in rows) + " },\n")

    f.write("};\n\n#endif\n")

print(f"Fonte do sistema gerada: {output}")
print("Caracteres gerados: 224 (códigos 32 a 255)")
print("Tamanho de cada caractere: 8x16 pixels")
