#pragma once

// Operation counters. Each data structure bumps its own fields as it works.
// The executor copies this before a statement and subtracts afterwards, which
// is how the workbench can say things like "3 B+ tree nodes visited".
struct DsStats {
    long long arrayGrows = 0;     // DynArray reallocations
    long long listMoves = 0;      // linked list relinks (LRU touches)
    long long hashProbes = 0;     // chain entries looked at
    long long hashRehashes = 0;
    long long stackPush = 0;
    long long stackPop = 0;
    long long queuePush = 0;
    long long queuePop = 0;
    long long avlVisits = 0;
    long long avlLL = 0;
    long long avlRR = 0;
    long long avlLR = 0;
    long long avlRL = 0;
    long long bptVisits = 0;
    long long bptSplits = 0;
    long long bptMerges = 0;
    long long bptBorrows = 0;
    long long bptLeafHops = 0;    // following leaf->next during range scans
    long long poolHits = 0;
    long long poolMisses = 0;
    long long poolEvictions = 0;
    long long diskReads = 0;
    long long diskWrites = 0;
    long long sortCompares = 0;
};

extern DsStats g_stats;

DsStats operator-(const DsStats& a, const DsStats& b);
