// MiniSQL Workbench front end. Everything goes through a handful of JSON
// endpoints served by the C++ engine (see src/server/Api.h).

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];
const enc = encodeURIComponent;

// ---------------------------------------------------------------- server

async function call(method, path, body) {
  let res;
  try {
    res = await fetch(path, { method, body, headers: body != null ? { 'Content-Type': 'text/plain' } : undefined });
  } catch (e) {
    setConnected(false);
    throw new Error('Lost connection to the MiniSQL server. Is it still running?');
  }
  setConnected(true);
  const text = await res.text();
  const isJson = (res.headers.get('Content-Type') || '').includes('json');
  const data = isJson ? JSON.parse(text) : text;
  if (!res.ok) throw new Error((isJson && data.error) || text || res.statusText);
  return data;
}

const api = {
  state: () => call('GET', '/api/state'),
  browse: (path) => call('GET', '/api/browse?path=' + enc(path || '')),
  open: (path) => call('POST', '/api/open?path=' + enc(path)),
  create: (parent, name) => call('POST', `/api/create?parent=${enc(parent)}&name=${enc(name)}`),
  memory: () => call('POST', '/api/memory'),
  close: () => call('POST', '/api/close'),
  query: (sql, limit, line) => call('POST', `/api/query?limit=${limit}&line=${line || 1}`, sql),
  readScript: (name) => call('GET', '/api/script?name=' + enc(name)),
  saveScript: (name, text) => call('POST', '/api/script?name=' + enc(name), text),
  deleteScript: (name) => call('POST', '/api/script/delete?name=' + enc(name)),
  inspect: (table) => call('GET', '/api/inspect?table=' + enc(table || '')),
  sample: () => call('GET', '/api/sample'),
};

// ---------------------------------------------------------------- state

const S = {
  ws: null,              // last /api/state answer
  tabs: [],              // open editor tabs
  active: null,          // id of the active tab
  runs: [],              // every statement run, for the Output log
  results: [],           // result sets of the last run
  view: 'output',        // results panel: 'output' or 'r<index>'
  selected: null,        // statement shown in the Explain tab
  insp: 'explain',
  structTable: '',
  expanded: new Set(),   // tables open in the navigator
  busy: false,
};
let editor;
let tabSeq = 0;
let untitled = 0;
let runSeq = 0;

const MEMORY_STARTER = `-- In-memory session: tables live in RAM and vanish when you close it.
-- Query > Open sample script loads a complete example.

CREATE TABLE notes (id INT PRIMARY KEY, title VARCHAR(40), stars INT);
INSERT INTO notes VALUES (1, 'B+ trees are wide', 5), (2, 'AVL trees rotate', 4), (3, 'Hash tables chain', 4);
SELECT * FROM notes WHERE id = 2;
`;

// ---------------------------------------------------------------- small helpers

function toast(text, kind = '') {
  const el = document.createElement('div');
  el.className = 'toast ' + kind;
  el.textContent = text;
  $('#toasts').appendChild(el);
  setTimeout(() => el.remove(), kind === 'err' ? 6500 : 3000);
}

function setConnected(ok) {
  const el = $('#sb-conn');
  if (!el) return;
  el.classList.toggle('down', !ok);
  el.lastChild.textContent = ok ? 'Connected' : 'Disconnected';
}

const fmtMs = (ms) => (ms < 1 ? ms.toFixed(3) : ms < 100 ? ms.toFixed(2) : ms.toFixed(0)) + ' ms';
const fmtSize = (n) => (n < 1024 ? n + ' B' : n < 1048576 ? (n / 1024).toFixed(1) + ' KB' : (n / 1048576).toFixed(1) + ' MB');
const plural = (n, one, many) => `${n} ${n === 1 ? one : many || one + 's'}`;
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

function activeTab() { return S.tabs.find((t) => t.id === S.active); }
function isDirty(t) { return t.text !== t.saved; }

// ---------------------------------------------------------------- dialogs

function modal({ title, sub, body, foot, onClose }) {
  const back = document.createElement('div');
  back.className = 'modal-back';
  back.innerHTML = `<div class="modal" role="dialog">
      <header><h3>${title}</h3>${sub ? `<p>${sub}</p>` : ''}</header>
      <div class="m-body">${body}</div>
      <footer>${foot}</footer></div>`;
  $('#modal-root').appendChild(back);
  paintIcons(back);
  const onKey = (e) => {
    if (e.key === 'Escape') {
      e.stopPropagation();
      m.close();
    }
  };
  const m = {
    q: (s) => back.querySelector(s),
    close() {
      back.remove();
      document.removeEventListener('keydown', onKey, true);
      if (onClose) onClose();
    },
  };
  document.addEventListener('keydown', onKey, true);
  back.addEventListener('mousedown', (e) => { if (e.target === back) m.close(); });
  const cancel = back.querySelector('[data-cancel]');
  if (cancel) cancel.onclick = () => m.close();
  return m;
}

function promptDialog(title, label, value, okText = 'OK') {
  return new Promise((resolve) => {
    let done = false;
    const m = modal({
      title,
      body: `<div class="field"><label>${label}</label><input data-in spellcheck="false" value="${esc(value || '')}"></div>`,
      foot: `<button class="btn" data-cancel>Cancel</button><button class="btn primary" data-ok>${okText}</button>`,
      onClose: () => { if (!done) resolve(null); },
    });
    const input = m.q('[data-in]');
    const ok = () => {
      done = true;
      const v = input.value.trim();
      m.close();
      resolve(v || null);
    };
    m.q('[data-ok]').onclick = ok;
    input.addEventListener('keydown', (e) => { if (e.key === 'Enter') ok(); });
    input.focus();
    const dot = input.value.lastIndexOf('.');
    input.setSelectionRange(0, dot > 0 ? dot : input.value.length);
  });
}

