#include "sql/Executor.h"
#include "sql/Parser.h"
#include "ds/Sort.h"
#include "util/Error.h"
#include "util/Platform.h"
#include <cctype>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------- helpers

static Str plural(long long n, const char* one, const char* many) {
    return Str::num(n) + " " + (n == 1 ? one : many);
}

static bool truthy(const Value& v) {
    switch (v.type) {
    case Type::Float: return v.f != 0;
    case Type::Varchar: return !v.s.empty();
    default: return v.i != 0;
    }
}

static Value zeroOf(Type t) {
    switch (t) {
    case Type::Float: return Value::ofFloat(0);
    case Type::Varchar: return Value::ofText("");
    case Type::Bool: return Value::ofBool(false);
    default: return Value::ofInt(0);
    }
}

static Str typeText(const Column& c) {
    if (c.type == Type::Varchar) return "VARCHAR(" + Str::num(c.size) + ")";
    return typeName(c.type);
}

// make a literal fit a column's type, or explain why it can't
static Value coerce(const Value& v, const Column& col) {
    switch (col.type) {
    case Type::Int:
        if (v.type == Type::Int) return v;
        if (v.type == Type::Float && v.f == (double)(long long)v.f) return Value::ofInt((long long)v.f);
        break;
    case Type::Float:
        if (v.isNumber()) return Value::ofFloat(v.asDouble());
        break;
    case Type::Bool:
        if (v.type == Type::Bool) return v;
        if (v.type == Type::Int && (v.i == 0 || v.i == 1)) return Value::ofBool(v.i == 1);
        break;
    case Type::Varchar:
        if (v.type == Type::Varchar) {
            if (v.s.size() > col.size)
                throw DbError("'" + v.s + "' is too long for " + col.name + " VARCHAR(" + Str::num(col.size) + ")");
            return v;
        }
        break;
    }
    throw DbError("column " + col.name + " is " + typeText(col) + " but got " + typeName(v.type) + " " + v.toSql());
}

// SQL LIKE, case-insensitive: % = any run of characters, _ = one character
static bool likeMatch(const Str& s, const Str& p) {
    int i = 0, j = 0, star = -1, mark = 0;
    while (i < s.size()) {
        if (j < p.size() && (p[j] == '_' || tolower((unsigned char)p[j]) == tolower((unsigned char)s[i]))) {
            i++;
            j++;
        } else if (j < p.size() && p[j] == '%') {
            star = j++;
            mark = i;
        } else if (star >= 0) {
            j = star + 1;   // let the last % swallow one more character
            i = ++mark;
        } else {
            return false;
        }
    }
    while (j < p.size() && p[j] == '%') j++;
    return j == p.size();
}

static Value arithmetic(Op op, const Value& a, const Value& b) {
    if (op == Op::Add && a.type == Type::Varchar && b.type == Type::Varchar) return Value::ofText(a.s + b.s);
    if (!a.isNumber() || !b.isNumber())
        throw DbError(Str("can't apply ") + opText(op) + " to " + a.toSql() + " and " + b.toSql());
    if (a.type == Type::Int && b.type == Type::Int) {
        long long x = a.i, y = b.i;
        if ((op == Op::Div || op == Op::Mod) && y == 0) throw DbError("division by zero");
        switch (op) {
        case Op::Add: return Value::ofInt(x + y);
        case Op::Sub: return Value::ofInt(x - y);
        case Op::Mul: return Value::ofInt(x * y);
        case Op::Div: return Value::ofInt(x / y);
        default: return Value::ofInt(x % y);
        }
    }
    double x = a.asDouble(), y = b.asDouble();
    if ((op == Op::Div || op == Op::Mod) && y == 0) throw DbError("division by zero");
    switch (op) {
    case Op::Add: return Value::ofFloat(x + y);
    case Op::Sub: return Value::ofFloat(x - y);
    case Op::Mul: return Value::ofFloat(x * y);
    case Op::Div: return Value::ofFloat(x / y);
    default: return Value::ofFloat(fmod(x, y));
    }
}

static Value apply(Op op, const Value& a, const Value& b) {
    switch (op) {
    case Op::Eq: return Value::ofBool(compareKeys(a, b) == 0);
    case Op::Ne: return Value::ofBool(compareKeys(a, b) != 0);
    case Op::Lt: return Value::ofBool(compareKeys(a, b) < 0);
    case Op::Le: return Value::ofBool(compareKeys(a, b) <= 0);
    case Op::Gt: return Value::ofBool(compareKeys(a, b) > 0);
    case Op::Ge: return Value::ofBool(compareKeys(a, b) >= 0);
    case Op::And: return Value::ofBool(truthy(a) && truthy(b));
    case Op::Or: return Value::ofBool(truthy(a) || truthy(b));
    case Op::Like:
        if (a.type != Type::Varchar || b.type != Type::Varchar) throw DbError("LIKE only works on text");
        return Value::ofBool(likeMatch(a.s, b.s));
    default:
        return arithmetic(op, a, b);
    }
}

static Row concat(const Row& a, const Row& b) {
    Row r(a);
    for (int i = 0; i < b.size(); i++) r.push(b[i]);
    return r;
}

static void sortRows(DynArray<Row>& rows, int col) {
    mergeSort(rows, [col](const Row& a, const Row& b) { return compareKeys(a[col], b[col]) < 0; });
}

static Str predText(const Pred& p) {
    Str col = p.table.empty() ? p.column : p.table + "." + p.column;
    if (p.op == Op::Between) return col + " BETWEEN " + p.a.toSql() + " AND " + p.b.toSql();
    return col + " " + opText(p.op) + " " + p.a.toSql();
}

static Str rangeText(const Str& col, const Bound& lo, const Bound& hi) {
    Str s;
    if (lo.on) s += lo.v.toSql() + (lo.inclusive ? " <= " : " < ");
    s += col;
    if (hi.on) s += (hi.inclusive ? " <= " : " < ") + hi.v.toSql();
    return s;
}

