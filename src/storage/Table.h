#pragma once
#include "ds/DynArray.h"
#include "storage/Value.h"
#include "storage/Disk.h"
#include "storage/BufferPool.h"
#include "index/Index.h"

struct Column {
    Str name;
    Type type = Type::Int;
    int size = 0;     // VARCHAR(n) limit
    int offset = 0;   // where it sits inside a row slot
    int width = 0;    // bytes it takes in the slot
};

struct SecondaryIndex {
    Str name;
    int col = -1;
    Index* index = nullptr;
};

// A heap file of fixed-size row slots.
//
// Every row takes exactly slotSize bytes: 1 "in use" byte, then each column
// at a fixed offset (INT/FLOAT 8 bytes, BOOL 1, VARCHAR(n) 1 + n). Fixed
// slots mean a row never moves, so its row id (rid) is just
//     rid = page * slotsPerPage + slot
// and that's what the indexes store.
//
// fill_ is the page directory: a DynArray with the number of used slots per
// page, so INSERT can skip full pages without reading them.
class Table {
public:
    Table(int id, const Str& name, const DynArray<Column>& cols, int pkCol,
          DiskFile* file, BufferPool* pool, int order);
    ~Table();

    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;

    int id;
    Str name;
    DynArray<Column> cols;
    int pkCol;                 // -1 = no primary key
    Index* pk;                 // B+ tree on pkCol
    DynArray<SecondaryIndex> indexes;
    int slotSize;
    int slotsPerPage;
    int rowCount;

    int colIndex(const Str& col) const;
    Index* indexOn(int col);
    Str indexName(int col) const;
    int pageCount() const { return fill_.size(); }
    int pageFill(int page) const { return fill_[page]; }
    DiskFile* file() { return file_; }

    int insert(const Row& row);                                   // returns rid
    void read(int rid, Row& out);
    void update(int rid, const Row& before, const Row& after);
    void remove(int rid, const Row& row);
    void scan(DynArray<int>& rids, DynArray<Row>& rows);

    void addIndex(const Str& indexName, int col);
    bool dropIndex(const Str& indexName);
    void loadExisting();    // reopened folder: rebuild page directory + primary index

private:
    void encode(const Row& row, char* slot) const;
    void decode(const char* slot, Row& out) const;
    int findFreeSlot();

    DiskFile* file_;
    BufferPool* pool_;
    DynArray<int> fill_;
    int freeHint_;   // no page before this one has room
};
