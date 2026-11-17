#pragma once
#include <utility>
#include "ds/DynArray.h"

// LIFO stack on top of DynArray. The executor evaluates WHERE clauses with
// it: the parser emits the condition in postfix order and the executor
// pushes operands / pops them for each operator.
template <typename T>
class Stack {
public:
    void push(const T& v) {
        items_.push(v);
        g_stats.stackPush++;
    }

    T pop() {
        T v = std::move(items_.back());
        items_.pop();
        g_stats.stackPop++;
        return v;
    }

    T& top() { return items_.back(); }
    bool empty() const { return items_.empty(); }
    int size() const { return items_.size(); }

private:
    DynArray<T> items_;
};
