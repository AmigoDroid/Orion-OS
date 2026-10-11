'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const os = require('os');
const path = require('path');

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'orionweb-'));
process.env.ORION_DISK = path.join(dir, 'disk.img');
process.env.DATA_DIR = path.join(dir, 'data');
process.env.ADMIN_PASSWORD = 'senha-admin-123';
const app = require('../server');

let base;
let server;
test.before(() => new Promise((ok) => { server = app.listen(0, '127.0.0.1', () => { base = `http://127.0.0.1:${server.address().port}`; ok(); }); }));
test.after(() => server.close());

async function call(method, url, body, cookie) {
  const res = await fetch(base + url, {
    method,
    headers: { 'Content-Type': 'application/json', ...(cookie ? { Cookie: cookie } : {}) },
    body: body ? JSON.stringify(body) : undefined,
  });
  return { status: res.status, body: await res.json().catch(() => ({})), cookie: (res.headers.get('set-cookie') || '').split(';')[0] };
}

async function login(username, password) {
  const r = await call('POST', '/api/login', { username, password });
  assert.strictEqual(r.status, 200);
  return r.cookie;
}

test('isolamento entre usuarios e acesso do admin', async () => {
  const admin = await login('admin', 'senha-admin-123');
  assert.strictEqual((await call('POST', '/api/users', { username: 'ana', password: 'senhaana1' }, admin)).status, 201);
  assert.strictEqual((await call('POST', '/api/users', { username: 'beto', password: 'senhabeto1' }, admin)).status, 201);
  const ana = await login('ana', 'senhaana1');
  const beto = await login('beto', 'senhabeto1');

  assert.strictEqual((await call('POST', '/api/files', { name: 'diario', content: 'segredo da ana' }, ana)).status, 201);
  assert.deepStrictEqual((await call('GET', '/api/files', null, beto)).body, []);
  assert.strictEqual((await call('GET', '/api/files/diario', null, beto)).status, 404);
  assert.strictEqual((await call('PUT', '/api/files/diario', { content: 'x' }, beto)).status, 404);
  assert.strictEqual((await call('DELETE', '/api/files/diario', null, beto)).status, 404);
  assert.strictEqual((await call('GET', '/api/files/diario', null, ana)).body.content, 'segredo da ana');
  assert.strictEqual((await call('GET', '/api/files/diario', null, admin)).body.owner, 'ana');
  assert.strictEqual((await call('GET', '/api/users', null, ana)).status, 403);
});

test('sem login, cookie falso e senha errada', async () => {
  assert.strictEqual((await call('GET', '/api/files')).status, 401);
  assert.strictEqual((await call('GET', '/api/files', null, 'orion_session=abc.def')).status, 401);
  assert.strictEqual((await call('POST', '/api/login', { username: 'admin', password: 'errada' })).status, 401);
});

test('remover usuario passa os arquivos para o admin', async () => {
  const admin = await login('admin', 'senha-admin-123');
  const u = (await call('POST', '/api/users', { username: 'caio', password: 'senhacaio1' }, admin)).body;
  const caio = await login('caio', 'senhacaio1');
  await call('POST', '/api/files', { name: 'caio.txt' }, caio);
  assert.strictEqual((await call('DELETE', `/api/users/${u.id}`, null, admin)).status, 200);
  assert.strictEqual((await call('GET', '/api/files/caio.txt', null, admin)).body.owner, 'admin');
  assert.strictEqual((await call('GET', '/api/files', null, caio)).status, 401);
});

test('troca de senha', async () => {
  const admin = await login('admin', 'senha-admin-123');
  assert.strictEqual((await call('POST', '/api/me/password', { current: 'errada', next: 'novasenha1' }, admin)).status, 403);
  assert.strictEqual((await call('POST', '/api/me/password', { current: 'senha-admin-123', next: 'novasenha1' }, admin)).status, 200);
  await login('admin', 'novasenha1');
});
