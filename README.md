# MiniSQL

A small in-memory relational database engine, written in C++17 for our Data Structures Lab.
Every container in it is hand-written. There is no `std::vector`, `std::map`, `std::list`,
`std::string` or `<algorithm>` anywhere, and `make` refuses to build if one shows up.

It comes with a browser-based workbench (served by the engine itself) where you write SQL
and watch a side panel explain which data structures each query went through.

## Build and run

You need `g++` (or `clang++`) with C++17 and `make`. Nothing else.

```bash
make
```

```bash
./minisql
```

That starts the workbench on http://localhost:8080 and opens your browser. From the
welcome page you can create a new folder (it gets a starter script), open an existing one,
or start an in-memory session.

Other ways to run it:

```bash
./minisql --cli                       # SQL prompt in the terminal, in-memory
./minisql --cli myfolder --explain    # prompt on a workspace folder, with traces
./minisql --run examples/getting_started.sql --explain
./minisql --bench 10000               # indexed vs full-scan benchmark
make test                             # data structure + end-to-end tests
```

Options: `--port N`, `--frames N` (buffer pool size, default 64), `--order N`
(B+ tree order, default 4 so the tree is small enough to look at), `--no-browser`.

### Windows

With MinGW-w64 on the PATH, run `build.bat` (or `build.bat test`), then `minisql.exe`.
CMake works too: `cmake -B build && cmake --build build`.

## The workbench

- **Welcome page**: new folder, open folder, in-memory session, and a list of recent folders.
- **Navigator** (left): tables with their columns and indexes, the folder's `.sql` scripts,
  and the raw storage files (`data/*.tbl`, `data/catalog.msql`) with their sizes.
- **Editor**: tabs, line numbers, SQL highlighting, and error lines marked in red.
  `Ctrl+Enter` runs the statement under the cursor, `Ctrl+Shift+Enter` runs everything
  (or just the selection), and `Ctrl+S` saves.
- **Results**: one grid per result set, plus an Output log of every statement with its timing.
- **Data structure panel** (right):
  - *Explain* shows the pipeline the statement went through (tokenizer, parser, catalog,
    index, buffer pool, disk), which structures were used and how hard they worked, a
    numbered step-by-step story, the token queue, and the raw counters.
  - *Structures* draws the live state: B+ tree levels and leaf chain, AVL trees, the page
    directory, the buffer pool's LRU list, and the catalog's hash buckets.
  - *Learn* has a card per structure covering how it works, its costs, where it lives in
    the code, and a query to try.
- Dark and light themes (the toggle at the top right) and resizable panels.

## SQL it understands

```sql
CREATE TABLE [IF NOT EXISTS] t (id INT PRIMARY KEY, name VARCHAR(20), gpa FLOAT, ok BOOL);
CREATE INDEX idx ON t(col);              -- AVL tree
DROP TABLE [IF EXISTS] t;   DROP INDEX idx;
INSERT INTO t [(cols)] VALUES (...), (...);
SELECT * | cols | COUNT/SUM/AVG/MIN/MAX(...) [AS x]
  FROM a [alias] [JOIN b [alias] ON a.x = b.y]
  [WHERE ...] [GROUP BY col] [ORDER BY col [DESC]] [LIMIT n];
UPDATE t SET col = expr, ... [WHERE ...];
DELETE FROM t [WHERE ...];
SHOW TABLES;   DESCRIBE t;
```

`WHERE` supports `= != <> < <= > >=`, `AND OR NOT`, `BETWEEN`, `LIKE` (with `%` and `_`),
`+ - * / %` and parentheses. Types are `INT`, `FLOAT`, `VARCHAR(n)` (up to 255) and `BOOL`.
There is no `NULL`, and no transactions.

## Where each data structure is used

| Structure | File | Used for |
|---|---|---|
| Dynamic array | `src/ds/DynArray.h` | rows, result sets, column lists, page directory |
| Doubly linked list | `src/ds/LinkedList.h` | LRU order of buffer pool frames |
| Hash table (chaining) | `src/ds/HashTable.h` | catalog, buffer pool page table, hash join, GROUP BY, keywords |
| AVL tree | `src/ds/AVLTree.h` | secondary indexes (`CREATE INDEX`) |
| B+ tree | `src/ds/BPlusTree.h` | primary key: lookups, ranges, ordered scans |
| Queue (circular array) | `src/ds/Queue.h` | tokens from the tokenizer to the parser |
| Stack | `src/ds/Stack.h` | evaluating WHERE / SET expressions in postfix |
| Merge sort | `src/ds/Sort.h` | ORDER BY when no index gives the order |
| String | `src/ds/Str.h` | every piece of text (no `std::string`) |

## How a query runs

```
 workbench / CLI
      |  SQL text
 Tokenizer  ---------------------------  Queue of tokens
      |
 Parser (recursive descent) -----------  WHERE compiled to postfix
      |  command
 Executor ---- Catalog (hash table: name -> table)
      |    \-- Index manager (B+ tree on the primary key, AVL trees on other columns)
      |  page requests
 Buffer pool (hash table + doubly linked list = LRU)
      |  miss / write back
 Storage: data/<table>.tbl, fixed-size row slots in 4 KB pages
```

The planner looks at the top-level `AND` conditions of the `WHERE` clause. An equality on
the primary key wins, then an equality on an AVL-indexed column, then a range on either,
and otherwise it does a full scan. Whatever the index returns is still checked against the
full `WHERE` on the stack machine, so the answer is always the same as a scan's. Joins use
the other table's index if its join column has one, and otherwise build a hash table.

A row id is `page * slotsPerPage + slot`. Rows never move, so the indexes can store row ids
directly. The catalog and every dirty page are written out after each batch of statements.
Indexes are rebuilt from the pages when a folder is opened.

## Project layout

```
src/ds/        the data structures (header-only, except Str and Stats)
src/storage/   Value, pages on disk, buffer pool, tables, catalog
src/index/     the Index interface, plus B+ tree and AVL adapters
src/sql/       lexer, parser, executor
src/server/    tiny HTTP server (raw sockets) and the JSON API
src/util/      JSON writer, trace, OS helpers
web/           the workbench (plain HTML/CSS/JS, no libraries)
tests/         assertion-based tests
scripts/       audit_stl.sh, the no-STL check
examples/      getting_started.sql
```

## Testing and the benchmark

`make test` checks each structure on its own (AVL rotations and balance, B+ tree
split/borrow/merge invariants after thousands of random deletes, LRU eviction order, hash
table resizing, stable merge sort), then runs SQL end to end, including reopening a folder
from disk.

`make bench` loads 10,000 rows and runs the same queries with and without indexes:

```
  query (avg per run)                  index ms    scan ms   speedup  pages idx     scan
  point lookup  id = ?        (B+)       0.0238     2.0664     86.7x        1.0    124.0
  range 100 ids BETWEEN      (B+)        0.0737     2.5849     35.1x      100.0    124.0
  secondary  grp = ?         (AVL)       0.0974     2.0328     20.9x      100.0    124.0
  ORDER BY id LIMIT 10 (leaf chain)      0.0630     1.8693     29.7x       10.0    124.0
```

(Times depend on the machine.) The buffer pool hit rate in that run is low on purpose: a
full scan of 124 pages through 64 frames evicts every page before it's needed again. That
is the classic weakness of plain LRU, known as sequential flooding.
