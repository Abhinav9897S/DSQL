#pragma once
#include "ds/Stack.h"
#include "ds/Stats.h"
#include "sql/Command.h"
#include "storage/Catalog.h"
#include "util/Trace.h"

// Everything the UI / CLI needs to show for one statement.
struct Result {
    bool ok = true;
    Str kind;           // "SELECT", "INSERT", ...
    Str sql;
    int line = 1;
    int errorLine = 0;  // line to flag in the editor when !ok
    Str message;
    Str plan;           // one-line access path summary
    DynArray<Str> columns;
    DynArray<Type> types;
    DynArray<Row> rows;
    double ms = 0;
    Trace trace;        // step-by-step story for the side panel
    DsStats stats;      // counters used by this statement only
    DynArray<Token> tokens;
    bool changed = false;   // schema or data changed
};

// The columns a query can see. For a join it's left table's columns followed
// by the right table's, which is also the layout of a joined row.
struct ScopeCol {
    Str alias;
    Str table;
    Str name;
    Type type = Type::Int;
};

struct Scope {
    DynArray<ScopeCol> cols;
    void add(Table* t, const Str& alias);
    int find(const Str& table, const Str& name) const;   // -1 if missing
    int need(const Str& table, const Str& name) const;   // throws if missing
};

// How rows of one table get produced.
struct Access {
    enum Kind { Scan, Lookup, Range, Ordered };
    Kind kind = Scan;
    Index* index = nullptr;
    int col = -1;
    Value key;          // Lookup
    Bound lo, hi;       // Range
};

class Executor {
public:
    explicit Executor(Catalog& cat) : cat_(cat) {}

    // runs every statement in `sql`, stopping at the first error;
    // firstLine is the editor line the text starts on
    void run(const Str& sql, DynArray<Result>& out, int firstLine = 1);

    bool useIndexes = true;   // the benchmark turns this off to compare

private:
    void execute(Command& c, Result& r);
    void createTable(Command& c, Result& r);
    void createIndex(Command& c, Result& r);
    void dropTable(Command& c, Result& r);
    void dropIndex(Command& c, Result& r);
    void insert(Command& c, Result& r);
    void select(Command& c, Result& r);
    void update(Command& c, Result& r);
    void remove(Command& c, Result& r);
    void showTables(Result& r);
    void describe(Command& c, Result& r);

    Table* table(const Str& name);
    Access plan(Table* t, const Str& alias, const DynArray<Pred>& preds, Table* other, int orderCol, bool hasWhere);
    void fetch(Table* t, const Access& a, DynArray<int>& rids, DynArray<Row>& rows, int limit = -1, bool fromEnd = false);
    void joinRows(Command& c, const Scope& scope, Table* left, Table* right, DynArray<Row>& rows, Result& r);
    void filter(const Expr& where, DynArray<Row>& rows, DynArray<int>* rids);
    void bind(Expr& e, const Scope& s);
    Value eval(const Expr& e, const Row& row);
    void noteParse(const Command& c);

    Catalog& cat_;
    Stack<Value> stack_;   // reused for every WHERE evaluation
};
