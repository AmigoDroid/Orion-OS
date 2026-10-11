'use strict';
// Servidor da area de trabalho do Orion. Sem dependencias: apenas modulos nativos do Node.
const http = require('http');
const path = require('path');
const crypto = require('crypto');
const fs = require('fs');
const { OrionFS, FsError, MAX_CONTENT } = require('./orionfs');
const { Users } = require('./users');

const PORT = Number(process.env.PORT || 3000);
const HOST = process.env.HOST || '127.0.0.1';
const DISK = path.resolve(process.env.ORION_DISK || path.join(__dirname, '..', 'build', 'orion-data.img'));
const DATA_DIR = path.resolve(process.env.DATA_DIR || path.join(__dirname, 'data'));
const PUBLIC = path.join(__dirname, 'public');
const SESSION_MS = 8 * 60 * 60 * 1000;
const COOKIE = 'orion_session';

fs.mkdirSync(path.dirname(DISK), { recursive: true });
const disk = new OrionFS(DISK);
disk.ensure();

const users = new Users(DATA_DIR);
const firstPassword = users.bootstrap();
if (firstPassword) {
  console.log('==============================================');
  console.log(' Usuario admin criado.  Senha: ' + firstPassword);
  console.log(' (mostrada somente agora - troque depois do login)');
  console.log('==============================================');
}

// ---- sessao: cookie assinado com HMAC ----
const secretFile = path.join(DATA_DIR, 'secret.key');
if (!fs.existsSync(secretFile)) fs.writeFileSync(secretFile, crypto.randomBytes(32).toString('hex'), { mode: 0o600 });
const SECRET = fs.readFileSync(secretFile, 'utf8');
const sign = (payload) => crypto.createHmac('sha256', SECRET).update(payload).digest('base64url');

function makeToken(user) {
  const payload = Buffer.from(JSON.stringify({ id: user.id, exp: Date.now() + SESSION_MS })).toString('base64url');
  return `${payload}.${sign(payload)}`;
}

function readToken(token) {
  const [payload, mac] = String(token || '').split('.');
  if (!payload || !mac) return null;
  const expected = sign(payload);
  if (mac.length !== expected.length || !crypto.timingSafeEqual(Buffer.from(mac), Buffer.from(expected))) return null;
  try {
    const data = JSON.parse(Buffer.from(payload, 'base64url').toString());
    return data.exp > Date.now() ? users.byId(data.id) : null;
  } catch {
    return null;
  }
}

function cookies(req) {
  return Object.fromEntries(
    String(req.headers.cookie || '').split(';').map((c) => c.trim().split('=')).filter((p) => p[0])
  );
}

// Limite de tentativas de login (por IP + usuario).
const attempts = new Map();
const throttled = (key) => {
  const a = attempts.get(key);
  return a && a.count >= 5 && Date.now() - a.last < 60000;
};

// ---- infraestrutura HTTP minima ----
function send(res, status, body, headers = {}) {
  const data = Buffer.from(JSON.stringify(body));
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': data.length, ...headers });
  res.end(data);
}

function readJson(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    let failed = false;
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > 65536) {
        if (!failed) reject(new FsError('Corpo da requisicao grande demais.', 413));
        failed = true;
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => {
      if (failed) return;
      if (!size) return resolve({});
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')));
      } catch {
        reject(new FsError('JSON invalido.'));
      }
    });
    req.on('error', reject);
  });
}

const routes = [];
function route(method, pattern, opts, fn) {
  const keys = [];
  const re = new RegExp('^' + pattern.replace(/:([a-z]+)/g, (_, k) => { keys.push(k); return '([^/]+)'; }) + '$');
  routes.push({ method, re, keys, opts, fn });
}

const canAccess = (user, file) => user.role === 'admin' || file.uid === user.id;
const describe = (f) => ({ name: f.name, size: f.size, owner: users.nameOf(f.uid), uid: f.uid });

function ownFile(req) {
  const file = disk.get(req.params.name);
  // Mesma resposta para "nao existe" e "nao e seu", para nao revelar nomes alheios.
  if (!file || !canAccess(req.user, file)) throw new FsError('Arquivo nao encontrado.', 404);
  return file;
}

// ---- autenticacao ----
route('POST', '/api/login', {}, (req, res) => {
  const { username, password } = req.body;
  const key = `${req.socket.remoteAddress}|${String(username).toLowerCase()}`;
  if (throttled(key)) throw new FsError('Muitas tentativas. Aguarde 1 minuto.', 429);

  const user = users.verify(username, password);
  if (!user) {
    const a = attempts.get(key) || { count: 0 };
    attempts.set(key, { count: a.count + 1, last: Date.now() });
    throw new FsError('Usuario ou senha incorretos.', 401);
  }
  attempts.delete(key);
  const secure = process.env.COOKIE_SECURE === '1' ? '; Secure' : '';
  send(res, 200, users.publicView(user), {
    'Set-Cookie': `${COOKIE}=${makeToken(user)}; HttpOnly; SameSite=Strict; Path=/; Max-Age=${SESSION_MS / 1000}${secure}`,
  });
});

route('POST', '/api/logout', {}, (req, res) => {
  send(res, 200, { ok: true }, { 'Set-Cookie': `${COOKIE}=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0` });
});