static Str accessText(const Access& a, Table* t) {
    if (a.kind == Access::Scan) return "Full scan of " + t->name;
    Str col = t->cols[a.col].name;
    Str kind = a.index->kind();
    if (a.kind == Access::Lookup) return kind + " lookup " + col + " = " + a.key.toSql();
    if (a.kind == Access::Range) return kind + " range " + rangeText(col, a.lo, a.hi);
    return kind + " ordered scan on " + col;
}

static bool isBPlus(Index* ix) { return strcmp(ix->tag(), "bptree") == 0; }

// ---------------------------------------------------------------- scope

void Scope::add(Table* t, const Str& alias) {
    for (int i = 0; i < t->cols.size(); i++) {
        ScopeCol c;
        c.alias = alias;
        c.table = t->name;
        c.name = t->cols[i].name;
        c.type = t->cols[i].type;
        cols.push(c);
    }
}

int Scope::find(const Str& table, const Str& name) const {
    int hit = -1;
    for (int i = 0; i < cols.size(); i++) {
        const ScopeCol& c = cols[i];
        if (c.name != name) continue;
        if (!table.empty() && table != c.alias && table != c.table) continue;
        if (hit >= 0) throw DbError("column '" + name + "' is in both tables; write it as table." + name);
        hit = i;
    }
    return hit;
}

int Scope::need(const Str& table, const Str& name) const {
    int i = find(table, name);
    if (i < 0) throw DbError("unknown column '" + (table.empty() ? name : table + "." + name) + "'");
    return i;
}

// ---------------------------------------------------------------- driver

void Executor::run(const Str& sql, DynArray<Result>& out, int firstLine) {
    Queue<Token> tokens;
    Lexer lexer(sql, firstLine);
    try {
        lexer.run(tokens);
    } catch (DbError& e) {
        Result r;
        r.ok = false;
        r.kind = "ERROR";
        r.sql = sql;
        r.line = firstLine;
        r.errorLine = lexer.errorLine;
        r.message = e.msg;
        out.push(std::move(r));
        return;
    }

    Parser parser(tokens, sql);
    while (!parser.done()) {
        Result r;
        DsStats before = g_stats;
        double t0 = plat::nowMs();
        g_trace = &r.trace;
        try {
            Command c = parser.next();
            r.kind = cmdName(c.kind);
            r.sql = c.sql;
            r.line = c.line;
            r.tokens = c.tokens;
            noteParse(c);
            execute(c, r);

            DsStats d = g_stats - before;
            long long touches = d.poolHits + d.poolMisses;
            if (touches > 1 || r.trace.pageNotesSkipped > 0) {
                Str s = plural(touches, "page request", "page requests") + ": " + Str::num(d.poolHits) + " hits, " +
                        Str::num(d.poolMisses) + " misses, " + Str::num(d.poolEvictions) + " evictions";
                if (r.trace.pageNotesSkipped > 0) s += " (" + Str::num(r.trace.pageNotesSkipped) + " not listed above)";
                s += ". Each hit was an O(1) hash lookup plus an O(1) move-to-front in the LRU list.";
                note("pool", "Buffer pool summary", s);
            }
        } catch (DbError& e) {
            r.ok = false;
            r.message = e.msg;
            r.errorLine = r.line;
            if (r.sql.empty()) {   // it never got past the parser
                r.kind = "ERROR";
                r.sql = parser.failedSql;
                r.line = parser.failedLine;
                r.errorLine = parser.errorLine;
            }
        }
        g_trace = nullptr;
        r.ms = plat::nowMs() - t0;
        r.stats = g_stats - before;
        bool failed = !r.ok;
        out.push(std::move(r));
        if (failed) break;
    }
}

void Executor::noteParse(const Command& c) {
    note("queue", "Tokenizer",
         "The scanner turned the text into " + plural(c.tokens.size(), "token", "tokens") +
             " and pushed them onto a queue. The parser pops them from the front (FIFO), peeking at most two ahead.");

    Str d = Str("Recursive descent recognised a ") + cmdName(c.kind) + " statement, one function per grammar rule";
    if (!c.where.empty()) {
        d += ". The WHERE clause was emitted in postfix order: " + c.where.postfix();
        if (!c.preds.empty()) {
            d += ". Conditions an index could answer: ";
            for (int i = 0; i < c.preds.size(); i++) d += (i ? ", " : "") + predText(c.preds[i]);
        }
    }
    note("sql", "Parser", d + ".");
}

void Executor::execute(Command& c, Result& r) {
    switch (c.kind) {
    case Cmd::CreateTable: createTable(c, r); break;
    case Cmd::CreateIndex: createIndex(c, r); break;
    case Cmd::DropTable: dropTable(c, r); break;
    case Cmd::DropIndex: dropIndex(c, r); break;
    case Cmd::Insert: insert(c, r); break;
    case Cmd::Select: select(c, r); break;
    case Cmd::Update: update(c, r); break;
    case Cmd::Delete: remove(c, r); break;
    case Cmd::ShowTables: showTables(r); break;
    case Cmd::Describe: describe(c, r); break;
    }
}

Table* Executor::table(const Str& name) {
    Table* t = cat_.find(name);
    if (!t) throw DbError("no table named '" + name + "'");
    return t;
}

// ---------------------------------------------------------------- DDL

void Executor::createTable(Command& c, Result& r) {
    if (c.ifExists && cat_.find(c.table)) {
        r.message = "Table " + c.table + " already exists, nothing to do";
        return;
    }
    DynArray<Column> cols;
    int pk = -1;
    for (int i = 0; i < c.cols.size(); i++) {
        for (int j = 0; j < i; j++)
            if (c.cols[j].name == c.cols[i].name) throw DbError("column '" + c.cols[i].name + "' is listed twice");
        if (c.cols[i].pk) {
            if (pk >= 0) throw DbError("only one PRIMARY KEY column is supported");
            pk = i;
        }
        Column col;
        col.name = c.cols[i].name;
        col.type = c.cols[i].type;
        col.size = c.cols[i].size;
        cols.push(col);
    }
    Table* t = cat_.create(c.table, cols, pk);

    HashTable<Str, Table*>& names = cat_.names();
    names.find(t->name);
    note("hash", "Catalog insert",
         "Added '" + t->name + "' to the catalog hash table: bucket " + Str::num(names.lastBucket) + " of " +
             Str::num(names.bucketCount()) + ", " + plural(names.size(), "table", "tables") + " in total.");
    note("array", "Row layout",
         "Every row gets a fixed " + Str::num(t->slotSize) + "-byte slot (1 in-use byte + the columns), so a 4 KB page holds " +
             Str::num(t->slotsPerPage) + " rows. A DynArray page directory will track how full each page is.");
    if (pk >= 0)
        note("bptree", "Primary key index",
             "Created an empty B+ tree of order " + Str::num(cat_.order()) + " on " + t->cols[pk].name +
                 ". Each INSERT adds its key here and a duplicate key is rejected.");
    else
        note("sql", "No primary key",
             "Without a primary key there is no B+ tree, so lookups scan every page unless you CREATE INDEX.");

    r.message = "Table " + t->name + " created";
    r.changed = true;
}

