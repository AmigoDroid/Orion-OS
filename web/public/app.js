'use strict';
/* Orion Desktop: area de trabalho em JavaScript puro, sem bibliotecas. */

const $ = (id) => document.getElementById(id);

function h(tag, props = {}, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k === 'style') Object.assign(el.style, v);
    else if (k.startsWith('on')) el[k] = v;
    else el.setAttribute(k, v);
  }
  for (const kid of kids.flat()) if (kid != null) el.append(kid);
  return el;
}

let me = null;
let files = [];
const listeners = new Set();

async function api(method, url, body) {
  const res = await fetch(url, {
    method,
    headers: body ? { 'Content-Type': 'application/json' } : {},
    body: body ? JSON.stringify(body) : undefined,
  });
  const data = await res.json().catch(() => ({}));
  if (res.status === 401 && me && url !== '/api/login') sessionExpired();
  if (!res.ok) throw new Error(data.error || 'Erro ' + res.status);
  return data;
}

function toast(text, isErr) {
  const t = h('div', { class: 'toast' + (isErr ? ' err' : ''), text });
  $('toasts').append(t);
  setTimeout(() => t.remove(), 3500);
}

const guard = (fn) => async (...args) => {
  try { await fn(...args); } catch (e) { toast(e.message, true); }
};

function dialog({ title, message, input, value, okText = 'OK', danger = false, type = 'text' }) {
  return new Promise((resolve) => {
    const field = input ? h('input', { type, value: value || '', placeholder: input }) : null;
    const done = (v) => { overlay.remove(); resolve(v); };
    const ok = h('button', { class: danger ? 'danger' : '', text: okText, onclick: () => done(field ? field.value : true) });
    const cancel = h('button', { class: 'ghost', text: 'Cancelar', onclick: () => done(null) });
    const overlay = h('div', { class: 'overlay' },
      h('div', { class: 'dialog' }, h('h3', { text: title }), message ? h('p', { text: message }) : null, field, h('div', { class: 'row' }, cancel, ok)));
    overlay.onkeydown = (e) => { if (e.key === 'Enter') ok.click(); if (e.key === 'Escape') cancel.click(); };
    document.body.append(overlay);
    (field || ok).focus();
  });
}

/* ---------- gerenciador de janelas ---------- */
const wins = new Map();
let zTop = 10;

function focusWin(w) {
  w.el.hidden = false;
  w.el.style.zIndex = ++zTop;
  wins.forEach((o) => {
    o.el.classList.toggle('active', o === w);
    o.btn.classList.toggle('active', o === w);
  });
}

function openWindow(id, title, icon, build, opts = {}) {
  if (wins.has(id)) { focusWin(wins.get(id)); return wins.get(id); }

  const n = wins.size;
  const el = h('section', { class: 'win' });
  el.style.width = (opts.width || 560) + 'px';
  el.style.height = (opts.height || 380) + 'px';
  el.style.left = (70 + n * 28) % 320 + 'px';
  el.style.top = (30 + n * 28) % 180 + 'px';

  const w = { id, el, beforeClose: null, cleanup: null };
  const titleEl = h('span', { class: 'wt', text: `${icon} ${title}` });
  w.btn = h('button', { class: 'task', text: `${icon} ${title}` });
  w.setTitle = (t) => { titleEl.textContent = `${icon} ${t}`; w.btn.textContent = `${icon} ${t}`; };
  w.btn.onclick = () => {
    if (!el.hidden && el.classList.contains('active')) { el.hidden = true; w.btn.classList.remove('active'); }
    else focusWin(w);
  };

  const toggleMax = () => el.classList.toggle('max');
  w.close = async () => {
    if (w.beforeClose && !(await w.beforeClose())) return;
    if (w.cleanup) w.cleanup();
    el.remove(); w.btn.remove(); wins.delete(id);
    const last = [...wins.values()].pop();
    if (last) focusWin(last);
  };

  const bar = h('div', { class: 'titlebar', ondblclick: toggleMax }, titleEl,
    h('button', { class: 'wbtn', title: 'Minimizar', text: '–', onclick: () => { el.hidden = true; w.btn.classList.remove('active'); } }),
    h('button', { class: 'wbtn', title: 'Maximizar', text: '□', onclick: toggleMax }),
    h('button', { class: 'wbtn x', title: 'Fechar', text: '✕', onclick: () => w.close() }));

  bar.onpointerdown = (e) => {
    if (e.target.closest('button') || el.classList.contains('max')) return;
    const sx = e.clientX, sy = e.clientY, ox = el.offsetLeft, oy = el.offsetTop;
    if (bar.setPointerCapture) bar.setPointerCapture(e.pointerId);
    bar.onpointermove = (ev) => {
      el.style.left = Math.max(80 - el.offsetWidth, ox + ev.clientX - sx) + 'px';
      el.style.top = Math.max(0, oy + ev.clientY - sy) + 'px';
    };
    bar.onpointerup = () => { bar.onpointermove = null; bar.onpointerup = null; };
  };
  el.onpointerdown = () => focusWin(w);

  wins.set(id, w);
  const body = h('div', { class: 'winbody' });
  el.append(bar, body);
  body.append(build(w));
  $('desk').append(el);
  $('tasks').append(w.btn);
  focusWin(w);
  return w;
}

