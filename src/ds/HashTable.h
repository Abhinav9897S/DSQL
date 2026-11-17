#pragma once
#include "ds/Keys.h"
#include "ds/Stats.h"

// Separate-chaining hash table, our std::unordered_map.
// buckets_ is an array of chain heads; colliding keys sit in a singly linked
// chain. Once more than 3/4 of the buckets' worth of keys are stored we double
// the bucket count and re-link every entry, which keeps chains short and
// lookups O(1) on average.
//
// K needs: hashOf(K) and operator==.
template <typename K, typename V>
class HashTable {
public:
    struct Entry {
        K key;
        V value;
        Entry* next;
    };

    explicit HashTable(int buckets = 16) : count_(0) {
        nb_ = buckets < 4 ? 4 : buckets;
        buckets_ = new Entry*[nb_];
        for (int i = 0; i < nb_; i++) buckets_[i] = nullptr;
    }

    ~HashTable() {
        clear();
        delete[] buckets_;
    }

    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    // pointer to the value, or null
    V* find(const K& key) {
        lastBucket = indexOf(key);
        lastProbes = 0;
        for (Entry* e = buckets_[lastBucket]; e; e = e->next) {
            lastProbes++;
            g_stats.hashProbes++;
            if (e->key == key) return &e->value;
        }
        return nullptr;
    }

    bool contains(const K& key) { return find(key) != nullptr; }

    // insert, or overwrite if the key is already there
    void put(const K& key, const V& value) {
        V* old = find(key);
        if (old) *old = value;
        else insertNew(key, value);
    }

    // value for key, default-constructed and inserted if missing
    V& at(const K& key) {
        V* old = find(key);
        if (old) return *old;
        return insertNew(key, V())->value;
    }

    bool remove(const K& key) {
        int b = indexOf(key);
        Entry* prev = nullptr;
        for (Entry* e = buckets_[b]; e; prev = e, e = e->next) {
            g_stats.hashProbes++;
            if (!(e->key == key)) continue;
            if (prev) prev->next = e->next;
            else buckets_[b] = e->next;
            delete e;
            count_--;
            return true;
        }
        return false;
    }

    void clear() {
        for (int i = 0; i < nb_; i++) {
            Entry* e = buckets_[i];
            while (e) {
                Entry* next = e->next;
                delete e;
                e = next;
            }
            buckets_[i] = nullptr;
        }
        count_ = 0;
    }

    template <typename F>
    void forEach(F fn) {
        for (int i = 0; i < nb_; i++)
            for (Entry* e = buckets_[i]; e; e = e->next) fn(e->key, e->value);
    }

    int size() const { return count_; }
    int bucketCount() const { return nb_; }
    Entry* bucket(int i) const { return buckets_[i]; }

    // where the most recent find() looked; the trace reports these
    int lastBucket = -1;
    int lastProbes = 0;

private:
    int indexOf(const K& key) const { return (int)(hashOf(key) % (unsigned)nb_); }

    Entry* insertNew(const K& key, const V& value) {
        if ((count_ + 1) * 4 > nb_ * 3) rehash();
        int b = indexOf(key);
        Entry* e = new Entry{key, value, buckets_[b]};
        buckets_[b] = e;
        count_++;
        return e;
    }

    void rehash() {
        int oldCount = nb_;
        Entry** old = buckets_;
        nb_ *= 2;
        buckets_ = new Entry*[nb_];
        for (int i = 0; i < nb_; i++) buckets_[i] = nullptr;

        for (int i = 0; i < oldCount; i++) {
            Entry* e = old[i];
            while (e) {
                Entry* next = e->next;
                int b = indexOf(e->key);
                e->next = buckets_[b];
                buckets_[b] = e;
                e = next;
            }
        }
        delete[] old;
        g_stats.hashRehashes++;
    }

    Entry** buckets_;
    int nb_;
    int count_;
};
