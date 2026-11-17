#pragma once
#include <utility>
#include "ds/DynArray.h"

// Bottom-up merge sort: merge runs of 1, then 2, then 4... between two
// buffers. O(n log n) always, and stable (equal rows keep their order),
// which matters when ORDER BY runs after an index already sorted the rows.
template <typename T, typename Less>
void mergeSort(DynArray<T>& a, Less less) {
    int n = a.size();
    if (n < 2) return;
    DynArray<T> tmp;
    tmp.resize(n);

    for (int width = 1; width < n; width *= 2) {
        for (int lo = 0; lo < n; lo += 2 * width) {
            int mid = lo + width < n ? lo + width : n;
            int hi = lo + 2 * width < n ? lo + 2 * width : n;
            int i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                g_stats.sortCompares++;
                if (less(a[j], a[i])) tmp[k++] = std::move(a[j++]);   // only strictly smaller jumps ahead
                else tmp[k++] = std::move(a[i++]);
            }
            while (i < mid) tmp[k++] = std::move(a[i++]);
            while (j < hi) tmp[k++] = std::move(a[j++]);
        }
        DynArray<T> t = std::move(a);
        a = std::move(tmp);
        tmp = std::move(t);
    }
}
