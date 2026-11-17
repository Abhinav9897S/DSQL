#pragma once
#include <utility>
#include "ds/Stats.h"

// Growable array, our std::vector.
// When it fills up the capacity doubles, so push() is amortized O(1):
// n pushes cause about log2(n) reallocations, each copying what is there.
template <typename T>
class DynArray {
public:
    DynArray() : data_(nullptr), size_(0), cap_(0) {}

    DynArray(const DynArray& o) : data_(nullptr), size_(0), cap_(0) {
        reserve(o.size_);
        for (int i = 0; i < o.size_; i++) data_[i] = o.data_[i];
        size_ = o.size_;
    }

    DynArray(DynArray&& o) noexcept : data_(o.data_), size_(o.size_), cap_(o.cap_) {
        o.data_ = nullptr;
        o.size_ = o.cap_ = 0;
    }

    ~DynArray() { delete[] data_; }

    DynArray& operator=(const DynArray& o) {
        if (this != &o) {
            DynArray copy(o);
            *this = std::move(copy);
        }
        return *this;
    }

    DynArray& operator=(DynArray&& o) noexcept {
        if (this != &o) {
            delete[] data_;
            data_ = o.data_; size_ = o.size_; cap_ = o.cap_;
            o.data_ = nullptr;
            o.size_ = o.cap_ = 0;
        }
        return *this;
    }

    void push(const T& v) {
        if (size_ == cap_) {
            T copy(v);   // v may point into our own storage
            grow();
            data_[size_++] = std::move(copy);
        } else {
            data_[size_++] = v;
        }
    }

    void push(T&& v) {
        if (size_ == cap_) grow();
        data_[size_++] = std::move(v);
    }

    void pop() {
        size_--;
        data_[size_] = T();
    }

    T& back() { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }
    T& operator[](int i) { return data_[i]; }
    const T& operator[](int i) const { return data_[i]; }
    int size() const { return size_; }
    bool empty() const { return size_ == 0; }

    void clear() {
        delete[] data_;
        data_ = nullptr;
        size_ = cap_ = 0;
    }

    void reserve(int n) {
        if (n <= cap_) return;
        T* fresh = new T[n];
        for (int i = 0; i < size_; i++) fresh[i] = std::move(data_[i]);
        delete[] data_;
        data_ = fresh;
        cap_ = n;
    }

    void resize(int n) {
        reserve(n);
        for (int i = n; i < size_; i++) data_[i] = T();
        for (int i = size_; i < n; i++) data_[i] = T();
        size_ = n;
    }

    void insertAt(int idx, const T& v) {
        T copy(v);
        push(T());
        for (int i = size_ - 1; i > idx; i--) data_[i] = std::move(data_[i - 1]);
        data_[idx] = std::move(copy);
    }

    void removeAt(int idx) {
        for (int i = idx; i + 1 < size_; i++) data_[i] = std::move(data_[i + 1]);
        pop();
    }

    void reverse() {
        for (int i = 0, j = size_ - 1; i < j; i++, j--) {
            T t = std::move(data_[i]);
            data_[i] = std::move(data_[j]);
            data_[j] = std::move(t);
        }
    }

    int capacity() const { return cap_; }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

private:
    void grow() {
        reserve(cap_ == 0 ? 4 : cap_ * 2);
        g_stats.arrayGrows++;
    }

    T* data_;
    int size_;
    int cap_;
};
