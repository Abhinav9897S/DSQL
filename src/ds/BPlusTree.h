#pragma once
#include "ds/DynArray.h"
#include "ds/Keys.h"
#include "ds/Stats.h"

// B+ tree used for the primary key: unique key -> row id.
//
//  - internal nodes only hold separator keys and child pointers
//  - every key/value pair lives in a leaf
//  - leaves are chained left to right, so a range query finds the first
//    leaf once and then just follows `next`
//
// `order` is the max number of children of an internal node, so a node holds
// at most order-1 keys and (except the root) at least (order-1)/2.
//
// Separator rule: child[i] holds keys < keys[i] <= keys in child[i+1].
//
// K needs compareKeys(a, b).
template <typename K>
class BPlusTree {
public:
    struct Node {
        bool leaf = true;
        int n = 0;               // keys in use
        K* keys = nullptr;       // one spare slot so a node can overflow before it splits
        Node** kids = nullptr;   // internal nodes: n + 1 children
        int* vals = nullptr;     // leaves: row id per key
        Node* next = nullptr;    // leaves: right neighbour
    };

    explicit BPlusTree(int order = 4) : count_(0) {
        order_ = order < 3 ? 3 : order;
        maxKeys_ = order_ - 1;
        minKeys_ = maxKeys_ / 2;
        root_ = newNode(true);
    }

    ~BPlusTree() { destroy(root_); }

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    // false if the key is already present
    bool insert(const K& key, int val) {
        K upKey;
        Node* right = nullptr;
        bool dup = false;
        if (insertInto(root_, key, val, upKey, right, dup)) {
            // the root split, so the tree grows one level taller
            Node* top = newNode(false);
            top->keys[0] = upKey;
            top->kids[0] = root_;
            top->kids[1] = right;
            top->n = 1;
            root_ = top;
        }
        if (!dup) count_++;
        return !dup;
    }

    bool find(const K& key, int& val) const {
        Node* leaf = findLeaf(key);
        int i = lowerBound(leaf, key);
        if (i < leaf->n && compareKeys(leaf->keys[i], key) == 0) {
            val = leaf->vals[i];
            return true;
        }
        return false;
    }

    bool remove(const K& key) {
        if (!removeFrom(root_, key)) return false;
        count_--;
        if (!root_->leaf && root_->n == 0) {   // root lost its last separator
            Node* old = root_;
            root_ = root_->kids[0];
            freeNode(old);
        }
        return true;
    }

    // values for keys in [lo, hi] in key order; null bound = open-ended
    void range(const K* lo, bool loInc, const K* hi, bool hiInc, DynArray<int>& out) const {
        Node* leaf;
        int i;
        if (lo) {
            leaf = findLeaf(*lo);
            i = lowerBound(leaf, *lo);
        } else {
            leaf = leftmost();
            i = 0;
        }
        while (leaf) {
            for (; i < leaf->n; i++) {
                if (lo && !loInc && compareKeys(leaf->keys[i], *lo) == 0) continue;
                if (hi) {
                    int c = compareKeys(leaf->keys[i], *hi);
                    if (c > 0 || (c == 0 && !hiInc)) return;
                }
                out.push(leaf->vals[i]);
            }
            leaf = leaf->next;
            i = 0;
            if (leaf) g_stats.bptLeafHops++;
        }
    }

    void all(DynArray<int>& out) const { range(nullptr, true, nullptr, true, out); }

    Node* root() const { return root_; }
    int order() const { return order_; }
    int size() const { return count_; }

    int height() const {
        int hgt = 1;
        for (Node* n = root_; !n->leaf; n = n->kids[0]) hgt++;
        return hgt;
    }

    int nodeCount() const { return countNodes(root_); }

    // used by the tests: sizes in bounds, leaves at one depth, chain sorted
    bool check() const {
        int leafDepth = -1, bad = 0;
        checkNode(root_, 1, leafDepth, bad, true);
        Node* leaf = leftmost();
        const K* prev = nullptr;
        int seen = 0;
        for (; leaf; leaf = leaf->next) {
            for (int i = 0; i < leaf->n; i++) {
                if (prev && compareKeys(*prev, leaf->keys[i]) >= 0) bad++;
                prev = &leaf->keys[i];
                seen++;
            }
        }
        if (seen != count_) bad++;
        return bad == 0;
    }

private:
    Node* newNode(bool leaf) {
        Node* n = new Node();
        n->leaf = leaf;
        n->keys = new K[maxKeys_ + 1];
        if (leaf) n->vals = new int[maxKeys_ + 1];
        else n->kids = new Node*[maxKeys_ + 2];
        return n;
    }

