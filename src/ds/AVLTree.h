#pragma once
#include <utility>
#include "ds/DynArray.h"
#include "ds/Keys.h"
#include "ds/Stats.h"

// Self-balancing BST used for secondary indexes.
//
// A secondary column is not unique (many students can be 20), so each node
// holds one key plus the list of row ids that have it.
//
// After every insert/delete we walk back up and fix heights. If a node's
// left and right heights differ by more than 1 we rotate:
//   LL  left-left heavy    -> rotate right
//   RR  right-right heavy  -> rotate left
//   LR  left-right heavy   -> rotate left on the child, then right
//   RL  right-left heavy   -> rotate right on the child, then left
// That keeps the height around 1.44 * log2(n), so search is O(log n).
//
// K needs compareKeys(a, b) returning <0, 0, >0.
template <typename K>
class AVLTree {
public:
    struct Node {
        K key;
        DynArray<int> rids;
        Node* left = nullptr;
        Node* right = nullptr;
        int height = 1;
    };

    AVLTree() : root_(nullptr), keys_(0), entries_(0) {}
    ~AVLTree() { destroy(root_); }

    AVLTree(const AVLTree&) = delete;
    AVLTree& operator=(const AVLTree&) = delete;

    void insert(const K& key, int rid) { root_ = insert(root_, key, rid); }

    // removes one (key, rid) pair; the node goes away when its list empties
    bool remove(const K& key, int rid) {
        bool removed = false;
        root_ = remove(root_, key, rid, removed);
        return removed;
    }

    // row ids stored under key, or null
    const DynArray<int>* find(const K& key) const {
        Node* n = root_;
        while (n) {
            g_stats.avlVisits++;
            int c = compareKeys(key, n->key);
            if (c == 0) return &n->rids;
            n = c < 0 ? n->left : n->right;
        }
        return nullptr;
    }

    // in-order walk limited to [lo, hi]; a null bound means unbounded
    void range(const K* lo, bool loInc, const K* hi, bool hiInc, DynArray<int>& out) const {
        range(root_, lo, loInc, hi, hiInc, out);
    }

    void inorder(DynArray<int>& out) const { range(root_, nullptr, true, nullptr, true, out); }

    Node* root() const { return root_; }
    int height() const { return h(root_); }
    int keyCount() const { return keys_; }
    int size() const { return entries_; }

    // used by the tests: every node balanced, heights right, keys in order
    bool check() const {
        int bad = 0;
        checkNode(root_, nullptr, nullptr, bad);
        return bad == 0;
    }

private:
    static int h(Node* n) { return n ? n->height : 0; }

    static void fixHeight(Node* n) {
        int l = h(n->left), r = h(n->right);
        n->height = 1 + (l > r ? l : r);
    }

    static Node* rotateRight(Node* y) {
        Node* x = y->left;
        y->left = x->right;
        x->right = y;
        fixHeight(y);
        fixHeight(x);
        return x;
    }

    static Node* rotateLeft(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        y->left = x;
        fixHeight(x);
        fixHeight(y);
        return y;
    }

    static Node* balance(Node* n) {
        fixHeight(n);
        int bf = h(n->left) - h(n->right);
        if (bf > 1) {
            if (h(n->left->left) >= h(n->left->right)) {
                g_stats.avlLL++;
            } else {
                g_stats.avlLR++;
                n->left = rotateLeft(n->left);
            }
            return rotateRight(n);
        }
        if (bf < -1) {
            if (h(n->right->right) >= h(n->right->left)) {
                g_stats.avlRR++;
            } else {
                g_stats.avlRL++;
                n->right = rotateRight(n->right);
            }
            return rotateLeft(n);
        }
        return n;
    }

    Node* insert(Node* n, const K& key, int rid) {
        if (!n) {
            Node* fresh = new Node();
            fresh->key = key;
            fresh->rids.push(rid);
            keys_++;
            entries_++;
            return fresh;
        }
        g_stats.avlVisits++;
        int c = compareKeys(key, n->key);
        if (c == 0) {
            n->rids.push(rid);   // duplicate key: just another row id, no shape change
            entries_++;
            return n;
        }
        if (c < 0) n->left = insert(n->left, key, rid);
        else n->right = insert(n->right, key, rid);
        return balance(n);
    }

    Node* remove(Node* n, const K& key, int rid, bool& removed) {
        if (!n) return nullptr;
        g_stats.avlVisits++;
        int c = compareKeys(key, n->key);
        if (c < 0) {
            n->left = remove(n->left, key, rid, removed);
        } else if (c > 0) {
            n->right = remove(n->right, key, rid, removed);
        } else {
            for (int i = 0; i < n->rids.size(); i++) {
                if (n->rids[i] == rid) {
                    n->rids.removeAt(i);
                    entries_--;
                    removed = true;
                    break;
                }
            }
            if (!n->rids.empty()) return n;   // other rows still use this key

            keys_--;
            if (!n->left || !n->right) {
                Node* child = n->left ? n->left : n->right;
                delete n;
                return child;
            }
            // two children: take over the in-order successor's data
            Node* s = n->right;
            while (s->left) s = s->left;
            n->key = s->key;
            n->rids = std::move(s->rids);
            n->right = detachMin(n->right);
        }
        return balance(n);
    }

    Node* detachMin(Node* n) {
        if (!n->left) {
            Node* r = n->right;
            delete n;
            return r;
        }
        n->left = detachMin(n->left);
        return balance(n);
    }

    void range(Node* n, const K* lo, bool loInc, const K* hi, bool hiInc, DynArray<int>& out) const {
        if (!n) return;
        g_stats.avlVisits++;
        int vsLo = lo ? compareKeys(n->key, *lo) : 1;
        int vsHi = hi ? compareKeys(n->key, *hi) : -1;
        if (vsLo > 0) range(n->left, lo, loInc, hi, hiInc, out);   // smaller keys may still qualify
        bool aboveLo = loInc ? vsLo >= 0 : vsLo > 0;
        bool belowHi = hiInc ? vsHi <= 0 : vsHi < 0;
        if (aboveLo && belowHi)
            for (int i = 0; i < n->rids.size(); i++) out.push(n->rids[i]);
        if (vsHi < 0) range(n->right, lo, loInc, hi, hiInc, out);
    }

    int checkNode(Node* n, const K* lo, const K* hi, int& bad) const {
        if (!n) return 0;
        if (lo && compareKeys(n->key, *lo) <= 0) bad++;
        if (hi && compareKeys(n->key, *hi) >= 0) bad++;
        int l = checkNode(n->left, lo, &n->key, bad);
        int r = checkNode(n->right, &n->key, hi, bad);
        if (l - r > 1 || r - l > 1) bad++;
        int real = 1 + (l > r ? l : r);
        if (real != n->height) bad++;
        return real;
    }

    void destroy(Node* n) {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }

    Node* root_;
    int keys_;      // distinct keys (nodes)
    int entries_;   // (key, rid) pairs
};
