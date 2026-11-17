#include "server/Api.h"
#include "util/Error.h"
#include "util/Json.h"
#include "util/Platform.h"
#include <cstdlib>

static Catalog* cat_ = nullptr;
static Executor* exec_ = nullptr;
static Str webDir_;
static Str examplesDir_;

void apiInit(Catalog* cat, Executor* exec, const Str& webDir, const Str& examplesDir) {
    cat_ = cat;
    exec_ = exec;
    webDir_ = webDir;
    examplesDir_ = examplesDir;
}

static void fail(HttpResponse& res, const Str& msg, int status = 400) {
    Json j;
    j.obj().key("error").str(msg).end();
    res.status = status;
    res.body = j.text();
}

static void valueJson(Json& j, const Value& v) {
    switch (v.type) {
    case Type::Int: j.num(v.i); break;
    case Type::Float: j.num(v.f); break;
    case Type::Bool: j.boolean(v.i != 0); break;
    case Type::Varchar: j.str(v.s); break;
    }
}

static void statsJson(Json& j, const DsStats& s) {
    j.obj();
    j.key("arrayGrows").num(s.arrayGrows);
    j.key("listMoves").num(s.listMoves);
    j.key("hashProbes").num(s.hashProbes);
    j.key("hashRehashes").num(s.hashRehashes);
    j.key("stackPush").num(s.stackPush);
    j.key("stackPop").num(s.stackPop);
    j.key("queuePush").num(s.queuePush);
    j.key("queuePop").num(s.queuePop);
    j.key("avlVisits").num(s.avlVisits);
    j.key("avlLL").num(s.avlLL);
    j.key("avlRR").num(s.avlRR);
    j.key("avlLR").num(s.avlLR);
    j.key("avlRL").num(s.avlRL);
    j.key("bptVisits").num(s.bptVisits);
    j.key("bptSplits").num(s.bptSplits);
    j.key("bptMerges").num(s.bptMerges);
    j.key("bptBorrows").num(s.bptBorrows);
    j.key("bptLeafHops").num(s.bptLeafHops);
    j.key("poolHits").num(s.poolHits);
    j.key("poolMisses").num(s.poolMisses);
    j.key("poolEvictions").num(s.poolEvictions);
    j.key("diskReads").num(s.diskReads);
    j.key("diskWrites").num(s.diskWrites);
    j.key("sortCompares").num(s.sortCompares);
    j.end();
}

static void resultJson(Json& j, const Result& r, int rowLimit) {
    j.obj();
    j.key("ok").boolean(r.ok);
    j.key("kind").str(r.kind);
    j.key("sql").str(r.sql);
    j.key("line").num(r.line);
    j.key("errorLine").num(r.errorLine);
    j.key("message").str(r.message);
    j.key("plan").str(r.plan);
    j.key("ms").num(r.ms);
    j.key("changed").boolean(r.changed);

    j.key("columns").arr();
    for (int i = 0; i < r.columns.size(); i++)
        j.obj().key("name").str(r.columns[i]).key("type").str(typeName(r.types[i])).end();
    j.end();

    j.key("rowCount").num(r.rows.size());
    j.key("rows").arr();
    for (int i = 0; i < r.rows.size() && i < rowLimit; i++) {
        j.arr();
        for (int k = 0; k < r.rows[i].size(); k++) valueJson(j, r.rows[i][k]);
        j.end();
    }
    j.end();

    j.key("steps").arr();
    for (int i = 0; i < r.trace.steps.size(); i++) {
        const TraceStep& s = r.trace.steps[i];
        j.obj().key("ds").str(s.ds).key("title").str(s.title).key("detail").str(s.detail).key("repeat").num(s.repeat).end();
    }
    j.end();

    j.key("tokens").arr();
    for (int i = 0; i < r.tokens.size() && i < 120; i++)
        j.obj().key("t").str(tokName(r.tokens[i].type)).key("v").str(r.tokens[i].text).end();
    j.end();

    j.key("stats");
    statsJson(j, r.stats);
    j.end();
}