void Executor::createIndex(Command& c, Result& r) {
    Table* t = table(c.table);
    int col = t->colIndex(c.indexCol);
    if (col < 0) throw DbError("table " + t->name + " has no column '" + c.indexCol + "'");
    if (col == t->pkCol) throw DbError(c.indexCol + " is the primary key, it is already indexed by a B+ tree");
    for (int i = 0; i < cat_.tableCount(); i++)
        for (int k = 0; k < cat_.tableAt(i)->indexes.size(); k++)
            if (cat_.tableAt(i)->indexes[k].name == c.index) throw DbError("an index named " + c.index + " already exists");
    if (t->indexOn(col)) throw DbError(c.indexCol + " already has an index (" + t->indexName(col) + ")");

    DsStats before = g_stats;
    t->addIndex(c.index, col);
    DsStats d = g_stats - before;
    Index* ix = t->indexes.back().index;
    note("avl", "AVL index built",
         "Scanned " + plural(t->rowCount, "row", "rows") + " and inserted each " + c.indexCol +
             " value into a new AVL tree: " + plural(ix->keys(), "distinct key", "distinct keys") + ", height " +
             Str::num(ix->height()) + ". Rebalancing rotations: LL " + Str::num(d.avlLL) + ", RR " + Str::num(d.avlRR) +
             ", LR " + Str::num(d.avlLR) + ", RL " + Str::num(d.avlRL) + ".");
    r.message = "Index " + c.index + " created on " + t->name + "(" + c.indexCol + ")";
    r.changed = true;
}

void Executor::dropTable(Command& c, Result& r) {
    if (c.ifExists && !cat_.find(c.table)) {
        r.message = "No table " + c.table + ", nothing to drop";
        return;
    }
    Table* t = table(c.table);
    int pages = t->pageCount();
    cat_.drop(t->name);
    note("hash", "Catalog remove", "Unlinked '" + c.table + "' from its hash bucket chain.");
    note("pool", "Frames released",
         "Any cached pages of the table were dropped from the buffer pool without writing them (" +
             plural(pages, "page", "pages") + " on disk), and the data file was deleted.");
    r.message = "Table " + c.table + " dropped";
    r.changed = true;
}

void Executor::dropIndex(Command& c, Result& r) {
    for (int i = 0; i < cat_.tableCount(); i++) {
        Table* t = cat_.tableAt(i);
        if (!t->dropIndex(c.index)) continue;
        note("avl", "Index dropped", "Freed every node of the AVL tree " + c.index + " on " + t->name + ".");
        r.message = "Index " + c.index + " dropped";
        r.changed = true;
        return;
    }
    if (c.index == "primary") throw DbError("the PRIMARY B+ tree can't be dropped");
    throw DbError("no index named '" + c.index + "'");
}

void Executor::showTables(Result& r) {
    r.columns.push("table");   r.types.push(Type::Varchar);
    r.columns.push("rows");    r.types.push(Type::Int);
    r.columns.push("pages");   r.types.push(Type::Int);
    r.columns.push("primary_key"); r.types.push(Type::Varchar);
    r.columns.push("indexes"); r.types.push(Type::Varchar);
    for (int i = 0; i < cat_.tableCount(); i++) {
        Table* t = cat_.tableAt(i);
        Row row;
        row.push(Value::ofText(t->name));
        row.push(Value::ofInt(t->rowCount));
        row.push(Value::ofInt(t->pageCount()));
        row.push(Value::ofText(t->pkCol >= 0 ? t->cols[t->pkCol].name : Str("-")));
        Str ix;
        for (int k = 0; k < t->indexes.size(); k++)
            ix += (k ? ", " : "") + t->indexes[k].name + "(" + t->cols[t->indexes[k].col].name + ")";
        row.push(Value::ofText(ix.empty() ? Str("-") : ix));
        r.rows.push(std::move(row));
    }
    note("hash", "Catalog",
         "Tables live in a hash table (" + Str::num(cat_.names().bucketCount()) +
             " buckets) for O(1) lookup by name; a DynArray keeps them in creation order for listing.");
    r.message = plural(r.rows.size(), "table", "tables");
}

void Executor::describe(Command& c, Result& r) {
    Table* t = table(c.table);
    r.columns.push("column"); r.types.push(Type::Varchar);
    r.columns.push("type");   r.types.push(Type::Varchar);
    r.columns.push("key");    r.types.push(Type::Varchar);
    r.columns.push("index");  r.types.push(Type::Varchar);
    r.columns.push("offset"); r.types.push(Type::Int);
    for (int i = 0; i < t->cols.size(); i++) {
        Row row;
        row.push(Value::ofText(t->cols[i].name));
        row.push(Value::ofText(typeText(t->cols[i])));
        row.push(Value::ofText(i == t->pkCol ? "PRIMARY" : ""));
        Index* ix = t->indexOn(i);
        row.push(Value::ofText(ix ? Str(ix->kind()) + " " + t->indexName(i) : Str("")));
        row.push(Value::ofInt(t->cols[i].offset));
        r.rows.push(std::move(row));
    }
    note("array", "Schema",
         "Columns are stored in a DynArray. 'offset' is where each column starts inside a " +
             Str::num(t->slotSize) + "-byte row slot.");
    r.message = t->name + ": " + plural(t->cols.size(), "column", "columns");
}

