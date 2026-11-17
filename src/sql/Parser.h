#pragma once
#include "ds/Queue.h"
#include "sql/Command.h"
#include "util/Error.h"

// Recursive descent: one method per grammar rule, each calling the rules
// below it. Tokens come off the front of the lexer's queue.
//
//   expr    := or
//   or      := and  { OR and }
//   and     := not  { AND not }
//   not     := NOT not | compare
//   compare := sum [ (= != < <= > >=) sum | BETWEEN sum AND sum | LIKE sum ]
//   sum     := product { (+ -) product }
//   product := unary { (* / %) unary }
//   unary   := - unary | primary
//   primary := literal | column | ( expr )
//
// Each rule appends its operator *after* its operands, which is exactly
// postfix order, so there is no separate conversion step.
class Parser {
public:
    Parser(Queue<Token>& tokens, const Str& source);

    bool done();
    Command next();   // throws DbError

    // set when next() throws, so the error can still show its statement
    Str failedSql;
    int failedLine = 1;    // where the statement starts
    int errorLine = 1;     // where the parser gave up
    int errorCol = 1;
    int errorLength = 1;   // characters of the offending token

private:
    const Token& peek(int k = 0);
    Token take();
    bool isKw(const char* kw, int k = 0);
    bool isSym(const char* s, int k = 0);
    bool acceptKw(const char* kw);
    bool acceptSym(const char* s);
    void expectKw(const char* kw);
    void expectSym(const char* s);
    Str expectIdent(const char* what);
    DbError error(const Str& msg);

    void createStmt(Command& c);
    void dropStmt(Command& c);
    void insertStmt(Command& c);
    void selectStmt(Command& c);
    void updateStmt(Command& c);
    void deleteStmt(Command& c);

    Value literal();
    void columnRef(Str& table, Str& column);
    void tableRef(Str& table, Str& alias);
    void whereClause(Command& c);

    void orExpr(Expr& e);
    void andExpr(Expr& e);
    void notExpr(Expr& e);
    void compareExpr(Expr& e);
    void sumExpr(Expr& e);
    void productExpr(Expr& e);
    void unaryExpr(Expr& e);
    void primaryExpr(Expr& e);
    void emit(Expr& e, Op op);

    Queue<Token>& q_;
    const Str& src_;
    Token end_;
    Command* cur_;
    int lastEnd_ = 0;    // end offset of the last token taken
    int lastLine_ = 1;   // line of the last token taken
    int lastCol_ = 1;    // column just after the last token taken

    // predicate collection for the planner
    bool collecting_ = false;
    int depth_ = 0;          // > 0 inside NOT / parentheses
    bool sawTopOr_ = false;
};
