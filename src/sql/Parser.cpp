#include "sql/Parser.h"
#include "util/Error.h"
#include <cstdlib>

Parser::Parser(Queue<Token>& tokens, const Str& source) : q_(tokens), src_(source), cur_(nullptr) {
    end_.type = Tok::End;
    end_.text = "end of input";
}

bool Parser::done() {
    while (!q_.empty() && q_.peek().type == Tok::Symbol && q_.peek().text == ";") q_.pop();
    return q_.empty();
}

const Token& Parser::peek(int k) { return k < q_.size() ? q_.peek(k) : end_; }

Token Parser::take() {
    if (q_.empty()) return end_;
    Token t = q_.pop();
    lastEnd_ = t.end;
    lastLine_ = t.line;
    if (cur_) cur_->tokens.push(t);
    return t;
}

bool Parser::isKw(const char* kw, int k) {
    const Token& t = peek(k);
    return t.type == Tok::Keyword && t.up == kw;
}

bool Parser::isSym(const char* s, int k) {
    const Token& t = peek(k);
    return t.type == Tok::Symbol && t.text == s;
}

bool Parser::acceptKw(const char* kw) {
    if (!isKw(kw)) return false;
    take();
    return true;
}

bool Parser::acceptSym(const char* s) {
    if (!isSym(s)) return false;
    take();
    return true;
}

static Str describe(const Token& t) {
    if (t.type == Tok::End) return "end of input";
    return "'" + t.text + "'";
}

DbError Parser::error(const Str& msg) {
    const Token& t = peek();
    errorLine = t.type == Tok::End ? lastLine_ : t.line;
    Str where = t.type == Tok::End ? Str("at the end") : "at line " + Str::num(t.line) + ", column " + Str::num(t.col);
    return DbError("Syntax error " + where + ": " + msg);
}

void Parser::expectKw(const char* kw) {
    if (!acceptKw(kw)) throw error(Str("expected ") + kw + " but found " + describe(peek()));
}

void Parser::expectSym(const char* s) {
    if (!acceptSym(s)) throw error(Str("expected '") + s + "' but found " + describe(peek()));
}

Str Parser::expectIdent(const char* what) {
    const Token& t = peek();
    if (t.type == Tok::Keyword) throw error("'" + t.text + "' is a reserved word, it can't be used as a " + what);
    if (t.type != Tok::Ident) throw error(Str("expected a ") + what + " but found " + describe(t));
    return take().text.lower();
}

Command Parser::next() {
    Command c;
    cur_ = &c;
    c.line = peek().line;
    int start = peek().start;
    lastEnd_ = start;
    try {
        if (acceptKw("CREATE")) createStmt(c);
        else if (acceptKw("DROP")) dropStmt(c);
        else if (acceptKw("INSERT")) insertStmt(c);
        else if (acceptKw("SELECT")) selectStmt(c);
        else if (acceptKw("UPDATE")) updateStmt(c);
        else if (acceptKw("DELETE")) deleteStmt(c);
        else if (acceptKw("SHOW")) {
            expectKw("TABLES");
            c.kind = Cmd::ShowTables;
        } else if (acceptKw("DESCRIBE") || acceptKw("DESC")) {
            c.kind = Cmd::Describe;
            c.table = expectIdent("table name");
        } else {
            throw error("expected a statement (SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, SHOW, DESCRIBE) but found " +
                        describe(peek()));
        }
        int stop = lastEnd_;
        if (!acceptSym(";") && peek().type != Tok::End)
            throw error("expected ';' or end of statement but found " + describe(peek()));
        c.sql = src_.substr(start, stop - start);
    } catch (DbError&) {
        // eat the rest of the broken statement so we can show all of it
        cur_ = nullptr;
        while (!q_.empty() && !(q_.peek().type == Tok::Symbol && q_.peek().text == ";")) take();
        failedSql = src_.substr(start, lastEnd_ - start);
        failedLine = c.line;
        throw;
    }
    cur_ = nullptr;
    return c;
}

