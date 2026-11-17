#pragma once
#include "ds/DynArray.h"
#include "ds/Str.h"

class Catalog;

// Something wrong with a script, found without running it: a syntax error,
// or a table that doesn't exist. line and col are 1-based; length is how
// many characters the editor should underline.
struct Problem {
    int line = 1;
    int col = 1;
    int length = 1;
    Str message;
};

// Lexes and parses every statement of `sql` (nothing is executed) and lists
// what's wrong. With a catalog it also flags unknown tables, keeping track
// of the tables the script itself creates or drops on the way.
void checkSql(const Str& sql, Catalog* cat, DynArray<Problem>& out);
