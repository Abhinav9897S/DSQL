#pragma once
#include "ds/DynArray.h"
#include "storage/Value.h"
#include "sql/Lexer.h"

enum class Cmd { CreateTable, CreateIndex, DropTable, DropIndex, Insert, Select, Update, Delete, ShowTables, Describe };

const char* cmdName(Cmd c);

// Expressions are stored in postfix (reverse Polish) order, e.g.
//   age > 20 AND city = 'Delhi'   ->   age 20 > city 'Delhi' = AND
// so evaluating one is a simple loop over a stack: push operands, and for
// each operator pop its inputs and push the result.
enum class Op {
    PushLit, PushCol,
    Add, Sub, Mul, Div, Mod, Neg,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or, Not,
    Between, Like,
};

const char* opText(Op op);

struct ExprOp {
    Op op = Op::PushLit;
    Value lit;      // PushLit
    Str table;      // PushCol, optional qualifier
    Str column;     // PushCol
    int slot = -1;  // PushCol: position in the row, filled in by the executor
};

struct Expr {
    DynArray<ExprOp> code;
    bool empty() const { return code.empty(); }
    Str postfix() const;
};

// A simple "column op literal" condition from the top-level AND chain of a
// WHERE clause. These are what the planner checks against the indexes.
struct Pred {
    Str table;
    Str column;
    Op op = Op::Eq;   // Eq, Lt, Le, Gt, Ge or Between
    Value a, b;       // b only for Between
};

enum class Agg { None, Count, Sum, Avg, Min, Max };

struct SelectItem {
    bool star = false;
    Agg agg = Agg::None;
    bool countStar = false;
    Str table;
    Str column;
    Str alias;
};

struct ColDef {
    Str name;
    Type type = Type::Int;
    int size = 0;
    bool pk = false;
};

// One parsed statement. Only the fields that matter for `kind` are filled.
struct Command {
    Cmd kind = Cmd::Select;
    Str sql;                    // this statement's source text
    int line = 1;
    DynArray<Token> tokens;     // what the parser consumed (shown in the UI)

    Str table, alias;
    bool ifExists = false;      // DROP TABLE IF EXISTS / CREATE TABLE IF NOT EXISTS

    DynArray<ColDef> cols;      // CREATE TABLE
    Str index, indexCol;        // CREATE / DROP INDEX

    DynArray<Str> insertCols;   // INSERT
    DynArray<Row> rows;

    DynArray<SelectItem> items; // SELECT
    bool join = false;
    Str joinTable, joinAlias;
    Str onLeftTable, onLeftCol, onRightTable, onRightCol;
    Str groupTable, groupCol;
    bool hasOrder = false;
    Str orderTable, orderCol;
    bool orderDesc = false;
    long long limit = -1;

    DynArray<Str> setCols;      // UPDATE
    DynArray<Expr> setExprs;

    Expr where;
    DynArray<Pred> preds;
};
