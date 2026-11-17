#include "ds/Str.h"
#include <cstring>
#include <cstdio>
#include <cctype>

Str::Str() : buf_(nullptr), len_(0), cap_(0) {}

Str::Str(const char* s) : buf_(nullptr), len_(0), cap_(0) {
    if (s) append(s, (int)strlen(s));
}

Str::Str(const char* s, int n) : buf_(nullptr), len_(0), cap_(0) { append(s, n); }

Str::Str(const Str& o) : buf_(nullptr), len_(0), cap_(0) { append(o.c_str(), o.len_); }

Str::Str(Str&& o) noexcept : buf_(o.buf_), len_(o.len_), cap_(o.cap_) {
    o.buf_ = nullptr;
    o.len_ = o.cap_ = 0;
}

Str::~Str() { delete[] buf_; }

Str& Str::operator=(const Str& o) {
    if (this != &o) {
        clear();
        append(o.c_str(), o.len_);
    }
    return *this;
}

Str& Str::operator=(Str&& o) noexcept {
    if (this != &o) {
        delete[] buf_;
        buf_ = o.buf_; len_ = o.len_; cap_ = o.cap_;
        o.buf_ = nullptr;
        o.len_ = o.cap_ = 0;
    }
    return *this;
}

void Str::clear() {
    len_ = 0;
    if (buf_) buf_[0] = 0;
}

void Str::reserve(int n) {
    if (n + 1 <= cap_) return;
    int nc = cap_ ? cap_ : 16;
    while (nc < n + 1) nc *= 2;
    char* nb = new char[nc];
    if (len_) memcpy(nb, buf_, len_);
    nb[len_] = 0;
    delete[] buf_;
    buf_ = nb;
    cap_ = nc;
}

void Str::append(const char* s, int n) {
    if (n <= 0) return;
    if (buf_ && s >= buf_ && s < buf_ + cap_) {   // appending part of ourselves
        Str copy(s, n);
        append(copy.buf_, n);
        return;
    }
    reserve(len_ + n);
    memcpy(buf_ + len_, s, n);
    len_ += n;
    buf_[len_] = 0;
}

Str& Str::operator+=(char c) { append(&c, 1); return *this; }
Str& Str::operator+=(const char* s) { if (s) append(s, (int)strlen(s)); return *this; }
Str& Str::operator+=(const Str& s) { append(s.c_str(), s.len_); return *this; }

int Str::compare(const Str& o) const {
    int n = len_ < o.len_ ? len_ : o.len_;
    int c = n ? memcmp(c_str(), o.c_str(), n) : 0;
    if (c != 0) return c;
    return len_ - o.len_;
}

bool Str::operator==(const Str& o) const {
    return len_ == o.len_ && (len_ == 0 || memcmp(buf_, o.buf_, len_) == 0);
}

bool Str::operator==(const char* s) const { return strcmp(c_str(), s ? s : "") == 0; }

Str Str::substr(int start, int n) const {
    if (start < 0) start = 0;
    if (start > len_) start = len_;
    if (n < 0 || start + n > len_) n = len_ - start;
    return Str(c_str() + start, n);
}

Str Str::lower() const {
    Str r(*this);
    for (int i = 0; i < r.len_; i++) r.buf_[i] = (char)tolower((unsigned char)r.buf_[i]);
    return r;
}

Str Str::upper() const {
    Str r(*this);
    for (int i = 0; i < r.len_; i++) r.buf_[i] = (char)toupper((unsigned char)r.buf_[i]);
    return r;
}

int Str::find(char c, int from) const {
    for (int i = from; i < len_; i++)
        if (buf_[i] == c) return i;
    return -1;
}

int Str::findLast(char c) const {
    for (int i = len_ - 1; i >= 0; i--)
        if (buf_[i] == c) return i;
    return -1;
}

bool Str::startsWith(const char* s) const {
    int n = (int)strlen(s);
    return n <= len_ && memcmp(c_str(), s, n) == 0;
}

bool Str::endsWith(const char* s) const {
    int n = (int)strlen(s);
    return n <= len_ && memcmp(c_str() + len_ - n, s, n) == 0;
}

Str Str::num(long long v) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%lld", v);
    return Str(tmp);
}

Str Str::num(double v) {
    char tmp[48];
    snprintf(tmp, sizeof tmp, "%.10g", v);
    Str r(tmp);
    // keep floats looking like floats: 3 -> 3.0
    if (r.find('.') < 0 && r.find('e') < 0 && r.find('n') < 0 && r.find('i') < 0) r += ".0";
    return r;
}

Str operator+(const Str& a, const Str& b) { Str r(a); r += b; return r; }
Str operator+(const Str& a, const char* b) { Str r(a); r += b; return r; }
Str operator+(const char* a, const Str& b) { Str r(a); r += b; return r; }

// FNV-1a
unsigned hashOf(const Str& s) {
    unsigned h = 2166136261u;
    for (int i = 0; i < s.size(); i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}