static void poolJson(Json& j, bool withFrames) {
    BufferPool& p = cat_->pool();
    j.obj();
    j.key("capacity").num(p.capacity());
    j.key("used").num(p.used());
    j.key("hits").num(p.hits);
    j.key("misses").num(p.misses);
    j.key("evictions").num(p.evictions);
    if (withFrames) {
        j.key("lru").arr();   // most recently used first
        for (LinkedList<int>::Node* n = p.lru().first(); n; n = p.lru().after(n)) {
            const BufferPool::Frame& f = p.frame(n->val);
            j.obj();
            j.key("frame").num(n->val);
            j.key("table").str(f.file ? f.file->table() : Str("?"));
            j.key("page").num(f.pageNo);
            j.key("dirty").boolean(f.dirty);
            j.end();
        }
        j.end();
    }
    j.end();
}

static bool validScriptName(const Str& n) {
    if (n.empty() || n.size() > 120 || !n.endsWith(".sql") || n[0] == '.') return false;
    for (int i = 0; i < n.size(); i++)
        if (n[i] == '/' || n[i] == '\\' || n[i] == ':') return false;
    return true;
}

static Str sepText() {
    char c = plat::sep();
    return Str(&c, 1);
}

static void tableJson(Json& j, Table* t) {
    j.obj();
    j.key("name").str(t->name);
    j.key("rows").num(t->rowCount);
    j.key("pages").num(t->pageCount());
    j.key("slotSize").num(t->slotSize);
    j.key("slotsPerPage").num(t->slotsPerPage);

    j.key("columns").arr();
    for (int c = 0; c < t->cols.size(); c++) {
        const Column& col = t->cols[c];
        Index* ix = t->indexOn(c);
        j.obj();
        j.key("name").str(col.name);
        j.key("type").str(col.type == Type::Varchar ? "VARCHAR(" + Str::num(col.size) + ")" : Str(typeName(col.type)));
        j.key("pk").boolean(c == t->pkCol);
        j.key("index").str(ix ? Str(ix->kind()) : Str());
        j.end();
    }
    j.end();

    j.key("indexes").arr();
    for (int c = 0; c < t->cols.size(); c++) {
        Index* ix = t->indexOn(c);
        if (!ix) continue;
        j.obj();
        j.key("name").str(t->indexName(c));
        j.key("column").str(t->cols[c].name);
        j.key("kind").str(ix->kind());
        j.key("height").num(ix->height());
        j.key("keys").num(ix->keys());
        j.end();
    }
    j.end();
    j.end();
}

// everything the navigator and status bar show
static Str stateText() {
    bool folder = cat_->isOpen() && !cat_->inMemory();
    Json j;
    j.obj();
    j.key("open").boolean(cat_->isOpen());
    j.key("memory").boolean(cat_->inMemory());
    j.key("path").str(cat_->folder());
    j.key("name").str(cat_->inMemory() ? Str("in-memory") : plat::baseName(cat_->folder()));
    j.key("sep").str(sepText());
    j.key("order").num(cat_->order());
    j.key("pool");
    poolJson(j, false);

    j.key("tables").arr();
    for (int i = 0; i < cat_->tableCount(); i++) tableJson(j, cat_->tableAt(i));
    j.end();

    j.key("scripts").arr();
    DynArray<plat::DirEntry> entries;
    if (folder && plat::listDir(cat_->folder(), entries))
        for (int i = 0; i < entries.size(); i++)
            if (!entries[i].dir && validScriptName(entries[i].name)) j.str(entries[i].name);
    j.end();

    j.key("files").arr();
    DynArray<plat::DirEntry> data;
    if (folder && plat::listDir(cat_->dataDir(), data))
        for (int i = 0; i < data.size(); i++)
            if (!data[i].dir) j.obj().key("name").str(data[i].name).key("size").num(data[i].size).end();
    j.end();
    j.end();
    return j.text();
}

