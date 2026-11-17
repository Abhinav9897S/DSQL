#include "storage/Table.h"
#include "util/Error.h"
#include "util/Trace.h"
#include <cstring>

Table::Table(int id_, const Str& name_, const DynArray<Column>& cols_, int pkCol_,
             DiskFile* file, BufferPool* pool, int order)
    : id(id_), name(name_), cols(cols_), pkCol(pkCol_), pk(nullptr),
      slotSize(0), slotsPerPage(0), rowCount(0), file_(file), pool_(pool), freeHint_(0) {
    int off = 1;   // byte 0 of a slot = "in use" flag
    for (int i = 0; i < cols.size(); i++) {
        Column& c = cols[i];
        switch (c.type) {
        case Type::Int:
        case Type::Float: c.width = 8; break;
        case Type::Bool: c.width = 1; break;
        case Type::Varchar: c.width = 1 + c.size; break;
        }
        c.offset = off;
        off += c.width;
    }
    slotSize = off;
    slotsPerPage = PAGE_SIZE / slotSize;
    if (pkCol >= 0) pk = new BPlusIndex(order);
}

Table::~Table() {
    pool_->forget(file_);
    delete pk;
    for (int i = 0; i < indexes.size(); i++) delete indexes[i].index;
    delete file_;
}

int Table::colIndex(const Str& col) const {
    for (int i = 0; i < cols.size(); i++)
        if (cols[i].name == col) return i;
    return -1;
}

Index* Table::indexOn(int col) {
    if (col < 0) return nullptr;
    if (col == pkCol) return pk;
    for (int i = 0; i < indexes.size(); i++)
        if (indexes[i].col == col) return indexes[i].index;
    return nullptr;
}

Str Table::indexName(int col) const {
    if (col == pkCol) return "PRIMARY";
    for (int i = 0; i < indexes.size(); i++)
        if (indexes[i].col == col) return indexes[i].name;
    return Str();
}

void Table::encode(const Row& row, char* slot) const {
    memset(slot, 0, slotSize);
    slot[0] = 1;
    for (int c = 0; c < cols.size(); c++) {
        const Column& col = cols[c];
        const Value& v = row[c];
        char* p = slot + col.offset;
        switch (col.type) {
        case Type::Int: memcpy(p, &v.i, 8); break;
        case Type::Float: memcpy(p, &v.f, 8); break;
        case Type::Bool: p[0] = v.i ? 1 : 0; break;
        case Type::Varchar: {
            int n = v.s.size() < col.size ? v.s.size() : col.size;
            p[0] = (char)(unsigned char)n;
            memcpy(p + 1, v.s.c_str(), n);
            break;
        }
        }
    }
}

void Table::decode(const char* slot, Row& out) const {
    out.resize(cols.size());
    for (int c = 0; c < cols.size(); c++) {
        const Column& col = cols[c];
        const char* p = slot + col.offset;
        Value& v = out[c];
        v.type = col.type;
        switch (col.type) {
        case Type::Int: memcpy(&v.i, p, 8); break;
        case Type::Float: memcpy(&v.f, p, 8); break;
        case Type::Bool: v.i = p[0] ? 1 : 0; break;
        case Type::Varchar: v.s = Str(p + 1, (unsigned char)p[0]); break;
        }
    }
}

int Table::findFreeSlot() {
    int p = freeHint_;
    while (p < fill_.size() && fill_[p] >= slotsPerPage) p++;
    if (p == fill_.size()) {
        file_->allocPage();
        fill_.push(0);
        note("disk", "New page", "Every page of " + name + " was full, so page " + Str::num(p) +
                                     " was appended to the file and to the page directory (DynArray).");
    }
    freeHint_ = p;
    char* page = pool_->fetch(file_, p, false);
    for (int s = 0; s < slotsPerPage; s++)
        if (page[s * slotSize] == 0) return p * slotsPerPage + s;
    throw DbError("page directory out of sync for " + name);
}