    void freeNode(Node* n) {
        delete[] n->keys;
        delete[] n->vals;
        delete[] n->kids;
        delete n;
    }

    void destroy(Node* n) {
        if (!n->leaf)
            for (int i = 0; i <= n->n; i++) destroy(n->kids[i]);
        freeNode(n);
    }

    int countNodes(Node* n) const {
        if (n->leaf) return 1;
        int c = 1;
        for (int i = 0; i <= n->n; i++) c += countNodes(n->kids[i]);
        return c;
    }

    // first slot whose key is >= key
    static int lowerBound(Node* n, const K& key) {
        int i = 0;
        while (i < n->n && compareKeys(n->keys[i], key) < 0) i++;
        return i;
    }

    // which child to descend into
    static int childIndex(Node* n, const K& key) {
        int i = 0;
        while (i < n->n && compareKeys(key, n->keys[i]) >= 0) i++;
        return i;
    }

    Node* findLeaf(const K& key) const {
        Node* n = root_;
        g_stats.bptVisits++;
        while (!n->leaf) {
            n = n->kids[childIndex(n, key)];
            g_stats.bptVisits++;
        }
        return n;
    }

    Node* leftmost() const {
        Node* n = root_;
        g_stats.bptVisits++;
        while (!n->leaf) {
            n = n->kids[0];
            g_stats.bptVisits++;
        }
        return n;
    }

    // Returns true if `node` split; the new right sibling and the key to
    // push up into the parent come back through upKey / right.
    bool insertInto(Node* node, const K& key, int val, K& upKey, Node*& right, bool& dup) {
        g_stats.bptVisits++;
        if (node->leaf) {
            int i = lowerBound(node, key);
            if (i < node->n && compareKeys(node->keys[i], key) == 0) {
                dup = true;
                return false;
            }
            for (int j = node->n; j > i; j--) {
                node->keys[j] = node->keys[j - 1];
                node->vals[j] = node->vals[j - 1];
            }
            node->keys[i] = key;
            node->vals[i] = val;
            node->n++;
            if (node->n <= maxKeys_) return false;
            splitLeaf(node, upKey, right);
            return true;
        }

        int i = childIndex(node, key);
        K childKey;
        Node* childRight = nullptr;
        if (!insertInto(node->kids[i], key, val, childKey, childRight, dup)) return false;

        // child i split: its new sibling goes in at i + 1
        for (int j = node->n; j > i; j--) {
            node->keys[j] = node->keys[j - 1];
            node->kids[j + 1] = node->kids[j];
        }
        node->keys[i] = childKey;
        node->kids[i + 1] = childRight;
        node->n++;
        if (node->n <= maxKeys_) return false;
        splitInternal(node, upKey, right);
        return true;
    }

    void splitLeaf(Node* node, K& upKey, Node*& right) {
        int keep = (node->n + 1) / 2;
        right = newNode(true);
        right->n = node->n - keep;
        for (int j = 0; j < right->n; j++) {
            right->keys[j] = node->keys[keep + j];
            right->vals[j] = node->vals[keep + j];
        }
        node->n = keep;
        right->next = node->next;
        node->next = right;
        upKey = right->keys[0];   // a copy of the key stays in the leaf
        g_stats.bptSplits++;
    }

    void splitInternal(Node* node, K& upKey, Node*& right) {
        int mid = node->n / 2;
        right = newNode(false);
        right->n = node->n - mid - 1;
        for (int j = 0; j < right->n; j++) right->keys[j] = node->keys[mid + 1 + j];
        for (int j = 0; j <= right->n; j++) right->kids[j] = node->kids[mid + 1 + j];
        upKey = node->keys[mid];   // moves up, does not stay here
        node->n = mid;
        g_stats.bptSplits++;
    }

    bool removeFrom(Node* node, const K& key) {
        g_stats.bptVisits++;
        if (node->leaf) {
            int i = lowerBound(node, key);
            if (i >= node->n || compareKeys(node->keys[i], key) != 0) return false;
            for (int j = i; j + 1 < node->n; j++) {
                node->keys[j] = node->keys[j + 1];
                node->vals[j] = node->vals[j + 1];
            }
            node->n--;
            return true;
        }
        int i = childIndex(node, key);
        if (!removeFrom(node->kids[i], key)) return false;
        if (node->kids[i]->n < minKeys_) fixUnderflow(node, i);
        return true;
    }

