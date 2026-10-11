'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { FsError } = require('./orionfs');

const USER_RE = /^[a-z0-9_]{3,20}$/;

function hashPassword(password, salt) {
  return crypto.scryptSync(password, salt, 64).toString('hex');
}

class Users {
  constructor(dir) {
    this.dir = dir;
    this.file = path.join(dir, 'users.json');
    fs.mkdirSync(dir, { recursive: true });
    this.users = fs.existsSync(this.file) ? JSON.parse(fs.readFileSync(this.file, 'utf8')) : [];
  }

  _save() {
    fs.writeFileSync(this.file, JSON.stringify(this.users, null, 2), { mode: 0o600 });
  }

  // Primeiro uso: cria o admin (uid 0, o mesmo dono dos arquivos criados no console do Orion).
  bootstrap() {
    if (this.users.length) return null;
    const password = process.env.ADMIN_PASSWORD || crypto.randomBytes(9).toString('base64url');
    this.add('admin', password, 'admin');
    return process.env.ADMIN_PASSWORD ? null : password;
  }

  add(username, password, role = 'user') {
    username = String(username || '').toLowerCase();
    if (!USER_RE.test(username)) throw new FsError('Usuario: 3 a 20 caracteres (a-z, 0-9 e _).');
    if (String(password || '').length < 8) throw new FsError('A senha precisa ter pelo menos 8 caracteres.');
    if (!['admin', 'user'].includes(role)) throw new FsError('Papel invalido.');
    if (this.users.some((u) => u.username === username)) throw new FsError('Usuario ja existe.', 409);

    let id = this.users.length === 0 ? 0 : 1;
    while (this.users.some((u) => u.id === id)) id++;
    if (id > 255) throw new FsError('Limite de 255 usuarios atingido.', 507);

    const salt = crypto.randomBytes(16).toString('hex');
    const user = { id, username, role, salt, hash: hashPassword(password, salt) };
    this.users.push(user);
    this._save();
    return this.publicView(user);
  }

  verify(username, password) {
    const user = this.users.find((u) => u.username === String(username || '').toLowerCase());
    const salt = user ? user.salt : '00';
    const hash = Buffer.from(hashPassword(String(password || ''), salt), 'hex');
    if (!user) return null;
    return crypto.timingSafeEqual(hash, Buffer.from(user.hash, 'hex')) ? user : null;
  }

  changePassword(id, password) {
    const user = this.byId(id);
    if (String(password || '').length < 8) throw new FsError('A senha precisa ter pelo menos 8 caracteres.');
    user.salt = crypto.randomBytes(16).toString('hex');
    user.hash = hashPassword(password, user.salt);
    this._save();
  }

  remove(id) {
    this.users = this.users.filter((u) => u.id !== id);
    this._save();
  }

  byId(id) {
    return this.users.find((u) => u.id === id) || null;
  }

  nameOf(uid) {
    const u = this.byId(uid);
    return u ? u.username : uid === 0 ? 'admin' : `uid ${uid}`;
  }

  publicView(u) {
    return { id: u.id, username: u.username, role: u.role };
  }

  list() {
    return this.users.map((u) => this.publicView(u));
  }
}

module.exports = { Users };
