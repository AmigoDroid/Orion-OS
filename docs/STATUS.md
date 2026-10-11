# Estado atual do Orion OS

## Versão 0.4.0

### Funcionando
- [x] Bootloader em Assembly e segundo estágio.
- [x] Modo protegido de 32 bits e kernel em C.
- [x] Terminal interativo com teclado (polling) e comandos básicos.
- [x] OrionFS v2 (64 arquivos, até 4 KB cada, dono por arquivo).
- [x] Orion Desktop: área de trabalho web com usuários, sem dependências.

### Em desenvolvimento
- [ ] Tratamento de interrupções e temporizador.
- [ ] Gerenciamento de memória.
- [ ] Teclado com Shift (maiúsculas e símbolos).
- [ ] Migração para 64 bits.

### Limitações atuais
- Sem multitarefa e sem login no kernel (usuários existem só na interface web).
- Não use a interface web e o QEMU ao mesmo tempo no mesmo disco.
- Conteúdo dos arquivos limitado a ASCII (a interface remove acentos).
