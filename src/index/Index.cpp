#include "index/Index.h"
#include "ds/Queue.h"

void BPlusIndex::find(const Value& key, DynArray<int>& out) {
    int rid;
    if (tree_.find(key, rid)) out.push(rid);
}

void BPlusIndex::range(const Bound& lo, const Bound& hi, DynArray<int>& out) {
    tree_.range(lo.on ? &lo.v : nullptr, lo.inclusive, hi.on ? &hi.v : nullptr, hi.inclusive, out);
}

// Level-by-level (BFS) dump. Each level is cut off after maxPerLevel nodes
// so a 10k-row table doesn't produce a megabyte of JSON.
void BPlusIndex::dump(Json& j, int maxPerLevel) const {
    typedef BPlusTree<Value>::Node Node;
    j.obj();
    j.key("kind").str(kind());
    j.key("order").num(tree_.order());
    j.key("height").num(tree_.height());
    j.key("keys").num(tree_.size());
    j.key("nodes").num(tree_.nodeCount());
    j.key("levels").arr();

    Queue<Node*> q;
    q.push(tree_.root());
    while (!q.empty()) {
        int width = q.size();
        j.obj();
        j.key("total").num(width);
        j.key("nodes").arr();
        for (int i = 0; i < width; i++) {
            Node* n = q.pop();
            if (!n->leaf)
                for (int k = 0; k <= n->n; k++) q.push(n->kids[k]);
            if (i >= maxPerLevel) continue;
            j.obj();
            j.key("leaf").boolean(n->leaf);
            j.key("keys").arr();
            for (int k = 0; k < n->n; k++) j.str(n->keys[k].toString());
            j.end();
            if (n->leaf) {
                j.key("rids").arr();
                for (int k = 0; k < n->n; k++) j.num(n->vals[k]);
                j.end();
            }
            j.end();
        }
        j.end();
        j.end();
    }
    j.end();
    j.end();
}

void AVLIndex::find(const Value& key, DynArray<int>& out) {
    const DynArray<int>* rids = tree_.find(key);
    if (!rids) return;
    for (int i = 0; i < rids->size(); i++) out.push((*rids)[i]);
}

void AVLIndex::range(const Bound& lo, const Bound& hi, DynArray<int>& out) {
    tree_.range(lo.on ? &lo.v : nullptr, lo.inclusive, hi.on ? &hi.v : nullptr, hi.inclusive, out);
}

static void dumpAvl(Json& j, AVLTree<Value>::Node* n, int depth, int maxDepth) {
    if (!n) {
        j.null();
        return;
    }
    j.obj();
    j.key("k").str(n->key.toString());
    j.key("rows").num(n->rids.size());
    j.key("h").num(n->height);
    if (depth >= maxDepth) {
        j.key("cut").boolean(n->left || n->right);
    } else {
        j.key("l");
        dumpAvl(j, n->left, depth + 1, maxDepth);
        j.key("r");
        dumpAvl(j, n->right, depth + 1, maxDepth);
    }
    j.end();
}

void AVLIndex::dump(Json& j, int maxDepth) const {
    j.obj();
    j.key("kind").str(kind());
    j.key("height").num(tree_.height());
    j.key("keys").num(tree_.keyCount());
    j.key("entries").num(tree_.size());
    j.key("root");
    dumpAvl(j, tree_.root(), 1, maxDepth);
    j.end();
}