function closeAllWindows() {
  [...wins.values()].forEach((w) => { if (w.cleanup) w.cleanup(); w.el.remove(); w.btn.remove(); });
  wins.clear();
}

/* ---------- arquivos ---------- */
async function loadFiles() {
  files = await api('GET', '/api/files');
  renderIcons();
  listeners.forEach((fn) => fn());
}

async function newFile() {
  const name = await dialog({ title: 'Novo arquivo', message: 'Até 15 caracteres: letras, números, ponto, _ e -', input: 'nome' });
  if (!name) return;
  await api('POST', '/api/files', { name: name.trim() });
  await loadFiles();
  await openEditor(name.trim());
}

async function deleteFile(name) {
  if (!(await dialog({ title: 'Excluir arquivo', message: `Excluir "${name}"? Não dá para desfazer.`, okText: 'Excluir', danger: true }))) return;
  await api('DELETE', '/api/files/' + encodeURIComponent(name));
  const w = wins.get('edit:' + name);
  if (w) { w.beforeClose = null; await w.close(); }
  await loadFiles();
}

async function openEditor(name) {
  const id = 'edit:' + name;
  if (wins.has(id)) { focusWin(wins.get(id)); return; }
  const f = await api('GET', '/api/files/' + encodeURIComponent(name));
  let saved = f.content;

  openWindow(id, name, '📝', (w) => {
    const ta = h('textarea', { spellcheck: 'false' });
    ta.value = f.content;
    const status = h('span', { class: 'muted grow' });
    const update = () => {
      const dirty = ta.value !== saved;
      status.textContent = `${ta.value.length} / ${me.maxContent} bytes · dono: ${f.owner}${dirty ? ' · não salvo' : ''}`;
      w.setTitle((dirty ? '● ' : '') + name);
    };
    const save = guard(async () => {
      await api('PUT', '/api/files/' + encodeURIComponent(name), { content: ta.value });
      saved = ta.value;
      update();
      await loadFiles();
      toast('Salvo.');
    });
    ta.oninput = update;
    ta.onkeydown = (e) => { if ((e.ctrlKey || e.metaKey) && e.key === 's') { e.preventDefault(); save(); } };
    w.beforeClose = async () => ta.value === saved ||
      (await dialog({ title: 'Fechar sem salvar?', message: `"${name}" tem alterações não salvas.`, okText: 'Fechar', danger: true })) === true;
    update();
    return h('div', { class: 'winbody editor', style: { flex: '1' } }, ta,
      h('div', { class: 'toolbar' }, h('button', { text: 'Salvar (Ctrl+S)', onclick: save }), status));
  }, { width: 540, height: 400 });
}

function appFiles() {
  openWindow('files', 'Arquivos', '📁', (w) => {
    const tbody = h('tbody');
    const empty = h('p', { class: 'muted pad', text: 'Nenhum arquivo ainda. Clique em "Novo arquivo".' });
    const render = () => {
      tbody.replaceChildren(...files.map((f) => h('tr', { ondblclick: guard(() => openEditor(f.name)) },
        h('td', { text: '📄 ' + f.name }),
        h('td', { text: f.size + ' B' }),
        h('td', { text: f.owner }),
        h('td', { class: 'act' },
          h('button', { class: 'small', text: 'Abrir', onclick: guard(() => openEditor(f.name)) }),
          h('button', { class: 'small danger', text: 'Excluir', onclick: guard(() => deleteFile(f.name)) })))));
      empty.hidden = files.length > 0;
    };
    listeners.add(render);
    w.cleanup = () => listeners.delete(render);
    render();
    return h('div', {},
      h('div', { class: 'toolbar' },
        h('button', { text: '+ Novo arquivo', onclick: guard(newFile) }),
        h('button', { class: 'ghost', text: 'Atualizar', onclick: guard(loadFiles) }),
        h('span', { class: 'muted', text: `${me.role === 'admin' ? 'Todos os arquivos' : 'Seus arquivos'} · limite de ${me.maxContent} bytes cada` })),
      h('table', {}, h('thead', {}, h('tr', {}, h('th', { text: 'Nome' }), h('th', { text: 'Tamanho' }), h('th', { text: 'Dono' }), h('th'))), tbody),
      empty);
  }, { width: 580, height: 380 });
}

