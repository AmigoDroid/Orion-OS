# Orion OS

**Versao:** 0.4.0  
**Estado:** Desenvolvimento experimental  
**Arquitetura atual:** x86 de 32 bits (i386)

## Sobre o projeto

O Orion OS e um sistema operacional experimental com bootloader proprio em Assembly e kernel em C.

## Componentes atuais

- Bootloader em dois estagios e entrada no modo protegido de 32 bits.
- Kernel em C com terminal VGA e teclado por polling.
- OrionFS v2 (ver `docs/ORIONFS.md`): arquivos de ate 4 KB, dono por arquivo.
- Orion Desktop: area de trabalho no navegador, com usuarios (pasta `web/`).

## Como usar

Requisitos: `nasm`, `gcc` (com suporte a `-m32`), `qemu-system-x86_64` e Node.js 18+.
A area de trabalho web **nao precisa de internet nem de `npm install`**: usa so modulos nativos do Node.

```sh
make            # compila a imagem de boot
make disk       # cria build/orion-data.img (nao e apagado pelo make clean)
make run        # abre o Orion no QEMU
```

Comandos do Orion: `help`, `clear`, `ls`, `format`, `create`, `write`, `read`, `rm`.
Na primeira vez, rode `format`.

## Orion Desktop (area de trabalho web)

```sh
make web        # ou: node web/server.js
```

Abra http://127.0.0.1:3000. No primeiro uso o usuario `admin` e criado com uma
senha aleatoria mostrada no terminal (ou defina `ADMIN_PASSWORD`).

Recursos: login, janelas (mover, redimensionar, minimizar, maximizar), icones dos
seus arquivos na area de trabalho, gerenciador de arquivos, editor (Ctrl+S),
terminal com os comandos do Orion, administracao de usuarios e papel de parede.
Clique direito na area de trabalho ou num icone abre o menu de contexto.

Variaveis: `PORT`, `HOST`, `ORION_DISK`, `DATA_DIR`, `COOKIE_SECURE=1` (atras de HTTPS).

**Importante:** nao use a area de trabalho e o QEMU ao mesmo tempo no mesmo
`orion-data.img`. O kernel nao conhece o cache do servidor e vice-versa. Feche um
antes de abrir o outro.

Testes: `cd web && npm test` (usa apenas `node:test`, nativo).
