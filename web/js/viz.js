// Pictures for the side panel: the query pipeline, B+ tree levels, the AVL
// tree, the page directory, the buffer pool's LRU list and catalog buckets.

const esc = escapeHtml;

// ---------------------------------------------------------------- explain

function renderPipeline(r) {
  const s = { ...ZERO_STATS, ...r.stats };
  const tags = new Set(r.steps.map((x) => x.ds));
  const catalog = r.steps.some((x) => x.ds === 'hash' && x.title.startsWith('Catalog'));
  const idxVisits = s.bptVisits + s.avlVisits;
  const idxKey = s.avlVisits > s.bptVisits ? 'avl' : 'bptree';
  const touches = s.poolHits + s.poolMisses;
  const executes = ['SELECT', 'INSERT', 'UPDATE', 'DELETE'].includes(r.kind);

  const node = (side, key, title, sub, on) =>
    `<div class="pnode ${side} ${on ? 'on' : ''}" style="--c:${DS[key].color}"><b>${title}</b><small>${sub}</small></div>`;
  const arrow = (label) => `<div class="parrow">${label}</div>`;

  return `<div class="pipe">
    ${node('', 'sql', 'Workbench', 'SQL text', true)}
    ${arrow('text')}
    ${node('', 'queue', 'Tokenizer', `queue: ${s.queuePop} tokens`, s.queuePop > 0)}
    ${arrow('tokens')}
    ${node('', 'sql', 'Parser', 'recursive descent', r.kind !== 'ERROR')}
    ${arrow('command')}
    <div class="prow">
      ${node('side-l', 'hash', 'Catalog', 'hash table', catalog)}
      ${node('', 'stack', 'Executor', s.stackPush ? `stack: ${s.stackPush} pushes` : 'plan + run', executes || r.ok)}
      ${node('side-r', idxKey, 'Index', idxVisits ? `${idxVisits} node visits` : 'B+ / AVL', idxVisits > 0 || tags.has('bptree') || tags.has('avl'))}
    </div>
    ${arrow('page requests')}
    ${node('', 'pool', 'Buffer pool', touches ? `${s.poolHits} hit · ${s.poolMisses} miss` : 'hash + LRU list', touches > 0)}
    ${arrow('miss / write back')}
    ${node('', 'disk', 'Pages on disk', s.diskReads || s.diskWrites ? `${s.diskReads} read · ${s.diskWrites} written` : '4 KB pages', s.diskReads + s.diskWrites > 0 || tags.has('disk'))}
  </div>`;
}

// one chip per structure the statement actually used, with its counters
function dsUsage(r) {
  const s = r.stats;
  const parts = {};
  const join = (...xs) => xs.filter(Boolean).join(' · ');
  const rot = s.avlLL + s.avlRR + s.avlLR + s.avlRL;
  if (s.bptVisits || s.bptSplits || s.bptMerges)
    parts.bptree = join(`${s.bptVisits} visits`, s.bptSplits && `${s.bptSplits} splits`, s.bptMerges && `${s.bptMerges} merges`,
      s.bptBorrows && `${s.bptBorrows} borrows`, s.bptLeafHops && `${s.bptLeafHops} leaf hops`);
  if (s.avlVisits || rot) parts.avl = join(`${s.avlVisits} visits`, rot && `${rot} rotations`);
  if (s.hashProbes) parts.hash = join(`${s.hashProbes} probes`, s.hashRehashes && `${s.hashRehashes} resizes`);
  if (s.poolHits || s.poolMisses) parts.pool = join(`${s.poolHits} hits`, `${s.poolMisses} misses`, s.poolEvictions && `${s.poolEvictions} evicted`);
  if (s.listMoves) parts.list = `${s.listMoves} relinks`;
  if (s.arrayGrows) parts.array = `${s.arrayGrows} regrows`;
  if (s.stackPush) parts.stack = `${s.stackPush} push · ${s.stackPop} pop`;
  if (s.queuePop) parts.queue = `${s.queuePop} tokens out`;
  if (s.sortCompares) parts.sort = `${s.sortCompares} compares`;
  if (s.diskReads || s.diskWrites) parts.disk = join(`${s.diskReads} reads`, s.diskWrites && `${s.diskWrites} writes`);
  return DS_ORDER.filter((k) => parts[k]).map((k) => ({ key: k, text: parts[k] }));
}

