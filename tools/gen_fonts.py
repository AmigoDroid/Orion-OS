#!/usr/bin/env python3
from pathlib import Path
import json
import sys

if len(sys.argv) != 3:
    print("Uso: gen_fonts.py DIRETORIO_FONTES ARQUIVO_SAIDA")
    sys.exit(1)

font_dir = Path(sys.argv[1])
output = Path(sys.argv[2])

if not font_dir.is_dir():
    print(f"ERRO: pasta nao encontrada: {font_dir}")
    sys.exit(1)

fonts = sorted(
    p for p in font_dir.rglob("*")
    if p.is_file() and p.suffix.lower() == ".ttf"
)

def c_string(value):
    return json.dumps(value, ensure_ascii=True)

lines = [
    "/* Arquivo gerado automaticamente por tools/gen_fonts.py. */",
    "#ifndef ORION_FONTS_DATA_H",
    "#define ORION_FONTS_DATA_H",
    "#include <stdint.h>",
    "",
    "typedef struct {",
    "    const char *name;",
    "    const char *path;",
    "    const uint8_t *data;",
    "    uint32_t size;",
    "} orion_font_asset_t;",
    "",
]

for index, path in enumerate(fonts):
    relative = path.relative_to(font_dir.parent).as_posix()
    lines.append(f"static const uint8_t orion_font_data_{index}[] = {{")
    data = path.read_bytes()

    for start in range(0, len(data), 12):
        chunk = data[start:start + 12]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

    lines.extend([
        "};",
        "",
    ])

lines.append("static const orion_font_asset_t orion_font_assets[] = {")

for index, path in enumerate(fonts):
    relative = path.relative_to(font_dir.parent).as_posix()
    lines.append(
        "    {"
        + c_string(path.stem) + ", "
        + c_string(relative) + ", "
        + f"orion_font_data_{index}, sizeof(orion_font_data_{index})"
        + "},"
    )

lines.extend([
    "};",
    f"#define ORION_FONT_ASSET_COUNT {len(fonts)}u",
    "",
    "#endif",
    "",
])

content = "\n".join(lines)
output.parent.mkdir(parents=True, exist_ok=True)

# Evita recompilar se o conteudo nao mudou.
if not output.exists() or output.read_text() != content:
    output.write_text(content)

print(f"Fontes encontradas: {len(fonts)}")
for path in fonts:
    print(f"  - {path}")
print(f"Catalogo gerado: {output}")