function confirmDialog(title, text, okText = 'OK', danger = false) {
  return new Promise((resolve) => {
    let done = false;
    const m = modal({
      title,
      body: `<p style="margin:0;color:var(--text-2)">${text}</p>`,
      foot: `<button class="btn" data-cancel>Cancel</button><button class="btn ${danger ? 'danger' : 'primary'}" data-ok>${okText}</button>`,
      onClose: () => { if (!done) resolve(false); },
    });
    m.q('[data-ok]').onclick = () => {
      done = true;
      m.close();
      resolve(true);
    };
    m.q('[data-ok]').focus();
  });
}

async function confirmDiscard() {
  const dirty = S.tabs.filter((t) => isDirty(t) && (t.file || t.text.trim()));
  if (!dirty.length) return true;
  return confirmDialog('Unsaved changes', `${dirty.map((t) => '<b>' + esc(t.name) + '</b>').join(', ')} ${dirty.length === 1 ? 'has' : 'have'} unsaved changes. Discard them?`, 'Discard', true);
}

// Folder picker for both "Open folder" and "New folder". The server lists
// directories; you click into the one you want and press Open.
function folderDialog(mode) {
  const creating = mode === 'create';
  let cur = null;
  const m = modal({
    title: creating ? 'New folder' : 'Open folder',
    sub: creating
      ? 'Go to where the new workspace should live, then give it a name.'
      : 'Click through to the folder you want and press Open. Tables are kept in its data/ subfolder.',
    body: `<div class="picker-path">
        <button class="btn sm" data-up title="Parent folder">${icon('up')}</button>
        <button class="btn sm" data-home title="Home folder">${icon('home')}</button>
        <input data-path spellcheck="false" title="Type a path and press Enter">
      </div>
      <div class="picker-list" data-list></div>
      ${creating ? '<div class="field" style="margin:12px 0 0"><label>New folder name</label><input data-name placeholder="my_database" spellcheck="false"></div>' : ''}`,
    foot: `<span class="left" data-hint></span><button class="btn" data-cancel>Cancel</button>
      <button class="btn primary" data-ok>${creating ? 'Create' : 'Open'}</button>`,
  });
  const list = m.q('[data-list]');
  const pathInput = m.q('[data-path]');

  async function go(path) {
    try {
      cur = await api.browse(path);
    } catch (e) {
      toast(e.message, 'err');
      return;
    }
    pathInput.value = cur.path || 'This PC';
    list.innerHTML =
      cur.entries
        .map((e) => `<div class="tree-row" data-p="${esc(e.path)}"><span data-icon="folder"></span>
          <span class="name">${esc(e.name)}</span>${e.workspace ? `<span class="ws-tag">${dsBadge('bptree', 'MiniSQL workspace')}</span>` : ''}</div>`)
        .join('') || '<div class="nav-empty">No subfolders here.</div>';
    paintIcons(list);
    m.q('[data-up]').disabled = !cur.parent;
    m.q('[data-hint]').textContent = creating ? '' : cur.path ? `Will open: ${cur.path}` : '';
  }

  list.addEventListener('click', (e) => {
    const row = e.target.closest('[data-p]');
    if (row) go(row.dataset.p);
  });
  m.q('[data-up]').onclick = () => cur && cur.parent && go(cur.parent);
  m.q('[data-home]').onclick = () => go('');
  pathInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') go(pathInput.value.trim()); });

  m.q('[data-ok]').onclick = async () => {
    if (!cur || !cur.path) return toast('Pick a folder first', 'err');
    if (creating) {
      const name = m.q('[data-name]').value.trim();
      if (!name) return m.q('[data-name]').focus();
      if (!(await confirmDiscard())) return;
      try {
        const st = await api.create(cur.path, name);
        m.close();
        enterWorkbench(st, true);
        toast(`Created ${st.name}`, 'ok');
      } catch (e) {
        toast(e.message, 'err');
      }
    } else {
      m.close();
      openFolder(cur.path);
    }
  };

  const recent = getRecent()[0];
  go(recent ? parentOf(recent.path) : '');
  if (creating) m.q('[data-name]').focus();
}

function parentOf(path) {
  const i = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
  return i > 0 ? path.slice(0, i) : path;
}

function showShortcuts() {
  const rows = [
    ['Run statement at cursor', 'Ctrl+Enter'], ['Run all (or the selection)', 'Ctrl+Shift+Enter'],
    ['Save script', 'Ctrl+S'], ['New script', 'Alt+N'], ['Toggle comment', 'Ctrl+/'],
    ['Indent / outdent', 'Tab / Shift+Tab'], ['Close dialog', 'Esc'],
  ];
  modal({
    title: 'Keyboard shortcuts',
    body: `<table class="shortcut-list">${rows.map(([a, k]) => `<tr><td>${a}</td><td>${k.split(/(\+| \/ )/).map((p) => (p === '+' || p === ' / ' ? p : `<kbd>${p}</kbd>`)).join('')}</td></tr>`).join('')}</table>`,
    foot: '<button class="btn primary" data-cancel>Close</button>',
  });
}

// ---------------------------------------------------------------- theme + recent