static bool typeFromName(const Str& up, Type& t, int& size) {
    if (up == "INT" || up == "INTEGER" || up == "BIGINT") { t = Type::Int; return true; }
    if (up == "FLOAT" || up == "REAL" || up == "DOUBLE") { t = Type::Float; return true; }
    if (up == "BOOL" || up == "BOOLEAN") { t = Type::Bool; return true; }
    if (up == "VARCHAR" || up == "CHAR") { t = Type::Varchar; size = 64; return true; }
    if (up == "TEXT") { t = Type::Varchar; size = 255; return true; }
    return false;
}

void Parser::createStmt(Command& c) {
    if (acceptKw("INDEX")) {
        c.kind = Cmd::CreateIndex;
        c.index = expectIdent("index name");
        expectKw("ON");
        c.table = expectIdent("table name");
        expectSym("(");
        c.indexCol = expectIdent("column name");
        expectSym(")");
        return;
    }
    if (!acceptKw("TABLE")) throw error("expected TABLE or INDEX after CREATE");

    c.kind = Cmd::CreateTable;
    if (acceptKw("IF")) {
        expectKw("NOT");
        expectKw("EXISTS");
        c.ifExists = true;
    }
    c.table = expectIdent("table name");
    expectSym("(");
    do {
        if (acceptKw("PRIMARY")) {   // table-level: PRIMARY KEY (col)
            expectKw("KEY");
            expectSym("(");
            Str name = expectIdent("column name");
            expectSym(")");
            bool found = false;
            for (int i = 0; i < c.cols.size(); i++)
                if (c.cols[i].name == name) c.cols[i].pk = found = true;
            if (!found) throw DbError("PRIMARY KEY names unknown column '" + name + "'");
            continue;
        }
        ColDef d;
        d.name = expectIdent("column name");
        Token t = peek();
        if (t.type != Tok::Ident || !typeFromName(t.up, d.type, d.size))
            throw error("expected a type (INT, FLOAT, VARCHAR(n), BOOL) but found " + describe(t));
        take();
        if (d.type == Type::Varchar && acceptSym("(")) {
            Token n = take();
            if (n.type != Tok::Int) throw error("expected a length inside VARCHAR( )");
            d.size = (int)strtol(n.text.c_str(), nullptr, 10);
            if (d.size < 1 || d.size > 255) throw DbError("VARCHAR length must be between 1 and 255");
            expectSym(")");
        }
        if (acceptKw("PRIMARY")) {
            expectKw("KEY");
            d.pk = true;
        }
        c.cols.push(d);
    } while (acceptSym(","));
    expectSym(")");
}

void Parser::dropStmt(Command& c) {
    if (acceptKw("TABLE")) {
        c.kind = Cmd::DropTable;
        if (acceptKw("IF")) {
            expectKw("EXISTS");
            c.ifExists = true;
        }
        c.table = expectIdent("table name");
    } else if (acceptKw("INDEX")) {
        c.kind = Cmd::DropIndex;
        c.index = expectIdent("index name");
    } else {
        throw error("expected TABLE or INDEX after DROP");
    }
}

void Parser::insertStmt(Command& c) {
    c.kind = Cmd::Insert;
    expectKw("INTO");
    c.table = expectIdent("table name");
    if (acceptSym("(")) {
        do c.insertCols.push(expectIdent("column name"));
        while (acceptSym(","));
        expectSym(")");
    }
    expectKw("VALUES");
    do {
        expectSym("(");
        Row r;
        do r.push(literal());
        while (acceptSym(","));
        expectSym(")");
        c.rows.push(std::move(r));
    } while (acceptSym(","));
}

