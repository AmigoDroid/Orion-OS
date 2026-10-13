#!/usr/bin/env python3

import sys
from pathlib import Path


def c_string(value):
    """Escapa o texto para uma string literal C."""
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )


def main():
    if len(sys.argv) != 3:
        print("Uso: gen_locale.py entrada.lang saida.h", file=sys.stderr)
        return 2

    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    entries = []
    seen = set()

    try:
        lines = source.read_text(encoding="utf-8").splitlines()

        for number, line in enumerate(lines, 1):
            line = line.strip()

            if not line or line.startswith("#"):
                continue

            if "=" not in line:
                raise ValueError(
                    f"{source}:{number}: esperado identificador=texto"
                )

            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()

            if not key:
                raise ValueError(f"{source}:{number}: identificador vazio")

            if key in seen:
                raise ValueError(
                    f"{source}:{number}: identificador duplicado: {key}"
                )

            seen.add(key)
            entries.append((key, value))

        if not entries:
            raise ValueError(f"{source}: catálogo vazio")

        output = [
            "/* Gerado automaticamente. Não editar manualmente. */",
            "#ifndef ORION_LENG_CATALOG_GENERATED_H",
            "#define ORION_LENG_CATALOG_GENERATED_H",
            "",
            "static const orionLeng_translation_t "
            "orionLeng_pt_BR_catalog[] = {",
        ]

        for key, value in entries:
            output.append(
                f'    {{"{c_string(key)}", "{c_string(value)}"}},'
            )

        output.extend([
            "};",
            "",
            "#define ORION_LENG_PT_BR_CATALOG_COUNT \\",
            "    (sizeof(orionLeng_pt_BR_catalog) / "
            "sizeof(orionLeng_pt_BR_catalog[0]))",
            "",
            "#endif",
            "",
        ])

        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text("\n".join(output), encoding="utf-8")
        print(f"Catálogo gerado: {destination} ({len(entries)} textos)")
        return 0

    except (OSError, UnicodeError, ValueError) as error:
        print(f"Erro ao gerar catálogo: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