int Table::insert(const Row& row) {
    int rid = findFreeSlot();
    if (pk && !pk->insert(row[pkCol], rid))
        throw DbError("duplicate primary key " + row[pkCol].toSql() + " in " + name);
    for (int i = 0; i < indexes.size(); i++) indexes[i].index->insert(row[indexes[i].col], rid);

    int page = rid / slotsPerPage;
    char* data = pool_->fetch(file_, page, true);
    encode(row, data + (rid % slotsPerPage) * slotSize);
    fill_[page]++;
    rowCount++;
    return rid;
}

void Table::read(int rid, Row& out) {
    char* data = pool_->fetch(file_, rid / slotsPerPage, false);
    const char* slot = data + (rid % slotsPerPage) * slotSize;
    if (!slot[0]) throw DbError("row " + Str::num(rid) + " of " + name + " is empty");
    decode(slot, out);
}

void Table::update(int rid, const Row& before, const Row& after) {
    if (pk && !(before[pkCol] == after[pkCol])) {
        // insert the new key first so a duplicate leaves everything untouched
        if (!pk->insert(after[pkCol], rid))
            throw DbError("duplicate primary key " + after[pkCol].toSql() + " in " + name);
        pk->remove(before[pkCol], rid);
    }
    for (int i = 0; i < indexes.size(); i++) {
        int c = indexes[i].col;
        if (before[c] == after[c]) continue;
        indexes[i].index->remove(before[c], rid);
        indexes[i].index->insert(after[c], rid);
    }
    char* data = pool_->fetch(file_, rid / slotsPerPage, true);
    encode(after, data + (rid % slotsPerPage) * slotSize);
}

void Table::remove(int rid, const Row& row) {
    if (pk) pk->remove(row[pkCol], rid);
    for (int i = 0; i < indexes.size(); i++) indexes[i].index->remove(row[indexes[i].col], rid);

    int page = rid / slotsPerPage;
    char* data = pool_->fetch(file_, page, true);
    data[(rid % slotsPerPage) * slotSize] = 0;
    fill_[page]--;
    rowCount--;
    if (page < freeHint_) freeHint_ = page;
}

void Table::scan(DynArray<int>& rids, DynArray<Row>& rows) {
    for (int p = 0; p < fill_.size(); p++) {
        if (fill_[p] == 0) continue;
        char* data = pool_->fetch(file_, p, false);
        for (int s = 0; s < slotsPerPage; s++) {
            const char* slot = data + s * slotSize;
            if (!slot[0]) continue;
            Row r;
            decode(slot, r);
            rids.push(p * slotsPerPage + s);
            rows.push(std::move(r));
        }
    }
}

void Table::addIndex(const Str& indexName, int col) {
    SecondaryIndex ix;
    ix.name = indexName;
    ix.col = col;
    ix.index = new AVLIndex();
    DynArray<int> rids;
    DynArray<Row> rows;
    scan(rids, rows);
    for (int i = 0; i < rows.size(); i++) ix.index->insert(rows[i][col], rids[i]);
    indexes.push(ix);
}

bool Table::dropIndex(const Str& indexName) {
    for (int i = 0; i < indexes.size(); i++) {
        if (indexes[i].name != indexName) continue;
        delete indexes[i].index;
        indexes.removeAt(i);
        return true;
    }
    return false;
}

void Table::loadExisting() {
    int pages = file_->pageCount();
    fill_.resize(pages);
    rowCount = 0;
    for (int p = 0; p < pages; p++) {
        char* data = pool_->fetch(file_, p, false);
        int used = 0;
        for (int s = 0; s < slotsPerPage; s++) {
            const char* slot = data + s * slotSize;
            if (!slot[0]) continue;
            used++;
            if (pk) {
                Row r;
                decode(slot, r);
                pk->insert(r[pkCol], p * slotsPerPage + s);
            }
        }
        fill_[p] = used;
        rowCount += used;
    }
    freeHint_ = 0;
}