const STAT_LABELS = {
  queuePush: 'queue pushes', queuePop: 'queue pops', stackPush: 'stack pushes', stackPop: 'stack pops',
  arrayGrows: 'DynArray regrows', listMoves: 'linked list relinks', hashProbes: 'hash chain probes',
  hashRehashes: 'hash table resizes', avlVisits: 'AVL node visits', avlLL: 'AVL LL rotations',
  avlRR: 'AVL RR rotations', avlLR: 'AVL LR rotations', avlRL: 'AVL RL rotations',
  bptVisits: 'B+ tree node visits', bptSplits: 'B+ tree splits', bptMerges: 'B+ tree merges',
  bptBorrows: 'B+ tree borrows', bptLeafHops: 'B+ tree leaf hops', poolHits: 'buffer pool hits',
  poolMisses: 'buffer pool misses', poolEvictions: 'buffer pool evictions', diskReads: 'disk page reads',
  diskWrites: 'disk page writes', sortCompares: 'merge sort comparisons',
};

const ZERO_STATS = Object.fromEntries(Object.keys(STAT_LABELS).map((k) => [k, 0]));

function renderCounters(stats) {
  const rows = Object.keys(STAT_LABELS)
    .filter((k) => stats[k])
    .map((k) => `<tr><td>${STAT_LABELS[k]}</td><td>${stats[k].toLocaleString()}</td></tr>`);
  return rows.length ? `<table class="kv">${rows.join('')}</table>` : '<p class="muted small">Nothing counted.</p>';
}

// ---------------------------------------------------------------- structures

function renderBPlus(t) {
  let html = `<div class="struct-meta">
    <span>order <b>${t.order}</b></span><span>height <b>${t.height}</b></span>
    <span>keys <b>${t.keys}</b></span><span>nodes <b>${t.nodes}</b></span></div>
    <div class="bpt-wrap">`;
  t.levels.forEach((lvl, i) => {
    const leaves = i === t.levels.length - 1;
    const tag = i === 0 ? 'root' : leaves ? 'leaves' : `level ${i}`;
    html += `<div class="bpt-level"><span class="lvl-tag">${tag}</span>`;
    lvl.nodes.forEach((n, k) => {
      if (k > 0 && leaves) html += '<span class="bpt-link">→</span>';
      const keys = n.keys.length ? n.keys.map((x) => `<span>${esc(x)}</span>`).join('') : '<span>empty</span>';
      const tip = n.leaf ? `keys ${n.keys.join(', ')}\nrow ids ${n.rids.join(', ')}` : `separators ${n.keys.join(', ')}`;
      html += `<div class="bpt-node ${n.leaf ? 'leaf' : ''} ${n.keys.length ? '' : 'empty'}" title="${esc(tip)}">${keys}</div>`;
    });
    if (lvl.total > lvl.nodes.length) html += `<span class="bpt-more">+${lvl.total - lvl.nodes.length} more</span>`;
    html += '</div>';
  });
  return html + '</div>';
}

function renderAVL(t) {
  let html = `<div class="struct-meta"><span>height <b>${t.height}</b></span>
    <span>distinct keys <b>${t.keys}</b></span><span>row ids <b>${t.entries}</b></span></div>`;
  if (!t.root) return html + '<div class="muted small">empty tree</div>';

  // x = in-order position, y = depth
  const nodes = [];
  let x = 0;
  let depth = 0;
  (function walk(n, d) {
    if (!n) return;
    walk(n.l, d + 1);
    n.x = x++;
    n.y = d;
    depth = Math.max(depth, d);
    nodes.push(n);
    walk(n.r, d + 1);
  })(t.root, 0);

  const W = 40, H = 50, R = 15, P = 22;
  const width = Math.max(nodes.length * W + P * 2, 240);
  const height = (depth + 1) * H + P * 2;
  const cx = (n) => P + n.x * W + W / 2;
  const cy = (n) => P + n.y * H + R;

  let edges = '';
  let dots = '';
  for (const n of nodes) {
    for (const c of [n.l, n.r]) if (c) edges += `<line x1="${cx(n)}" y1="${cy(n)}" x2="${cx(c)}" y2="${cy(c)}"/>`;
    const label = n.k.length > 5 ? n.k.slice(0, 4) + '…' : n.k;
    dots += `<g><title>key ${esc(n.k)}: ${n.rows} row id(s), subtree height ${n.h}</title>
      <circle cx="${cx(n)}" cy="${cy(n)}" r="${R}"/>
      <text x="${cx(n)}" y="${cy(n) + 4}" text-anchor="middle">${esc(label)}</text>
      <text class="h" x="${cx(n) + R + 2}" y="${cy(n) - R + 4}">${n.rows > 1 ? '×' + n.rows : ''}</text>
      ${n.cut ? `<text class="cut" x="${cx(n)}" y="${cy(n) + R + 13}" text-anchor="middle">⋯</text>` : ''}</g>`;
  }
  return html + `<svg class="avl-svg" data-root-x="${cx(t.root)}" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">${edges}${dots}</svg>`;
}