/* ---------- terminal ---------- */
function appTerminal() {
  openWindow('term', 'Terminal', '⌨', (w) => {
    const out = h('div', { class: 'termout' });
    const input = h('input', { spellcheck: 'false', autocomplete: 'off', autocapitalize: 'off' });
    const history = [];
    let pos = 0;
    const print = (text, cls) => { out.append(h('div', { class: cls || '', text })); out.scrollTop = out.scrollHeight; };
    const file = (n) => encodeURIComponent(n || '');
    const need = (a, uso) => { if (!a) throw new Error('Uso: ' + uso); };

    const commands = {
      help: () => 'Comandos: help, ls, read <nome>, create <nome>, write <nome> <texto>, append <nome> <texto>, rm <nome>, whoami, clear',
      whoami: () => `${me.username} (${me.role}, uid ${me.id})`,
      clear: () => { out.replaceChildren(); return null; },
      ls: async () => {
        await loadFiles();
        return files.length ? files.map((f) => `${f.name.padEnd(16)}${String(f.size).padStart(6)} B  ${f.owner}`).join('\n') : '(vazio)';
      },
      read: async (a) => { need(a, 'read <nome>'); return (await api('GET', '/api/files/' + file(a))).content; },
      create: async (a) => { need(a, 'create <nome>'); await api('POST', '/api/files', { name: a }); await loadFiles(); return 'Arquivo criado.'; },
      write: async (a, t) => {
        need(a && t, 'write <nome> <texto>');
        await api('PUT', '/api/files/' + file(a), { content: t }); await loadFiles(); return 'Arquivo gravado.';
      },
      append: async (a, t) => {
        need(a && t, 'append <nome> <texto>');
        const old = (await api('GET', '/api/files/' + file(a))).content;
        await api('PUT', '/api/files/' + file(a), { content: old + (old && !old.endsWith('\n') ? '\n' : '') + t });
        await loadFiles(); return 'Texto adicionado.';
      },
      rm: async (a) => { need(a, 'rm <nome>'); await api('DELETE', '/api/files/' + file(a)); await loadFiles(); return 'Arquivo removido.'; },
    };

    input.onkeydown = async (e) => {
      if (e.key === 'ArrowUp' && pos > 0) { input.value = history[--pos]; e.preventDefault(); }
      else if (e.key === 'ArrowDown') { input.value = history[++pos] || ''; e.preventDefault(); }
      else if (e.key === 'Enter') {
        const line = input.value.trim();
        input.value = '';
        if (!line) return;
        history.push(line); pos = history.length;
        print(`${me.username}@orion> ${line}`, 'cmd');
        const m = line.match(/^(\S+)(?:\s+(\S+))?(?:\s+([\s\S]*))?$/);
        const run = commands[m[1]];
        if (!run) return print(`Comando desconhecido: ${m[1]}. Digite help.`, 'e');
        try {
          const result = await run(m[2], m[3]);
          if (result != null) print(result);
        } catch (err) { print(err.message, 'e'); }
      }
    };
    print('Terminal do Orion. Digite help para ver os comandos.');
    setTimeout(() => input.focus(), 0);
    return h('div', { class: 'winbody', style: { flex: '1', overflow: 'hidden' }, onclick: () => input.focus() },
      out, h('div', { class: 'termline' }, h('span', { text: '>' }), input));
  }, { width: 560, height: 340 });
}