    // child i of p has too few keys: borrow from a sibling, or merge with one
    void fixUnderflow(Node* p, int i) {
        Node* c = p->kids[i];
        Node* left = i > 0 ? p->kids[i - 1] : nullptr;
        Node* right = i < p->n ? p->kids[i + 1] : nullptr;

        if (left && left->n > minKeys_) {
            borrowFromLeft(p, i, left, c);
            g_stats.bptBorrows++;
        } else if (right && right->n > minKeys_) {
            borrowFromRight(p, i, c, right);
            g_stats.bptBorrows++;
        } else if (left) {
            merge(p, i - 1, left, c);
            g_stats.bptMerges++;
        } else {
            merge(p, i, c, right);
            g_stats.bptMerges++;
        }
    }

    void borrowFromLeft(Node* p, int i, Node* left, Node* c) {
        for (int j = c->n; j > 0; j--) c->keys[j] = c->keys[j - 1];
        if (c->leaf) {
            for (int j = c->n; j > 0; j--) c->vals[j] = c->vals[j - 1];
            c->keys[0] = left->keys[left->n - 1];
            c->vals[0] = left->vals[left->n - 1];
            p->keys[i - 1] = c->keys[0];
        } else {
            // rotate through the parent: separator comes down, left's last key goes up
            for (int j = c->n + 1; j > 0; j--) c->kids[j] = c->kids[j - 1];
            c->keys[0] = p->keys[i - 1];
            c->kids[0] = left->kids[left->n];
            p->keys[i - 1] = left->keys[left->n - 1];
        }
        c->n++;
        left->n--;
    }

    void borrowFromRight(Node* p, int i, Node* c, Node* right) {
        if (c->leaf) {
            c->keys[c->n] = right->keys[0];
            c->vals[c->n] = right->vals[0];
            for (int j = 0; j + 1 < right->n; j++) {
                right->keys[j] = right->keys[j + 1];
                right->vals[j] = right->vals[j + 1];
            }
            right->n--;
            p->keys[i] = right->keys[0];
        } else {
            c->keys[c->n] = p->keys[i];
            c->kids[c->n + 1] = right->kids[0];
            p->keys[i] = right->keys[0];
            for (int j = 0; j + 1 < right->n; j++) right->keys[j] = right->keys[j + 1];
            for (int j = 0; j < right->n; j++) right->kids[j] = right->kids[j + 1];
            right->n--;
        }
        c->n++;
    }

    // fold b into a (b is a's right neighbour), drop separator sep from p
    void merge(Node* p, int sep, Node* a, Node* b) {
        if (a->leaf) {
            for (int j = 0; j < b->n; j++) {
                a->keys[a->n + j] = b->keys[j];
                a->vals[a->n + j] = b->vals[j];
            }
            a->n += b->n;
            a->next = b->next;
        } else {
            a->keys[a->n] = p->keys[sep];   // separator comes down between the two halves
            for (int j = 0; j < b->n; j++) a->keys[a->n + 1 + j] = b->keys[j];
            for (int j = 0; j <= b->n; j++) a->kids[a->n + 1 + j] = b->kids[j];
            a->n += 1 + b->n;
        }
        freeNode(b);
        for (int j = sep; j + 1 < p->n; j++) p->keys[j] = p->keys[j + 1];
        for (int j = sep + 1; j < p->n; j++) p->kids[j] = p->kids[j + 1];
        p->n--;
    }

    void checkNode(Node* n, int depth, int& leafDepth, int& bad, bool isRoot) const {
        if (!isRoot && n->n < minKeys_) bad++;
        if (n->n > maxKeys_) bad++;
        for (int i = 0; i + 1 < n->n; i++)
            if (compareKeys(n->keys[i], n->keys[i + 1]) >= 0) bad++;
        if (n->leaf) {
            if (leafDepth < 0) leafDepth = depth;
            else if (leafDepth != depth) bad++;
            return;
        }
        for (int i = 0; i <= n->n; i++) checkNode(n->kids[i], depth + 1, leafDepth, bad, false);
    }

    Node* root_;
    int order_;
    int maxKeys_;
    int minKeys_;
    int count_;
};