// after the pictures are in the DOM: centre small B+ trees, and scroll big
// AVL trees so the root is in view instead of the far-left leaves
function settleTrees(root) {
  root.querySelectorAll('.bpt-wrap').forEach((w) => {
    const box = w.closest('.struct-box');
    if (w.scrollWidth <= box.clientWidth) w.classList.add('fits');
  });
  root.querySelectorAll('.avl-svg').forEach((svg) => {
    const box = svg.closest('.struct-box');
    box.scrollLeft = Math.max(0, +svg.dataset.rootX - box.clientWidth / 2);
  });
}

function renderPages(pages, perPage) {
  if (!pages.length) return '<div class="muted small">No pages yet. The first INSERT allocates one.</div>';
  const shown = pages.slice(0, 240);
  const used = pages.reduce((a, b) => a + b, 0);
  let html = `<div class="struct-meta"><span>pages <b>${pages.length}</b></span>
    <span>slots/page <b>${perPage}</b></span><span>rows <b>${used}</b></span></div><div class="pages">`;
  shown.forEach((n, i) => {
    html += `<div class="page" title="page ${i}: ${n}/${perPage} slots used"><i style="height:${Math.round((100 * n) / perPage)}%"></i></div>`;
  });
  if (pages.length > shown.length) html += `<span class="bpt-more">+${pages.length - shown.length} more</span>`;
  return html + '</div>';
}

function renderPool(p) {
  const total = p.hits + p.misses;
  const rate = total ? ((100 * p.hits) / total).toFixed(1) + '%' : '-';
  let html = `<div class="pool-stats">
    <div><b>${p.used}/${p.capacity}</b><small>frames used</small></div>
    <div><b>${p.hits.toLocaleString()}</b><small>hits</small></div>
    <div><b>${p.misses.toLocaleString()}</b><small>misses</small></div>
    <div><b>${rate}</b><small>hit rate</small></div></div>`;
  if (!p.lru || !p.lru.length) return html + '<div class="muted small">No pages cached yet.</div>';
  html += '<div class="lru"><span class="lru-arrow">MRU</span>';
  p.lru.slice(0, 40).forEach((f, i) => {
    if (i) html += '<span class="lru-arrow">⇄</span>';
    html += `<span class="frame ${f.dirty ? 'dirty' : ''}" title="frame ${f.frame}${f.dirty ? ', dirty (modified, not yet written)' : ''}">${esc(f.table)}:p${f.page}</span>`;
  });
  if (p.lru.length > 40) html += `<span class="bpt-more">+${p.lru.length - 40}</span>`;
  return html + '<span class="lru-arrow">LRU (next victim)</span></div>';
}

function renderBuckets(buckets) {
  const filled = buckets.filter((b) => b.length).length;
  let html = `<div class="struct-meta"><span>buckets <b>${buckets.length}</b></span><span>used <b>${filled}</b></span></div><div class="buckets">`;
  buckets.forEach((chain, i) => {
    if (!chain.length && buckets.length > 16) return;   // keep big tables readable
    html += `<div class="bucket ${chain.length ? '' : 'empty'}"><span class="bi">${i}</span><span class="slot"></span>`;
    html += chain.map((name) => `<span class="lru-arrow">→</span><span class="ent">${esc(name)}</span>`).join('');
    html += '</div>';
  });
  if (buckets.length > 16) html += `<div class="muted small">${buckets.length - filled} empty buckets hidden</div>`;
  return html + '</div>';
}