route('GET', '/api/me', { auth: true }, (req, res) => {
  send(res, 200, { ...users.publicView(req.user), maxContent: MAX_CONTENT });
});

route('POST', '/api/me/password', { auth: true }, (req, res) => {
  const { current, next } = req.body;
  if (!users.verify(req.user.username, current)) throw new FsError('Senha atual incorreta.', 403);
  users.changePassword(req.user.id, next);
  send(res, 200, { ok: true });
});

// ---- arquivos ----
route('GET', '/api/files', { auth: true }, (req, res) => {
  send(res, 200, disk.list().filter((f) => canAccess(req.user, f)).map(describe));
});

route('POST', '/api/files', { auth: true }, (req, res) => {
  const { name, content } = req.body;
  const file = disk.create(String(name || ''), req.user.id);
  if (content) {
    try {
      disk.write(file.name, content);
    } catch (err) {
      disk.remove(file.name);
      throw err;
    }
  }
  send(res, 201, describe(disk.get(file.name)));
});

route('GET', '/api/files/:name', { auth: true }, (req, res) => {
  const file = ownFile(req);
  send(res, 200, { ...describe(file), content: disk.read(file.name) });
});

route('PUT', '/api/files/:name', { auth: true }, (req, res) => {
  const file = ownFile(req);
  send(res, 200, describe(disk.write(file.name, req.body.content ?? '')));
});

route('DELETE', '/api/files/:name', { auth: true }, (req, res) => {
  disk.remove(ownFile(req).name);
  send(res, 200, { ok: true });
});

// ---- administracao ----
route('GET', '/api/users', { auth: true, admin: true }, (req, res) => send(res, 200, users.list()));

route('POST', '/api/users', { auth: true, admin: true }, (req, res) => {
  const { username, password, role } = req.body;
  send(res, 201, users.add(username, password, role || 'user'));
});

route('DELETE', '/api/users/:id', { auth: true, admin: true }, (req, res) => {
  const id = Number(req.params.id);
  if (!users.byId(id)) throw new FsError('Usuario nao encontrado.', 404);
  if (id === 0 || id === req.user.id) throw new FsError('Esse usuario nao pode ser removido.', 403);
  // Os arquivos dele passam para o admin (uid 0).
  disk.list().filter((f) => f.uid === id).forEach((f) => disk.chown(f.name, 0));
  users.remove(id);
  send(res, 200, { ok: true });
});

route('POST', '/api/format', { auth: true, admin: true }, (req, res) => {
  if (req.body.confirm !== 'FORMAT') throw new FsError('Envie {"confirm":"FORMAT"} para formatar o disco.');
  disk.format();
  send(res, 200, { ok: true });
});

// ---- arquivos estaticos da area de trabalho ----
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
};

function serveStatic(req, res, pathname) {
  if (req.method !== 'GET' && req.method !== 'HEAD') return send(res, 405, { error: 'Metodo nao permitido.' });
  let rel;
  try {
    rel = decodeURIComponent(pathname === '/' ? '/index.html' : pathname);
  } catch {
    return send(res, 400, { error: 'URL invalida.' });
  }
  const file = path.normalize(path.join(PUBLIC, rel));
  if (!file.startsWith(PUBLIC + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    return send(res, 404, { error: 'Nao encontrado.' });
  }
  const data = fs.readFileSync(file);
  res.writeHead(200, {
    'Content-Type': MIME[path.extname(file)] || 'application/octet-stream',
    'Content-Length': data.length,
    'Cache-Control': 'no-cache',
  });
  res.end(req.method === 'HEAD' ? undefined : data);
}

async function handler(req, res) {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('X-Frame-Options', 'DENY');
  res.setHeader('Content-Security-Policy', "default-src 'self'; style-src 'self' 'unsafe-inline'");

  const { pathname } = new URL(req.url, 'http://localhost');
  if (!pathname.startsWith('/api/')) return serveStatic(req, res, pathname);

  try {
    const match = routes.map((r) => ({ r, m: r.method === req.method && r.re.exec(pathname) })).find((x) => x.m);
    if (!match) throw new FsError('Rota nao encontrada.', 404);
    const { r, m } = match;

    req.params = {};
    r.keys.forEach((k, i) => { req.params[k] = decodeURIComponent(m[i + 1]); });

    if (r.opts.auth) {
      req.user = readToken(cookies(req)[COOKIE]);
      if (!req.user) throw new FsError('Faca login.', 401);
      if (r.opts.admin && req.user.role !== 'admin') throw new FsError('Apenas administradores.', 403);
    }

    req.body = ['POST', 'PUT', 'PATCH'].includes(req.method) ? await readJson(req) : {};
    r.fn(req, res);
  } catch (err) {
    if (err instanceof FsError) return send(res, err.status, { error: err.message });
    if (err instanceof URIError) return send(res, 400, { error: 'URL invalida.' });
    console.error(err);
    send(res, 500, { error: 'Erro interno.' });
  }
}

const server = http.createServer(handler);

if (require.main === module) {
  server.listen(PORT, HOST, () => {
    console.log(`Orion Desktop em http://${HOST}:${PORT}  (disco: ${DISK})`);
    console.log('Nao use a area de trabalho enquanto o QEMU estiver com o mesmo disco aberto.');
  });
}

module.exports = server;
