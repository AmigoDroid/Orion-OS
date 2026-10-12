# Histórico de versões

## 0.1.0 — Primeira versão funcional

- Criado o primeiro estágio do bootloader em Assembly.
- Criado o segundo estágio.
- Implementado o carregamento do kernel a partir do disco.
- Criado o kernel inicial em C.
- Implementada a transição para o modo protegido de 32 bits.
- Exibida a primeira mensagem do kernel no QEMU.

### Próxima versão planejada: 0.2.0

- Implementar funções de terminal.
- Adicionar entrada de teclado.
- Criar os comandos iniciais do Orion.

## 0.3.0

- Terminal interativo: o kernel agora le o teclado e executa os comandos.
- OrionFS v2: 64 arquivos, ate 4096 bytes cada (multi-setor), uid do dono por arquivo.
- Interface web com login, usuarios (admin/user), editor de arquivos e administracao.
- Corrigido o `hlt` com interrupcoes desligadas no fim do `kernel_main`.
- Corrigido o loop infinito de `\t` no terminal.
- Makefile: alvo `disk`, `make clean` preserva `orion-data.img`.

## 0.4.0

- Interface web virou uma area de trabalho (Orion Desktop): janelas, icones, barra de tarefas, menu Iniciar, menu de contexto.
- Apps: Arquivos, Editor, Terminal, Usuarios (admin) e Configuracoes (papel de parede, senha).
- Servidor reescrito sem Express: zero dependencias, nao precisa de `npm install` nem de internet.

## 0.5.0 - Memoria, boot unico e tratamento de falhas

- Gerenciador de memoria completo: PMM (bitmap), heap com canario/veneno/coalescencia, VMM (paginacao). Ver `docs/MEMORY.md`.
- Excecoes da CPU (IDT), panic com registradores, #DF em pilha propria, pagina de guarda da pilha.
- Boot normalizado: um kernel, um linker script, uma entrada. O bootloader proprio agora fala Multiboot (E820, A20, LBA). Ver `docs/BOOT.md`.
- Makefile unico com descoberta automatica de fontes; `make test` (QEMU) cobre as duas formas de boot.
- Pastas reorganizadas (`docs/STRUCTURE.md`); shell por tabela de comandos; `kprintf`; log espelha o terminal na porta 0xE9.
- GFD sempre linkado (hooks fracos removidos). Limite de 32 KiB do kernel removido.

## Anteriores nao lancados

- OrionGFD 0.1.0: interface grafica no kernel (framebuffer, janelas, mouse PS/2, terminal em janela). Substitui o antigo `kernel/gui.c`, que so desenhava um mockup estatico.
- Corrigido o header Multiboot do `grub_entry.asm` (faltavam 5 campos antes do pedido de video).
- Corrigido o layout de `multiboot_info` (framebuffer no offset 88, `color_info` no 112).
- Makefile: alvo `run-grub`.