// ---------------------------------------------------------------- DML

void Executor::insert(Command& c, Result& r) {
    Table* t = table(c.table);
    DynArray<int> target;   // value position -> column
    if (c.insertCols.empty()) {
        for (int i = 0; i < t->cols.size(); i++) target.push(i);
    } else {
        for (int i = 0; i < c.insertCols.size(); i++) {
            int col = t->colIndex(c.insertCols[i]);
            if (col < 0) throw DbError("table " + t->name + " has no column '" + c.insertCols[i] + "'");
            for (int j = 0; j < target.size(); j++)
                if (target[j] == col) throw DbError("column " + c.insertCols[i] + " is listed twice");
            target.push(col);
        }
        if (t->pkCol >= 0) {
            bool hasPk = false;
            for (int j = 0; j < target.size(); j++) hasPk = hasPk || target[j] == t->pkCol;
            if (!hasPk) throw DbError("INSERT must give a value for the primary key " + t->cols[t->pkCol].name);
        }
    }

    DsStats before = g_stats;
    int firstPage = -1, lastPage = -1;
    for (int i = 0; i < c.rows.size(); i++) {
        const Row& given = c.rows[i];
        if (given.size() != target.size())
            throw DbError("row " + Str::num(i + 1) + " has " + plural(given.size(), "value", "values") + " but " +
                          Str::num(target.size()) + " were expected");
        Row row;
        for (int k = 0; k < t->cols.size(); k++) row.push(zeroOf(t->cols[k].type));
        for (int k = 0; k < target.size(); k++) row[target[k]] = coerce(given[k], t->cols[target[k]]);
        int page = t->insert(row) / t->slotsPerPage;
        if (firstPage < 0 || page < firstPage) firstPage = page;
        if (page > lastPage) lastPage = page;
    }
    DsStats d = g_stats - before;
    int n = c.rows.size();

    if (t->pk)
        note("bptree", "B+ tree insert",
             plural(n, "key", "keys") + " added to the primary-key B+ tree: " + Str::num(d.bptVisits) + " node visits, " +
                 plural(d.bptSplits, "split", "splits") + ". It now has " + Str::num(t->pk->keys()) + " keys in " +
                 plural(t->pk->nodes(), "node", "nodes") + ", height " + Str::num(t->pk->height()) + ".");
    if (!t->indexes.empty())
        note("avl", "AVL index insert",
             "The new values also went into " + plural(t->indexes.size(), "AVL index", "AVL indexes") + ": " +
                 Str::num(d.avlVisits) + " node visits, rotations LL " + Str::num(d.avlLL) + ", RR " + Str::num(d.avlRR) +
                 ", LR " + Str::num(d.avlLR) + ", RL " + Str::num(d.avlRL) + ".");
    Str pages = firstPage == lastPage ? "page " + Str::num(firstPage)
                                      : "pages " + Str::num(firstPage) + "-" + Str::num(lastPage);
    note("array", "Row storage",
         "Rows were encoded into " + Str::num(t->slotSize) + "-byte slots on " + pages +
             ". The page directory (a DynArray of used-slot counts) now covers " + plural(t->pageCount(), "page", "pages") + ".");

    r.message = plural(n, "row", "rows") + " inserted";
    r.changed = true;
}

void Executor::update(Command& c, Result& r) {
    Table* t = table(c.table);
    Scope scope;
    scope.add(t, t->name);
    bind(c.where, scope);
    DynArray<int> cols;
    bool pkChanges = false;
    for (int i = 0; i < c.setCols.size(); i++) {
        int col = t->colIndex(c.setCols[i]);
        if (col < 0) throw DbError("table " + t->name + " has no column '" + c.setCols[i] + "'");
        cols.push(col);
        if (col == t->pkCol) pkChanges = true;
        bind(c.setExprs[i], scope);
    }

    Access a = plan(t, t->name, c.preds, nullptr, -1, !c.where.empty());
    r.plan = accessText(a, t);
    DynArray<int> rids;
    DynArray<Row> rows;
    fetch(t, a, rids, rows);
    if (!c.where.empty()) filter(c.where, rows, &rids);

    DsStats before = g_stats;
    for (int i = 0; i < rows.size(); i++) {
        Row after(rows[i]);
        for (int k = 0; k < cols.size(); k++) after[cols[k]] = coerce(eval(c.setExprs[k], rows[i]), t->cols[cols[k]]);
        t->update(rids[i], rows[i], after);
    }
    DsStats d = g_stats - before;

    if (t->pk) {
        if (pkChanges)
            note("bptree", "B+ tree re-key",
                 "The primary key changed, so each old key was deleted and the new one inserted: " +
                     Str::num(d.bptVisits) + " node visits, " + Str::num(d.bptSplits) + " splits, " +
                     Str::num(d.bptMerges) + " merges, " + Str::num(d.bptBorrows) + " borrows.");
        else
            note("bptree", "B+ tree untouched",
                 "The primary key didn't change and rows are updated in place, so their row ids and the B+ tree stay as they are.");
    }
    for (int i = 0; i < t->indexes.size(); i++) {
        bool touched = false;
        for (int k = 0; k < cols.size(); k++) touched = touched || cols[k] == t->indexes[i].col;
        if (touched)
            note("avl", "AVL index maintenance",
                 t->indexes[i].name + ": for each changed row the old (key, row id) pair was removed and the new one inserted. Rotations LL " +
                     Str::num(d.avlLL) + ", RR " + Str::num(d.avlRR) + ", LR " + Str::num(d.avlLR) + ", RL " + Str::num(d.avlRL) + ".");
    }
    if (!rows.empty())
        note("pool", "Dirty pages",
             "Updated rows were re-encoded in their slots. Those pages are now marked dirty in the buffer pool and will be written at the next checkpoint or when evicted.");

    r.message = plural(rows.size(), "row", "rows") + " updated";
    r.changed = rows.size() > 0;
}

