# Estado atual do Orion OS

## Versão 0.1.0

### Funcionando
- [x] Bootloader em Assembly.
- [x] Segundo estágio de inicialização.
- [x] Carregamento do kernel a partir do disco.
- [x] Entrada no modo protegido de 32 bits.
- [x] Kernel em C.
- [x] Escrita de texto na memória de vídeo.
- [x] Inicialização testada no QEMU.

### Em desenvolvimento
- [ ] Terminal interativo.
- [ ] Leitura do teclado.
- [ ] Comandos básicos.
- [ ] Tratamento de interrupções.
- [ ] Gerenciamento de memória.
- [ ] Temporizador.
- [ ] Migração para 64 bits.

### Próximo objetivo
Implementar um terminal que aceite comandos digitados pelo usuário.

### Limitações atuais
- Não possui multitarefa.
- Não possui sistema de arquivos próprio.
- Ainda não possui drivers completos.
- Está em fase experimental.
