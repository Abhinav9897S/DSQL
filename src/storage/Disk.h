#pragma once
#include <cstdio>
#include "ds/Str.h"
#include "ds/DynArray.h"

const int PAGE_SIZE = 4096;

// All pages of one table. With a path, page n lives at byte n * PAGE_SIZE of
// a binary file. With an empty path the "disk" is just a DynArray of page
// buffers in memory (in-memory sessions and the benchmark use that).
class DiskFile {
public:
    DiskFile(int id, const Str& table, const Str& path);
    ~DiskFile();

    DiskFile(const DiskFile&) = delete;
    DiskFile& operator=(const DiskFile&) = delete;

    int id() const { return id_; }
    const Str& table() const { return table_; }
    const Str& path() const { return path_; }
    int pageCount() const { return pages_; }

    int allocPage();                         // appends a zeroed page, returns its number
    void read(int pageNo, char* out);
    void write(int pageNo, const char* in);
    void sync();
    void destroy();                          // DROP TABLE: delete the file

private:
    int id_;
    Str table_;
    Str path_;
    FILE* fp_;
    int pages_;
    DynArray<char*> mem_;
};
