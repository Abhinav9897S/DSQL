#pragma once
#include "ds/BPlusTree.h"
#include "ds/AVLTree.h"
#include "storage/Value.h"

struct Bound {
    bool on = false;      // false = no limit on this side
    Value v;
    bool inclusive = true;
};

// The index manager's common interface. The executor only talks to this,
// so a new kind of index can be added without touching query code.
class Index {
public:
    virtual ~Index() {}
    virtual const char* kind() const = 0;   // shown to the user
    virtual const char* tag() const = 0;    // trace badge: "bptree" or "avl"
    virtual bool unique() const = 0;

    virtual bool insert(const Value& key, int rid) = 0;   // false = key already there (unique only)
    virtual void remove(const Value& key, int rid) = 0;
    virtual void find(const Value& key, DynArray<int>& out) = 0;
    virtual void range(const Bound& lo, const Bound& hi, DynArray<int>& out) = 0;
    virtual void all(DynArray<int>& out) = 0;   // every row id, in key order

    virtual int height() const = 0;
    virtual int keys() const = 0;
    virtual int nodes() const = 0;
};

// Primary key: unique, ordered, leaves linked for range scans.
class BPlusIndex : public Index {
public:
    explicit BPlusIndex(int order) : tree_(order) {}
    const char* kind() const override { return "B+ Tree"; }
    const char* tag() const override { return "bptree"; }
    bool unique() const override { return true; }

    bool insert(const Value& key, int rid) override { return tree_.insert(key, rid); }
    void remove(const Value& key, int) override { tree_.remove(key); }
    void find(const Value& key, DynArray<int>& out) override;
    void range(const Bound& lo, const Bound& hi, DynArray<int>& out) override;
    void all(DynArray<int>& out) override { tree_.all(out); }

    int height() const override { return tree_.height(); }
    int keys() const override { return tree_.size(); }
    int nodes() const override { return tree_.nodeCount(); }
    int order() const { return tree_.order(); }
    const BPlusTree<Value>& tree() const { return tree_; }   // read-only, for the GUI's pictures

private:
    BPlusTree<Value> tree_;
};

// Secondary index: duplicates allowed, each key keeps a list of row ids.
class AVLIndex : public Index {
public:
    const char* kind() const override { return "AVL Tree"; }
    const char* tag() const override { return "avl"; }
    bool unique() const override { return false; }

    bool insert(const Value& key, int rid) override { tree_.insert(key, rid); return true; }
    void remove(const Value& key, int rid) override { tree_.remove(key, rid); }
    void find(const Value& key, DynArray<int>& out) override;
    void range(const Bound& lo, const Bound& hi, DynArray<int>& out) override;
    void all(DynArray<int>& out) override { tree_.inorder(out); }

    int height() const override { return tree_.height(); }
    int keys() const override { return tree_.keyCount(); }
    int nodes() const override { return tree_.keyCount(); }
    const AVLTree<Value>& tree() const { return tree_; }

private:
    AVLTree<Value> tree_;
};
