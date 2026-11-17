// SQL editor. A transparent <textarea> sits exactly on top of a <pre> that
// holds a syntax-highlighted copy of the same text: you type into the
// textarea, the pre just mirrors it. No editor library involved.

const LINE_H = 21;   // keep in sync with --line-h in style.css
const PAD_TOP = 10;

const SQL_KEYWORDS = new Set((
  'SELECT FROM WHERE INSERT INTO VALUES CREATE TABLE INDEX ON DROP DELETE UPDATE SET AND OR NOT ' +
  'JOIN INNER PRIMARY KEY ORDER BY ASC DESC LIMIT GROUP AS BETWEEN LIKE TRUE FALSE SHOW TABLES DESCRIBE NULL IF EXISTS'
).split(' '));
const SQL_TYPES = new Set('INT INTEGER BIGINT FLOAT REAL DOUBLE VARCHAR CHAR TEXT BOOL BOOLEAN'.split(' '));
const SQL_FUNCS = new Set('COUNT SUM AVG MIN MAX'.split(' '));

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function highlightSql(src) {
  // comment | string | number | word | anything else that isn't space
  const re = /(--[^\n]*|\/\*[\s\S]*?(?:\*\/|$))|('(?:[^']|'')*'?)|(\b\d+(?:\.\d+)?\b)|([A-Za-z_][A-Za-z0-9_]*)|([^\sA-Za-z0-9_'])/g;
  let out = '';
  let last = 0;
  let m;
  while ((m = re.exec(src))) {
    out += escapeHtml(src.slice(last, m.index));
    const tok = m[0];
    let cls = '';
    if (m[1]) cls = 'c';
    else if (m[2]) cls = 's';
    else if (m[3]) cls = 'n';
    else if (m[4]) {
      const up = tok.toUpperCase();
      cls = SQL_KEYWORDS.has(up) ? 'k' : SQL_TYPES.has(up) ? 't' : SQL_FUNCS.has(up) ? 'f' : '';
    } else cls = 'o';
    out += cls ? `<span class="tk-${cls}">${escapeHtml(tok)}</span>` : escapeHtml(tok);
    last = re.lastIndex;
  }
  return out + escapeHtml(src.slice(last));
}

// true if the text has something other than whitespace and comments
function hasSql(text) {
  return text.replace(/--[^\n]*/g, '').replace(/\/\*[\s\S]*?(\*\/|$)/g, '').trim().length > 0;
}

// [{start, end}] for each statement, splitting on ';' outside strings and comments
function splitStatements(text) {
  const out = [];
  let start = 0;
  let i = 0;
  while (i < text.length) {
    const c = text[i];
    if (c === "'") {
      i++;
      while (i < text.length) {
        if (text[i] === "'") {
          if (text[i + 1] === "'") { i += 2; continue; }
          break;
        }
        i++;
      }
    } else if (c === '-' && text[i + 1] === '-') {
      while (i < text.length && text[i] !== '\n') i++;
      continue;
    } else if (c === '/' && text[i + 1] === '*') {
      const e = text.indexOf('*/', i + 2);
      i = e < 0 ? text.length : e + 2;
      continue;
    } else if (c === ';') {
      out.push({ start, end: i + 1 });
      start = i + 1;
    }
    i++;
  }
  if (start < text.length) out.push({ start, end: text.length });
  return out;
}

class SqlEditor {
  constructor(host) {
    host.innerHTML = `
      <div class="editor">
        <div class="gutter"><div class="gutter-lines"></div></div>
        <div class="code-wrap">
          <div class="active-line"></div>
          <pre class="code-hl" aria-hidden="true"></pre>
          <textarea class="code-input" spellcheck="false" autocapitalize="off" autocomplete="off" wrap="off"></textarea>
        </div>
      </div>`;
    this.ta = host.querySelector('.code-input');
    this.pre = host.querySelector('.code-hl');
    this.lines = host.querySelector('.gutter-lines');
    this.gutter = host.querySelector('.gutter');
    this.activeLine = host.querySelector('.active-line');
    this.wrap = host.querySelector('.code-wrap');
    this.lineCount = 0;
    this.curLine = 1;
    this.errLine = 0;
    this.onChange = () => {};
    this.onCursor = () => {};

    this.ta.addEventListener('input', () => { this.render(); this.onChange(); });
    this.ta.addEventListener('scroll', () => this.syncScroll());
    for (const ev of ['keyup', 'mouseup', 'select', 'focus']) this.ta.addEventListener(ev, () => this.cursorMoved());
    this.ta.addEventListener('keydown', (e) => this.keydown(e));
    this.render();
  }

  get value() { return this.ta.value; }
  set value(v) {
    this.ta.value = v;
    this.errLine = 0;
    this.render();
  }

  focus() { this.ta.focus(); }

  render() {
    const text = this.ta.value;
    this.pre.innerHTML = highlightSql(text) + '\n';
    const count = text.split('\n').length;
    if (count !== this.lineCount) {
      let html = '';
      for (let i = 1; i <= count; i++) html += `<div>${i}</div>`;
      this.lines.innerHTML = html;
      this.lineCount = count;
    }
    this.markLines();
    this.syncScroll();
    this.cursorMoved();
  }

  syncScroll() {
    this.pre.scrollTop = this.ta.scrollTop;
    this.pre.scrollLeft = this.ta.scrollLeft;
    this.gutter.scrollTop = this.ta.scrollTop;
    this.activeLine.style.top = PAD_TOP + (this.curLine - 1) * LINE_H - this.ta.scrollTop + 'px';
  }

