#include "storage/BufferPool.h"
#include "util/Trace.h"

BufferPool::BufferPool(int frames) : table_(frames * 2) {
    cap_ = frames < 2 ? 2 : frames;
    frames_ = new Frame[cap_];
    for (int i = 0; i < cap_; i++) frames_[i].data = new char[PAGE_SIZE];
    for (int i = cap_ - 1; i >= 0; i--) free_.push(i);   // frame 0 handed out first
}

BufferPool::~BufferPool() {
    for (int i = 0; i < cap_; i++) delete[] frames_[i].data;
    delete[] frames_;
}

static Str pageName(DiskFile* f, int pageNo) {
    return f->table() + " page " + Str::num(pageNo);
}

// only the first few touches per statement get a line in the trace
static bool narrate() {
    if (!g_trace) return false;
    if (g_trace->pageNotesLeft > 0) {
        g_trace->pageNotesLeft--;
        return true;
    }
    g_trace->pageNotesSkipped++;
    return false;
}

char* BufferPool::fetch(DiskFile* file, int pageNo, bool willWrite) {
    long long key = keyOf(file, pageNo);
    int* slot = table_.find(key);
    int f;
    if (slot) {
        f = *slot;
        lru_.moveToFront(frames_[f].lru);
        hits++;
        g_stats.poolHits++;
        if (narrate())
            note("pool", "Buffer pool hit",
                 pageName(file, pageNo) + " was already in frame " + Str::num(f) +
                 " (found via the page hash table). Its node moved to the front of the LRU list.");
    } else {
        Str evicted;
        f = grabFrame(evicted);
        Frame& fr = frames_[f];
        file->read(pageNo, fr.data);
        fr.file = file;
        fr.pageNo = pageNo;
        fr.dirty = false;
        table_.put(key, f);
        misses++;
        g_stats.poolMisses++;
        if (narrate())
            note("pool", "Buffer pool miss",
                 pageName(file, pageNo) + " was not cached, so it was read from disk into frame " +
                 Str::num(f) + evicted + ".");
    }
    if (willWrite) frames_[f].dirty = true;
    return frames_[f].data;
}

int BufferPool::grabFrame(Str& evictedNote) {
    if (!free_.empty()) {
        int f = free_.back();
        free_.pop();
        frames_[f].lru = lru_.pushFront(f);
        return f;
    }
    // pool is full: the back of the list is the least recently used frame
    int f = lru_.last()->val;
    Frame& v = frames_[f];
    evictedNote = ", evicting " + pageName(v.file, v.pageNo) + " (least recently used";
    if (v.dirty) {
        v.file->write(v.pageNo, v.data);
        evictedNote += ", dirty so written back first";
    }
    evictedNote += ")";
    table_.remove(keyOf(v.file, v.pageNo));
    lru_.moveToFront(v.lru);
    evictions++;
    g_stats.poolEvictions++;
    return f;
}

void BufferPool::flushAll() {
    for (int i = 0; i < cap_; i++) {
        Frame& fr = frames_[i];
        if (fr.lru && fr.dirty) {
            fr.file->write(fr.pageNo, fr.data);
            fr.dirty = false;
        }
    }
    for (int i = 0; i < cap_; i++)
        if (frames_[i].lru) frames_[i].file->sync();
}

void BufferPool::forget(DiskFile* file) {
    for (int i = 0; i < cap_; i++) {
        Frame& fr = frames_[i];
        if (!fr.lru || fr.file != file) continue;
        table_.remove(keyOf(fr.file, fr.pageNo));
        lru_.erase(fr.lru);
        fr = Frame{nullptr, -1, false, fr.data, nullptr};
        free_.push(i);
    }
}

void BufferPool::forgetAll() {
    for (int i = 0; i < cap_; i++)
        if (frames_[i].lru) forget(frames_[i].file);
    hits = misses = evictions = 0;
}
