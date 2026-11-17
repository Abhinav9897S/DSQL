#include "sql/Checker.h"
#include "sql/Parser.h"
#include "storage/Catalog.h"
#include "ds/HashTable.h"

// put the mark on the spot where the table name was written
static Problem atTableName(const Command& c, const Str& name, const Str& message) {
    Problem p;
    p.line = c.line;
    p.message = message;
    for (int i = 0; i < c.tokens.size(); i++) {
        const Token& t = c.tokens[i];
        if (t.type == Tok::Ident && t.text.lower() == name) {
            p.line = t.line;
            p.col = t.col;
            p.length = t.text.size();
            break;
        }
    }
    return p;
}

void checkSql(const Str& sql, Catalog* cat, DynArray<Problem>& out) {
    Queue<Token> tokens;
    Lexer lexer(sql);
    try {
        lexer.run(tokens);
    } catch (DbError& e) {
        Problem p;
        p.line = lexer.errorLine;
        p.col = lexer.errorCol;
        p.message = e.msg;
        out.push(p);
        return;   // can't parse without the tokens
    }

    // true = the script created it, false = the script dropped it
    HashTable<Str, bool> script(16);
    auto exists = [&](const Str& name) {
        bool* seen = script.find(name);
        if (seen) return *seen;
        return cat && cat->isOpen() && cat->find(name, false) != nullptr;
    };
    auto needTable = [&](const Command& c, const Str& name) {
        if (!name.empty() && !exists(name)) out.push(atTableName(c, name, "no table named '" + name + "'"));
    };

    Parser parser(tokens, sql);
    while (!parser.done()) {
        try {
            Command c = parser.next();
            switch (c.kind) {
            case Cmd::CreateTable:
                if (!c.ifExists && exists(c.table))
                    out.push(atTableName(c, c.table, "table '" + c.table + "' already exists"));
                script.put(c.table, true);
                break;
            case Cmd::DropTable:
                if (!c.ifExists) needTable(c, c.table);
                script.put(c.table, false);
                break;
            case Cmd::Select:
                needTable(c, c.table);
                if (c.join) needTable(c, c.joinTable);
                break;
            case Cmd::Insert:
            case Cmd::Update:
            case Cmd::Delete:
            case Cmd::Describe:
            case Cmd::CreateIndex:
                needTable(c, c.table);
                break;
            default:
                break;
            }
        } catch (DbError& e) {
            // the parser already skipped to the next ';', so keep going
            Problem p;
            p.line = parser.errorLine;
            p.col = parser.errorCol;
            p.length = parser.errorLength;
            p.message = e.msg;
            out.push(p);
        }
    }
}