function paintThemeButtons() {
  const dark = document.documentElement.dataset.theme === 'dark';
  $$('.theme-toggle').forEach((b) => {
    b.innerHTML = icon(dark ? 'sun' : 'moon');
    b.title = dark ? 'Switch to light mode' : 'Switch to dark mode';
  });
}

function toggleTheme() {
  const next = document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark';
  document.documentElement.dataset.theme = next;
  try { localStorage.setItem('minisql.theme', next); } catch (e) { /* private mode */ }
  paintThemeButtons();
}

function getRecent() {
  try { return JSON.parse(localStorage.getItem('minisql.recent')) || []; } catch (e) { return []; }
}

function setRecent(list) {
  try { localStorage.setItem('minisql.recent', JSON.stringify(list.slice(0, 8))); } catch (e) { /* ignore */ }
}

function rememberFolder(path, name) {
  setRecent([{ path, name, at: Date.now() }, ...getRecent().filter((r) => r.path !== path)]);
}

function renderRecent() {
  const list = getRecent();
  $('#recent-list').innerHTML = list
    .map((r) => `<li data-path="${esc(r.path)}" title="${esc(r.path)}"><span data-icon="folder"></span>
      <span class="r-text"><b>${esc(r.name)}</b><small>${esc(r.path)}</small></span>
      <button class="mini" data-forget title="Remove from list">${icon('x')}</button></li>`)
    .join('');
  $('#recent-empty').classList.toggle('hidden', list.length > 0);
  paintIcons($('#recent-list'));
}

function renderDsCards() {
  $('#ds-cards').innerHTML = DS_ORDER.map((k) => {
    const d = DS[k];
    return `<button class="ds-card" data-ds="${k}" style="--c:${d.color}">
      <b><span class="swatch"></span>${d.name}</b><p>${d.role}</p><code>${d.file}</code></button>`;
  }).join('');
}

function showDsModal(key) {
  const d = DS[key];
  modal({
    title: d.name,
    sub: d.file,
    body: learnCard(key, false),
    foot: '<button class="btn primary" data-cancel>Close</button>',
  });
}

// ---------------------------------------------------------------- screens

function showWelcome() {
  S.ws = null;
  renderRecent();
  $('#workbench').classList.add('hidden');
  $('#welcome').classList.remove('hidden');
  document.title = 'MiniSQL Workbench';
}

function enterWorkbench(st, fresh) {
  S.ws = st;
  if (!st.memory) rememberFolder(st.path, st.name);
  if (fresh || !S.tabs.length) {
    S.tabs = [];
    S.active = null;
    S.runs = [];
    S.results = [];
    S.selected = null;
    S.view = 'output';
    S.expanded.clear();
    S.structTable = '';
    untitled = 0;
    if (st.scripts.includes('getting_started.sql')) openScript('getting_started.sql');
    else if (st.scripts.length) openScript(st.scripts[0]);
    else if (st.memory) {
      const t = newTab(MEMORY_STARTER);
      t.saved = t.text;   // nothing to lose here, don't nag about it
      renderTabs();
    } else newTab();
  }
  $('#welcome').classList.add('hidden');
  $('#workbench').classList.remove('hidden');
  document.title = `${st.name} - MiniSQL Workbench`;
  renderWorkspace();
  renderResults();
  renderExplain();
  if (S.insp === 'structures') renderStructures();
  editor.focus();
}

async function openFolder(path) {
  if (!(await confirmDiscard())) return;
  try {
    enterWorkbench(await api.open(path), true);
  } catch (e) {
    toast(e.message, 'err');
    if (/not a folder/.test(e.message)) {
      setRecent(getRecent().filter((r) => r.path !== path));
      renderRecent();
    }
  }
}

async function openMemory() {
  if (!(await confirmDiscard())) return;
  try { enterWorkbench(await api.memory(), true); } catch (e) { toast(e.message, 'err'); }
}

async function closeFolder() {
  if (!(await confirmDiscard())) return;
  try { await api.close(); } catch (e) { toast(e.message, 'err'); }
  S.tabs = [];
  S.active = null;
  showWelcome();
}

async function refreshState() {
  try {
    S.ws = await api.state();
    if (!S.ws.open) return showWelcome();
    renderWorkspace();
  } catch (e) {
    toast(e.message, 'err');
  }
}

// ---------------------------------------------------------------- navigator, title, status

function renderWorkspace() {
  const ws = S.ws;
  $('#ws-name').textContent = ws.name;
  $('#ws-path').textContent = ws.memory ? 'nothing is saved to disk' : ws.path;
  $('#chip-order').textContent = `B+ order ${ws.order}`;
  $('#chip-pool').textContent = `pool ${ws.pool.capacity} × 4 KB`;
  $('#sb-ws').textContent = ws.memory ? 'In-memory session' : ws.path;
  $('#sb-tables').textContent = plural(ws.tables.length, 'table');
  updatePool(ws.pool);
  renderNavigator();
}

function updatePool(p) {
  const total = p.hits + p.misses;
  $('#sb-pool').textContent = `Buffer pool ${p.used}/${p.capacity}` + (total ? ` · ${((100 * p.hits) / total).toFixed(1)}% hits` : '');
}

