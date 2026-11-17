#pragma once
#include "ds/HashTable.h"
#include "ds/DynArray.h"
#include "storage/Table.h"
#include "storage/BufferPool.h"

// Knows every table. Table names are looked up through a hash table
// (name -> Table*), which is the first thing nearly every query does.
//
// A workspace folder looks like:
//   myproject/
//     *.sql              scripts shown in the workbench
//     data/catalog.msql  table definitions (plain text)
//     data/<table>.tbl   raw 4 KB pages
class Catalog {
public:
    Catalog(int poolFrames, int order);
    ~Catalog();

    void openMemory();
    void openFolder(const Str& folder);   // throws DbError
    void close();

    bool isOpen() const { return state_ != Closed; }
    bool inMemory() const { return state_ == Memory; }
    const Str& folder() const { return folder_; }
    Str dataDir() const;

    // traced = add a line to the current query's trace
    Table* find(const Str& name, bool traced = true);
    Table* create(const Str& name, DynArray<Column>& cols, int pkCol);
    void drop(const Str& name);

    int tableCount() const { return list_.size(); }
    Table* tableAt(int i) { return list_[i]; }

    // write dirty pages + the catalog file (after every batch of statements)
    void checkpoint();

    BufferPool& pool() { return pool_; }
    HashTable<Str, Table*>& names() { return tables_; }
    int order() const { return order_; }

private:
    enum State { Closed, Memory, Folder };

    Table* attach(const Str& name, DynArray<Column>& cols, int pkCol, bool fresh);
    void save();
    void load();
    void clearTables();

    HashTable<Str, Table*> tables_;
    DynArray<Table*> list_;     // creation order, for SHOW TABLES and saving
    BufferPool pool_;
    State state_;
    Str folder_;
    int nextId_;
    int order_;
};
