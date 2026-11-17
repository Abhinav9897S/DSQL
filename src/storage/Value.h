#pragma once
#include "ds/Str.h"
#include "ds/DynArray.h"

enum class Type { Int, Float, Varchar, Bool };

const char* typeName(Type t);

// One cell. A field per kind instead of a union: a few wasted bytes,
// but nothing clever to get wrong.
struct Value {
    Type type = Type::Int;
    long long i = 0;   // Int, and Bool as 0/1
    double f = 0;      // Float
    Str s;             // Varchar

    static Value ofInt(long long v);
    static Value ofFloat(double v);
    static Value ofText(const Str& v);
    static Value ofBool(bool v);

    bool isNumber() const { return type == Type::Int || type == Type::Float; }
    double asDouble() const { return type == Type::Float ? f : (double)i; }
    Str toString() const;
    Str toSql() const;   // like toString but strings come back quoted
};

typedef DynArray<Value> Row;

// <0, 0, >0. Int and Float compare as numbers; other mixes throw DbError.
int compareKeys(const Value& a, const Value& b);

// equality that never throws (different kinds are simply not equal)
bool operator==(const Value& a, const Value& b);

unsigned hashOf(const Value& v);