void Parser::selectStmt(Command& c) {
    c.kind = Cmd::Select;
    if (acceptSym("*")) {
        SelectItem it;
        it.star = true;
        c.items.push(it);
    } else {
        do {
            SelectItem it;
            if (peek().type == Tok::Ident && isSym("(", 1)) {
                Str fn = peek().up;
                if (fn == "COUNT") it.agg = Agg::Count;
                else if (fn == "SUM") it.agg = Agg::Sum;
                else if (fn == "AVG") it.agg = Agg::Avg;
                else if (fn == "MIN") it.agg = Agg::Min;
                else if (fn == "MAX") it.agg = Agg::Max;
                else throw error("unknown function " + fn + " (try COUNT, SUM, AVG, MIN, MAX)");
                take();
                expectSym("(");
                if (acceptSym("*")) {
                    if (it.agg != Agg::Count) throw error("only COUNT accepts *");
                    it.countStar = true;
                } else {
                    columnRef(it.table, it.column);
                }
                expectSym(")");
            } else {
                columnRef(it.table, it.column);
            }
            if (acceptKw("AS")) it.alias = expectIdent("alias");
            c.items.push(it);
        } while (acceptSym(","));
    }

    expectKw("FROM");
    tableRef(c.table, c.alias);
    if (acceptKw("INNER")) {
        expectKw("JOIN");
        c.join = true;
    } else if (acceptKw("JOIN")) {
        c.join = true;
    }
    if (c.join) {
        tableRef(c.joinTable, c.joinAlias);
        expectKw("ON");
        columnRef(c.onLeftTable, c.onLeftCol);
        expectSym("=");
        columnRef(c.onRightTable, c.onRightCol);
    }

    whereClause(c);

    if (acceptKw("GROUP")) {
        expectKw("BY");
        columnRef(c.groupTable, c.groupCol);
    }
    if (acceptKw("ORDER")) {
        expectKw("BY");
        c.hasOrder = true;
        columnRef(c.orderTable, c.orderCol);
        if (acceptKw("DESC")) c.orderDesc = true;
        else acceptKw("ASC");
    }
    if (acceptKw("LIMIT")) {
        if (peek().type != Tok::Int) throw error("LIMIT needs a whole number");
        c.limit = strtoll(take().text.c_str(), nullptr, 10);
    }
}

void Parser::updateStmt(Command& c) {
    c.kind = Cmd::Update;
    c.table = expectIdent("table name");
    expectKw("SET");
    do {
        c.setCols.push(expectIdent("column name"));
        expectSym("=");
        Expr e;
        orExpr(e);
        c.setExprs.push(std::move(e));
    } while (acceptSym(","));
    whereClause(c);
}

void Parser::deleteStmt(Command& c) {
    c.kind = Cmd::Delete;
    expectKw("FROM");
    c.table = expectIdent("table name");
    whereClause(c);
}

Value Parser::literal() {
    bool neg = acceptSym("-");
    Token t = peek();
    if (t.type == Tok::Int) {
        take();
        long long v = strtoll(t.text.c_str(), nullptr, 10);
        return Value::ofInt(neg ? -v : v);
    }
    if (t.type == Tok::Float) {
        take();
        double v = strtod(t.text.c_str(), nullptr);
        return Value::ofFloat(neg ? -v : v);
    }
    if (neg) throw error("expected a number after '-'");
    if (t.type == Tok::String) {
        take();
        return Value::ofText(t.text);
    }
    if (acceptKw("TRUE")) return Value::ofBool(true);
    if (acceptKw("FALSE")) return Value::ofBool(false);
    if (isKw("NULL")) throw error("NULL values aren't supported by this engine");
    throw error("expected a value but found " + describe(t));
}

void Parser::columnRef(Str& table, Str& column) {
    Str first = expectIdent("column name");
    if (acceptSym(".")) {
        table = first;
        column = expectIdent("column name");
    } else {
        column = first;
    }
}

void Parser::tableRef(Str& table, Str& alias) {
    table = expectIdent("table name");
    if (acceptKw("AS")) alias = expectIdent("alias");
    else if (peek().type == Tok::Ident) alias = expectIdent("alias");
}

void Parser::whereClause(Command& c) {
    if (!acceptKw("WHERE")) return;
    collecting_ = true;
    depth_ = 0;
    sawTopOr_ = false;
    orExpr(c.where);
    collecting_ = false;
    if (sawTopOr_) c.preds.clear();   // an OR at the top means no single index can answer it
}

void Parser::emit(Expr& e, Op op) {
    ExprOp o;
    o.op = op;
    e.code.push(o);
}

void Parser::orExpr(Expr& e) {
    andExpr(e);
    while (acceptKw("OR")) {
        if (depth_ == 0) sawTopOr_ = true;
        andExpr(e);
        emit(e, Op::Or);
    }
}

void Parser::andExpr(Expr& e) {
    notExpr(e);
    while (acceptKw("AND")) {
        notExpr(e);
        emit(e, Op::And);
    }
}

void Parser::notExpr(Expr& e) {
    if (acceptKw("NOT")) {
        depth_++;
        notExpr(e);
        depth_--;
        emit(e, Op::Not);
    } else {
        compareExpr(e);
    }
}