function renderNavigator() {
  const ws = S.ws;
  const cur = activeTab();

  $('#nav-tables').innerHTML = ws.tables.length
    ? ws.tables.map((t) => {
        const open = S.expanded.has(t.name);
        let html = `<div class="tree-row ${open ? 'open' : ''}" data-table="${esc(t.name)}" title="${t.rows} rows on ${t.pages} page(s), ${t.slotSize}-byte slots">
          <span class="chev">${icon('chevron')}</span><span class="ico-table">${icon('table')}</span>
          <span class="name">${esc(t.name)}</span>
          <span class="acts"><button class="mini" data-select-rows title="SELECT * FROM ${esc(t.name)}">${icon('play')}</button>
          <button class="mini" data-inspect title="Show its B+ tree, indexes and pages">${icon('layers')}</button></span>
          <span class="meta">${t.rows}</span></div>`;
        if (open) {
          html += '<div class="tree-kids">';
          for (const c of t.columns) {
            const mark = c.pk ? `<span class="key" title="primary key, B+ tree">${icon('key')}</span>`
              : c.index ? `<span class="idx" title="${c.index} index">${icon('diamond')}</span>` : '<span class="blank"></span>';
            html += `<div class="tree-row col-row">${mark}<span class="name">${esc(c.name)}</span><span class="type">${c.type}</span></div>`;
          }
          for (const ix of t.indexes)
            html += `<div class="tree-row col-row" title="${ix.kind} on ${esc(ix.column)}: ${ix.keys} keys, height ${ix.height}">
              <span class="idx">${icon('layers')}</span><span class="name">${esc(ix.name)}</span>
              <span class="type">${ix.kind === 'B+ Tree' ? 'B+' : 'AVL'} · h${ix.height}</span></div>`;
          html += '</div>';
        }
        return html;
      }).join('')
    : '<div class="nav-empty">No tables yet. Try CREATE TABLE.</div>';

  $('#nav-scripts').innerHTML = ws.memory
    ? '<div class="nav-empty">Scripts can\'t be saved in an in-memory session.</div>'
    : ws.scripts.length
      ? ws.scripts.map((name) => `<div class="tree-row ${cur && cur.file === name ? 'active' : ''}" data-script="${esc(name)}">
          <span class="ico-file">${icon('file')}</span><span class="name">${esc(name)}</span>
          <span class="acts"><button class="mini" data-del-script title="Delete ${esc(name)}">${icon('trash')}</button></span></div>`).join('')
      : '<div class="nav-empty">No .sql files in this folder.</div>';

  $('#nav-files').innerHTML = ws.memory
    ? '<div class="nav-empty">Pages live in RAM in this session.</div>'
    : ws.files.length
      ? ws.files.map((f) => `<div class="tree-row" title="data/${esc(f.name)}${f.name.endsWith('.tbl') ? ` (${f.size / 4096} pages of 4 KB)` : ''}">
          <span class="ico-disk">${icon('disk')}</span><span class="name">${esc(f.name)}</span><span class="meta">${fmtSize(f.size)}</span></div>`).join('')
      : '<div class="nav-empty">data/ is empty until you create a table.</div>';
}

// ---------------------------------------------------------------- editor tabs

function newTab(text = '', name = null, file = null) {
  const tab = { id: ++tabSeq, name: name || `Query ${++untitled}`, file, text, saved: file ? text : '', view: null };
  S.tabs.push(tab);
  switchTab(tab.id);
  return tab;
}

async function openScript(name) {
  const open = S.tabs.find((t) => t.file === name);
  if (open) return switchTab(open.id);
  try {
    newTab(await api.readScript(name), name, name);
  } catch (e) {
    toast(e.message, 'err');
  }
}

function switchTab(id) {
  const old = activeTab();
  if (old) {
    old.text = editor.value;
    old.view = editor.getView();
  }
  S.active = id;
  const tab = activeTab();
  editor.value = tab.text;
  editor.setView(tab.view);
  renderTabs();
  if (S.ws) renderNavigator();
  editor.focus();
}

async function closeTab(id) {
  const tab = S.tabs.find((t) => t.id === id);
  if (!tab) return;
  if (isDirty(tab) && (tab.file || tab.text.trim()) &&
      !(await confirmDialog('Close tab', `<b>${esc(tab.name)}</b> has unsaved changes. Close it anyway?`, 'Close', true))) return;
  const i = S.tabs.indexOf(tab);
  S.tabs.splice(i, 1);
  if (S.active === id) {
    S.active = null;
    if (S.tabs.length) switchTab(S.tabs[Math.max(0, i - 1)].id);
    else newTab();
  } else {
    renderTabs();
  }
}

function renderTabs() {
  $('#editor-tabs').innerHTML =
    S.tabs.map((t) => `<div class="etab ${t.id === S.active ? 'active' : ''} ${isDirty(t) ? 'dirty' : ''}" data-tab="${t.id}" title="${esc(t.file || 'unsaved script')}">
        <span data-icon="file"></span><span>${esc(t.name)}</span>
        <button class="close" data-close-tab title="Close">${icon('x')}</button></div>`).join('') +
    `<button class="etab-add" data-action="new-script" title="New script">+</button>`;
  paintIcons($('#editor-tabs'));
}

function onEditorChange() {
  const tab = activeTab();
  if (!tab) return;
  const wasDirty = isDirty(tab);
  tab.text = editor.value;
  if (wasDirty !== isDirty(tab)) renderTabs();
}

