#pragma once
#include <utility>
#include "ds/Stats.h"

// FIFO queue on a circular array. head_ walks forward as items are popped
// and wraps around, so neither push nor pop ever shifts elements.
// The tokenizer fills one of these and the parser drains it; peek(k) gives
// the parser its little bit of look-ahead.
template <typename T>
class Queue {
public:
    Queue() : buf_(nullptr), cap_(0), head_(0), count_(0) {}
    ~Queue() { delete[] buf_; }

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    void push(const T& v) {
        if (count_ == cap_) grow();
        buf_[(head_ + count_) % cap_] = v;
        count_++;
        g_stats.queuePush++;
    }

    T pop() {
        T v = std::move(buf_[head_]);
        head_ = (head_ + 1) % cap_;
        count_--;
        g_stats.queuePop++;
        return v;
    }

    // k-th item from the front without removing it (k < size())
    const T& peek(int k = 0) const { return buf_[(head_ + k) % cap_]; }

    int size() const { return count_; }
    bool empty() const { return count_ == 0; }

private:
    void grow() {
        int nc = cap_ == 0 ? 8 : cap_ * 2;
        T* nb = new T[nc];
        for (int i = 0; i < count_; i++) nb[i] = std::move(buf_[(head_ + i) % cap_]);
        delete[] buf_;
        buf_ = nb;
        cap_ = nc;
        head_ = 0;
    }

    T* buf_;
    int cap_;
    int head_;
    int count_;
};
