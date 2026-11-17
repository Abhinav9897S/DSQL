#include "Cli.h"
#include "util/Platform.h"
#include <cstdio>
#include <cstring>

static int textWidth(const Str& s) {
    int w = 0;   // count UTF-8 characters, not bytes
    for (int i = 0; i < s.size(); i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) w++;
    return w;
}

static void pad(const Str& s, int width, bool right) {
    int gap = width - textWidth(s);
    if (right)
        for (int i = 0; i < gap; i++) putchar(' ');
    fputs(s.c_str(), stdout);
    if (!right)
        for (int i = 0; i < gap; i++) putchar(' ');
}

static void border(const DynArray<int>& w) {
    putchar('+');
    for (int i = 0; i < w.size(); i++) {
        for (int k = 0; k < w[i] + 2; k++) putchar('-');
        putchar('+');
    }
    putchar('\n');
}

void printResult(const Result& r, bool explain) {
    if (!r.ok) {
        printf("ERROR: %s\n", r.message.c_str());
        return;
    }
    if (!r.columns.empty()) {
        int shown = r.rows.size() < 200 ? r.rows.size() : 200;
        DynArray<int> w;
        for (int c = 0; c < r.columns.size(); c++) {
            int width = textWidth(r.columns[c]);
            for (int i = 0; i < shown; i++) {
                int x = textWidth(r.rows[i][c].toString());
                if (x > width) width = x;
            }
            w.push(width > 40 ? 40 : width);
        }
        border(w);
        for (int c = 0; c < r.columns.size(); c++) {
            fputs("| ", stdout);
            pad(r.columns[c], w[c], false);
            putchar(' ');
        }
        puts("|");
        border(w);
        for (int i = 0; i < shown; i++) {
            for (int c = 0; c < r.columns.size(); c++) {
                Str cell = r.rows[i][c].toString();
                if (textWidth(cell) > 40) cell = cell.substr(0, 37) + "...";
                fputs("| ", stdout);
                pad(cell, w[c], r.types[c] == Type::Int || r.types[c] == Type::Float);
                putchar(' ');
            }
            puts("|");
        }
        border(w);
        if (shown < r.rows.size()) printf("(showing %d of %d rows)\n", shown, r.rows.size());
    }
    printf("%s  (%.3f ms)", r.message.c_str(), r.ms);
    if (!r.plan.empty()) printf("  [%s]", r.plan.c_str());
    putchar('\n');

    if (explain) {
        for (int i = 0; i < r.trace.steps.size(); i++) {
            const TraceStep& s = r.trace.steps[i];
            printf("  %-7s %s: %s", s.ds.c_str(), s.title.c_str(), s.detail.c_str());
            if (s.repeat > 1) printf(" (x%d)", s.repeat);
            putchar('\n');
        }
    }
}

static void runText(Executor& ex, Catalog& cat, const Str& sql, bool explain) {
    DynArray<Result> results;
    ex.run(sql, results);
    cat.checkpoint();
    for (int i = 0; i < results.size(); i++) {
        if (results.size() > 1) printf("\n> %s\n", results[i].sql.c_str());
        printResult(results[i], explain);
    }
}

