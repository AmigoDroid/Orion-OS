# Orion OS

**Versão:** 0.1.0  
**Estado:** Desenvolvimento experimental  
**Arquitetura atual:** x86 de 32 bits (i386)

## Sobre o projeto

O Orion OS é um sistema operacional experimental desenvolvido do zero, com bootloader próprio em Assembly e kernel em C.

O objetivo é construir progressivamente um sistema independente, com gerenciamento de memória, entrada pelo teclado, terminal de comandos e, futuramente, suporte a 64 bits.

## Componentes atuais

- Bootloader de primeiro estágio em Assembly.
- Segundo estágio de carregamento.
- Transição para o modo protegido de 32 bits.
- Kernel em C compilado em modo freestanding.
- Escrita de texto diretamente na memória de vídeo.
- Inicialização testada no emulador QEMU.

## Próximas etapas

1. Criar funções próprias para o terminal.
2. Implementar leitura do teclado.
3. Desenvolver os primeiros comandos.
4. Implementar interrupções e temporizador.
5. Desenvolver gerenciamento de memória.
6. Planejar a migração para x86-64.

## Ambiente de desenvolvimento

- Sistema usado para desenvolver: Ubuntu.
- Linguagens: C e Assembly.
- Ferramentas: GCC, NASM, GNU Make e QEMU.

## Observação

O Orion OS está em fase experimental. A versão 0.1.0 identifica a primeira etapa funcional do projeto, não uma versão pronta para uso geral.