static void browse(const HttpRequest& req, HttpResponse& res) {
    Str path = req.param("path");
    bool roots = path == "::roots";
    Json j;
    j.obj();
    j.key("home").str(plat::home());
    j.key("sep").str(sepText());
    DynArray<plat::DirEntry> entries;
    if (roots) {
        plat::listRoots(entries);
        j.key("path").str("");
        j.key("parent").str("");
    } else {
        if (path.empty()) path = plat::home();
        path = plat::absolute(path);
        if (!plat::isDir(path)) {
            fail(res, "not a folder: " + path);
            return;
        }
        if (!plat::listDir(path, entries)) {
            fail(res, "can't read " + path);
            return;
        }
        Str parent = plat::parent(path);
        if (parent == path) parent = Str();
#ifdef _WIN32
        if (parent.empty()) parent = "::roots";
#endif
        j.key("path").str(path);
        j.key("parent").str(parent);
    }
    j.key("entries").arr();
    for (int i = 0; i < entries.size(); i++) {
        const plat::DirEntry& e = entries[i];
        if (!e.dir || e.name[0] == '.') continue;
        Str full = roots ? e.name : plat::join(path, e.name);
        bool ws = plat::exists(plat::join(plat::join(full, "data"), "catalog.msql"));
        j.obj().key("name").str(e.name).key("path").str(full).key("workspace").boolean(ws).end();
    }
    j.end();
    j.end();
    res.body = j.text();
}

static void inspect(const HttpRequest& req, HttpResponse& res) {
    Json j;
    j.obj();
    Table* t = cat_->isOpen() ? cat_->find(req.param("table"), false) : nullptr;
    j.key("table");
    if (!t) {
        j.null();
    } else {
        j.obj();
        j.key("name").str(t->name);
        j.key("rows").num(t->rowCount);
        j.key("slotsPerPage").num(t->slotsPerPage);
        j.key("pages").arr();
        for (int p = 0; p < t->pageCount(); p++) j.num(t->pageFill(p));
        j.end();
        j.key("pk");
        if (t->pk) {
            j.obj().key("column").str(t->cols[t->pkCol].name).key("tree");
            t->pk->dump(j, 24);
            j.end();
        } else {
            j.null();
        }
        j.key("indexes").arr();
        for (int k = 0; k < t->indexes.size(); k++) {
            j.obj().key("name").str(t->indexes[k].name).key("column").str(t->cols[t->indexes[k].col].name).key("tree");
            t->indexes[k].index->dump(j, 6);
            j.end();
        }
        j.end();
        j.end();
    }

    j.key("pool");
    poolJson(j, true);

    j.key("catalog").obj();
    HashTable<Str, Table*>& names = cat_->names();
    j.key("buckets").arr();
    for (int b = 0; b < names.bucketCount(); b++) {
        j.arr();
        for (HashTable<Str, Table*>::Entry* e = names.bucket(b); e; e = e->next) j.str(e->key);
        j.end();
    }
    j.end();
    j.end();
    j.end();
    res.body = j.text();
}

static void writeStarterScript(const Str& folder) {
    Str text;
    if (plat::readFile(plat::join(examplesDir_, "getting_started.sql"), text))
        plat::writeFile(plat::join(folder, "getting_started.sql"), text);
}