static Str trimmed(const char* line) {
    Str s(line);
    int a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

int runRepl(Catalog& cat, Executor& ex, bool explain) {
    printf("MiniSQL %s. End statements with ';'. Type .help for commands.\n",
           cat.inMemory() ? "(in-memory session)" : ("workspace " + cat.folder()).c_str());
    Str buffer;
    char line[4096];
    for (;;) {
        fputs(buffer.empty() ? "minisql> " : "    ...> ", stdout);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        Str t = trimmed(line);

        if (buffer.empty() && t.startsWith(".")) {
            if (t == ".quit" || t == ".exit") break;
            if (t == ".explain on") explain = true;
            else if (t == ".explain off") explain = false;
            else if (t == ".tables") runText(ex, cat, "SHOW TABLES;", false);
            else if (t == ".stats") {
                BufferPool& p = cat.pool();
                long long total = p.hits + p.misses;
                printf("buffer pool: %d/%d frames used, %lld hits, %lld misses, %lld evictions, hit rate %.1f%%\n",
                       p.used(), p.capacity(), p.hits, p.misses, p.evictions, total ? 100.0 * p.hits / total : 0.0);
            } else {
                puts(".tables         list tables\n"
                     ".explain on|off show which data structures each query used\n"
                     ".stats          buffer pool counters\n"
                     ".quit           leave");
            }
            continue;
        }
        if (t.empty() && buffer.empty()) continue;
        buffer += line;
        if (t.endsWith(";")) {
            runText(ex, cat, buffer, explain);
            buffer.clear();
        }
    }
    putchar('\n');
    return 0;
}

int runFile(Catalog& cat, Executor& ex, const Str& path, bool explain) {
    Str sql;
    if (!plat::readFile(path, sql)) {
        fprintf(stderr, "can't read %s\n", path.c_str());
        return 1;
    }
    DynArray<Result> results;
    ex.run(sql, results);
    cat.checkpoint();
    bool ok = true;
    for (int i = 0; i < results.size(); i++) {
        printf("\n> %s\n", results[i].sql.c_str());
        printResult(results[i], explain);
        ok = ok && results[i].ok;
    }
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------- benchmark

static unsigned benchSeed = 2024;
static int benchRand(int n) {
    benchSeed = benchSeed * 1103515245u + 12345u;
    return (int)((benchSeed >> 8) % (unsigned)n);
}

struct BenchRun {
    double ms = 0;
    double pages = 0;   // page requests per query
};

static BenchRun timeQueries(Executor& ex, const DynArray<Str>& queries) {
    BenchRun b;
    DsStats before = g_stats;
    double t0 = plat::nowMs();
    for (int i = 0; i < queries.size(); i++) {
        DynArray<Result> r;
        ex.run(queries[i], r);
    }
    DsStats d = g_stats - before;
    b.ms = (plat::nowMs() - t0) / queries.size();
    b.pages = (double)(d.poolHits + d.poolMisses) / queries.size();
    return b;
}

static void compare(Executor& ex, const char* label, const DynArray<Str>& queries) {
    ex.useIndexes = true;
    BenchRun idx = timeQueries(ex, queries);
    ex.useIndexes = false;
    BenchRun scan = timeQueries(ex, queries);
    ex.useIndexes = true;
    double speed = idx.ms > 0 ? scan.ms / idx.ms : 0;
    printf("  %-34s %10.4f %10.4f %8.1fx %10.1f %8.1f\n", label, idx.ms, scan.ms, speed, idx.pages, scan.pages);
}

int runBenchmark(int rows, int frames, int order) {
    if (rows < 100) rows = 100;
    Catalog cat(frames, order);
    cat.openMemory();
    Executor ex(cat);
    printf("MiniSQL benchmark: %d rows, B+ tree order %d, buffer pool of %d x 4 KB frames\n\n", rows, order, frames);

    DynArray<Result> r;
    ex.run("CREATE TABLE bench (id INT PRIMARY KEY, grp INT, score FLOAT, name VARCHAR(24));", r);

    double t0 = plat::nowMs();
    for (int start = 0; start < rows; start += 500) {
        Str sql("INSERT INTO bench VALUES ");
        for (int i = start; i < start + 500 && i < rows; i++) {
            if (i > start) sql += ",";
            sql += "(" + Str::num(i) + "," + Str::num(i % 100) + "," + Str::num((double)benchRand(10000) / 100) +
                   ",'user" + Str::num(i) + "')";
        }
        DynArray<Result> ins;
        ex.run(sql, ins);
        if (!ins.back().ok) {
            printf("insert failed: %s\n", ins.back().message.c_str());
            return 1;
        }
    }
    double loadMs = plat::nowMs() - t0;
    Table* t = cat.find("bench", false);
    printf("  loaded %d rows in %.1f ms -> %d pages, B+ tree height %d (%d nodes)\n", rows, loadMs, t->pageCount(),
           t->pk->height(), t->pk->nodes());

    t0 = plat::nowMs();
    r.clear();
    ex.run("CREATE INDEX idx_grp ON bench(grp);", r);
    printf("  built AVL index on grp in %.1f ms -> height %d, %d distinct keys\n\n", plat::nowMs() - t0,
           t->indexes[0].index->height(), t->indexes[0].index->keys());

    printf("  %-34s %10s %10s %9s %10s %8s\n", "query (avg per run)", "index ms", "scan ms", "speedup", "pages idx", "scan");
    printf("  %-34s %10s %10s %9s %10s %8s\n", "-------------------", "--------", "-------", "-------", "---------", "----");

    DynArray<Str> q;
    for (int i = 0; i < 200; i++) q.push("SELECT * FROM bench WHERE id = " + Str::num(benchRand(rows)) + ";");
    compare(ex, "point lookup  id = ?        (B+)", q);

    q.clear();
    for (int i = 0; i < 50; i++) {
        int lo = benchRand(rows - 100);
        q.push("SELECT * FROM bench WHERE id BETWEEN " + Str::num(lo) + " AND " + Str::num(lo + 99) + ";");
    }
    compare(ex, "range 100 ids BETWEEN      (B+)", q);

    q.clear();
    for (int i = 0; i < 50; i++) q.push("SELECT * FROM bench WHERE grp = " + Str::num(benchRand(100)) + ";");
    compare(ex, "secondary  grp = ?         (AVL)", q);

    q.clear();
    for (int i = 0; i < 20; i++) q.push("SELECT id, name FROM bench ORDER BY id LIMIT 10;");
    compare(ex, "ORDER BY id LIMIT 10 (leaf chain)", q);

    BufferPool& p = cat.pool();
    long long total = p.hits + p.misses;
    printf("\n  buffer pool over the whole run: %lld hits, %lld misses, %lld evictions (hit rate %.1f%%)\n", p.hits,
           p.misses, p.evictions, total ? 100.0 * p.hits / total : 0.0);
    printf("  note: 'pages' counts buffer pool requests per query; a scan touches every page.\n");
    return 0;
}
