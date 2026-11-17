# MiniSQL

A small relational database engine, written in C++17 for our Data Structures Lab. Every
container in the engine is hand-written. There is no `std::vector`, `std::map`,
`std::list`, `std::string` or `<algorithm>` anywhere, and the build fails if one shows up.

It comes as a desktop app, **MiniSQL Workbench** (built with Qt). You write SQL in it, and a
side panel explains which data structures each query went through.

## Running it on Windows

1. **Install Qt.** Download the *Qt Online Installer* from qt.io (it asks you to make a free
   Qt account). In the component list, pick the newest **Qt 6.x**, and under it
   **MinGW 64-bit**. Leave the defaults under *Build Tools*, which include the MinGW
   compiler, CMake and Ninja. Qt Creator, the IDE, is installed with it.
2. **Open the project.** Start Qt Creator, choose *File → Open File or Project…*, and pick
   `CMakeLists.txt` in this folder. When it asks for a kit, tick
   *Desktop Qt 6.x MinGW 64-bit* and press *Configure Project*.
3. **Run.** Press the green ▶ button (or `Ctrl+R`). If another program starts, use the
   target selector above the ▶ button (the monitor icon) and pick **MiniSQLWorkbench**.

The welcome screen lets you create a new folder (it gets a starter script with sample
tables and queries), open an existing folder, or start an in-memory session.

**Sharing the .exe:** build in *Release* mode, then open the *Qt 6.x (MinGW) command
prompt* from the Start menu, `cd` to the build folder and run
`windeployqt MiniSQLWorkbench.exe`. That copies the Qt DLLs next to the exe, and the
folder can then be zipped and run on any Windows PC.

## Linux and macOS

Install Qt 6 (`sudo apt install qt6-base-dev qt6-svg-dev cmake` on Ubuntu, or
`brew install qt cmake` on a Mac), then:

```bash
cmake -B build
```
```bash
cmake --build build
```
```bash
./build/MiniSQLWorkbench
```

Qt Creator works the same way as on Windows.

## Without Qt

The engine itself needs nothing but a C++17 compiler. `make` builds the terminal version
and the tests:

```bash
make
```
```bash
./minisql-cli                                  # SQL prompt, in-memory
```
```bash
./minisql-cli --run examples/getting_started.sql --explain
```
```bash
make test
```
```bash
make bench
```

`--run … --explain` prints the same data structure trace the app shows. `make test` runs
the data structure and end-to-end tests, and `make bench` runs the indexed vs full-scan
benchmark.

## The workbench

- **Welcome screen**: *New folder…*, *Open folder…* (real Windows folder dialogs),
  *In-memory session*, and your recent folders. *Close Folder* in the File menu brings you
  back to it.
- **Navigator** (left dock): tables with their columns, keys and indexes; the folder's
  `.sql` scripts; and the raw storage files (`data/*.tbl`, `data/catalog.msql`) with their
  sizes. Double-click a table to see its rows; right-click for *Describe* and *Show Its
  Structures*.
- **Editor**: tabs, line numbers, SQL highlighting, the error line marked in red, `Tab`
  indentation and `Ctrl+/` comments.
  - Run the statement under the cursor with `Ctrl+Enter`.
  - Run everything, or just the selection, with `Ctrl+Shift+Enter` or `F5`.
  - Save with `Ctrl+S`.
- **Results**: a grid per result set, plus an *Output* log of every statement with its
  time. Click a line in the log to explain that statement again.
- **Data Structures panel** (right dock):
  - **Explain** shows, for the selected statement:
    - a pipeline diagram with the stages it used lit up (tokenizer, parser, catalog,
      index, buffer pool, disk);
    - a card for each structure it touched, with its counters;
    - a numbered step-by-step story;
    - the token queue and the raw counters.
  - **Structures** draws the live state: the B+ tree with its linked leaves, each AVL
    index, the page directory, the buffer pool's LRU list and the catalog's hash buckets.
    Hover a node for its row ids.
  - **Learn** has a card per structure: how it works, its costs, where it lives in the
    code, and a query you can insert into the editor.
- Dark and light themes (the sun/moon button), and docks you can move, close or float.

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
There is no `NULL`, and there are no transactions.

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
| String | `src/ds/Str.h` | every piece of text in the engine |

The GUI talks to Qt in Qt's own types (`QString`, widgets), but it keeps its own data in
these structures too. Script lists and run history are `DynArray`s, the editor's keyword
lookup is a `HashTable`, and the B+ tree picture is laid out with a breadth-first walk
over a `Queue`.

## How a query runs

```
 Workbench / CLI
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

**Planning.** The planner looks at the top-level `AND` conditions of the `WHERE` clause and
picks the first of these that applies:

1. an equality on the primary key;
2. an equality on an AVL-indexed column;
3. a range on either of those;
4. otherwise, a full scan.

Whatever the index returns is still checked against the full `WHERE` on the stack
machine, so the answer always matches a scan's. A join uses the other table's index if
its join column has one, and builds a hash table otherwise.

**Storage.** A row id is `page * slotsPerPage + slot`. Rows never move, so indexes can
store row ids directly. The catalog and every dirty page are written to disk after each
run, and indexes are rebuilt from the pages when a folder is opened.

## Project layout

```
src/ds/        the data structures (header-only, except Str and Stats)
src/storage/   Value, pages on disk, buffer pool, tables, catalog
src/index/     the Index interface, plus B+ tree and AVL adapters
src/sql/       lexer, parser, executor
src/util/      trace, OS helpers
src/gui/       the Qt desktop app
src/main.cpp   the terminal version (minisql-cli)
tests/         assertion-based tests
cmake/, scripts/   the no-STL audit (CMake for Qt Creator builds, shell for make)
examples/      getting_started.sql (also built into the app)
```

## Testing and the benchmark

`make test` checks each structure on its own:

- AVL rotations and balance;
- B+ tree split/borrow/merge invariants after thousands of random deletes;
- LRU eviction order;
- hash table resizing;
- stable merge sort.

It then runs SQL end to end, including reopening a folder from disk.

`make bench` loads 10,000 rows and runs the same queries with and without indexes:

```
  query (avg per run)                  index ms    scan ms   speedup  pages idx     scan
  point lookup  id = ?        (B+)       0.0238     2.0664     86.7x        1.0    124.0
  range 100 ids BETWEEN      (B+)        0.0737     2.5849     35.1x      100.0    124.0
  secondary  grp = ?         (AVL)       0.0974     2.0328     20.9x      100.0    124.0
  ORDER BY id LIMIT 10 (leaf chain)      0.0630     1.8693     29.7x       10.0    124.0
```

Times depend on the machine. The buffer pool hit rate in that run is low on purpose: a
full scan of 124 pages through 64 frames evicts every page before it's needed again. That
is the classic weakness of plain LRU, known as sequential flooding.
