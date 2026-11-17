#pragma once
#include "ds/HashTable.h"
#include "ds/LinkedList.h"
#include "ds/DynArray.h"
#include "storage/Disk.h"

// A fixed number of page frames shared by every table.
//
//   page table : hash table (file id, page no) -> frame   O(1) "is it cached?"
//   LRU list   : doubly linked list of frames, most recent at the front
//
// Hit  -> move that frame's node to the front (O(1), we keep the node pointer).
// Miss -> take a free frame, or evict the frame at the back of the list,
//         writing it to disk first if it was modified.
class BufferPool {
public:
    struct Frame {
        DiskFile* file = nullptr;
        int pageNo = -1;
        bool dirty = false;
        char* data = nullptr;
        LinkedList<int>::Node* lru = nullptr;   // null while the frame is free
    };

    explicit BufferPool(int frames);
    ~BufferPool();

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // page bytes, valid until the next fetch; willWrite marks the frame dirty
    char* fetch(DiskFile* file, int pageNo, bool willWrite);

    void flushAll();
    void forget(DiskFile* file);   // drop a file's frames without writing (DROP TABLE)
    void forgetAll();

    int capacity() const { return cap_; }
    int used() const { return lru_.size(); }
    const Frame& frame(int i) const { return frames_[i]; }
    LinkedList<int>& lru() { return lru_; }
    HashTable<long long, int>& pageTable() { return table_; }

    // lifetime totals for the status bar
    long long hits = 0, misses = 0, evictions = 0;

private:
    static long long keyOf(DiskFile* f, int pageNo) {
        return ((long long)f->id() << 32) | (unsigned)pageNo;
    }
    int grabFrame(Str& evictedNote);

    Frame* frames_;
    int cap_;
    HashTable<long long, int> table_;
    LinkedList<int> lru_;
    DynArray<int> free_;
};
