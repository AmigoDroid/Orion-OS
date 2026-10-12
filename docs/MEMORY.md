# Gerenciador de memoria

Tres camadas, uma sobre a outra. Codigo em `mm/`, API publica em `mm/mm.h`.

| Camada | Arquivo | Pergunta que responde |
| --- | --- | --- |
| PMM (fisica) | `pmm.c` | "Qual frame de 4 KiB esta livre?" |
| Heap | `heap.c` | "Me de N bytes" (`kmalloc`/`kfree`) |
| VMM (virtual) | `vmm.c` | "Quem pode ler/escrever/executar o que?" |

Ordem no boot: `mm_init()` (PMM + heap) e depois `vmm_init()`.

## PMM

- Dois bitmaps (`used_map`, `rsvd_map`), um bit por frame, cobrindo 4 GiB.
- Tudo comeca **reservado**. So vira livre o que o firmware (mapa E820, tipo 1) declara
  disponivel. Depois o kernel retira: primeiro 1 MiB, a imagem do kernel (`__kernel_start..__kernel_end`),
  e as estruturas Multiboot (info, mapa, nome, modulos).
- `pmm_free_*` valida tudo antes de mexer: alinhamento, faixa, "frame reservado" e free duplo.
- `pmm_check()` reconta os bitmaps e compara com os contadores.

## Heap

Bloco = cabecalho de 16 bytes + payload + canario de 4 bytes colado ao fim do tamanho pedido.

| Defesa | Pega |
| --- | --- |
| magic USADO/LIVRE | free duplo, ponteiro invalido |
| canario | escrever 1 byte alem do buffer |
| veneno `0xDD` nos livres | escrever depois do `free` (use-after-free) |
| `0xCD` em memoria nova | ler memoria nao inicializada |
| lista encadeada conferida a cada passo | cabecalho destruido |
| coalescencia imediata | fragmentacao por vizinhos livres |

`kmalloc` devolve `NULL` se nao couber. Corrupcao detectada e **fatal** (`panic` com o endereco do bloco).
`kfree_checked()` devolve o erro em vez de dar panic (usado nos testes).
`heap_check(deep, verbose)` percorre todos os blocos.

Tamanho do heap: 1/4 da RAM livre, entre 256 KiB e 32 MiB, num trecho contiguo do PMM.

## VMM

Identidade (virtual = fisico), entao nenhum ponteiro muda. A paginacao serve para **proteger**:

- pagina 0 sem mapeamento -> `NULL` deref vira page fault legivel;
- `.text` e `.rodata` somente leitura (CR0.WP);
- pagina de guarda abaixo da pilha -> estouro de pilha e pego (e o #DF roda em pilha propria, por task gate);
- o que esta fora da RAM e do framebuffer fica sem mapeamento. Drivers de MMIO usam `vmm_map_mmio()`.

RAM usa paginas de 4 MiB (PSE); so os primeiros 4 MiB usam paginas de 4 KiB. `vmm_map`/`vmm_unmap` quebram uma pagina grande quando preciso.

## Excecoes

`arch/i386/idt.c` instala as 32 excecoes da CPU. Falha vira tela de panic com EIP, registradores,
CR2 e dica ("ponteiro NULL", "estouro de pilha"). Interrupcoes de hardware continuam desligadas (polling).

## Comandos do shell

`mem`, `memmap`, `vmm`, `heapcheck [deep]`, `memtest` e `crash <null|read|text|stack|div|ud|bp|heap|free2|panic>`.

## Limites conhecidos

- Sem trava: o Orion roda com interrupcoes desligadas. Ao habilitar IRQs/multitarefa, kmalloc/kfree precisam de trava.
- O heap e uma regiao fixa (nao cresce). Para mais memoria, aumente `HEAP_MAX` em `mm/mm.c`.
- Sem alocacao alinhada alem de 16 bytes; para buffers de 4 KiB use `pmm_alloc_frames`.
- Maximo de 4 GiB de RAM enderecavel (32 bits, sem PAE).