async function saveTab(tab, asNew) {
  if (!tab) return;
  if (S.ws.memory) return toast('This is an in-memory session. Open or create a folder to save scripts.', 'err');
  tab.text = editor.value;
  let name = tab.file;
  if (!name || asNew) {
    const suggestion = tab.file || tab.name.toLowerCase().replace(/[^a-z0-9_.-]+/g, '_').replace(/\.sql$/, '') + '.sql';
    name = await promptDialog(asNew ? 'Save script as' : 'Save script', 'File name (saved in the workspace folder)', suggestion, 'Save');
    if (!name) return;
    if (!name.toLowerCase().endsWith('.sql')) name += '.sql';
  }
  try {
    S.ws = await api.saveScript(name, tab.text);
    tab.file = name;
    tab.name = name;
    tab.saved = tab.text;
    renderTabs();
    renderWorkspace();
    toast(`Saved ${name}`, 'ok');
  } catch (e) {
    toast(e.message, 'err');
  }
}

async function deleteScript(name) {
  if (!(await confirmDialog('Delete script', `Delete <b>${esc(name)}</b> from the workspace folder? This can't be undone.`, 'Delete', true))) return;
  try {
    S.ws = await api.deleteScript(name);
    for (const t of S.tabs) if (t.file === name) { t.file = null; t.saved = ''; }
    renderTabs();
    renderWorkspace();
  } catch (e) {
    toast(e.message, 'err');
  }
}

async function openSample() {
  try {
    newTab(await api.sample(), 'sample.sql');
  } catch (e) {
    toast(e.message, 'err');
  }
}

// ---------------------------------------------------------------- running SQL

function setBusy(on) {
  S.busy = on;
  $$('[data-action="run-all"], [data-action="run-statement"]').forEach((b) => b.classList.toggle('busy', on));
}

// mode 'all' runs the selection (or everything), 'statement' the one under
// the cursor; `sql` runs a given string without touching the editor
async function run(mode, sql) {
  if (S.busy || !S.ws) return;
  let offset = -1;
  if (sql == null) {
    const sel = editor.selection();
    if (mode === 'all') {
      const useSel = hasSql(sel.text);
      sql = useSel ? sel.text : editor.value;
      offset = useSel ? sel.start : 0;
    } else {
      const st = editor.statementAtCursor();
      if (!st) return toast('Nothing to run here');
      sql = st.text;
      offset = st.start;
    }
    if (!hasSql(sql)) return toast('Nothing to run: the editor is empty');
    editor.clearError();
    editor.flash(offset, offset + sql.length);
  }

  setBusy(true);
  try {
    // tell the server which editor line the text starts on, so every line
    // number it reports (and every error) matches what you see
    const fromEditor = offset >= 0;
    const data = await api.query(sql, $('#row-limit').value, fromEditor ? editor.lineOf(offset) + 1 : 1);
    const now = new Date();
    for (const r of data.results) {
      r.n = ++runSeq;
      r.time = now;
      r.absLine = fromEditor ? r.line : null;
      r.absErrLine = fromEditor && r.errorLine ? r.errorLine : null;
    }
    S.runs.push(...data.results);
    if (S.runs.length > 500) S.runs.splice(0, S.runs.length - 500);
    S.results = data.results.filter((r) => r.ok && r.columns.length);

    const failed = data.results.find((r) => !r.ok);
    S.selected = failed || data.results[data.results.length - 1] || null;
    S.view = failed || !S.results.length ? 'output' : 'r' + (S.results.length - 1);
    if (failed && failed.absErrLine) editor.markError(failed.absErrLine, failed.message);

    const total = data.results.reduce((a, r) => a + r.ms, 0);
    $('#sb-last').textContent = `${plural(data.results.length, 'statement')} · ${fmtMs(total)}`;
    if (failed) toast(failed.message, 'err');

    if (data.results.some((r) => r.changed)) await refreshState();
    else updatePool(data.pool);
    renderResults();
    renderExplain();
    if (S.insp === 'structures') renderStructures();
  } catch (e) {
    toast(e.message, 'err');
  } finally {
    setBusy(false);
  }
}

// ---------------------------------------------------------------- results panel

function renderResults() {
  const tabs = S.results.map((r, i) => `<button class="rtab ${S.view === 'r' + i ? 'active' : ''}" data-view="r${i}" title="${esc(r.sql)}">
      ${icon('table')}Result ${i + 1}<span class="count">${r.rowCount}</span></button>`);
  tabs.push(`<button class="rtab ${S.view === 'output' ? 'active' : ''}" data-view="output">
      ${icon('terminal')}Output<span class="count">${S.runs.length}</span></button>`);
  $('#results-tabs').innerHTML = tabs.join('');

  const body = $('#results-body');
  if (S.view === 'output') {
    body.innerHTML = renderLog();
    body.scrollTop = body.scrollHeight;
  } else {
    const r = S.results[+S.view.slice(1)];
    body.innerHTML = r ? renderGrid(r) : '';
    body.scrollTop = 0;
  }
}

function cell(v, type) {
  if (type === 'INT') return `<td class="num">${v}</td>`;
  if (type === 'FLOAT') return `<td class="num">${Number.isInteger(v) ? v.toFixed(1) : v}</td>`;
  if (type === 'BOOL') return `<td class="bool">${v ? 'TRUE' : 'FALSE'}</td>`;
  return `<td title="${esc(v)}">${esc(v)}</td>`;
}