/* ---------- usuarios (admin) ---------- */
function appUsers() {
  if (me.role !== 'admin') return;
  openWindow('users', 'Usuários', '👥', (w) => {
    const tbody = h('tbody');
    const loadUsers = guard(async () => {
      const list = await api('GET', '/api/users');
      tbody.replaceChildren(...list.map((u) => h('tr', {},
        h('td', { text: u.username }), h('td', { text: u.role }), h('td', { text: String(u.id) }),
        h('td', { class: 'act' }, u.id === 0 || u.id === me.id ? '' : h('button', {
          class: 'small danger', text: 'Remover',
          onclick: guard(async () => {
            if (!(await dialog({ title: 'Remover usuário', message: `Remover ${u.username}? Os arquivos dele passam para o admin.`, okText: 'Remover', danger: true }))) return;
            await api('DELETE', '/api/users/' + u.id); await loadUsers(); await loadFiles();
          }),
        })))));
    });
    const nu = h('input', { placeholder: 'novo usuário', autocomplete: 'off' });
    const np = h('input', { type: 'password', placeholder: 'senha (mín. 8)', autocomplete: 'new-password' });
    const nr = h('select', {}, h('option', { value: 'user', text: 'user' }), h('option', { value: 'admin', text: 'admin' }));
    const add = guard(async () => {
      await api('POST', '/api/users', { username: nu.value, password: np.value, role: nr.value });
      nu.value = ''; np.value = ''; toast('Usuário criado.'); await loadUsers();
    });
    const format = guard(async () => {
      const v = await dialog({ title: 'Formatar disco', message: 'Isto apaga TODOS os arquivos. Digite FORMAT para confirmar.', input: 'FORMAT', okText: 'Formatar', danger: true });
      if (v !== 'FORMAT') return;
      await api('POST', '/api/format', { confirm: 'FORMAT' });
      await loadFiles(); toast('Disco formatado.');
    });
    loadUsers();
    return h('div', {},
      h('div', { class: 'section' }, h('h3', { text: 'Contas' }),
        h('table', {}, h('thead', {}, h('tr', {}, h('th', { text: 'Usuário' }), h('th', { text: 'Papel' }), h('th', { text: 'UID' }), h('th'))), tbody),
        h('div', { class: 'form inline' }, nu, np, nr, h('button', { text: 'Criar', onclick: add }))),
      h('div', { class: 'section' }, h('h3', { text: 'Disco' }),
        h('p', { class: 'muted', text: 'Formatar apaga todos os arquivos de todos os usuários.' }),
        h('button', { class: 'danger', text: 'Formatar disco', onclick: format })));
  }, { width: 560, height: 420 });
}

/* ---------- configuracoes ---------- */
const WALLPAPERS = {
  Floresta: 'linear-gradient(135deg,#0b3d2e,#0b1f3a)',
  Noite: 'linear-gradient(135deg,#05070a,#1a2233)',
  Pôr: 'linear-gradient(135deg,#3a1c4a,#0b2a4a)',
  Grafite: 'linear-gradient(135deg,#1c1f1d,#2e3430)',
  Oceano: 'linear-gradient(135deg,#06425c,#0a1f44)',
};

function applyWallpaper(name) {
  const css = WALLPAPERS[name] || WALLPAPERS.Floresta;
  $('shell').style.background = css;
}

function wallpaperKey() { return 'orion.wallpaper.' + (me ? me.username : ''); }
function savedWallpaper() { try { return localStorage.getItem(wallpaperKey()) || 'Floresta'; } catch { return 'Floresta'; } }

function appSettings() {
  openWindow('settings', 'Configurações', '⚙', () => {
    const swatches = h('div', { class: 'swatches' });
    const paint = () => {
      const cur = savedWallpaper();
      swatches.replaceChildren(...Object.entries(WALLPAPERS).map(([name, css]) => h('button', {
        class: 'swatch' + (name === cur ? ' on' : ''), title: name, style: { background: css },
        onclick: () => { try { localStorage.setItem(wallpaperKey(), name); } catch { /* sem armazenamento */ } applyWallpaper(name); paint(); },
      })));
    };
    paint();
    const cur = h('input', { type: 'password', placeholder: 'senha atual', autocomplete: 'current-password' });
    const nxt = h('input', { type: 'password', placeholder: 'nova senha (mín. 8)', autocomplete: 'new-password' });
    const change = guard(async () => {
      await api('POST', '/api/me/password', { current: cur.value, next: nxt.value });
      cur.value = ''; nxt.value = ''; toast('Senha alterada.');
    });
    return h('div', {},
      h('div', { class: 'section' }, h('h3', { text: 'Conta' }), h('p', { text: `${me.username} · ${me.role} · uid ${me.id}` })),
      h('div', { class: 'section' }, h('h3', { text: 'Papel de parede' }), swatches),
      h('div', { class: 'section' }, h('h3', { text: 'Trocar senha' }), h('div', { class: 'form' }, cur, nxt, h('button', { text: 'Alterar senha', onclick: change }))));
  }, { width: 420, height: 440 });
}

