#pragma once
#include "ds/Stats.h"

// Doubly linked list with head and tail sentinels, our std::list.
// The sentinels mean we never special-case an empty list or the ends:
// every real node always has a prev and a next.
//
// pushFront/pushBack hand back the node, so the owner can later move or
// erase it in O(1). The buffer pool's LRU list depends on that.
template <typename T>
class LinkedList {
public:
    struct Node {
        T val;
        Node* prev = nullptr;
        Node* next = nullptr;
    };

    LinkedList() : size_(0) {
        head_ = new Node();
        tail_ = new Node();
        head_->next = tail_;
        tail_->prev = head_;
    }

    ~LinkedList() {
        clear();
        delete head_;
        delete tail_;
    }

    LinkedList(const LinkedList&) = delete;
    LinkedList& operator=(const LinkedList&) = delete;

    Node* pushFront(const T& v) { return linkAfter(head_, v); }
    Node* pushBack(const T& v) { return linkAfter(tail_->prev, v); }

    void moveToFront(Node* n) {
        if (head_->next == n) return;
        unlink(n);
        n->prev = head_;
        n->next = head_->next;
        head_->next->prev = n;
        head_->next = n;
        g_stats.listMoves++;
    }

    void erase(Node* n) {
        unlink(n);
        delete n;
        size_--;
    }

    // null when empty / at the end
    Node* first() const { return head_->next == tail_ ? nullptr : head_->next; }
    Node* last() const { return tail_->prev == head_ ? nullptr : tail_->prev; }
    Node* after(Node* n) const { return n->next == tail_ ? nullptr : n->next; }
    Node* before(Node* n) const { return n->prev == head_ ? nullptr : n->prev; }

    int size() const { return size_; }
    bool empty() const { return size_ == 0; }

    void clear() {
        Node* n = head_->next;
        while (n != tail_) {
            Node* next = n->next;
            delete n;
            n = next;
        }
        head_->next = tail_;
        tail_->prev = head_;
        size_ = 0;
    }

private:
    Node* linkAfter(Node* where, const T& v) {
        Node* n = new Node();
        n->val = v;
        n->prev = where;
        n->next = where->next;
        where->next->prev = n;
        where->next = n;
        size_++;
        g_stats.listMoves++;
        return n;
    }

    void unlink(Node* n) {
        n->prev->next = n->next;
        n->next->prev = n->prev;
    }

    Node* head_;
    Node* tail_;
    int size_;
};
