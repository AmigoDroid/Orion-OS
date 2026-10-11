# OrionGFD

Interface grafica do Orion OS. Roda dentro do kernel, sobre o framebuffer linear
de 32 bpp que o GRUB entrega via Multiboot (800x600 por padrao).

## Recursos (0.1.0)

- Framebuffer com fonte bitmap 8x16 (ASCII, `include/gfd_font.h`).
- Janelas com barra de titulo, botao de fechar, sombra, foco e ordem (z-order).
- Mouse PS/2 por polling: cursor, clique, arrastar janelas.
- Barra superior e barra de tarefas (clique abre, oculta ou traz a janela para frente).
- Janela **Terminal**: o shell do Orion (`help`, `ls`, `format`, ...) roda nela.
- Janela **Sobre**: versao, resolucao e estado do mouse.

## Arquivos

| Arquivo | Funcao |
| --- | --- |
| `include/gfd.h` | API publica e hooks fracos |
| `kernel/gfd.c` | framebuffer, desenho, janelas, mouse, console |
| `include/gfd_font.h` | fonte 8x16 gerada de DejaVu Sans Mono |

## Como integra

- `boot/grub_entry.asm` chama `gfd_init(multiboot_info)` antes de `kernel_main`.
- `terminal.c` desvia `terminal_putchar` / `terminal_clear` para o console do GFD.
- `keyboard.c` repassa ao GFD os bytes do mouse (bit 5 do status da porta 0x64).
- `kernel.c` chama `gfd_poll()` a cada volta do laco, que redesenha so se algo mudou.
- Os hooks sao `weak`: o build do disquete (`make`), sem `gfd.o`, continua em modo texto.

## Como testar

```sh
make grub        # gera build/orion-grub.iso (precisa de grub-mkrescue e xorriso)
make run-grub    # abre a ISO no QEMU
```

O teclado vai direto para o shell (nao ha foco de teclado por janela ainda).

## Armadilhas ja resolvidas

- Header Multiboot: o pedido de video fica no offset 32, entao os 5 campos de
  endereco (`dd 0` x5) precisam existir mesmo com o bit 16 desligado.
- `multiboot_info`: o framebuffer comeca no byte 88 e `color_info` no 112
  (ha 2 bytes de padding depois de `framebuffer_type`). Ha `_Static_assert` no `gfd.c`.

## Proximos passos

- Teclado com Shift e foco por janela.
- Janela de arquivos (OrionFS) e editor.
- Relogio (depende do tratamento de interrupcoes/timer).
- Buffer duplo (hoje a cena e redesenhada direto no framebuffer).