  cursorMoved() {
    const before = this.ta.value.slice(0, this.ta.selectionStart);
    const line = before.split('\n').length;
    const col = before.length - before.lastIndexOf('\n');
    if (line !== this.curLine) {
      this.curLine = line;
      this.markLines();
      this.syncScroll();
    }
    this.onCursor(line, col);
  }

  markLines() {
    const kids = this.lines.children;
    for (let i = 0; i < kids.length; i++) {
      kids[i].className = i + 1 === this.errLine ? 'err' : i + 1 === this.curLine ? 'cur' : '';
    }
  }

  markError(line, message) {
    this.errLine = line;
    this.markLines();
    const el = this.lines.children[line - 1];
    if (el) el.title = message || '';
  }

  clearError() {
    this.errLine = 0;
    this.markLines();
  }

  // replace the selection, keeping the browser's undo history when possible
  insert(text) {
    this.ta.focus();
    if (!document.execCommand('insertText', false, text)) {
      this.ta.setRangeText(text, this.ta.selectionStart, this.ta.selectionEnd, 'end');
      this.render();
      this.onChange();
    }
  }

  keydown(e) {
    const ta = this.ta;
    if (e.key === 'Tab' && !e.ctrlKey && !e.altKey) {
      e.preventDefault();
      if (e.shiftKey) this.outdent();
      else this.insert('  ');
    } else if (e.key === 'Enter' && !e.ctrlKey && !e.metaKey && !e.shiftKey && !e.altKey) {
      // keep the current line's indentation
      e.preventDefault();
      const v = ta.value;
      const lineStart = v.lastIndexOf('\n', ta.selectionStart - 1) + 1;
      const indent = /^[ \t]*/.exec(v.slice(lineStart))[0];
      this.insert('\n' + indent);
    } else if ((e.ctrlKey || e.metaKey) && e.key === '/') {
      e.preventDefault();
      this.toggleComment();
    }
  }

  outdent() {
    const ta = this.ta;
    const v = ta.value;
    const lineStart = v.lastIndexOf('\n', ta.selectionStart - 1) + 1;
    const spaces = /^ {1,2}/.exec(v.slice(lineStart));
    if (!spaces) return;
    const pos = ta.selectionStart;
    ta.setSelectionRange(lineStart, lineStart + spaces[0].length);
    this.insert('');
    const back = Math.max(lineStart, pos - spaces[0].length);
    ta.setSelectionRange(back, back);
  }

  toggleComment() {
    const ta = this.ta;
    const v = ta.value;
    const s = ta.selectionStart;
    const e = ta.selectionEnd;
    const from = v.lastIndexOf('\n', s - 1) + 1;
    let to = v.indexOf('\n', e > s ? e - 1 : e);
    if (to < 0) to = v.length;
    const lines = v.slice(from, to).split('\n');
    const commented = lines.filter((l) => l.trim()).every((l) => /^\s*--/.test(l));
    const out = lines
      .map((l) => (commented ? l.replace(/^(\s*)-- ?/, '$1') : l.trim() ? l.replace(/^(\s*)/, '$1-- ') : l))
      .join('\n');
    ta.setSelectionRange(from, to);
    this.insert(out);
    ta.setSelectionRange(from, from + out.length);
  }

  selection() {
    const { selectionStart: start, selectionEnd: end, value } = this.ta;
    return { start, end, text: value.slice(start, end) };
  }

  // the statement under the cursor (or the one just before it)
  statementAtCursor() {
    const v = this.ta.value;
    const pos = this.ta.selectionStart;
    const parts = splitStatements(v).filter((p) => hasSql(v.slice(p.start, p.end)));
    if (!parts.length) return null;
    let pick = parts[0];
    for (const p of parts) if (p.start <= pos) pick = p;
    for (const p of parts) if (pos >= p.start && pos <= p.end) pick = p;
    return { start: pick.start, end: pick.end, text: v.slice(pick.start, pick.end) };
  }

  // 0-based line number of a character offset
  lineOf(offset) {
    let n = 0;
    const v = this.ta.value;
    for (let i = 0; i < offset && i < v.length; i++) if (v[i] === '\n') n++;
    return n;
  }

  // briefly highlight the lines that were just sent to the server
  flash(start, end) {
    const v = this.ta.value;
    let a = start;
    while (a < end && /\s/.test(v[a])) a++;
    const first = this.lineOf(a);
    const last = this.lineOf(Math.max(a, end - 1));
    const el = document.createElement('div');
    el.className = 'flash-line';
    el.style.top = PAD_TOP + first * LINE_H - this.ta.scrollTop + 'px';
    el.style.height = (last - first + 1) * LINE_H + 'px';
    this.wrap.appendChild(el);
    setTimeout(() => el.remove(), 800);
  }

  getView() {
    return { scrollTop: this.ta.scrollTop, scrollLeft: this.ta.scrollLeft, start: this.ta.selectionStart, end: this.ta.selectionEnd };
  }

  setView(v) {
    if (!v) return;
    this.ta.setSelectionRange(v.start, v.end);
    this.ta.scrollTop = v.scrollTop;
    this.ta.scrollLeft = v.scrollLeft;
    this.cursorMoved();
    this.syncScroll();
  }
}
