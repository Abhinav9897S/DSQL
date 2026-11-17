# MiniSQL

A small relational database engine, written in C++17 for our Data Structures Lab. Every
container in the engine is hand-written. There is no `std::vector`, `std::map`,
`std::list`, `std::string` or `<algorithm>` anywhere, and the build fails if one shows up.

It comes with a simple desktop app (built with Qt): type SQL, run it, and a side panel
lists which data structures the query used, step by step.

## Download the Windows app (no install needed)

Every push is built and tested on a real Windows machine by GitHub Actions
(`.github/workflows/windows.yml`). To get the ready-made app:

1. Open the repo's **Actions** tab, click the newest **Windows build** run, and download
   **MiniSQL-windows** at the bottom (you need to be signed in to GitHub). The **Run
   workflow** button on the *Windows build* page makes a fresh one at any time.
2. Extract the zip anywhere, e.g. `C:\MiniSQL`.
3. Double-click **MiniSQL.exe**. Keep the DLLs and subfolders next to it.

## Building it on Windows yourself

1. **Install Qt.** Download the *Qt Online Installer* from qt.io (it asks you to make a free
   Qt account). Pick the newest **Qt 6.x** and under it **MinGW 64-bit**. The default build
   tools (MinGW compiler, CMake, Ninja) and Qt Creator come with it.
2. **Open the project.** In Qt Creator choose *File → Open File or Project…* and pick
   `CMakeLists.txt` in this folder. Tick the *Desktop Qt 6.x MinGW 64-bit* kit and press
   *Configure Project*.
3. **Run.** Press the green ▶ button. If something else starts, pick **MiniSQL** in the
   target selector just above the ▶ button.

To share the app, build in *Release* mode, then open the *Qt 6.x (MinGW)* command prompt
from the Start menu, `cd` into the build folder and run `windeployqt MiniSQL.exe`. That
copies the Qt DLLs next to the exe, so the folder runs on any Windows PC.

## Using the app

- **Start screen:** *New Folder…*, *Open Folder…*, *In-Memory Session*, and your recent
  folders (double-click one to open it). Tables are saved in the folder's `data/`
  subfolder.
- **Left:** the tables, with their columns and which ones have an index. Double-click a
  table to see its rows.
- **SQL editor:**
  - line numbers, and syntax colours for keywords, types, functions, strings, numbers and
    comments;
  - SQL words become capitals as you type (`select` → `SELECT`), and pasted SQL is
    capitalised too. Text inside quotes and comments is never changed, and
    *Edit → Capitalise SQL Keywords* (`Ctrl+Shift+U`) does the whole script.
- **Error detector:** the script is checked as you type, without running anything.
  - It catches syntax errors and tables that don't exist, including tables the script
    creates further up, which count as existing.
  - Each problem gets a red squiggle at the exact spot and a red line number. Hover
    either one to read the message.
  - The count shows above the editor, and the *Problems* tab lists them all
    (double-click one to jump there).
- **Running:**
  - **Run All** (`F5`) runs everything, or only the selected text.
  - **Run Statement** (`Ctrl+Enter`) runs the statement the cursor is in.
  - `Ctrl+S` saves the script.
- **Terminal** (bottom): every statement that ran, its output as a text table, and
  errors in red. You can also type a statement at the `minisql>` prompt and press Enter.
  `Up`/`Down` bring back earlier ones, and `clear` empties it. The **Result** tab shows
  the last result as a grid.
- **Right:** *How the query ran* shows the plan, the data structures used with their
  counts, and the step-by-step story from the engine.
- **Dark mode:** the checkbox above the editor (or *View → Dark Mode*). *File → Close
  Folder* goes back to the start screen.

## Linux and macOS

Install Qt 6 (`sudo apt install qt6-base-dev cmake` on Ubuntu, `brew install qt cmake` on
a Mac), then:

```bash
cmake -B build
```
```bash
cmake --build build
```
```bash
./build/MiniSQL
```

## Without Qt

The engine itself needs only a C++17 compiler. `make` builds a terminal version and the
tests:

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

`--explain` prints the same data structure steps the app shows. `make bench` compares
indexed lookups against full scans on 10,000 rows.

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

The app's code (`src/gui/`) only uses Qt for the window itself. Everything the database
does goes through the structures above.

## How a query runs

```
 App / CLI
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
src/gui/       the Qt desktop app (one window, MainWindow.cpp)
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
