#pragma once
#include "ds/Str.h"
#include "ds/DynArray.h"

// Tiny streaming JSON writer. It remembers, per open object/array, whether
// anything has been written yet so it knows where commas go.
//
//   Json j;
//   j.obj().key("rows").num(3).key("ok").boolean(true).end();
class Json {
public:
    Json& obj();
    Json& arr();
    Json& end();
    Json& key(const char* k);
    Json& str(const Str& s);
    Json& str(const char* s) { return str(Str(s)); }
    Json& num(long long v);
    Json& num(int v) { return num((long long)v); }
    Json& num(double v);
    Json& boolean(bool v);
    Json& null();

    const Str& text() const { return out_; }

private:
    void comma();
    void open(char c, char close);

    Str out_;
    DynArray<char> closers_;   // what to write when end() is called
    DynArray<bool> first_;     // nothing written yet at this level?
    bool afterKey_ = false;
};

Str jsonEscape(const Str& s);