static bool single(const Expr& e, int from, int to, Op op) {
    return to - from == 1 && e.code[from].op == op;
}

static Op mirror(Op op) {
    switch (op) {
    case Op::Lt: return Op::Gt;
    case Op::Le: return Op::Ge;
    case Op::Gt: return Op::Lt;
    case Op::Ge: return Op::Le;
    default: return op;
    }
}

void Parser::compareExpr(Expr& e) {
    int start = e.code.size();
    sumExpr(e);
    int mid = e.code.size();

    bool negate = false;
    if (isKw("NOT") && (isKw("BETWEEN", 1) || isKw("LIKE", 1))) {
        take();
        negate = true;
    }
    bool track = collecting_ && depth_ == 0 && !negate;

    if (acceptKw("BETWEEN")) {
        sumExpr(e);
        int hiStart = e.code.size();
        expectKw("AND");
        sumExpr(e);
        int stop = e.code.size();
        emit(e, Op::Between);
        if (track && single(e, start, mid, Op::PushCol) && single(e, mid, hiStart, Op::PushLit) &&
            single(e, hiStart, stop, Op::PushLit)) {
            Pred p;
            p.table = e.code[start].table;
            p.column = e.code[start].column;
            p.op = Op::Between;
            p.a = e.code[mid].lit;
            p.b = e.code[hiStart].lit;
            cur_->preds.push(p);
        }
    } else if (acceptKw("LIKE")) {
        sumExpr(e);
        emit(e, Op::Like);
    } else {
        Op op;
        if (isSym("=")) op = Op::Eq;
        else if (isSym("!=") || isSym("<>")) op = Op::Ne;
        else if (isSym("<")) op = Op::Lt;
        else if (isSym("<=")) op = Op::Le;
        else if (isSym(">")) op = Op::Gt;
        else if (isSym(">=")) op = Op::Ge;
        else return;   // a bare value, e.g. WHERE active
        take();
        sumExpr(e);
        int stop = e.code.size();
        emit(e, op);

        if (track && op != Op::Ne) {
            Pred p;
            p.op = op;
            if (single(e, start, mid, Op::PushCol) && single(e, mid, stop, Op::PushLit)) {
                p.table = e.code[start].table;
                p.column = e.code[start].column;
                p.a = e.code[mid].lit;
                cur_->preds.push(p);
            } else if (single(e, start, mid, Op::PushLit) && single(e, mid, stop, Op::PushCol)) {
                p.table = e.code[mid].table;   // 5 < id  is  id > 5
                p.column = e.code[mid].column;
                p.a = e.code[start].lit;
                p.op = mirror(op);
                cur_->preds.push(p);
            }
        }
    }
    if (negate) emit(e, Op::Not);
}

void Parser::sumExpr(Expr& e) {
    productExpr(e);
    while (isSym("+") || isSym("-")) {
        Op op = isSym("+") ? Op::Add : Op::Sub;
        take();
        productExpr(e);
        emit(e, op);
    }
}

void Parser::productExpr(Expr& e) {
    unaryExpr(e);
    while (isSym("*") || isSym("/") || isSym("%")) {
        Op op = isSym("*") ? Op::Mul : (isSym("/") ? Op::Div : Op::Mod);
        take();
        unaryExpr(e);
        emit(e, op);
    }
}

void Parser::unaryExpr(Expr& e) {
    if (!isSym("-")) {
        primaryExpr(e);
        return;
    }
    if (peek(1).type == Tok::Int || peek(1).type == Tok::Float) {   // fold -5 into one literal
        ExprOp o;
        o.op = Op::PushLit;
        o.lit = literal();
        e.code.push(o);
        return;
    }
    take();
    unaryExpr(e);
    emit(e, Op::Neg);
}

void Parser::primaryExpr(Expr& e) {
    const Token& t = peek();
    if (acceptSym("(")) {
        depth_++;
        orExpr(e);
        depth_--;
        expectSym(")");
        return;
    }
    ExprOp o;
    if (t.type == Tok::Ident) {
        o.op = Op::PushCol;
        columnRef(o.table, o.column);
    } else {
        o.op = Op::PushLit;
        o.lit = literal();
    }
    e.code.push(o);
}