void Executor::remove(Command& c, Result& r) {
    Table* t = table(c.table);
    Scope scope;
    scope.add(t, t->name);
    bind(c.where, scope);

    Access a = plan(t, t->name, c.preds, nullptr, -1, !c.where.empty());
    r.plan = accessText(a, t);
    DynArray<int> rids;
    DynArray<Row> rows;
    fetch(t, a, rids, rows);
    if (!c.where.empty()) filter(c.where, rows, &rids);

    DsStats before = g_stats;
    for (int i = 0; i < rows.size(); i++) t->remove(rids[i], rows[i]);
    DsStats d = g_stats - before;
    int n = rows.size();

    if (t->pk && n > 0)
        note("bptree", "B+ tree delete",
             plural(n, "key", "keys") + " removed from the B+ tree. Underfull nodes were fixed by " +
                 plural(d.bptBorrows, "borrow", "borrows") + " from a sibling and " + plural(d.bptMerges, "merge", "merges") +
                 ". Height is now " + Str::num(t->pk->height()) + ".");
    if (!t->indexes.empty() && n > 0)
        note("avl", "AVL index delete",
             "Removed the rows' (key, row id) pairs from " + plural(t->indexes.size(), "AVL index", "AVL indexes") +
                 "; empty nodes were unlinked and the path rebalanced (rotations LL " + Str::num(d.avlLL) + ", RR " +
                 Str::num(d.avlRR) + ", LR " + Str::num(d.avlLR) + ", RL " + Str::num(d.avlRL) + ").");
    if (n > 0)
        note("array", "Free slots",
             "Each slot's in-use byte was cleared and the page directory counts went down, so later INSERTs reuse the space.");

    r.message = plural(n, "row", "rows") + " deleted";
    r.changed = n > 0;
}

// ---------------------------------------------------------------- SELECT

namespace {
struct OutCol {
    Str label;
    Type type = Type::Int;
    int slot = -1;
    Agg agg = Agg::None;
};

struct Acc {
    long long count = 0;
    long long isum = 0;
    double fsum = 0;
    bool any = false;
    Value best;   // MIN / MAX so far
};
}

static const char* aggName(Agg a) {
    switch (a) {
    case Agg::Count: return "COUNT";
    case Agg::Sum: return "SUM";
    case Agg::Avg: return "AVG";
    case Agg::Min: return "MIN";
    case Agg::Max: return "MAX";
    default: return "";
    }
}

