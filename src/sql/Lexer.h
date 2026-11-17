#pragma once
#include "ds/Str.h"
#include "ds/Queue.h"

enum class Tok { Keyword, Ident, Int, Float, String, Symbol, End };

const char* tokName(Tok t);

struct Token {
    Tok type = Tok::End;
    Str text;     // as written (strings without quotes)
    Str up;       // upper-cased, for keyword checks
    int line = 1;
    int col = 1;
    int start = 0;   // byte offsets into the script, used to cut out
    int end = 0;     // each statement's own text
};

// Hand-written scanner: walks the text one character at a time and pushes
// tokens onto a queue. No regex, no string streams.
class Lexer {
public:
    // firstLine: line number of the text's first line, so errors match the editor
    explicit Lexer(const Str& src, int firstLine = 1) : src_(src), pos_(0), line_(firstLine), lineStart_(0) {}
    void run(Queue<Token>& out);   // throws DbError on bad input
    int line() const { return line_; }

    // where the text went wrong, set before run() throws
    int errorLine = 1;
    int errorCol = 1;

private:
    char peek(int k = 0) const { return pos_ + k < src_.size() ? src_[pos_ + k] : 0; }
    void skipSpaceAndComments();
    Token make(Tok type, int start);

    const Str& src_;
    int pos_;
    int line_;
    int lineStart_;
};

bool isKeyword(const Str& upper);
