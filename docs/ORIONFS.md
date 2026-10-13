# OrionFS v3

Sistema de arquivos hierarquico do Orion OS. Codigo em `fs/`; formato aqui; uso no shell abaixo.

> O v3 **nao le** discos do v2 (`ORIONFS2`), nem o v2 le o v3. Use `format sim` uma vez.
> `web/orionfs.js` (interface web) ainda fala o formato v2 e precisa ser portado para o v3.

## Em uma frase

Blocos de 512 bytes, inodes de 64 bytes, pastas de verdade (`/a/b/c`, `.`, `..`), arquivos de ate ~8,4 MiB,
bitmaps de blocos e de inodes, permissoes de dono/outros, datas (relogio CMOS), cache de blocos e `fsck`.

## Layout do disco (setor = bloco = 512 bytes)

| Bloco | Conteudo |
| --- | --- |
| 0 | reservado (nao e tocado) |
| 1 | superbloco (`ORIONFS3`, CRC-32, geometria, contadores, estado limpo/sujo) |
| 2.. | bitmap de inodes |
| ..   | bitmap de blocos (1 bit por bloco do disco inteiro) |
| ..   | tabela de inodes (8 por bloco) |
| `data_start`.. | dados |

A geometria e calculada no `format` a partir do tamanho do disco (inodes = tamanho/4 KiB, entre 32 e 65536).
O inode `1` e sempre a raiz.

### Inode (64 bytes)

`tipo(1) uid(1) modo(2) tamanho(4) ctime(4) mtime(4) nlink(2) reservado(2) direct[9] indirect dindirect`

| Ponteiros | Cobrem | Total acumulado |
| --- | --- | --- |
| 9 diretos | 4,5 KiB | 4,5 KiB |
| 1 indireto (128 ponteiros) | 64 KiB | ~68 KiB |
| 1 duplo indireto (128x128) | 8 MiB | ~8,4 MiB (limite de um arquivo) |

Arquivos podem ter buracos (esparsos): blocos nao escritos nao ocupam espaco e leem como zeros.

### Pasta

Um inode de tipo `DIR` cujo conteudo e um vetor de entradas de 32 bytes: `inode(4) + nome(28)`.
Os dois primeiros slots sao sempre `.` e `..`. Nomes: 1 a 27 bytes, sem `/` nem espacos.
`nlink` de pasta = 2 + numero de subpastas.

## Confiabilidade

- **Escrita direta** (write-through): o cache de 64 blocos nunca guarda dado que o disco nao tem.
- **Transacao por operacao**: antes da 1a escrita o superbloco e marcado "sujo"; no fim recebe os contadores, e
  e marcado "limpo" + `FLUSH CACHE` do disco. Queda de energia no meio deixa o disco "sujo" e o proximo boot avisa.
- **Ordem segura**: cria = inode -> entrada na pasta; apaga = tira a entrada -> libera blocos. O pior caso e um
  inode/bloco orfao (vazamento), nunca uma entrada apontando para dado inexistente. O `fsck` recolhe os orfaos.
- **CRC-32** no superbloco e validacao da geometria no mount.
- `fsck` confere a arvore inteira contra os bitmaps, links e contadores; `fsck repair` conserta o que da para decidir
  sozinho. Bloco fora do volume ou compartilhado entre dois arquivos e so reportado.
- `format` exige `format sim` e mostra modelo e tamanho do disco antes (o Orion fala com o IDE primario: em hardware
  real isso pode ser o seu HD).

## Permissoes

Cada inode guarda `uid` do dono e modo `rwxrwxrwx` (so "dono" e "outros" sao usados). `uid 0` (console) pode tudo.
`fs_set_uid()` muda quem esta agindo. Criar/remover numa pasta exige escrita nela.

## Comandos do shell

| Comando | O que faz |
| --- | --- |
| `ls [-l] [pasta]` | lista (com `-l`: permissoes, dono, tamanho, data) |
| `cd [pasta]`, `pwd` | navegar (o prompt mostra a pasta) |
| `mkdir`, `rmdir` | criar / remover pasta vazia |
| `touch`, `write`, `append`, `cat` | criar, gravar, acrescentar, mostrar |
| `rm [-r]`, `cp [-r]`, `mv` | remover, copiar, mover/renomear |
| `stat`, `chmod`, `tree`, `df` | detalhes, permissoes, arvore, espaco |
| `fsck [repair]`, `sync`, `format sim`, `fstest` | manutencao e teste |

No teclado ABNT2, `/` e `?` ficam na tecla extra antes do Shift direito ou em **AltGr+Q** / **AltGr+W**.

## API em C (`fs/fs.h`)

`fs_mount/format/sync/statfs/fsck`, `fs_mkdir/rmdir/rmtree/readdir`, `fs_create/unlink/rename/copy/truncate/stat/chmod/chown`,
`fs_open/read/write/seek/close` (16 descritores), `fs_read_file/write_file`, `fs_chdir/getcwd`. Todas devolvem `>= 0`
ou um `FS_ERR_*`; `fs_strerror()` traduz.

## Limites

| | |
| --- | --- |
| nome | 27 bytes |
| caminho | 255 bytes |
| arquivo | ~8,4 MiB |
| disco | 128 GiB (LBA28) |
| descritores abertos | 16 |
| profundidade (`rm -r`, `cp -r`) | 40 niveis |

## Testes

`fstest` (no shell) roda dentro de `/.fstest`, nao toca nos seus arquivos e confere que blocos e inodes voltaram ao valor
inicial. Ele tambem causa danos de proposito (bloco vazado, inode orfao, nlink errado, entrada fantasma) e confere que o
`fsck` os acha e conserta. `make test` ainda reinicia a VM e simula queda de energia.