void Executor::select(Command& c, Result& r) {
    Table* left = table(c.table);
    Str leftAlias = c.alias.empty() ? c.table : c.alias;
    Table* right = nullptr;
    Scope scope;
    scope.add(left, leftAlias);
    if (c.join) {
        right = table(c.joinTable);
        Str rightAlias = c.joinAlias.empty() ? c.joinTable : c.joinAlias;
        if (rightAlias == leftAlias)
            throw DbError("joining " + c.table + " with itself needs an alias, e.g. JOIN " + c.joinTable + " b");
        scope.add(right, rightAlias);
    }
    bind(c.where, scope);

    // output columns
    bool aggregate = !c.groupCol.empty();
    DynArray<OutCol> outs;
    for (int i = 0; i < c.items.size(); i++) {
        const SelectItem& it = c.items[i];
        if (it.star) {
            for (int k = 0; k < scope.cols.size(); k++) {
                OutCol o;
                o.label = scope.cols[k].name;
                o.type = scope.cols[k].type;
                o.slot = k;
                outs.push(o);
            }
            continue;
        }
        OutCol o;
        o.agg = it.agg;
        if (!it.countStar) o.slot = scope.need(it.table, it.column);
        if (it.agg == Agg::None) {
            o.label = it.alias.empty() ? it.column : it.alias;
            o.type = scope.cols[o.slot].type;
        } else {
            aggregate = true;
            Type src = it.countStar ? Type::Int : scope.cols[o.slot].type;
            if ((it.agg == Agg::Sum || it.agg == Agg::Avg) && src != Type::Int && src != Type::Float)
                throw DbError(Str(aggName(it.agg)) + " needs a number column, " + it.column + " is " + typeName(src));
            o.type = it.agg == Agg::Count ? Type::Int : it.agg == Agg::Avg ? Type::Float : src;
            o.label = it.alias.empty() ? Str(aggName(it.agg)) + "(" + (it.countStar ? Str("*") : it.column) + ")" : it.alias;
        }
        outs.push(o);
    }

    // ORDER BY a source column can sometimes come straight out of an index
    int orderSlot = c.hasOrder ? scope.find(c.orderTable, c.orderCol) : -1;
    int indexOrderCol = (!c.join && !aggregate) ? orderSlot : -1;

    Access a = plan(left, leftAlias, c.preds, right, indexOrderCol, !c.where.empty());
    r.plan = accessText(a, left);
    bool sorted = indexOrderCol >= 0 && a.index && a.col == indexOrderCol;
    bool earlyLimit = sorted && c.limit >= 0 && c.where.empty();
    DynArray<int> rids;
    DynArray<Row> rows;
    fetch(left, a, rids, rows, earlyLimit ? (int)c.limit : -1, c.orderDesc);

    if (c.join) joinRows(c, scope, left, right, rows, r);
    if (!c.where.empty()) filter(c.where, rows, nullptr);

    DynArray<Row> result;
    int sortOut = -1;   // ORDER BY on an output column, applied after projection

    if (!aggregate) {
        if (c.hasOrder && orderSlot >= 0) {
            if (sorted) {
                note(a.index->tag(), "ORDER BY from the index",
                     "Rows came out of the " + Str(a.index->kind()) + " in " + c.orderCol + " order, so no sort was needed" +
                         (c.orderDesc ? Str(" - just a reverse for DESC.") : Str(".")));
            } else {
                long long cmp0 = g_stats.sortCompares;
                sortRows(rows, orderSlot);
                note("sort", "ORDER BY merge sort",
                     "Bottom-up merge sort on " + c.orderCol + ": " + plural(rows.size(), "row", "rows") + ", " +
                         Str::num(g_stats.sortCompares - cmp0) + " comparisons (O(n log n), stable).");
            }
            if (c.orderDesc) rows.reverse();
        } else if (c.hasOrder) {
            for (int k = 0; k < outs.size(); k++)
                if (outs[k].label == c.orderCol) sortOut = k;
            if (sortOut < 0) throw DbError("ORDER BY: unknown column '" + c.orderCol + "'");
        }
        if (c.limit >= 0 && sortOut < 0)
            while (rows.size() > c.limit) rows.pop();
        for (int i = 0; i < rows.size(); i++) {
            Row o;
            for (int k = 0; k < outs.size(); k++) o.push(rows[i][outs[k].slot]);
            result.push(std::move(o));
        }
    } else {
        int groupSlot = c.groupCol.empty() ? -1 : scope.need(c.groupTable, c.groupCol);
        HashTable<Value, int> groupOf(32);
        DynArray<Row> firstRow;
        DynArray<DynArray<Acc>> accs;
        long long probes0 = g_stats.hashProbes;

        for (int i = 0; i < rows.size(); i++) {
            const Row& row = rows[i];
            int g = 0;
            if (groupSlot >= 0) {
                int* found = groupOf.find(row[groupSlot]);
                if (found) {
                    g = *found;
                } else {
                    g = firstRow.size();
                    groupOf.put(row[groupSlot], g);
                }
            }
            if (g == firstRow.size()) {
                firstRow.push(row);
                DynArray<Acc> fresh;
                fresh.resize(outs.size());
                accs.push(std::move(fresh));
            }
            for (int k = 0; k < outs.size(); k++) {
                const OutCol& o = outs[k];
                if (o.agg == Agg::None) continue;
                Acc& acc = accs[g][k];
                acc.count++;
                if (o.slot < 0) continue;   // COUNT(*)
                const Value& v = row[o.slot];
                if (v.type == Type::Int) acc.isum += v.i;
                acc.fsum += v.asDouble();
                if (!acc.any || (o.agg == Agg::Min && compareKeys(v, acc.best) < 0) ||
                    (o.agg == Agg::Max && compareKeys(v, acc.best) > 0))
                    acc.best = v;
                acc.any = true;
            }
        }
        if (groupSlot < 0 && firstRow.empty()) {   // aggregate over nothing: one row of zeros
            Row blank;
            for (int k = 0; k < scope.cols.size(); k++) blank.push(zeroOf(scope.cols[k].type));
            firstRow.push(blank);
            DynArray<Acc> fresh;
            fresh.resize(outs.size());
            accs.push(std::move(fresh));
        }

        for (int g = 0; g < firstRow.size(); g++) {
            Row o;
            for (int k = 0; k < outs.size(); k++) {
                const OutCol& oc = outs[k];
                const Acc& acc = accs[g][k];
                switch (oc.agg) {
                case Agg::None: o.push(firstRow[g][oc.slot]); break;
                case Agg::Count: o.push(Value::ofInt(acc.count)); break;
                case Agg::Sum: o.push(oc.type == Type::Int ? Value::ofInt(acc.isum) : Value::ofFloat(acc.fsum)); break;
                case Agg::Avg: o.push(Value::ofFloat(acc.count ? acc.fsum / acc.count : 0)); break;
                default: o.push(acc.any ? acc.best : zeroOf(oc.type)); break;
                }
            }
            result.push(std::move(o));
        }

        if (groupSlot >= 0)
            note("hash", "GROUP BY hash table",
                 "Grouped " + plural(rows.size(), "row", "rows") + " into " + plural(firstRow.size(), "group", "groups") +
                     " using a hash table keyed on " + c.groupCol + " (" + Str::num(groupOf.bucketCount()) + " buckets, " +
                     Str::num(g_stats.hashProbes - probes0) + " probes). Each group keeps running COUNT/SUM/MIN/MAX accumulators.");
        else
            note("array", "Aggregate",
                 "Folded " + plural(rows.size(), "row", "rows") + " into one result row in a single pass, keeping running totals.");

        if (c.hasOrder) {
            for (int k = 0; k < outs.size(); k++)
                if (outs[k].label == c.orderCol || (outs[k].agg == Agg::None && outs[k].slot == orderSlot)) sortOut = k;
            if (sortOut < 0) throw DbError("ORDER BY " + c.orderCol + " must name a selected column or alias");
        }
    }

    if (sortOut >= 0) {
        long long cmp0 = g_stats.sortCompares;
        sortRows(result, sortOut);
        if (c.orderDesc) result.reverse();
        note("sort", "ORDER BY merge sort",
             "Sorted the " + plural(result.size(), "output row", "output rows") + " on " + outs[sortOut].label + " with merge sort: " +
                 Str::num(g_stats.sortCompares - cmp0) + " comparisons.");
    }
    if (c.limit >= 0)
        while (result.size() > c.limit) result.pop();

    for (int k = 0; k < outs.size(); k++) {
        r.columns.push(outs[k].label);
        r.types.push(outs[k].type);
    }
    r.rows = std::move(result);
    note("array", "Result set",
         plural(r.rows.size(), "row", "rows") + " x " + plural(outs.size(), "column", "columns") +
             " collected in a DynArray<Row> (each row is itself a DynArray<Value>).");
    r.message = plural(r.rows.size(), "row", "rows");
}

// ---------------------------------------------------------------- planning

static void tighten(Bound& b, const Value& v, bool inclusive, bool isLower) {
    if (!b.on) {
        b.on = true;
        b.v = v;
        b.inclusive = inclusive;
        return;
    }
    int c = compareKeys(v, b.v);
    bool tighter = isLower ? c > 0 : c < 0;
    if (tighter || (c == 0 && !inclusive)) {
        b.v = v;
        b.inclusive = inclusive;
    }
}

// does this WHERE condition talk about table t?
static bool predOn(const Pred& p, Table* t, const Str& alias, Table* other) {
    if (!p.table.empty()) return p.table == alias || p.table == t->name;
    if (t->colIndex(p.column) < 0) return false;
    return !other || other->colIndex(p.column) < 0;
}

