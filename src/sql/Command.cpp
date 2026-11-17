#include "sql/Command.h"

const char* cmdName(Cmd c) {
    switch (c) {
    case Cmd::CreateTable: return "CREATE TABLE";
    case Cmd::CreateIndex: return "CREATE INDEX";
    case Cmd::DropTable: return "DROP TABLE";
    case Cmd::DropIndex: return "DROP INDEX";
    case Cmd::Insert: return "INSERT";
    case Cmd::Select: return "SELECT";
    case Cmd::Update: return "UPDATE";
    case Cmd::Delete: return "DELETE";
    case Cmd::ShowTables: return "SHOW TABLES";
    case Cmd::Describe: return "DESCRIBE";
    }
    return "?";
}

const char* opText(Op op) {
    switch (op) {
    case Op::PushLit: return "lit";
    case Op::PushCol: return "col";
    case Op::Add: return "+";
    case Op::Sub: return "-";
    case Op::Mul: return "*";
    case Op::Div: return "/";
    case Op::Mod: return "%";
    case Op::Neg: return "NEG";
    case Op::Eq: return "=";
    case Op::Ne: return "!=";
    case Op::Lt: return "<";
    case Op::Le: return "<=";
    case Op::Gt: return ">";
    case Op::Ge: return ">=";
    case Op::And: return "AND";
    case Op::Or: return "OR";
    case Op::Not: return "NOT";
    case Op::Between: return "BETWEEN";
    case Op::Like: return "LIKE";
    }
    return "?";
}

Str Expr::postfix() const {
    Str out;
    for (int i = 0; i < code.size(); i++) {
        if (i) out += ' ';
        const ExprOp& o = code[i];
        if (o.op == Op::PushLit) {
            out += o.lit.toSql();
        } else if (o.op == Op::PushCol) {
            if (!o.table.empty()) out += o.table + ".";
            out += o.column;
        } else {
            out += opText(o.op);
        }
    }
    return out;
}