function renderGrid(r) {
  const head = `<tr><th class="rn">#</th>${r.columns.map((c) => `<th>${esc(c.name)}<small>${c.type}</small></th>`).join('')}</tr>`;
  const rows = r.rows.map((row, i) => `<tr><td class="rn">${i + 1}</td>${row.map((v, k) => cell(v, r.columns[k].type)).join('')}</tr>`).join('');
  const shown = r.rows.length < r.rowCount ? ` · showing the first ${r.rows.length} (change Limit in the toolbar)` : '';
  const empty = r.rowCount ? '' : `<div class="empty-state" style="height:auto;padding:28px"><span><b>No rows matched</b>The query ran fine, nothing satisfied it.</span></div>`;
  return `<table class="grid"><thead>${head}</thead><tbody>${rows}</tbody></table>${empty}
    <div class="grid-foot">${plural(r.rowCount, 'row')} · ${fmtMs(r.ms)}${r.plan ? ' · ' + esc(r.plan) : ''}${shown}</div>`;
}

function renderLog() {
  if (!S.runs.length)
    return `<div class="empty-state"><span><b>Nothing run yet</b>
      Press <kbd>Ctrl</kbd>+<kbd>Enter</kbd> to run the statement under the cursor, or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Enter</kbd> for the whole script.</span></div>`;
  const rows = S.runs.map((r) => `<tr class="${r.ok ? 'ok' : 'bad'} ${r === S.selected ? 'sel' : ''}" data-run="${r.n}">
      <td class="st">${icon(r.ok ? 'check' : 'alert')}</td>
      <td class="time">${r.n}</td>
      <td class="time">${r.time.toLocaleTimeString()}</td>
      <td class="sql" title="${esc(r.sql)}">${esc(r.sql.replace(/\s+/g, ' '))}</td>
      <td class="msg">${esc(r.message)}</td>
      <td class="ms">${fmtMs(r.ms)}</td></tr>`).join('');
  return `<table class="log"><thead><tr><th></th><th>#</th><th>Time</th><th>Statement</th><th>Message</th><th>Duration</th></tr></thead><tbody>${rows}</tbody></table>`;
}

function clearOutput() {
  S.runs = [];
  S.results = [];
  S.selected = null;
  S.view = 'output';
  renderResults();
  renderExplain();
}

// ---------------------------------------------------------------- inspector

function showInspector(tab, focusKey) {
  $('#main').classList.remove('no-insp');
  S.insp = tab;
  $$('.insp-tabs button').forEach((b) => b.classList.toggle('active', b.dataset.insp === tab));
  for (const name of ['explain', 'structures', 'learn']) $('#insp-' + name).classList.toggle('hidden', name !== tab);
  if (tab === 'structures') renderStructures();
  if (tab === 'learn') renderLearn(focusKey);
  if (tab === 'explain') renderExplain();
}

function renderExplain() {
  const el = $('#insp-explain');
  const r = S.selected;
  if (!r) {
    el.innerHTML = `<div class="sec-title">How a query runs</div>
      <p class="muted" style="margin:0 0 14px">Run any statement and this panel walks through it: the order the engine touched each
      data structure, what every step did and what it cost.</p>
      ${renderPipeline({ kind: '', ok: false, steps: [], stats: {} })}
      <div class="sec-title">Structures in this engine</div>
      <div class="ds-used">${DS_ORDER.map((k) => `<button class="ds-chip" data-learn="${k}" style="--c:${DS[k].color}"><b><span class="swatch"></span>${DS[k].name}</b><span>${DS[k].file}</span></button>`).join('')}</div>`;
    return;
  }

  const used = dsUsage(r);
  const tokens = r.tokens.map((t) => `<span class="tok ${t.t}">${esc(t.v)}<i>${t.t.toLowerCase()}</i></span>`).join('');
  const steps = r.steps.map((s, i) => {
    const d = DS[s.ds] || DS.sql;
    return `<li class="step" style="--c:${d.color}"><span class="n">${i + 1}</span>
      <div class="st-head">${dsBadge(s.ds, s.ds === 'sql' ? 'SQL' : d.name)}<b>${esc(s.title)}</b>${s.repeat > 1 ? `<span class="rep">×${s.repeat}</span>` : ''}</div>
      <p>${esc(s.detail)}</p></li>`;
  }).join('');

  el.innerHTML = `
    <div class="q-card">
      <div class="q-top"><span class="q-kind ${r.ok ? '' : 'err'}">${esc(r.kind)}</span>
        ${r.absLine ? `<span class="muted small">line ${r.absLine}</span>` : ''}<span class="q-ms">${fmtMs(r.ms)}</span></div>
      <pre>${highlightSql(r.sql)}</pre>
      ${r.plan ? `<div class="q-plan"><span class="muted">Plan</span><b>${esc(r.plan)}</b></div>` : ''}
      <div class="q-msg ${r.ok ? '' : 'err'}">${esc(r.message)}</div>
    </div>

    <div class="sec-title">Pipeline</div>
    ${renderPipeline(r)}

    ${used.length ? `<div class="sec-title">Data structures used<span class="count">${used.length}</span></div>
    <div class="ds-used">${used.map((u) => `<button class="ds-chip" data-learn="${u.key}" style="--c:${DS[u.key].color}" title="What is a ${DS[u.key].name}?">
      <b><span class="swatch"></span>${DS[u.key].name}</b><span>${u.text}</span></button>`).join('')}</div>` : ''}

    ${steps ? `<div class="sec-title">Step by step<span class="count">${r.steps.length} steps</span></div><ol class="steps">${steps}</ol>` : ''}

    ${tokens ? `<div class="sec-title">Token queue<span class="count">${r.tokens.length} tokens, FIFO</span></div><div class="tokens">${tokens}</div>` : ''}

    <div class="sec-title">Counters for this statement</div>
    ${renderCounters(r.stats)}`;
}