Access Executor::plan(Table* t, const Str& alias, const DynArray<Pred>& preds, Table* other, int orderCol, bool hasWhere) {
    Access best;
    if (!useIndexes) {
        note("sql", "Planner", "Indexes are switched off (benchmark baseline), so this is a full scan.");
        return best;
    }

    // score every indexed column against the WHERE conditions
    int bestScore = 0;
    for (int col = 0; col < t->cols.size(); col++) {
        Index* ix = t->indexOn(col);
        if (!ix) continue;
        Access a;
        a.index = ix;
        a.col = col;
        bool eq = false;
        for (int i = 0; i < preds.size(); i++) {
            const Pred& p = preds[i];
            if (p.column != t->cols[col].name || !predOn(p, t, alias, other)) continue;
            switch (p.op) {
            case Op::Eq: eq = true; a.key = p.a; break;
            case Op::Gt: tighten(a.lo, p.a, false, true); break;
            case Op::Ge: tighten(a.lo, p.a, true, true); break;
            case Op::Lt: tighten(a.hi, p.a, false, false); break;
            case Op::Le: tighten(a.hi, p.a, true, false); break;
            case Op::Between:
                tighten(a.lo, p.a, true, true);
                tighten(a.hi, p.b, true, false);
                break;
            default: break;
            }
        }
        bool pk = col == t->pkCol;
        int score = 0;
        if (eq) {
            a.kind = Access::Lookup;
            score = pk ? 4 : 3;
        } else if (a.lo.on || a.hi.on) {
            a.kind = Access::Range;
            score = pk ? 2 : 1;
        }
        if (score > bestScore) {
            bestScore = score;
            best = a;
        }
    }
    if (bestScore == 0 && orderCol >= 0 && t->indexOn(orderCol)) {
        best.kind = Access::Ordered;
        best.index = t->indexOn(orderCol);
        best.col = orderCol;
    }

    // explain the decision
    Str col = best.col >= 0 ? t->cols[best.col].name : Str();
    Str ixName = best.col >= 0 ? t->indexName(best.col) : Str();
    switch (best.kind) {
    case Access::Lookup:
        note("sql", "Planner",
             best.col == t->pkCol
                 ? col + " is the primary key and the WHERE fixes it to " + best.key.toSql() +
                       ", so a B+ tree point lookup, O(log n), beats scanning all " + plural(t->rowCount, "row", "rows") + "."
                 : col + " has an AVL index (" + ixName + ") and the WHERE fixes it to " + best.key.toSql() +
                       ", so the tree is searched instead of scanning.");
        break;
    case Access::Range:
        note("sql", "Planner",
             "The WHERE limits " + col + " to " + rangeText(col, best.lo, best.hi) + " and " + col + " is indexed by " +
                 (best.col == t->pkCol ? Str("the primary-key B+ tree, so the leaf chain is walked across the range.")
                                       : "the AVL tree " + ixName + ", so only the matching subtrees are visited."));
        break;
    case Access::Ordered:
        note("sql", "Planner",
             "No WHERE condition hits an index, but ORDER BY " + col + " matches the " + Str(best.index->kind()) +
                 ", so rows will be read in key order and need no sort.");
        break;
    case Access::Scan: {
        Str why;
        if (!hasWhere) why = "There is no WHERE clause";
        else if (preds.empty()) why = "The WHERE has no simple 'column op value' condition joined by AND";
        else {
            why = "None of the conditions are on an indexed column";
            for (int i = 0; i < preds.size(); i++)
                if (predOn(preds[i], t, alias, other)) {
                    why += " (CREATE INDEX on " + preds[i].column + " would help)";
                    break;
                }
        }
        note("sql", "Planner", why + ", so every page of " + t->name + " is scanned.");
        break;
    }
    }
    return best;
}

// limit >= 0: the caller only needs that many rows from the front (or the
// back, for DESC) of the index order, so the rest are never read
void Executor::fetch(Table* t, const Access& a, DynArray<int>& rids, DynArray<Row>& rows, int limit, bool fromEnd) {
    if (a.kind == Access::Scan) {
        int pages = 0;
        for (int p = 0; p < t->pageCount(); p++)
            if (t->pageFill(p) > 0) pages++;
        t->scan(rids, rows);
        note("array", "Full table scan",
             "Walked the page directory and read " + plural(pages, "page", "pages") + " through the buffer pool, decoding " +
                 plural(rows.size(), "row", "rows") + ". Cost grows linearly with the table: O(n).");
        return;
    }

    DsStats before = g_stats;
    if (a.kind == Access::Lookup) a.index->find(a.key, rids);
    else if (a.kind == Access::Range) a.index->range(a.lo, a.hi, rids);
    else a.index->all(rids);
    DsStats d = g_stats - before;

    Str col = t->cols[a.col].name;
    Str h = Str::num(a.index->height());
    Str found = plural(rids.size(), "row id", "row ids");
    if (isBPlus(a.index)) {
        if (a.kind == Access::Lookup)
            note("bptree", "B+ tree search",
                 "Went from the root down to a leaf comparing separator keys: " + plural(d.bptVisits, "node", "nodes") +
                     " visited (height " + h + "). " + (rids.empty() ? Str("Key not found.") : "Found row id " + Str::num(rids[0]) + "."));
        else if (a.kind == Access::Range)
            note("bptree", "B+ tree range scan",
                 "Descended " + plural(d.bptVisits, "node", "nodes") + " to the leaf holding the lower bound, then followed " +
                     plural(d.bptLeafHops, "leaf link", "leaf links") + " to the right. " + found + ", already sorted by " + col + ".");
        else
            note("bptree", "B+ tree leaf chain",
                 "Went down the left edge to the first leaf and followed the leaf links to the end (" +
                     plural(d.bptLeafHops, "hop", "hops") + "): " + found + " in " + col + " order.");
    } else {
        if (a.kind == Access::Lookup)
            note("avl", "AVL search",
                 "Binary search down the AVL tree: " + plural(d.avlVisits, "node", "nodes") + " visited (height " + h +
                     "). The node for that key holds a list of " + found + " since " + col + " isn't unique.");
        else if (a.kind == Access::Range)
            note("avl", "AVL range walk",
                 "In-order walk that skips subtrees outside the range: " + plural(d.avlVisits, "node", "nodes") + " visited, " +
                     found + " in key order.");
        else
            note("avl", "AVL in-order walk",
                 "Full in-order walk (" + plural(d.avlVisits, "node", "nodes") + "): " + found + " in " + col + " order.");
    }

    if (limit >= 0 && rids.size() > limit) {
        int total = rids.size();
        if (fromEnd) {
            DynArray<int> tail;
            for (int i = total - limit; i < total; i++) tail.push(rids[i]);
            rids = std::move(tail);
        } else {
            while (rids.size() > limit) rids.pop();
        }
        note("array", "LIMIT pushed into the index",
             "The index already returns rows in order, so only " + plural(limit, "row id", "row ids") + " of " +
                 Str::num(total) + " need their row read. The other pages are never touched.");
    }

    for (int i = 0; i < rids.size(); i++) {
        Row row;
        t->read(rids[i], row);
        rows.push(std::move(row));
    }
    if (!rids.empty())
        note("array", "Rows by id",
             "Each row id was turned into page = rid / " + Str::num(t->slotsPerPage) + ", slot = rid % " +
                 Str::num(t->slotsPerPage) + " and read through the buffer pool.");
}