static void serveStatic(const HttpRequest& req, HttpResponse& res) {
    Str path = req.path == "/" ? Str("/index.html") : req.path;
    if (path.find('.') < 0 || path.startsWith("/..")) {
        fail(res, "not found", 404);
        return;
    }
    for (int i = 0; i + 1 < path.size(); i++)
        if (path[i] == '.' && path[i + 1] == '.') {
            fail(res, "not found", 404);
            return;
        }
    Str file = plat::join(webDir_, path.substr(1, -1));
    if (!plat::readFile(file, res.body)) {
        fail(res, "not found", 404);
        return;
    }
    if (path.endsWith(".html")) res.type = "text/html; charset=utf-8";
    else if (path.endsWith(".css")) res.type = "text/css; charset=utf-8";
    else if (path.endsWith(".js")) res.type = "application/javascript; charset=utf-8";
    else if (path.endsWith(".svg")) res.type = "image/svg+xml";
    else if (path.endsWith(".png")) res.type = "image/png";
    else res.type = "application/octet-stream";
}

void apiHandle(const HttpRequest& req, HttpResponse& res) {
    if (!req.path.startsWith("/api/")) {
        serveStatic(req, res);
        return;
    }
    const Str& p = req.path;
    bool post = req.method == "POST";
    try {
        if (p == "/api/state") {
            res.body = stateText();
        } else if (p == "/api/browse") {
            browse(req, res);
        } else if (p == "/api/open" && post) {
            Str path = plat::absolute(req.param("path"));
            cat_->openFolder(path);
            res.body = stateText();
        } else if (p == "/api/create" && post) {
            Str name = req.param("name");
            if (name.empty() || name == "." || name == ".." || name.find('/') >= 0 || name.find('\\') >= 0)
                throw DbError("pick a plain folder name");
            Str folder = plat::join(plat::absolute(req.param("parent")), name);
            if (plat::exists(folder)) throw DbError(folder + " already exists");
            if (!plat::makeDir(folder)) throw DbError("can't create " + folder);
            writeStarterScript(folder);
            cat_->openFolder(folder);
            res.body = stateText();
        } else if (p == "/api/memory" && post) {
            cat_->openMemory();
            res.body = stateText();
        } else if (p == "/api/close" && post) {
            cat_->close();
            res.body = stateText();
        } else if (p == "/api/query" && post) {
            if (!cat_->isOpen()) throw DbError("open a folder or an in-memory session first");
            int limit = (int)strtol(req.param("limit").c_str(), nullptr, 10);
            if (limit <= 0) limit = 1000;
            int line = (int)strtol(req.param("line").c_str(), nullptr, 10);
            DynArray<Result> results;
            exec_->run(req.body, results, line > 0 ? line : 1);
            cat_->checkpoint();
            Json j;
            j.obj().key("results").arr();
            for (int i = 0; i < results.size(); i++) resultJson(j, results[i], limit);
            j.end();
            j.key("pool");
            poolJson(j, false);
            j.end();
            res.body = j.text();
        } else if (p == "/api/script") {
            Str name = req.param("name");
            if (!validScriptName(name)) throw DbError("script names must end in .sql and contain no slashes");
            if (!cat_->isOpen() || cat_->inMemory()) throw DbError("open a folder to work with script files");
            Str file = plat::join(cat_->folder(), name);
            if (post) {
                if (!plat::writeFile(file, req.body)) throw DbError("can't write " + file);
                res.body = stateText();
            } else {
                if (!plat::readFile(file, res.body)) throw DbError("can't read " + file);
                res.type = "text/plain; charset=utf-8";
            }
        } else if (p == "/api/script/delete" && post) {
            Str name = req.param("name");
            if (!validScriptName(name) || !cat_->isOpen() || cat_->inMemory()) throw DbError("bad script name");
            plat::removeFile(plat::join(cat_->folder(), name));
            res.body = stateText();
        } else if (p == "/api/inspect") {
            inspect(req, res);
        } else if (p == "/api/sample") {
            if (!plat::readFile(plat::join(examplesDir_, "getting_started.sql"), res.body))
                throw DbError("examples/getting_started.sql not found");
            res.type = "text/plain; charset=utf-8";
        } else {
            fail(res, "no such endpoint", 404);
        }
    } catch (DbError& e) {
        fail(res, e.msg);
    }
}
