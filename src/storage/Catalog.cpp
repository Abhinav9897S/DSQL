#include "storage/Catalog.h"
#include "util/Error.h"
#include "util/Platform.h"
#include "util/Trace.h"
#include <cstdio>

Catalog::Catalog(int poolFrames, int order)
    : tables_(16), pool_(poolFrames), state_(Closed), nextId_(1), order_(order) {}

Catalog::~Catalog() { close(); }

Str Catalog::dataDir() const { return plat::join(folder_, "data"); }

void Catalog::openMemory() {
    close();
    state_ = Memory;
    folder_ = Str();
}

void Catalog::openFolder(const Str& folder) {
    close();
    if (!plat::isDir(folder)) throw DbError("not a folder: " + folder);
    folder_ = folder;
    Str data = dataDir();
    if (!plat::isDir(data) && !plat::makeDir(data)) throw DbError("can't create " + data);
    state_ = Folder;
    try {
        load();
    } catch (DbError&) {
        clearTables();
        state_ = Closed;
        folder_ = Str();
        throw;
    }
}

void Catalog::close() {
    if (state_ == Closed) return;
    checkpoint();
    clearTables();
    pool_.forgetAll();
    state_ = Closed;
    folder_ = Str();
}

void Catalog::clearTables() {
    for (int i = 0; i < list_.size(); i++) delete list_[i];
    list_.clear();
    tables_.clear();
    nextId_ = 1;
}

Table* Catalog::find(const Str& name, bool traced) {
    Table** t = tables_.find(name);
    if (traced && g_trace) {
        Str where = "bucket " + Str::num(tables_.lastBucket) + " of " + Str::num(tables_.bucketCount());
        Str probes = Str::num(tables_.lastProbes) + (tables_.lastProbes == 1 ? " probe" : " probes");
        note("hash", "Catalog lookup",
             t ? "Hashed '" + name + "' to " + where + " and found the table after " + probes + "."
               : "Hashed '" + name + "' to " + where + "; no such table after " + probes + ".");
    }
    return t ? *t : nullptr;
}

Table* Catalog::create(const Str& name, DynArray<Column>& cols, int pkCol) {
    return attach(name, cols, pkCol, true);
}

// fresh = brand new table (wipe any leftover file), otherwise reopen its pages
Table* Catalog::attach(const Str& name, DynArray<Column>& cols, int pkCol, bool fresh) {
    if (!isOpen()) throw DbError("no workspace is open");
    if (find(name, false)) throw DbError("table '" + name + "' already exists");
    int id = nextId_++;
    Str path = state_ == Folder ? plat::join(dataDir(), name + ".tbl") : Str();
    if (fresh && !path.empty()) plat::removeFile(path);
    DiskFile* file = new DiskFile(id, name, path);
    Table* t = new Table(id, name, cols, pkCol, file, &pool_, order_);
    if (t->slotsPerPage < 1) {
        delete t;
        throw DbError("a row of '" + name + "' is wider than a 4 KB page");
    }
    tables_.put(name, t);
    list_.push(t);
    return t;
}

void Catalog::drop(const Str& name) {
    Table* t = find(name, false);
    if (!t) throw DbError("no table named '" + name + "'");
    tables_.remove(name);
    for (int i = 0; i < list_.size(); i++)
        if (list_[i] == t) list_.removeAt(i);
    pool_.forget(t->file());
    t->file()->destroy();
    delete t;
}

void Catalog::checkpoint() {
    if (state_ == Closed) return;
    pool_.flushAll();
    if (state_ == Folder) save();
}

// catalog.msql:
//   TABLE <name> <columns> <pk column or -1>
//   COL <name> <type> <size>
//   INDEX <name> <column>
//   END
void Catalog::save() {
    Str out("MINISQL 1\n");
    for (int i = 0; i < list_.size(); i++) {
        Table* t = list_[i];
        out += "TABLE " + t->name + " " + Str::num(t->cols.size()) + " " + Str::num(t->pkCol) + "\n";
        for (int c = 0; c < t->cols.size(); c++)
            out += "COL " + t->cols[c].name + " " + typeName(t->cols[c].type) + " " +
                   Str::num(t->cols[c].size) + "\n";
        for (int k = 0; k < t->indexes.size(); k++)
            out += "INDEX " + t->indexes[k].name + " " + Str::num(t->indexes[k].col) + "\n";
        out += "END\n";
    }
    Str path = plat::join(dataDir(), "catalog.msql");
    if (!plat::writeFile(path, out)) throw DbError("can't write " + path);
}

static Type parseType(const char* s) {
    Str t(s);
    if (t == "FLOAT") return Type::Float;
    if (t == "VARCHAR") return Type::Varchar;
    if (t == "BOOL") return Type::Bool;
    return Type::Int;
}

void Catalog::load() {
    Str path = plat::join(dataDir(), "catalog.msql");
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;   // fresh folder

    char word[64], name[128], colName[128], type[32];
    int ncols, pkCol, size, col;
    if (fscanf(f, "%63s %d", word, &size) != 2 || Str(word) != "MINISQL") {
        fclose(f);
        throw DbError(path + " is not a MiniSQL catalog");
    }
    while (fscanf(f, "%63s", word) == 1) {
        if (Str(word) != "TABLE" || fscanf(f, "%127s %d %d", name, &ncols, &pkCol) != 3) break;
        DynArray<Column> cols;
        for (int c = 0; c < ncols; c++) {
            Column column;
            if (fscanf(f, "%63s %127s %31s %d", word, colName, type, &size) != 4) break;
            column.name = Str(colName);
            column.type = parseType(type);
            column.size = size;
            cols.push(column);
        }
        Table* t = attach(Str(name), cols, pkCol, false);
        t->loadExisting();
        while (fscanf(f, "%63s", word) == 1 && Str(word) == "INDEX") {
            char ixName[128];
            if (fscanf(f, "%127s %d", ixName, &col) != 2) break;
            t->addIndex(Str(ixName), col);
        }
    }
    fclose(f);
}