void Executor::joinRows(Command& c, const Scope& scope, Table* left, Table* right, DynArray<Row>& rows, Result& r) {
    int a = scope.need(c.onLeftTable, c.onLeftCol);
    int b = scope.need(c.onRightTable, c.onRightCol);
    int nl = left->cols.size();
    if ((a < nl) == (b < nl))
        throw DbError("JOIN ... ON must compare a column of " + left->name + " with a column of " + right->name);
    int leftKey = a < nl ? a : b;
    int rightCol = (a < nl ? b : a) - nl;
    Str rname = right->name + "." + right->cols[rightCol].name;
    Index* ix = useIndexes ? right->indexOn(rightCol) : nullptr;

    DynArray<Row> out;
    DsStats before = g_stats;
    if (ix) {
        for (int i = 0; i < rows.size(); i++) {
            DynArray<int> rids;
            ix->find(rows[i][leftKey], rids);
            for (int k = 0; k < rids.size(); k++) {
                Row rr;
                right->read(rids[k], rr);
                out.push(concat(rows[i], rr));
            }
        }
        DsStats d = g_stats - before;
        note(ix->tag(), "Index nested-loop join",
             "For each of the " + plural(rows.size(), "row", "rows") + " from " + left->name + ", searched the " + ix->kind() +
                 " on " + rname + " for a match: " + Str::num(d.bptVisits + d.avlVisits) + " node visits in total, " +
                 plural(out.size(), "joined row", "joined rows") + ".");
        r.plan += ", index join on " + rname;
    } else {
        DynArray<int> rr;
        DynArray<Row> rightRows;
        right->scan(rr, rightRows);
        HashTable<Value, DynArray<int>> build(rightRows.size() + 16);
        for (int i = 0; i < rightRows.size(); i++) build.at(rightRows[i][rightCol]).push(i);
        long long probes0 = g_stats.hashProbes;
        for (int i = 0; i < rows.size(); i++) {
            DynArray<int>* match = build.find(rows[i][leftKey]);
            if (!match) continue;
            for (int k = 0; k < match->size(); k++) out.push(concat(rows[i], rightRows[(*match)[k]]));
        }
        note("hash", "Hash join",
             "No index on " + rname + ", so the engine built a hash table over all " + plural(rightRows.size(), "row", "rows") +
                 " of " + right->name + " keyed on the join column (" + Str::num(build.bucketCount()) +
                 " buckets), then probed it once per left row: " + Str::num(g_stats.hashProbes - probes0) + " chain probes, " +
                 plural(out.size(), "match", "matches") + ". That's O(n + m) instead of comparing every pair, O(n x m).");
        r.plan += ", hash join on " + rname;
    }
    rows = std::move(out);
}

// ---------------------------------------------------------------- expressions

void Executor::bind(Expr& e, const Scope& s) {
    for (int i = 0; i < e.code.size(); i++)
        if (e.code[i].op == Op::PushCol) e.code[i].slot = s.need(e.code[i].table, e.code[i].column);
}

// the postfix stack machine
Value Executor::eval(const Expr& e, const Row& row) {
    while (!stack_.empty()) stack_.pop();   // only non-empty if a previous row threw
    for (int k = 0; k < e.code.size(); k++) {
        const ExprOp& o = e.code[k];
        switch (o.op) {
        case Op::PushLit:
            stack_.push(o.lit);
            break;
        case Op::PushCol:
            stack_.push(row[o.slot]);
            break;
        case Op::Not: {
            Value v = stack_.pop();
            stack_.push(Value::ofBool(!truthy(v)));
            break;
        }
        case Op::Neg: {
            Value v = stack_.pop();
            if (v.type == Type::Int) v.i = -v.i;
            else if (v.type == Type::Float) v.f = -v.f;
            else throw DbError("can't negate " + v.toSql());
            stack_.push(v);
            break;
        }
        case Op::Between: {
            Value hi = stack_.pop();
            Value lo = stack_.pop();
            Value x = stack_.pop();
            stack_.push(Value::ofBool(compareKeys(x, lo) >= 0 && compareKeys(x, hi) <= 0));
            break;
        }
        default: {
            Value b = stack_.pop();
            Value a = stack_.pop();
            stack_.push(apply(o.op, a, b));
        }
        }
    }
    return stack_.pop();
}

void Executor::filter(const Expr& where, DynArray<Row>& rows, DynArray<int>* rids) {
    long long pushes0 = g_stats.stackPush;
    DynArray<Row> keptRows;
    DynArray<int> keptRids;
    int total = rows.size();
    for (int i = 0; i < total; i++) {
        if (!truthy(eval(where, rows[i]))) continue;
        keptRows.push(std::move(rows[i]));
        if (rids) keptRids.push((*rids)[i]);
    }
    note("stack", "WHERE on a stack",
         "Ran the postfix program [" + where.postfix() + "] once per candidate row: operands are pushed, each operator pops its inputs and pushes the result. " +
             plural(total, "row", "rows") + " in, " + Str::num(keptRows.size()) + " matched, " +
             Str::num(g_stats.stackPush - pushes0) + " pushes.");
    rows = std::move(keptRows);
    if (rids) *rids = std::move(keptRids);
}
