#include "util/Json.h"
#include <cstdio>
#include <cmath>

void Json::comma() {
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (first_.empty()) return;
    if (first_.back()) first_.back() = false;
    else out_ += ',';
}

void Json::open(char c, char close) {
    comma();
    out_ += c;
    closers_.push(close);
    first_.push(true);
}

Json& Json::obj() { open('{', '}'); return *this; }
Json& Json::arr() { open('[', ']'); return *this; }

Json& Json::end() {
    out_ += closers_.back();
    closers_.pop();
    first_.pop();
    return *this;
}

Json& Json::key(const char* k) {
    comma();
    out_ += '"';
    out_ += k;
    out_ += "\":";
    afterKey_ = true;
    return *this;
}

Json& Json::str(const Str& s) {
    comma();
    out_ += '"';
    out_ += jsonEscape(s);
    out_ += '"';
    return *this;
}

Json& Json::num(long long v) {
    comma();
    out_ += Str::num(v);
    return *this;
}

Json& Json::num(double v) {
    comma();
    if (std::isnan(v) || std::isinf(v)) {
        out_ += "null";
        return *this;
    }
    char tmp[48];
    snprintf(tmp, sizeof tmp, "%.10g", v);
    out_ += tmp;
    return *this;
}

Json& Json::boolean(bool v) {
    comma();
    out_ += v ? "true" : "false";
    return *this;
}

Json& Json::null() {
    comma();
    out_ += "null";
    return *this;
}

Str jsonEscape(const Str& s) {
    Str r;
    r.reserve(s.size() + 8);
    for (int i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        default:
            if (c < 0x20) {
                char tmp[8];
                snprintf(tmp, sizeof tmp, "\\u%04x", c);
                r += tmp;
            } else {
                r += (char)c;
            }
        }
    }
    return r;
}