let inspectSeq = 0;
async function renderStructures() {
  const el = $('#insp-structures');
  const tables = S.ws ? S.ws.tables : [];
  if (!tables.find((t) => t.name === S.structTable)) S.structTable = tables.length ? tables[0].name : '';
  const seq = ++inspectSeq;
  let data;
  try {
    data = await api.inspect(S.structTable);
  } catch (e) {
    el.innerHTML = `<p class="muted">${esc(e.message)}</p>`;
    return;
  }
  if (seq !== inspectSeq) return;   // a newer request is on its way

  let html = `<div class="struct-head">
      <select data-struct-table>${tables.map((t) => `<option ${t.name === S.structTable ? 'selected' : ''}>${esc(t.name)}</option>`).join('') || '<option>no tables</option>'}</select>
      <button class="btn sm" data-struct-refresh title="Refresh">${icon('refresh')}</button></div>`;

  const t = data.table;
  if (t) {
    html += `<div class="sec-title">${dsBadge('bptree')} Primary key${t.pk ? ' on ' + esc(t.pk.column) : ''}</div>`;
    html += t.pk ? `<div class="struct-box">${renderBPlus(t.pk.tree)}</div>`
      : '<p class="muted small">This table has no primary key, so it has no B+ tree.</p>';
    for (const ix of t.indexes)
      html += `<div class="sec-title">${dsBadge('avl')} ${esc(ix.name)} on ${esc(ix.column)}</div><div class="struct-box">${renderAVL(ix.tree)}</div>`;
    if (!t.indexes.length)
      html += `<div class="sec-title">${dsBadge('avl')} Secondary indexes</div><p class="muted small">None yet. <code>CREATE INDEX idx ON ${esc(t.name)}(column);</code> builds an AVL tree.</p>`;
    html += `<div class="sec-title">${dsBadge('disk')} Page directory</div><div class="struct-box">${renderPages(t.pages, t.slotsPerPage)}</div>`;
  } else {
    html += '<p class="muted">No tables yet. Create one and its B+ tree, indexes and pages show up here.</p>';
  }
  html += `<div class="sec-title">${dsBadge('pool')} Buffer pool · LRU list</div><div class="struct-box">${renderPool(data.pool)}</div>`;
  html += `<div class="sec-title">${dsBadge('hash')} Catalog hash table</div><div class="struct-box">${renderBuckets(data.catalog.buckets)}</div>`;
  el.innerHTML = html;
  settleTrees(el);
}

function learnCard(key, withTry = true) {
  const d = DS[key];
  return `<div class="learn-card" data-card="${key}" style="--c:${d.color}">
    <h3><span class="swatch"></span>${d.name}<code>${d.file}</code></h3>
    <div class="role">${d.role}</div>
    <p>${d.how}</p>
    <table>${d.cost.map(([op, c]) => `<tr><td>${op}</td><td>${c}</td></tr>`).join('')}</table>
    ${withTry ? `<div class="try"><code>${esc(d.try)}</code><button class="btn sm" data-try="${key}">Insert</button></div>` : ''}
  </div>`;
}

function renderLearn(focusKey) {
  const el = $('#insp-learn');
  el.innerHTML = `<p class="muted small" style="margin:0 0 12px">Every structure below is written by hand in C++.
    No std::vector, std::map or std::list anywhere; <code>make audit</code> checks that.</p>` +
    [...DS_ORDER, 'sql'].map((k) => learnCard(k)).join('');
  if (focusKey) {
    const card = el.querySelector(`[data-card="${focusKey}"]`);
    if (card) {
      card.classList.add('focus');
      card.scrollIntoView({ block: 'start', behavior: 'smooth' });
    }
  }
}

// put a snippet on a fresh line at the end of the editor, cursor on it
function insertSnippet(sql) {
  const v = editor.value;
  editor.ta.focus();
  editor.ta.setSelectionRange(v.length, v.length);
  editor.insert((v && !v.endsWith('\n') ? '\n' : '') + (v ? '\n' : '') + sql);
  toast('Inserted. Press Ctrl+Enter to run it.');
}

// ---------------------------------------------------------------- wiring

const ACTIONS = {
  'new-folder': () => folderDialog('create'),
  'open-folder': () => folderDialog('open'),
  'memory': openMemory,
  'close-folder': closeFolder,
  'new-script': () => newTab(),
  'save': () => saveTab(activeTab(), false),
  'save-as': () => saveTab(activeTab(), true),
  'run-all': () => run('all'),
  'run-statement': () => run('statement'),
  'sample': openSample,
  'clear-output': clearOutput,
  'toggle-nav': () => $('#main').classList.toggle('no-nav'),
  'toggle-inspector': () => $('#main').classList.toggle('no-insp'),
  'toggle-theme': toggleTheme,
  'shortcuts': showShortcuts,
  'learn': () => showInspector('learn'),
  'refresh': refreshState,
};

function closeMenus() { $$('.menu.open').forEach((m) => m.classList.remove('open')); }

