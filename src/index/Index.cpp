#include "index/Index.h"

void BPlusIndex::find(const Value& key, DynArray<int>& out) {
    int rid;
    if (tree_.find(key, rid)) out.push(rid);
}

void BPlusIndex::range(const Bound& lo, const Bound& hi, DynArray<int>& out) {
    tree_.range(lo.on ? &lo.v : nullptr, lo.inclusive, hi.on ? &hi.v : nullptr, hi.inclusive, out);
}

void AVLIndex::find(const Value& key, DynArray<int>& out) {
    const DynArray<int>* rids = tree_.find(key);
    if (!rids) return;
    for (int i = 0; i < rids->size(); i++) out.push((*rids)[i]);
}

void AVLIndex::range(const Bound& lo, const Bound& hi, DynArray<int>& out) {
    tree_.range(lo.on ? &lo.v : nullptr, lo.inclusive, hi.on ? &hi.v : nullptr, hi.inclusive, out);
}

