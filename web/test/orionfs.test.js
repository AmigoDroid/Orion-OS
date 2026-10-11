'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { OrionFS } = require('../orionfs');

function tmpDisk() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'orionfs-'));
  const disk = new OrionFS(path.join(dir, 'disk.img'));
  disk.ensure();
  return disk;
}

test('criar, gravar, ler e remover', () => {
  const d = tmpDisk();
  d.create('nota.txt', 3);
  d.write('nota.txt', 'ola mundo\nsegunda linha');
  assert.strictEqual(d.read('nota.txt'), 'ola mundo\nsegunda linha');
  assert.deepStrictEqual(d.list().map((f) => [f.name, f.size, f.uid]), [['nota.txt', 23, 3]]);
  d.remove('nota.txt');
  assert.strictEqual(d.list().length, 0);
});

test('arquivo multi-setor (4096 bytes) e limite', () => {
  const d = tmpDisk();
  d.create('grande', 1);
  const text = 'x'.repeat(4096);
  d.write('grande', text);
  assert.strictEqual(d.read('grande'), text);
  assert.throws(() => d.write('grande', 'x'.repeat(4097)), /limite/);
});

test('nome invalido, duplicado e diretorio cheio', () => {
  const d = tmpDisk();
  assert.throws(() => d.create('com espaco', 1), /invalido/);
  assert.throws(() => d.create('nome_muito_longo_demais', 1), /invalido/);
  d.create('a', 1);
  assert.throws(() => d.create('a', 1), /Ja existe/);
  for (let i = 1; i < 64; i++) d.create('f' + i, 1);
  assert.throws(() => d.create('extra', 1), /cheio/);
});

test('acentos sao removidos e conteudo nao ASCII e rejeitado', () => {
  const d = tmpDisk();
  d.create('pt', 1);
  d.write('pt', 'configuração é útil');
  assert.strictEqual(d.read('pt'), 'configuracao e util');
  assert.throws(() => d.write('pt', 'emoji \u{1F600}'), /ASCII/);
});

test('sobrescrever com conteudo menor atualiza o tamanho', () => {
  const d = tmpDisk();
  d.create('r', 1);
  d.write('r', 'y'.repeat(900));
  d.write('r', 'curto');
  assert.strictEqual(d.read('r'), 'curto');
});