/* ---------- icones, menus e barra de tarefas ---------- */
const coarse = !!(window.matchMedia && window.matchMedia('(pointer: coarse)').matches);

function appList() {
  return [
    ['📁', 'Arquivos', appFiles],
    ['⌨', 'Terminal', appTerminal],
    ...(me.role === 'admin' ? [['👥', 'Usuários', appUsers]] : []),
    ['⚙', 'Configurações', appSettings],
  ];
}

function makeIcon(glyph, label, open, file) {
  const el = h('button', { class: 'icon', title: file ? `${file.name} · ${file.size} bytes · ${file.owner}` : label },
    h('span', { class: 'glyph', text: glyph }), h('span', { class: 'label', text: label }));
  if (coarse) el.onclick = guard(open); else el.ondblclick = guard(open);
  if (file) {
    el.oncontextmenu = (e) => {
      e.preventDefault(); e.stopPropagation();
      menu(e, [['Abrir', open], ['Excluir', () => deleteFile(file.name)]]);
    };
  }
  return el;
}

function renderIcons() {
  if (!me) return;
  $('icons').replaceChildren(
    ...appList().map(([g, l, fn]) => makeIcon(g, l, fn)),
    ...files.map((f) => makeIcon('📄', f.name, () => openEditor(f.name), f)));
}

function closeMenu() {
  document.querySelectorAll('.ctx').forEach((m) => m.remove());
  $('startmenu').hidden = true;
}

function menu(e, items) {
  closeMenu();
  const m = h('div', { class: 'ctx', style: { left: e.clientX + 'px', top: e.clientY + 'px' } },
    ...items.map(([text, fn]) => h('button', { text, onclick: () => { closeMenu(); guard(fn)(); } })));
  document.body.append(m);
}

document.addEventListener('click', (e) => { if (!e.target.closest('#start, #startmenu')) closeMenu(); });
$('desk').oncontextmenu = (e) => {
  if (e.target.closest('.win, .icon')) return;
  e.preventDefault();
  menu(e, [['Novo arquivo', newFile], ['Atualizar', loadFiles]]);
};

$('start').onclick = () => {
  const open = $('startmenu').hidden;
  closeMenu();
  if (!open) return;
  $('startmenu').replaceChildren(
    ...appList().map(([g, l, fn]) => h('button', { text: `${g}  ${l}`, onclick: () => { closeMenu(); fn(); } })),
    h('button', { text: '＋  Novo arquivo', onclick: () => { closeMenu(); guard(newFile)(); } }),
    h('button', { text: '⏻  Sair', onclick: () => { closeMenu(); logout(); } }));
  $('startmenu').hidden = false;
};

function tick() {
  $('clock').textContent = new Date().toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' });
}
setInterval(tick, 10000);

/* ---------- sessao ---------- */
async function enterDesktop() {
  $('login').hidden = true;
  $('shell').hidden = false;
  $('who').textContent = `${me.username} (${me.role})`;
  applyWallpaper(savedWallpaper());
  tick();
  await loadFiles();
}

function leaveDesktop() {
  closeAllWindows();
  closeMenu();
  me = null; files = [];
  $('icons').replaceChildren();
  $('shell').hidden = true;
  $('login').hidden = false;
  $('loginPass').value = '';
}

function sessionExpired() {
  leaveDesktop();
  $('loginMsg').textContent = 'Sessão expirada. Entre novamente.';
}

async function logout() {
  try { await api('POST', '/api/logout'); } catch { /* ja saiu */ }
  leaveDesktop();
  $('loginMsg').textContent = '';
}

$('loginForm').onsubmit = async (e) => {
  e.preventDefault();
  $('loginMsg').textContent = '';
  try {
    await api('POST', '/api/login', { username: $('loginUser').value, password: $('loginPass').value });
    me = await api('GET', '/api/me');
    await enterDesktop();
  } catch (err) {
    me = null;
    $('loginMsg').textContent = err.message;
  }
};

(async () => {
  try { me = await api('GET', '/api/me'); await enterDesktop(); } catch { me = null; }
})();
