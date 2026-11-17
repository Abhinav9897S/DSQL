#include "storage/Disk.h"
#include "ds/Stats.h"
#include "util/Error.h"
#include <cstring>

DiskFile::DiskFile(int id, const Str& table, const Str& path)
    : id_(id), table_(table), path_(path), fp_(nullptr), pages_(0) {
    if (path_.empty()) return;
    fp_ = fopen(path_.c_str(), "r+b");
    if (!fp_) fp_ = fopen(path_.c_str(), "w+b");
    if (!fp_) throw DbError("can't open data file " + path_);
    fseek(fp_, 0, SEEK_END);
    pages_ = (int)(ftell(fp_) / PAGE_SIZE);
}

DiskFile::~DiskFile() {
    if (fp_) fclose(fp_);
    for (int i = 0; i < mem_.size(); i++) delete[] mem_[i];
}

int DiskFile::allocPage() {
    if (fp_) {
        char zero[PAGE_SIZE];
        memset(zero, 0, PAGE_SIZE);
        write(pages_, zero);
    } else {
        char* p = new char[PAGE_SIZE];
        memset(p, 0, PAGE_SIZE);
        mem_.push(p);
    }
    return pages_++;
}

void DiskFile::read(int pageNo, char* out) {
    g_stats.diskReads++;
    if (!fp_) {
        memcpy(out, mem_[pageNo], PAGE_SIZE);
        return;
    }
    fseek(fp_, (long)pageNo * PAGE_SIZE, SEEK_SET);
    size_t got = fread(out, 1, PAGE_SIZE, fp_);
    if (got < (size_t)PAGE_SIZE) memset(out + got, 0, PAGE_SIZE - got);
}

void DiskFile::write(int pageNo, const char* in) {
    g_stats.diskWrites++;
    if (!fp_) {
        memcpy(mem_[pageNo], in, PAGE_SIZE);
        return;
    }
    fseek(fp_, (long)pageNo * PAGE_SIZE, SEEK_SET);
    fwrite(in, 1, PAGE_SIZE, fp_);
}

void DiskFile::sync() {
    if (fp_) fflush(fp_);
}

void DiskFile::destroy() {
    if (fp_) {
        fclose(fp_);
        fp_ = nullptr;
        remove(path_.c_str());
    }
    pages_ = 0;
}
