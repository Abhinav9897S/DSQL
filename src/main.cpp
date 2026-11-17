// minisql-cli: the engine in a terminal (the desktop app is MiniSQLWorkbench).
//
//   minisql-cli [folder] [--explain]       interactive SQL prompt
//   minisql-cli --run file.sql [--folder dir] [--explain]
//   minisql-cli --bench [rows]              indexed vs full-scan benchmark
//
// Options: --frames N (buffer pool size), --order N (B+ tree order)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Cli.h"
#include "util/Error.h"
#include "util/Platform.h"

static void usage() {
    puts("usage: minisql-cli [folder] [--explain] [--frames 64] [--order 4]\n"
         "       minisql-cli --run file.sql [--folder dir] [--explain]\n"
         "       minisql-cli --bench [rows]");
}

int main(int argc, char** argv) {
    int frames = 64, order = 4, benchRows = 10000;
    bool bench = false, explain = false, orderGiven = false;
    Str runPath, folder;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        bool hasNext = i + 1 < argc && argv[i + 1][0] != '-';
        if (!strcmp(a, "--frames") && hasNext) {
            frames = atoi(argv[++i]);
        } else if (!strcmp(a, "--order") && hasNext) {
            order = atoi(argv[++i]);
            orderGiven = true;
        } else if (!strcmp(a, "--folder") && hasNext) {
            folder = argv[++i];
        } else if (!strcmp(a, "--run") && hasNext) {
            runPath = argv[++i];
        } else if (!strcmp(a, "--explain")) {
            explain = true;
        } else if (!strcmp(a, "--cli")) {
            // the old spelling; the prompt is the default now
        } else if (!strcmp(a, "--bench")) {
            bench = true;
            if (hasNext) benchRows = atoi(argv[++i]);
        } else if (a[0] != '-' && folder.empty()) {
            folder = a;
        } else {
            usage();
            return !strcmp(a, "--help") || !strcmp(a, "-h") ? 0 : 1;
        }
    }

    // a bigger node is closer to what real databases use; the workbench
    // defaults to order 4 so the tree is small enough to look at
    if (bench) return runBenchmark(benchRows, frames, orderGiven ? order : 32);

    Catalog cat(frames, order);
    Executor ex(cat);
    try {
        if (folder.empty()) cat.openMemory();
        else cat.openFolder(plat::absolute(folder));
    } catch (DbError& e) {
        fprintf(stderr, "%s\n", e.msg.c_str());
        return 1;
    }
    int rc = runPath.empty() ? runRepl(cat, ex, explain) : runFile(cat, ex, runPath, explain);
    cat.close();
    return rc;
}
