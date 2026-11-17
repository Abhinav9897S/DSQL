// Assertion-based test harness: no framework, just CHECK and a counter.
// Covers each data structure on its own, then whole queries end to end.

#include <cstdio>
#include <cstdlib>
#include "ds/DynArray.h"
#include "ds/LinkedList.h"
#include "ds/HashTable.h"
#include "ds/Stack.h"
#include "ds/Queue.h"
#include "ds/AVLTree.h"
#include "ds/BPlusTree.h"
#include "ds/Sort.h"
#include "ds/Str.h"
#include "storage/BufferPool.h"
#include "storage/Catalog.h"
#include "sql/Executor.h"
#include "sql/Checker.h"
#include "util/Platform.h"

static int passed = 0, failed = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (cond) passed++;                                               \
        else {                                                            \
            failed++;                                                     \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                 \
    } while (0)

static void section(const char* name) { printf("- %s\n", name); }

// deterministic pseudo-random numbers so failures are reproducible
static unsigned seed = 12345;
static int rnd(int n) {
    seed = seed * 1103515245u + 12345u;
    return (int)((seed >> 8) % (unsigned)n);
}

static void shuffle(DynArray<int>& a) {
    for (int i = a.size() - 1; i > 0; i--) {
        int j = rnd(i + 1);
        int t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static void testStr() {
    section("Str");
    Str a("hello");
    Str b = a + " world";
    CHECK(b == "hello world");
    CHECK(b.size() == 11);
    CHECK(b.upper() == "HELLO WORLD");
    CHECK(b.substr(6, 5) == "world");
    CHECK(b.find('w') == 6);
    CHECK(Str("abc") < Str("abd"));
    CHECK(Str("ab") < Str("abc"));
    Str self("ab");
    self += self;
    CHECK(self == "abab");
    CHECK(Str::num(42LL) == "42");
    CHECK(Str::num(2.5) == "2.5");
    CHECK(Str::num(3.0) == "3.0");
}

static void testDynArray() {
    section("DynArray");
    DynArray<int> a;
    for (int i = 0; i < 1000; i++) a.push(i);
    CHECK(a.size() == 1000);
    CHECK(a[999] == 999);
    a.removeAt(0);
    CHECK(a[0] == 1 && a.size() == 999);
    a.insertAt(0, -5);
    CHECK(a[0] == -5 && a[1] == 1);
    a.reverse();
    CHECK(a[0] == 999 && a.back() == -5);

    DynArray<int> b(a);
    b[0] = 7;
    CHECK(a[0] == 999);   // deep copy

    DynArray<Str> s;
    s.push("x");
    for (int i = 0; i < 50; i++) s.push(s[0]);   // pushing an element of itself while growing
    CHECK(s.size() == 51 && s[50] == "x");

    // amortized growth: 1000 pushes need only ~log2(1000) reallocations
    long long before = g_stats.arrayGrows;
    DynArray<int> c;
    for (int i = 0; i < 1000; i++) c.push(i);
    CHECK(g_stats.arrayGrows - before <= 10);
}

static void testLinkedList() {
    section("LinkedList");
    LinkedList<int> l;
    auto* n1 = l.pushBack(1);
    auto* n2 = l.pushBack(2);
    auto* n3 = l.pushBack(3);
    (void)n1;
    CHECK(l.size() == 3 && l.first()->val == 1 && l.last()->val == 3);
    l.moveToFront(n3);
    CHECK(l.first()->val == 3 && l.last()->val == 2);
    l.erase(n2);
    CHECK(l.size() == 2 && l.last()->val == 1);
    int sum = 0;
    for (auto* n = l.first(); n; n = l.after(n)) sum += n->val;
    CHECK(sum == 4);
    l.clear();
    CHECK(l.empty() && !l.first() && !l.last());
}

static void testHashTable() {
    section("HashTable");
    HashTable<Str, int> h(4);
    for (int i = 0; i < 500; i++) h.put("key" + Str::num(i), i);
    CHECK(h.size() == 500);
    CHECK(h.bucketCount() >= 500 * 4 / 3);   // grew to keep load <= 0.75
    bool all = true;
    for (int i = 0; i < 500; i++) {
        int* v = h.find("key" + Str::num(i));
        all = all && v && *v == i;
    }
    CHECK(all);
    CHECK(!h.find("nope"));
    h.put("key7", 70);
    CHECK(*h.find("key7") == 70 && h.size() == 500);
    CHECK(h.remove("key7") && !h.find("key7") && h.size() == 499);
    CHECK(!h.remove("key7"));
    h.at("fresh") += 5;
    CHECK(*h.find("fresh") == 5);

    HashTable<long long, int> ints;
    for (long long i = 0; i < 2000; i++) ints.put(i * 4096, (int)i);
    CHECK(*ints.find(1999 * 4096LL) == 1999);
}

static void testStackQueue() {
    section("Stack / Queue");
    Stack<int> s;
    for (int i = 0; i < 100; i++) s.push(i);
    CHECK(s.top() == 99);
    bool lifo = true;
    for (int i = 99; i >= 0; i--) lifo = lifo && s.pop() == i;
    CHECK(lifo && s.empty());

    Queue<int> q;
    for (int i = 0; i < 5; i++) q.push(i);
    q.pop();
    q.pop();
    for (int i = 5; i < 40; i++) q.push(i);   // wraps around, then grows
    CHECK(q.peek() == 2 && q.peek(3) == 5 && q.size() == 38);
    bool fifo = true;
    for (int i = 2; i < 40; i++) fifo = fifo && q.pop() == i;
    CHECK(fifo && q.empty());
}

static void testAVL() {
    section("AVL tree");
    AVLTree<int> t;
    for (int i = 1; i <= 1000; i++) t.insert(i, i * 10);   // sorted input: worst case for a plain BST
    CHECK(t.check());
    CHECK(t.height() <= 15);   // 1.44 * log2(1000) ~ 14.4
    CHECK(t.keyCount() == 1000);

    // the four rotation cases on tiny trees
    long long ll = g_stats.avlLL, rr = g_stats.avlRR, lr = g_stats.avlLR, rl = g_stats.avlRL;
    { AVLTree<int> x; x.insert(3, 0); x.insert(2, 0); x.insert(1, 0); CHECK(x.root()->key == 2); }
    { AVLTree<int> x; x.insert(1, 0); x.insert(2, 0); x.insert(3, 0); CHECK(x.root()->key == 2); }
    { AVLTree<int> x; x.insert(3, 0); x.insert(1, 0); x.insert(2, 0); CHECK(x.root()->key == 2); }
    { AVLTree<int> x; x.insert(1, 0); x.insert(3, 0); x.insert(2, 0); CHECK(x.root()->key == 2); }
    CHECK(g_stats.avlLL > ll && g_stats.avlRR > rr && g_stats.avlLR > lr && g_stats.avlRL > rl);

    // duplicates keep a list of row ids
    t.insert(500, 1);
    const DynArray<int>* rids = t.find(500);
    CHECK(rids && rids->size() == 2);
    CHECK(t.remove(500, 1) && t.find(500)->size() == 1);

    DynArray<int> out;
    int lo = 10, hi = 20;
    t.range(&lo, true, &hi, false, out);
    CHECK(out.size() == 10 && out[0] == 100 && out[9] == 190);

    // delete half in random order, tree must stay balanced
    DynArray<int> keys;
    for (int i = 1; i <= 1000; i++) keys.push(i);
    shuffle(keys);
    for (int i = 0; i < 500; i++) t.remove(keys[i], keys[i] * 10);
    CHECK(t.check());
    CHECK(t.keyCount() == 500);
    bool gone = true;
    for (int i = 0; i < 500; i++) gone = gone && !t.find(keys[i]);
    CHECK(gone);
    out.clear();
    t.inorder(out);
    bool ordered = out.size() == 500;
    for (int i = 1; i < out.size(); i++) ordered = ordered && out[i - 1] < out[i];
    CHECK(ordered);
}

static void testBPlus(int order) {
    char name[64];
    snprintf(name, sizeof name, "B+ tree (order %d)", order);
    section(name);
    BPlusTree<int> t(order);
    DynArray<int> keys;
    for (int i = 0; i < 5000; i++) keys.push(i * 3);
    shuffle(keys);
    for (int i = 0; i < keys.size(); i++) t.insert(keys[i], keys[i] + 1);
    CHECK(t.size() == 5000);
    CHECK(t.check());
    CHECK(!t.insert(keys[0], 0));   // duplicate rejected

    bool found = true;
    for (int i = 0; i < keys.size(); i++) {
        int v = -1;
        found = found && t.find(keys[i], v) && v == keys[i] + 1;
    }
    CHECK(found);
    int dummy;
    CHECK(!t.find(1, dummy));

    DynArray<int> out;
    int lo = 30, hi = 60;
    t.range(&lo, true, &hi, true, out);
    CHECK(out.size() == 11 && out[0] == 31 && out[10] == 61);
    out.clear();
    t.range(&lo, false, &hi, false, out);
    CHECK(out.size() == 9);
    out.clear();
    t.all(out);
    CHECK(out.size() == 5000);

    // delete most keys in random order; invariants must hold throughout
    shuffle(keys);
    bool ok = true;
    for (int i = 0; i < 4900; i++) {
        ok = ok && t.remove(keys[i]);
        if (i % 700 == 0) ok = ok && t.check();
    }
    CHECK(ok);
    CHECK(t.check());
    CHECK(t.size() == 100);
    CHECK(!t.remove(keys[0]));
    bool rest = true;
    for (int i = 4900; i < 5000; i++) rest = rest && t.find(keys[i], dummy);
    CHECK(rest);
    for (int i = 4900; i < 5000; i++) t.remove(keys[i]);
    CHECK(t.size() == 0 && t.height() == 1 && t.check());
}

static void testSort() {
    section("Merge sort");
    DynArray<int> a;
    for (int i = 0; i < 1000; i++) a.push(rnd(100));
    mergeSort(a, [](int x, int y) { return x < y; });
    bool ok = true;
    for (int i = 1; i < a.size(); i++) ok = ok && a[i - 1] <= a[i];
    CHECK(ok);

    // stability: equal keys keep their original order
    struct P { int key = 0, pos = 0; };
    DynArray<P> p;
    for (int i = 0; i < 200; i++) p.push(P{rnd(5), i});
    mergeSort(p, [](const P& x, const P& y) { return x.key < y.key; });
    bool stable = true;
    for (int i = 1; i < p.size(); i++)
        if (p[i - 1].key == p[i].key) stable = stable && p[i - 1].pos < p[i].pos;
    CHECK(stable);
}

static void testBufferPool() {
    section("Buffer pool (LRU)");
    BufferPool pool(3);
    DiskFile f(1, "t", "");
    for (int i = 0; i < 5; i++) f.allocPage();

    pool.fetch(&f, 0, true)[0] = 'A';   // dirty
    pool.fetch(&f, 1, false);
    pool.fetch(&f, 2, false);
    CHECK(pool.misses == 3 && pool.used() == 3);
    pool.fetch(&f, 0, false);   // hit, page 0 becomes most recent
    CHECK(pool.hits == 1);
    pool.fetch(&f, 3, false);   // evicts page 1 (least recent)
    CHECK(pool.evictions == 1);
    CHECK(pool.pageTable().find(((long long)1 << 32) | 1) == nullptr);
    CHECK(pool.pageTable().find(((long long)1 << 32) | 0) != nullptr);
    pool.fetch(&f, 4, false);   // evicts page 2
    pool.fetch(&f, 1, false);   // evicts page 0, which is dirty -> written back
    char buf[PAGE_SIZE];
    f.read(0, buf);
    CHECK(buf[0] == 'A');
    // MRU order is now 1, 4, 3
    CHECK(pool.frame(pool.lru().first()->val).pageNo == 1);
    CHECK(pool.frame(pool.lru().last()->val).pageNo == 3);
}

// ------------------------------------------------------------------ SQL

static Executor* ex = nullptr;

static DynArray<Result> run(const char* sql) {
    DynArray<Result> out;
    ex->run(Str(sql), out);
    return out;
}

static bool ok(const char* sql) {
    DynArray<Result> r = run(sql);
    for (int i = 0; i < r.size(); i++)
        if (!r[i].ok) {
            printf("    error: %s\n", r[i].message.c_str());
            return false;
        }
    return true;
}

static Result one(const char* sql) {
    DynArray<Result> r = run(sql);
    return std::move(r.back());
}

static bool hasStep(const Result& r, const char* ds) {
    for (int i = 0; i < r.trace.steps.size(); i++)
        if (r.trace.steps[i].ds == ds) return true;
    return false;
}

static void testSql() {
    section("SQL end to end");
    Catalog cat(8, 4);
    cat.openMemory();
    Executor e(cat);
    ex = &e;

    CHECK(ok("CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR(20), age INT, gpa FLOAT, active BOOL);"));
    CHECK(ok("INSERT INTO students VALUES (1,'Asha',20,8.5,TRUE),(2,'Ravi',21,7.2,TRUE),(3,'Meera',20,9.1,FALSE),"
             "(4,'Kabir',22,6.8,TRUE),(5,'Isha',21,8.0,TRUE),(6,'Dev',23,7.7,FALSE);"));

    Result r = one("SELECT name FROM students WHERE id = 3;");
    CHECK(r.ok && r.rows.size() == 1 && r.rows[0][0].s == "Meera");
    CHECK(hasStep(r, "bptree") && hasStep(r, "hash") && hasStep(r, "queue") && hasStep(r, "stack"));
    CHECK(r.plan.startsWith("B+ Tree lookup"));

    r = one("SELECT id FROM students WHERE id BETWEEN 2 AND 4;");
    CHECK(r.rows.size() == 3 && r.rows[0][0].i == 2 && r.plan.startsWith("B+ Tree range"));

    r = one("SELECT * FROM students WHERE age = 20;");
    CHECK(r.rows.size() == 2 && r.plan.startsWith("Full scan"));

    CHECK(ok("CREATE INDEX idx_age ON students(age);"));
    r = one("SELECT name FROM students WHERE age = 20;");
    CHECK(r.rows.size() == 2 && r.plan.startsWith("AVL Tree lookup") && hasStep(r, "avl"));
    r = one("SELECT name FROM students WHERE age >= 21 AND age < 23;");
    CHECK(r.rows.size() == 3 && r.plan.startsWith("AVL Tree range"));

    r = one("SELECT name, age FROM students WHERE age > 20 AND name LIKE '%a%' ORDER BY age DESC;");
    CHECK(r.ok && r.rows.size() == 3 && r.rows[0][1].i == 22);

    r = one("SELECT id FROM students ORDER BY id DESC LIMIT 2;");
    CHECK(r.rows.size() == 2 && r.rows[0][0].i == 6 && r.plan.startsWith("B+ Tree ordered"));

    r = one("SELECT name FROM students ORDER BY gpa;");
    CHECK(r.rows[0][0].s == "Kabir" && hasStep(r, "sort"));

    r = one("SELECT age, COUNT(*) AS n, AVG(gpa) FROM students GROUP BY age ORDER BY n DESC;");
    CHECK(r.ok && r.rows.size() == 4 && r.rows[0][1].i == 2);

    r = one("SELECT COUNT(*), SUM(age), MIN(name), MAX(gpa) FROM students WHERE active = TRUE;");
    CHECK(r.ok && r.rows[0][0].i == 4 && r.rows[0][1].i == 84 && r.rows[0][2].s == "Asha");

    r = one("SELECT COUNT(*) FROM students WHERE id > 100;");
    CHECK(r.ok && r.rows.size() == 1 && r.rows[0][0].i == 0);

    // joins: hash join without an index, index join with one
    CHECK(ok("CREATE TABLE enroll (eid INT PRIMARY KEY, sid INT, course VARCHAR(10));"));
    CHECK(ok("INSERT INTO enroll VALUES (1,1,'DSA'),(2,1,'DBMS'),(3,3,'DSA'),(4,5,'OS'),(5,9,'AI');"));
    r = one("SELECT s.name, e.course FROM students s JOIN enroll e ON s.id = e.sid ORDER BY name;");
    CHECK(r.ok && r.rows.size() == 4 && r.rows[0][0].s == "Asha" && hasStep(r, "hash"));
    r = one("SELECT s.name, e.course FROM enroll e JOIN students s ON e.sid = s.id WHERE e.course = 'DSA';");
    CHECK(r.ok && r.rows.size() == 2 && r.plan.find('i') >= 0);

    // updates, including the primary key
    CHECK(ok("UPDATE students SET age = age + 1 WHERE id = 1;"));
    r = one("SELECT age FROM students WHERE id = 1;");
    CHECK(r.rows[0][0].i == 21);
    r = one("SELECT COUNT(*) FROM students WHERE age = 21;");
    CHECK(r.rows[0][0].i == 3);   // AVL index followed the change
    CHECK(ok("UPDATE students SET id = 60 WHERE id = 6;"));
    CHECK(one("SELECT * FROM students WHERE id = 60;").rows.size() == 1);
    CHECK(one("SELECT * FROM students WHERE id = 6;").rows.size() == 0);
    CHECK(!one("UPDATE students SET id = 1 WHERE id = 2;").ok);   // duplicate key

    CHECK(ok("DELETE FROM students WHERE age = 21;"));
    CHECK(one("SELECT COUNT(*) FROM students;").rows[0][0].i == 3);
    CHECK(one("SELECT * FROM students WHERE age = 21;").rows.size() == 0);

    // errors carry a useful message
    Result bad = one("SELEC * FROM students;");
    CHECK(!bad.ok && bad.message.startsWith("Syntax error"));
    CHECK(!one("SELECT nope FROM students;").ok);
    CHECK(!one("INSERT INTO students VALUES (3,'x',1,1.0,TRUE);").ok);   // duplicate pk
    CHECK(!one("INSERT INTO students VALUES (99,'this name is way too long',1,1.0,TRUE);").ok);
    CHECK(!one("SELECT * FROM missing;").ok);

    // many rows: forces page growth, B+ splits and LRU evictions with an 8-frame pool
    CHECK(ok("CREATE TABLE big (k INT PRIMARY KEY, v INT, tag VARCHAR(40));"));
    Str ins("INSERT INTO big VALUES ");
    for (int i = 0; i < 3000; i++) {
        if (i) ins += ",";
        ins += "(" + Str::num(i) + "," + Str::num(i % 50) + ",'tag" + Str::num(i) + "')";
    }
    ins += ";";
    CHECK(ok(ins.c_str()));
    r = one("SELECT COUNT(*) FROM big WHERE v = 7;");
    CHECK(r.rows[0][0].i == 60);
    CHECK(r.stats.poolEvictions > 0);
    r = one("SELECT k FROM big WHERE k >= 2990;");
    CHECK(r.rows.size() == 10 && r.stats.bptLeafHops > 0);
    CHECK(ok("DELETE FROM big WHERE k < 2500;"));
    CHECK(one("SELECT COUNT(*) FROM big;").rows[0][0].i == 500);
    CHECK(ok("INSERT INTO big VALUES (1, 1, 'reused');"));   // fills a freed slot
    CHECK(one("SELECT tag FROM big WHERE k = 1;").rows[0][0].s == "reused");

    CHECK(ok("DROP TABLE big;"));
    CHECK(!one("SELECT * FROM big;").ok);
    CHECK(ok("DROP TABLE IF EXISTS big; CREATE TABLE IF NOT EXISTS students (x INT);"));
    CHECK(one("SELECT COUNT(*) FROM students;").rows[0][0].i == 3);   // untouched
    ex = nullptr;
}

static void testPersistence() {
    section("Persistence (folder workspace)");
    Str dir = plat::join(plat::cwd(), "build/test_ws");
    plat::makeDir(dir);
    Str data = plat::join(dir, "data");
    plat::removeFile(plat::join(data, "catalog.msql"));
    plat::removeFile(plat::join(data, "items.tbl"));

    {
        Catalog cat(4, 4);
        cat.openFolder(dir);
        Executor e(cat);
        ex = &e;
        CHECK(ok("CREATE TABLE items (id INT PRIMARY KEY, label VARCHAR(30), qty INT);"));
        Str ins("INSERT INTO items VALUES ");
        for (int i = 0; i < 400; i++) {
            if (i) ins += ",";
            ins += "(" + Str::num(i) + ",'item " + Str::num(i) + "'," + Str::num(i % 7) + ")";
        }
        CHECK(ok((ins + ";").c_str()));
        CHECK(ok("CREATE INDEX idx_qty ON items(qty); DELETE FROM items WHERE id = 5;"));
        cat.checkpoint();
        ex = nullptr;
    }
    {
        Catalog cat(4, 4);
        cat.openFolder(dir);
        Executor e(cat);
        ex = &e;
        Result r = one("SELECT label FROM items WHERE id = 399;");
        CHECK(r.ok && r.rows.size() == 1 && r.rows[0][0].s == "item 399");
        CHECK(one("SELECT COUNT(*) FROM items;").rows[0][0].i == 399);
        r = one("SELECT COUNT(*) FROM items WHERE qty = 3;");
        CHECK(r.rows[0][0].i == 57 && r.plan.startsWith("AVL"));
        CHECK(one("SELECT * FROM items WHERE id = 5;").rows.size() == 0);
        ex = nullptr;
    }
}

static void testChecker() {
    section("Error checker (no execution)");
    Catalog cat(4, 4);
    cat.openMemory();
    Executor e(cat);
    DynArray<Result> r;
    e.run("CREATE TABLE people (id INT PRIMARY KEY, name VARCHAR(10));", r);

    DynArray<Problem> p;
    checkSql("SELECT * FROM people;\nSELECT name FROM people WHERE id = 1;", &cat, p);
    CHECK(p.size() == 0);

    p.clear();
    checkSql("SELECT * FROM people\nWHERE id = ;", &cat, p);
    CHECK(p.size() == 1 && p[0].line == 2 && p[0].col == 12 && p[0].length == 1);

    p.clear();   // two broken statements -> two problems, the good one in between is fine
    checkSql("SELEC 1;\nSELECT * FROM people;\nINSERT INTO people VALUES (1,;", &cat, p);
    CHECK(p.size() == 2 && p[0].line == 1 && p[1].line == 3);

    p.clear();   // unknown table, marked on the name itself
    checkSql("SELECT * FROM peple WHERE id = 1;", &cat, p);
    CHECK(p.size() == 1 && p[0].col == 15 && p[0].length == 5 && p[0].message.startsWith("no table"));

    p.clear();   // tables the script creates first are fine
    checkSql("CREATE TABLE pets (id INT PRIMARY KEY);\nINSERT INTO pets VALUES (1);\nDROP TABLE pets;\nSELECT * FROM pets;", &cat, p);
    CHECK(p.size() == 1 && p[0].line == 4);

    p.clear();
    checkSql("SELECT 'oops FROM people;", &cat, p);
    CHECK(p.size() == 1 && p[0].col == 8 && p[0].message.startsWith("unterminated"));

    p.clear();
    checkSql("CREATE TABLE people (x INT);", &cat, p);
    CHECK(p.size() == 1 && p[0].message.endsWith("already exists"));
    // the checker must not have run anything
    CHECK(cat.find("pets", false) == nullptr && cat.tableCount() == 1);
}

int main() {
    printf("MiniSQL tests\n");
    testStr();
    testDynArray();
    testLinkedList();
    testHashTable();
    testStackQueue();
    testAVL();
    testBPlus(3);
    testBPlus(4);
    testBPlus(32);
    testSort();
    testBufferPool();
    testSql();
    testPersistence();
    testChecker();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
