# OrionFS v2

Sistema de arquivos simples do Orion OS. O mesmo formato e usado pelo kernel
(`kernel/fs.c`) e pela interface web (`web/orionfs.js`).

## Layout do disco (setores de 512 bytes)

| LBA     | Conteudo                                                        |
|---------|-----------------------------------------------------------------|
| 0       | reservado                                                       |
| 1       | superbloco: `"ORIONFS2"` nos bytes 0-7, versao `2` no byte 8    |
| 2 a 5   | diretorio: 64 entradas de 32 bytes (16 por setor)               |
| 16 ...  | dados: a entrada `i` tem um slot fixo de 8 setores (LBA 16+8*i) |

Limites: 64 arquivos, ate 4096 bytes cada, nomes de ate 15 caracteres.

## Entrada de diretorio (32 bytes)

| Bytes | Campo                                   |
|-------|-----------------------------------------|
| 0-15  | nome (terminado em 0)                   |
| 16-19 | tamanho em bytes (uint32 little-endian) |
| 20    | `1` = em uso, `0` = livre               |
| 21    | `uid` do dono (0 = admin / console)     |
| 22-31 | reservado                               |

## Usuarios

O kernel nao tem login: arquivos criados no console do Orion ficam com `uid 0`.
Usuarios, senhas e papeis ficam na interface web (`web/data/users.json`, senhas
com scrypt). O `uid` gravado em cada arquivo e o id do usuario web que o criou;
usuarios comuns so veem os proprios arquivos, o admin (uid 0) ve todos.

## Compatibilidade

A v2 nao le discos da v1 (`ORIONFS1`). Use `format` no Orion ou apague
`build/orion-data.img` e rode `make disk`.
