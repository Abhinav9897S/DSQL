#include "storage/Value.h"
#include "ds/Keys.h"
#include "util/Error.h"
#include <cstring>

const char* typeName(Type t) {
    switch (t) {
    case Type::Int: return "INT";
    case Type::Float: return "FLOAT";
    case Type::Varchar: return "VARCHAR";
    case Type::Bool: return "BOOL";
    }
    return "?";
}

Value Value::ofInt(long long v) { Value x; x.type = Type::Int; x.i = v; return x; }
Value Value::ofFloat(double v) { Value x; x.type = Type::Float; x.f = v; return x; }
Value Value::ofText(const Str& v) { Value x; x.type = Type::Varchar; x.s = v; return x; }
Value Value::ofBool(bool v) { Value x; x.type = Type::Bool; x.i = v ? 1 : 0; return x; }

Str Value::toString() const {
    switch (type) {
    case Type::Int: return Str::num(i);
    case Type::Float: return Str::num(f);
    case Type::Varchar: return s;
    case Type::Bool: return i ? "TRUE" : "FALSE";
    }
    return Str();
}

Str Value::toSql() const {
    if (type != Type::Varchar) return toString();
    Str r("'");
    r += s;
    r += "'";
    return r;
}

static int sign(long long a, long long b) { return a < b ? -1 : (a > b ? 1 : 0); }

int compareKeys(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) {
        if (a.type == Type::Int && b.type == Type::Int) return sign(a.i, b.i);
        double x = a.asDouble(), y = b.asDouble();
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (a.type != b.type)
        throw DbError(Str("can't compare ") + typeName(a.type) + " " + a.toSql() + " with " +
                      typeName(b.type) + " " + b.toSql());
    if (a.type == Type::Varchar) {
        int c = a.s.compare(b.s);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    return sign(a.i, b.i);
}

bool operator==(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) {
        if (a.type == Type::Int && b.type == Type::Int) return a.i == b.i;
        return a.asDouble() == b.asDouble();
    }
    if (a.type != b.type) return false;
    return a.type == Type::Varchar ? a.s == b.s : a.i == b.i;
}

unsigned hashOf(const Value& v) {
    switch (v.type) {
    case Type::Varchar:
        return hashOf(v.s);
    case Type::Float: {
        // 3.0 must land in the same bucket as 3, because they compare equal
        double d = v.f;
        if (d > -9e18 && d < 9e18 && d == (double)(long long)d) return hashOf((long long)d);
        unsigned long long bits;
        memcpy(&bits, &d, sizeof bits);
        return hashOf((long long)bits);
    }
    default:
        return hashOf(v.i);
    }
}
