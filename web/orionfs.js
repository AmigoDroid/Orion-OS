'use strict';
// Implementacao em Node do OrionFS v2 (mesmo formato do kernel, ver docs/ORIONFS.md).
const fs = require('fs');

const SECTOR = 512;
const SUPER_LBA = 1;
const DIR_LBA = 2;
const DIR_SECTORS = 4;
const DATA_START = 16;
const SLOT_SECTORS = 8;
const PER_SECTOR = 16;
const ENTRY_SIZE = 32;
const ENTRIES = DIR_SECTORS * PER_SECTOR;
const MAX_CONTENT = SLOT_SECTORS * SECTOR;
const MAGIC = 'ORIONFS2';
const DISK_SIZE = 1024 * 1024;
const NAME_RE = /^[A-Za-z0-9._-]{1,15}$/;

class FsError extends Error {
  constructor(message, status = 400) {
    super(message);
    this.status = status;
  }
}

// O terminal do Orion so mostra ASCII: remove acentos e rejeita o resto.
function sanitizeContent(text) {
  const clean = String(text)
    .normalize('NFD').replace(/[\u0300-\u036f]/g, '')
    .replace(/\r\n?/g, '\n');
  for (const ch of clean) {
    const c = ch.charCodeAt(0);
    if (!(c === 10 || c === 9 || (c >= 32 && c <= 126))) {
      throw new FsError('Conteudo so pode ter ASCII imprimivel (acentos sao removidos).');
    }
  }
  return clean;
}

class OrionFS {
  constructor(path) {
    this.path = path;
  }

  ensure() {
    if (!fs.existsSync(this.path)) fs.writeFileSync(this.path, Buffer.alloc(DISK_SIZE));
    if (fs.statSync(this.path).size < DISK_SIZE) fs.truncateSync(this.path, DISK_SIZE);
    if (!this._run((d) => this._isFormatted(d))) this.format();
  }

  _run(fn) {
    const fd = fs.openSync(this.path, 'r+');
    try {
      return fn({ fd });
    } finally {
      fs.closeSync(fd);
    }
  }

  _read(d, lba) {
    const buf = Buffer.alloc(SECTOR);
    fs.readSync(d.fd, buf, 0, SECTOR, lba * SECTOR);
    return buf;
  }

  _write(d, lba, buf) {
    fs.writeSync(d.fd, buf, 0, SECTOR, lba * SECTOR);
  }

  _isFormatted(d) {
    const s = this._read(d, SUPER_LBA);
    return s.toString('latin1', 0, 8) === MAGIC && s[8] === 2;
  }

  _entries(d) {
    const out = [];
    for (let s = 0; s < DIR_SECTORS; s++) {
      const sec = this._read(d, DIR_LBA + s);
      for (let i = 0; i < PER_SECTOR; i++) {
        const o = i * ENTRY_SIZE;
        const nameEnd = sec.indexOf(0, o);
        out.push({
          index: s * PER_SECTOR + i,
          used: sec[o + 20] === 1,
          name: sec.toString('latin1', o, Math.min(nameEnd < 0 ? o + 16 : nameEnd, o + 16)),
          size: sec.readUInt32LE(o + 16),
          uid: sec[o + 21],
        });
      }
    }
    return out;
  }

  _storeEntry(d, index, e) {
    const lba = DIR_LBA + Math.floor(index / PER_SECTOR);
    const sec = this._read(d, lba);
    const o = (index % PER_SECTOR) * ENTRY_SIZE;
    sec.fill(0, o, o + ENTRY_SIZE);
    if (e) {
      sec.write(e.name, o, 'latin1');
      sec.writeUInt32LE(e.size, o + 16);
      sec[o + 20] = 1;
      sec[o + 21] = e.uid;
    }
    this._write(d, lba, sec);
  }

  _find(d, name) {
    return this._entries(d).find((e) => e.used && e.name === name) || null;
  }

  format() {
    this._run((d) => {
      const sup = Buffer.alloc(SECTOR);
      sup.write(MAGIC, 0, 'latin1');
      sup[8] = 2;
      this._write(d, SUPER_LBA, sup);
      for (let s = 0; s < DIR_SECTORS; s++) this._write(d, DIR_LBA + s, Buffer.alloc(SECTOR));
    });
  }

  list() {
    return this._run((d) => this._entries(d).filter((e) => e.used));
  }

  get(name) {
    return this._run((d) => this._find(d, name));
  }

  create(name, uid) {
    if (!NAME_RE.test(name)) {
      throw new FsError('Nome invalido: use 1 a 15 caracteres entre letras, numeros, ponto, _ e -.');
    }
    return this._run((d) => {
      const entries = this._entries(d);
      if (entries.some((e) => e.used && e.name === name)) throw new FsError('Ja existe um arquivo com esse nome.', 409);
      const free = entries.find((e) => !e.used);
      if (!free) throw new FsError('Diretorio cheio (maximo de 64 arquivos).', 507);
      this._storeEntry(d, free.index, { name, size: 0, uid });
      return { index: free.index, name, size: 0, uid };
    });
  }

  read(name) {
    return this._run((d) => {
      const e = this._find(d, name);
      if (!e) throw new FsError('Arquivo nao encontrado.', 404);
      const chunks = [];
      for (let s = 0; s * SECTOR < e.size; s++) {
        chunks.push(this._read(d, DATA_START + e.index * SLOT_SECTORS + s));
      }
      return Buffer.concat(chunks).subarray(0, e.size).toString('latin1');
    });
  }

  write(name, text) {
    const data = Buffer.from(sanitizeContent(text), 'latin1');
    if (data.length > MAX_CONTENT) throw new FsError(`Arquivo maior que o limite de ${MAX_CONTENT} bytes.`, 413);
    return this._run((d) => {
      const e = this._find(d, name);
      if (!e) throw new FsError('Arquivo nao encontrado.', 404);
      for (let off = 0, s = 0; off < data.length; off += SECTOR, s++) {
        const sec = Buffer.alloc(SECTOR);
        data.copy(sec, 0, off, Math.min(off + SECTOR, data.length));
        this._write(d, DATA_START + e.index * SLOT_SECTORS + s, sec);
      }
      this._storeEntry(d, e.index, { name: e.name, size: data.length, uid: e.uid });
      return { ...e, size: data.length };
    });
  }

  chown(name, uid) {
    return this._run((d) => {
      const e = this._find(d, name);
      if (!e) throw new FsError('Arquivo nao encontrado.', 404);
      this._storeEntry(d, e.index, { name: e.name, size: e.size, uid });
    });
  }

  remove(name) {
    return this._run((d) => {
      const e = this._find(d, name);
      if (!e) throw new FsError('Arquivo nao encontrado.', 404);
      this._storeEntry(d, e.index, null);
    });
  }
}

module.exports = { OrionFS, FsError, MAX_CONTENT, ENTRIES };
