#pragma once
#include "ds/Str.h"

// Thrown for anything the user got wrong: bad SQL, unknown table, duplicate key...
// The executor catches it and turns it into an error result.
struct DbError {
    Str msg;
    explicit DbError(const Str& m) : msg(m) {}
};
