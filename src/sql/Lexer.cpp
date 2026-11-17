#include "sql/Lexer.h"
#include "ds/HashTable.h"
#include "util/Error.h"
#include <cctype>

const char* tokName(Tok t) {
    switch (t) {
    case Tok::Keyword: return "KEYWORD";
    case Tok::Ident: return "IDENT";
    case Tok::Int: return "INT";
    case Tok::Float: return "FLOAT";
    case Tok::String: return "STRING";
    case Tok::Symbol: return "SYMBOL";
    case Tok::End: return "END";
    }
    return "?";
}

// Reserved words live in a hash table, so classifying an identifier is one lookup.
bool isKeyword(const Str& upper) {
    static HashTable<Str, bool> words(64);
    if (words.size() == 0) {
        const char* list[] = {
            "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "CREATE", "TABLE", "INDEX",
            "ON", "DROP", "DELETE", "UPDATE", "SET", "AND", "OR", "NOT", "JOIN", "INNER",
            "PRIMARY", "KEY", "ORDER", "BY", "ASC", "DESC", "LIMIT", "GROUP", "AS", "BETWEEN",
            "LIKE", "TRUE", "FALSE", "SHOW", "TABLES", "DESCRIBE", "NULL", "IF", "EXISTS",
        };
        for (const char* w : list) words.put(Str(w), true);
    }
    return words.find(upper) != nullptr;
}

void Lexer::skipSpaceAndComments() {
    for (;;) {
        char c = peek();
        if (c == '\n') {
            pos_++;
            line_++;
            lineStart_ = pos_;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            pos_++;
        } else if (c == '-' && peek(1) == '-') {
            while (peek() && peek() != '\n') pos_++;
        } else if (c == '/' && peek(1) == '*') {
            pos_ += 2;
            while (peek() && !(peek() == '*' && peek(1) == '/')) {
                if (peek() == '\n') {
                    line_++;
                    lineStart_ = pos_ + 1;
                }
                pos_++;
            }
            if (peek()) pos_ += 2;
        } else {
            return;
        }
    }
}

Token Lexer::make(Tok type, int start) {
    Token t;
    t.type = type;
    t.text = src_.substr(start, pos_ - start);
    t.up = t.text.upper();
    t.line = line_;
    t.col = start - lineStart_ + 1;
    t.start = start;
    t.end = pos_;
    return t;
}

void Lexer::run(Queue<Token>& out) {
    for (;;) {
        skipSpaceAndComments();
        int start = pos_;
        char c = peek();
        if (!c) break;

        if (isalpha((unsigned char)c) || c == '_') {
            while (isalnum((unsigned char)peek()) || peek() == '_') pos_++;
            Token t = make(Tok::Ident, start);
            if (isKeyword(t.up)) t.type = Tok::Keyword;
            out.push(t);
        } else if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)peek(1)))) {
            bool dot = false;
            while (isdigit((unsigned char)peek()) || (peek() == '.' && !dot)) {
                if (peek() == '.') dot = true;
                pos_++;
            }
            out.push(make(dot ? Tok::Float : Tok::Int, start));
        } else if (c == '\'') {
            int startLine = line_, startCol = start - lineStart_ + 1;
            pos_++;
            Str text;
            for (;;) {
                char ch = peek();
                if (!ch) {
                    errorLine = startLine;
                    errorCol = startCol;
                    throw DbError("unterminated string starting at line " + Str::num(startLine));
                }
                if (ch == '\'') {
                    if (peek(1) == '\'') {   // '' is an escaped quote
                        text += '\'';
                        pos_ += 2;
                        continue;
                    }
                    pos_++;
                    break;
                }
                if (ch == '\n') {
                    line_++;
                    lineStart_ = pos_ + 1;
                }
                text += ch;
                pos_++;
            }
            Token t = make(Tok::String, start);
            t.text = text;
            out.push(t);
        } else {
            // two-character operators first
            char d = peek(1);
            if ((c == '<' && (d == '=' || d == '>')) || (c == '>' && d == '=') || (c == '!' && d == '=')) {
                pos_ += 2;
            } else if (Str("(),;*=<>+-/%.").find(c) >= 0) {
                pos_++;
            } else {
                errorLine = line_;
                errorCol = start - lineStart_ + 1;
                throw DbError(Str("unexpected character '") + Str(&c, 1) + "' at line " + Str::num(line_) +
                              ", column " + Str::num(start - lineStart_ + 1));
            }
            out.push(make(Tok::Symbol, start));
        }
    }
}
