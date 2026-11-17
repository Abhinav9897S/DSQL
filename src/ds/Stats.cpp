#include "ds/Stats.h"

DsStats g_stats;

DsStats operator-(const DsStats& a, const DsStats& b) {
    DsStats d;
    d.arrayGrows = a.arrayGrows - b.arrayGrows;
    d.listMoves = a.listMoves - b.listMoves;
    d.hashProbes = a.hashProbes - b.hashProbes;
    d.hashRehashes = a.hashRehashes - b.hashRehashes;
    d.stackPush = a.stackPush - b.stackPush;
    d.stackPop = a.stackPop - b.stackPop;
    d.queuePush = a.queuePush - b.queuePush;
    d.queuePop = a.queuePop - b.queuePop;
    d.avlVisits = a.avlVisits - b.avlVisits;
    d.avlLL = a.avlLL - b.avlLL;
    d.avlRR = a.avlRR - b.avlRR;
    d.avlLR = a.avlLR - b.avlLR;
    d.avlRL = a.avlRL - b.avlRL;
    d.bptVisits = a.bptVisits - b.bptVisits;
    d.bptSplits = a.bptSplits - b.bptSplits;
    d.bptMerges = a.bptMerges - b.bptMerges;
    d.bptBorrows = a.bptBorrows - b.bptBorrows;
    d.bptLeafHops = a.bptLeafHops - b.bptLeafHops;
    d.poolHits = a.poolHits - b.poolHits;
    d.poolMisses = a.poolMisses - b.poolMisses;
    d.poolEvictions = a.poolEvictions - b.poolEvictions;
    d.diskReads = a.diskReads - b.diskReads;
    d.diskWrites = a.diskWrites - b.diskWrites;
    d.sortCompares = a.sortCompares - b.sortCompares;
    return d;
}