function bindEvents() {
  document.addEventListener('click', (e) => {
    const t = e.target;
    const action = t.closest('[data-action]');
    if (action && ACTIONS[action.dataset.action]) {
      closeMenus();
      ACTIONS[action.dataset.action]();
      return;
    }
    if (!t.closest('.menu')) closeMenus();
    if (t.closest('.theme-toggle')) return toggleTheme();

    // welcome page
    const forget = t.closest('[data-forget]');
    if (forget) {
      const path = forget.closest('li').dataset.path;
      setRecent(getRecent().filter((r) => r.path !== path));
      return renderRecent();
    }
    const recent = t.closest('#recent-list li');
    if (recent) return openFolder(recent.dataset.path);
    const card = t.closest('.ds-card');
    if (card) return showDsModal(card.dataset.ds);

    // navigator
    const tableRow = t.closest('[data-table]');
    if (tableRow) {
      const name = tableRow.dataset.table;
      if (t.closest('[data-select-rows]')) return run('all', `SELECT * FROM ${name};`);
      if (t.closest('[data-inspect]')) {
        S.structTable = name;
        return showInspector('structures');
      }
      S.expanded.has(name) ? S.expanded.delete(name) : S.expanded.add(name);
      return renderNavigator();
    }
    const script = t.closest('[data-script]');
    if (script) {
      if (t.closest('[data-del-script]')) return deleteScript(script.dataset.script);
      return openScript(script.dataset.script);
    }

    // editor tabs
    const etab = t.closest('[data-tab]');
    if (etab) {
      const id = +etab.dataset.tab;
      return t.closest('[data-close-tab]') ? closeTab(id) : switchTab(id);
    }

    // results + output
    const view = t.closest('[data-view]');
    if (view) {
      S.view = view.dataset.view;
      if (S.view !== 'output') S.selected = S.results[+S.view.slice(1)];
      renderResults();
      return renderExplain();
    }
    const runRow = t.closest('[data-run]');
    if (runRow) {
      S.selected = S.runs.find((r) => r.n === +runRow.dataset.run);
      $$('.log tr.sel').forEach((r) => r.classList.remove('sel'));
      runRow.classList.add('sel');
      return showInspector('explain');
    }

    // inspector
    const itab = t.closest('[data-insp]');
    if (itab) return showInspector(itab.dataset.insp);
    const learn = t.closest('[data-learn]');
    if (learn) return showInspector('learn', learn.dataset.learn);
    const tryBtn = t.closest('[data-try]');
    if (tryBtn) return insertSnippet(DS[tryBtn.dataset.try].try);
    if (t.closest('[data-struct-refresh]')) return renderStructures();
  });

  document.addEventListener('change', (e) => {
    if (e.target.matches('[data-struct-table]')) {
      S.structTable = e.target.value;
      renderStructures();
    }
  });

  // menus open on click, then follow the mouse like a desktop menubar
  $$('.menu-btn').forEach((btn) => {
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      const menu = btn.parentElement;
      const wasOpen = menu.classList.contains('open');
      closeMenus();
      if (!wasOpen) menu.classList.add('open');
    });
    btn.addEventListener('mouseenter', () => {
      if ($('.menu.open') && !btn.parentElement.classList.contains('open')) {
        closeMenus();
        btn.parentElement.classList.add('open');
      }
    });
  });

  window.addEventListener('keydown', (e) => {
    if ($('#workbench').classList.contains('hidden') || $('.modal-back')) return;
    const mod = e.ctrlKey || e.metaKey;
    const key = e.key.toLowerCase();
    if (mod && e.key === 'Enter') {
      e.preventDefault();
      run(e.shiftKey ? 'all' : 'statement');
    } else if (mod && key === 's') {
      e.preventDefault();
      saveTab(activeTab(), e.shiftKey);
    } else if (e.altKey && key === 'n') {
      e.preventDefault();
      newTab();
    } else if (e.key === 'F5') {
      e.preventDefault();
      run('all');
    }
  });

  window.addEventListener('beforeunload', (e) => {
    if (S.tabs.some((t) => isDirty(t) && t.file)) {
      e.preventDefault();
      e.returnValue = '';
    }
  });

  // drag handles between panels
  $$('[data-resize]').forEach((bar) => bar.addEventListener('mousedown', (e) => {
    e.preventDefault();
    const kind = bar.dataset.resize;
    const style = document.documentElement.style;
    const x0 = e.clientX;
    const y0 = e.clientY;
    const nav = $('#navigator').offsetWidth;
    const insp = $('#inspector').offsetWidth;
    const res = $('.results').offsetHeight;
    bar.classList.add('drag');
    document.body.style.cursor = kind === 'results' ? 'row-resize' : 'col-resize';
    const move = (ev) => {
      if (kind === 'nav') style.setProperty('--nav-w', clamp(nav + ev.clientX - x0, 170, 520) + 'px');
      if (kind === 'inspector') style.setProperty('--insp-w', clamp(insp - (ev.clientX - x0), 280, 760) + 'px');
      if (kind === 'results') style.setProperty('--res-h', clamp(res - (ev.clientY - y0), 90, window.innerHeight - 240) + 'px');
    };
    const up = () => {
      bar.classList.remove('drag');
      document.body.style.cursor = '';
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
  }));
}

async function init() {
  paintIcons();
  paintThemeButtons();
  editor = new SqlEditor($('#editor-host'));
  editor.onChange = onEditorChange;
  editor.onCursor = (line, col) => { $('#sb-cursor').textContent = `Ln ${line}, Col ${col}`; };
  bindEvents();
  renderDsCards();
  renderRecent();
  renderExplain();
  try {
    const st = await api.state();
    if (st.open) enterWorkbench(st, true);
    else showWelcome();
  } catch (e) {
    showWelcome();
    toast(e.message, 'err');
  }
}

init();
