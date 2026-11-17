#pragma once

// Small growable string. The engine never touches std::string.
// The buffer is always null-terminated so c_str() is free.
class Str {
public:
    Str();
    Str(const char* s);
    Str(const char* s, int n);
    Str(const Str& o);
    Str(Str&& o) noexcept;
    ~Str();

    Str& operator=(const Str& o);
    Str& operator=(Str&& o) noexcept;

    int size() const { return len_; }
    bool empty() const { return len_ == 0; }
    const char* c_str() const { return buf_ ? buf_ : ""; }
    char operator[](int i) const { return buf_[i]; }
    char& operator[](int i) { return buf_[i]; }

    void clear();
    void reserve(int n);
    void append(const char* s, int n);
    Str& operator+=(char c);
    Str& operator+=(const char* s);
    Str& operator+=(const Str& s);

    int compare(const Str& o) const;
    bool operator==(const Str& o) const;
    bool operator!=(const Str& o) const { return !(*this == o); }
    bool operator<(const Str& o) const { return compare(o) < 0; }
    bool operator==(const char* s) const;
    bool operator!=(const char* s) const { return !(*this == s); }

    Str substr(int start, int n) const;
    Str lower() const;
    Str upper() const;
    int find(char c, int from = 0) const;
    int findLast(char c) const;
    bool startsWith(const char* s) const;
    bool endsWith(const char* s) const;

    static Str num(long long v);
    static Str num(int v) { return num((long long)v); }
    static Str num(double v);

private:
    char* buf_;
    int len_;
    int cap_;
};

Str operator+(const Str& a, const Str& b);
Str operator+(const Str& a, const char* b);
Str operator+(const char* a, const Str& b);

unsigned hashOf(const Str& s);
