// What each data structure is, where it lives in the C++ code and what it
// costs. Shared by the welcome page, the Learn tab and the Explain panel.
// The keys match the `ds` tags the engine puts on its trace steps.

const DS = {
  bptree: {
    name: 'B+ Tree',
    color: '#2dd4bf',
    file: 'src/ds/BPlusTree.h',
    role: 'Primary key index: point lookups, ranges and ORDER BY on the key.',
    how: 'A wide, shallow tree. Internal nodes only hold separator keys that route the search; every key and its row id live in the leaves, and the leaves are linked left to right. A full node splits and pushes a separator up; a node that gets too empty after a delete borrows a key from a sibling or merges with it.',
    cost: [['search', 'O(log n)'], ['insert / delete', 'O(log n)'], ['range of k rows', 'O(log n + k)'], ['ordered scan', 'O(n), no sort']],
    try: 'SELECT * FROM students WHERE id BETWEEN 4 AND 9;',
  },
  avl: {
    name: 'AVL Tree',
    color: '#34d399',
    file: 'src/ds/AVLTree.h',
    role: 'Secondary indexes made with CREATE INDEX.',
    how: 'A binary search tree that checks the heights of both subtrees after every insert or delete. If they differ by more than one it rotates (LL, RR, LR or RL), which keeps the height under about 1.44 log2 n. The column may repeat, so each node keeps a list of the row ids that share its key.',
    cost: [['search', 'O(log n)'], ['insert / delete', 'O(log n)'], ['range of k keys', 'O(log n + k)']],
    try: 'CREATE INDEX idx_city ON students(city);',
  },
  hash: {
    name: 'Hash Table',
    color: '#fbbf24',
    file: 'src/ds/HashTable.h',
    role: 'Catalog (table name to table), buffer pool page table, hash joins, GROUP BY and SQL keywords.',
    how: 'An array of buckets. A key is hashed to a bucket and colliding keys are chained in a linked list there. Once the table is more than 3/4 full the bucket array doubles and every entry is re-linked, so chains stay short.',
    cost: [['find', 'O(1) average'], ['insert / delete', 'O(1) average'], ['resize', 'O(n), rare']],
    try: 'SELECT city, COUNT(*) FROM students GROUP BY city;',
  },
  pool: {
    name: 'Buffer Pool (LRU)',
    color: '#fb923c',
    file: 'src/storage/BufferPool.cpp',
    role: 'Keeps recently used 4 KB pages in memory so repeated reads skip the disk.',
    how: 'A hash table maps (table, page number) to a frame, so "is it cached?" is O(1). A doubly linked list keeps the frames in recency order. A hit moves the frame to the front; a miss takes the frame at the back, writing it out first if it was modified (dirty).',
    cost: [['hit', 'O(1)'], ['miss', 'O(1) + disk read'], ['eviction', 'O(1)']],
    try: 'SELECT * FROM students;',
  },
  list: {
    name: 'Doubly Linked List',
    color: '#f472b6',
    file: 'src/ds/LinkedList.h',
    role: 'The LRU order inside the buffer pool.',
    how: 'Head and tail sentinel nodes mean there are no special cases for an empty list or the ends. The pool keeps a pointer to every frame\'s node, so it can unlink one and relink it at the front without searching.',
    cost: [['push front / back', 'O(1)'], ['move to front (with node)', 'O(1)'], ['remove (with node)', 'O(1)']],
    try: 'SELECT * FROM enrollments;',
  },
  array: {
    name: 'Dynamic Array',
    color: '#60a5fa',
    file: 'src/ds/DynArray.h',
    role: 'Rows, result sets, column lists and the page directory. Our std::vector.',
    how: 'When it fills up, it allocates twice the capacity and moves the elements over. n pushes cause only about log2 n reallocations, so a push is O(1) amortized.',
    cost: [['index', 'O(1)'], ['push', 'O(1) amortized'], ['insert / remove in the middle', 'O(n)']],
    try: 'SELECT name, gpa FROM students;',
  },
  stack: {
    name: 'Stack',
    color: '#a78bfa',
    file: 'src/ds/Stack.h',
    role: 'Evaluates WHERE conditions and SET expressions.',
    how: 'The parser writes a condition in postfix order, e.g. age 20 > city \'Delhi\' = AND. For each row the executor walks that list: values are pushed, and each operator pops its inputs and pushes the result. Whatever is left on top decides if the row matches.',
    cost: [['push / pop', 'O(1)'], ['evaluate a condition', 'O(length) per row']],
    try: "SELECT name FROM students WHERE age > 20 AND city = 'Delhi';",
  },
  queue: {
    name: 'Queue',
    color: '#38bdf8',
    file: 'src/ds/Queue.h',
    role: 'Carries tokens from the tokenizer to the parser.',
    how: 'A circular array: push writes at head + count and pop reads at head, then moves head forward and wraps around. Nothing ever shifts. peek(k) lets the parser look a token or two ahead.',
    cost: [['push', 'O(1) amortized'], ['pop', 'O(1)'], ['peek', 'O(1)']],
    try: 'SHOW TABLES;',
  },
  sort: {
    name: 'Merge Sort',
    color: '#818cf8',
    file: 'src/ds/Sort.h',
    role: 'ORDER BY when no index already supplies the order.',
    how: 'Bottom-up merge sort: merge runs of length 1, then 2, 4, 8 and so on, between two buffers. Always O(n log n), and stable, so equal keys keep their order.',
    cost: [['sort n rows', 'O(n log n)'], ['extra memory', 'O(n)']],
    try: 'SELECT name, gpa FROM students ORDER BY gpa DESC;',
  },
  disk: {
    name: 'Paged Storage',
    color: '#94a3b8',
    file: 'src/storage/Table.cpp',
    role: 'Rows live in fixed-size slots on 4 KB pages in data/<table>.tbl.',
    how: 'Every row of a table takes the same number of bytes, so a row id is just page x slotsPerPage + slot and an index can point straight at it. A DynArray "page directory" counts used slots per page so INSERT can skip full pages without reading them.',
    cost: [['read row by id', 'O(1) + page fetch'], ['full scan', 'O(pages)']],
    try: 'DESCRIBE students;',
  },
  sql: {
    name: 'Parser / Planner',
    color: '#9ca3af',
    file: 'src/sql/Parser.cpp',
    role: 'Turns tokens into a command and picks how to read each table.',
    how: 'Recursive descent: one function per grammar rule, each calling the rules below it. The planner then scores every indexed column against the WHERE conditions: primary-key equality, then secondary equality, then ranges, then a full scan.',
    cost: [['parse', 'O(tokens)'], ['plan', 'O(columns x conditions)']],
    try: 'SELECT * FROM students WHERE id = 3;',
  },
};

// display order for cards and chips
const DS_ORDER = ['bptree', 'avl', 'hash', 'pool', 'list', 'array', 'stack', 'queue', 'sort', 'disk'];

function dsBadge(key, label) {
  const d = DS[key] || DS.sql;
  return `<span class="badge" style="--c:${d.color}">${label || d.name}</span>`;
}
