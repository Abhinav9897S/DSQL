#include "gui/DsInfo.h"
#include <cstring>

const DsInfo DS_INFO[] = {
    {"bptree", "B+ Tree", "#2dd4bf", "src/ds/BPlusTree.h",
     "Primary key index: point lookups, ranges and ORDER BY on the key.",
     "A wide, shallow tree. Internal nodes only hold separator keys that route the search; every key and its "
     "row id live in the leaves, and the leaves are linked left to right. A full node splits and pushes a "
     "separator up; a node that gets too empty after a delete borrows a key from a sibling or merges with it.",
     {{"search", "O(log n)"}, {"insert / delete", "O(log n)"}, {"range of k rows", "O(log n + k)"}, {"ordered scan", "O(n), no sort"}},
     "SELECT * FROM students WHERE id BETWEEN 4 AND 9;"},

    {"avl", "AVL Tree", "#34d399", "src/ds/AVLTree.h",
     "Secondary indexes made with CREATE INDEX.",
     "A binary search tree that checks the heights of both subtrees after every insert or delete. If they "
     "differ by more than one it rotates (LL, RR, LR or RL), which keeps the height under about 1.44 log2 n. "
     "The column may repeat, so each node keeps a list of the row ids that share its key.",
     {{"search", "O(log n)"}, {"insert / delete", "O(log n)"}, {"range of k keys", "O(log n + k)"}, {nullptr, nullptr}},
     "CREATE INDEX idx_city ON students(city);"},

    {"hash", "Hash Table", "#fbbf24", "src/ds/HashTable.h",
     "Catalog (table name to table), buffer pool page table, hash joins, GROUP BY and SQL keywords.",
     "An array of buckets. A key is hashed to a bucket and colliding keys are chained in a linked list there. "
     "Once the table is more than 3/4 full the bucket array doubles and every entry is re-linked, so chains "
     "stay short.",
     {{"find", "O(1) average"}, {"insert / delete", "O(1) average"}, {"resize", "O(n), rare"}, {nullptr, nullptr}},
     "SELECT city, COUNT(*) FROM students GROUP BY city;"},

    {"pool", "Buffer Pool (LRU)", "#fb923c", "src/storage/BufferPool.cpp",
     "Keeps recently used 4 KB pages in memory so repeated reads skip the disk.",
     "A hash table maps (table, page number) to a frame, so \"is it cached?\" is O(1). A doubly linked list "
     "keeps the frames in recency order. A hit moves the frame to the front; a miss takes the frame at the "
     "back, writing it out first if it was modified (dirty).",
     {{"hit", "O(1)"}, {"miss", "O(1) + disk read"}, {"eviction", "O(1)"}, {nullptr, nullptr}},
     "SELECT * FROM students;"},

    {"list", "Doubly Linked List", "#f472b6", "src/ds/LinkedList.h",
     "The LRU order inside the buffer pool.",
     "Head and tail sentinel nodes mean there are no special cases for an empty list or the ends. The pool "
     "keeps a pointer to every frame's node, so it can unlink one and relink it at the front without searching.",
     {{"push front / back", "O(1)"}, {"move to front (with node)", "O(1)"}, {"remove (with node)", "O(1)"}, {nullptr, nullptr}},
     "SELECT * FROM enrollments;"},

    {"array", "Dynamic Array", "#60a5fa", "src/ds/DynArray.h",
     "Rows, result sets, column lists and the page directory. Our own take on the STL vector.",
     "When it fills up, it allocates twice the capacity and moves the elements over. n pushes cause only "
     "about log2 n reallocations, so a push is O(1) amortized.",
     {{"index", "O(1)"}, {"push", "O(1) amortized"}, {"insert / remove in the middle", "O(n)"}, {nullptr, nullptr}},
     "SELECT name, gpa FROM students;"},

    {"stack", "Stack", "#a78bfa", "src/ds/Stack.h",
     "Evaluates WHERE conditions and SET expressions.",
     "The parser writes a condition in postfix order, e.g. age 20 > city 'Delhi' = AND. For each row the "
     "executor walks that list: values are pushed, and each operator pops its inputs and pushes the result. "
     "Whatever is left on top decides if the row matches.",
     {{"push / pop", "O(1)"}, {"evaluate a condition", "O(length) per row"}, {nullptr, nullptr}, {nullptr, nullptr}},
     "SELECT name FROM students WHERE age > 20 AND city = 'Delhi';"},

    {"queue", "Queue", "#38bdf8", "src/ds/Queue.h",
     "Carries tokens from the tokenizer to the parser.",
     "A circular array: push writes at head + count and pop reads at head, then moves head forward and wraps "
     "around. Nothing ever shifts. peek(k) lets the parser look a token or two ahead.",
     {{"push", "O(1) amortized"}, {"pop", "O(1)"}, {"peek", "O(1)"}, {nullptr, nullptr}},
     "SHOW TABLES;"},

    {"sort", "Merge Sort", "#818cf8", "src/ds/Sort.h",
     "ORDER BY when no index already supplies the order.",
     "Bottom-up merge sort: merge runs of length 1, then 2, 4, 8 and so on, between two buffers. Always "
     "O(n log n), and stable, so equal keys keep their order.",
     {{"sort n rows", "O(n log n)"}, {"extra memory", "O(n)"}, {nullptr, nullptr}, {nullptr, nullptr}},
     "SELECT name, gpa FROM students ORDER BY gpa DESC;"},

    {"disk", "Paged Storage", "#94a3b8", "src/storage/Table.cpp",
     "Rows live in fixed-size slots on 4 KB pages in data/<table>.tbl.",
     "Every row of a table takes the same number of bytes, so a row id is just page x slotsPerPage + slot "
     "and an index can point straight at it. A DynArray \"page directory\" counts used slots per page so "
     "INSERT can skip full pages without reading them.",
     {{"read row by id", "O(1) + page fetch"}, {"full scan", "O(pages)"}, {nullptr, nullptr}, {nullptr, nullptr}},
     "DESCRIBE students;"},

    {"sql", "Parser / Planner", "#9ca3af", "src/sql/Parser.cpp",
     "Turns tokens into a command and picks how to read each table.",
     "Recursive descent: one function per grammar rule, each calling the rules below it. The planner then "
     "scores every indexed column against the WHERE conditions: primary-key equality, then secondary "
     "equality, then ranges, then a full scan.",
     {{"parse", "O(tokens)"}, {"plan", "O(columns x conditions)"}, {nullptr, nullptr}, {nullptr, nullptr}},
     "SELECT * FROM students WHERE id = 3;"},
};

const int DS_INFO_COUNT = sizeof(DS_INFO) / sizeof(DS_INFO[0]);

const DsInfo& dsInfo(const char* key) {
    for (int i = 0; i < DS_INFO_COUNT; i++)
        if (strcmp(DS_INFO[i].key, key) == 0) return DS_INFO[i];
    return DS_INFO[DS_INFO_COUNT - 1];
}
