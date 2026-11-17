// MiniSQL entry point.
//
//   minisql                       start the workbench (opens your browser)
//   minisql --cli [folder]        interactive SQL prompt
//   minisql --run file.sql [--folder dir] [--explain]
//   minisql --bench [rows]        indexed vs full-scan benchmark
//
// Options: --port N, --frames N (buffer pool size), --order N (B+ tree order),
//          --web DIR, --no-browser

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Cli.h"
#include "server/Api.h"
#include "util/Error.h"
#include "util/Platform.h"

static bool openBrowser = true;

static void usage() {
    puts("usage: minisql [--port 8080] [--frames 64] [--order 4] [--no-browser]\n"
         "       minisql --cli [folder] [--explain]\n"
         "       minisql --run file.sql [--folder dir] [--explain]\n"
         "       minisql --bench [rows]");
}

static Str findWebDir(const char* argv0, const Str& given) {
    DynArray<Str> tries;
    if (!given.empty()) tries.push(given);
    tries.push("web");
    Str exeDir = plat::parent(Str(argv0));
    if (!exeDir.empty()) {
        tries.push(plat::join(exeDir, "web"));
        tries.push(plat::join(plat::parent(exeDir), "web"));
    }
    for (int i = 0; i < tries.size(); i++)
        if (plat::exists(plat::join(tries[i], "index.html"))) return plat::absolute(tries[i]);
    return Str();
}

static void onReady(int port) {
    Str url = "http://localhost:" + Str::num(port);
    printf("\n  MiniSQL Workbench is running at %s\n", url.c_str());
    printf("  (Ctrl+C to stop; your workspace is saved after every run)\n\n");
    fflush(stdout);
    if (openBrowser) plat::openBrowser(url);
}

int main(int argc, char** argv) {
    int port = 8080, frames = 64, order = 4, benchRows = 10000;
    bool cli = false, bench = false, explain = false, orderGiven = false;
    Str runPath, folder, web;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        bool hasNext = i + 1 < argc && argv[i + 1][0] != '-';
        if (!strcmp(a, "--port") && hasNext) port = atoi(argv[++i]);
        else if (!strcmp(a, "--frames") && hasNext) frames = atoi(argv[++i]);
        else if (!strcmp(a, "--order") && hasNext) {
            order = atoi(argv[++i]);
            orderGiven = true;
        } else if (!strcmp(a, "--web") && hasNext) web = argv[++i];
        else if (!strcmp(a, "--folder") && hasNext) folder = argv[++i];
        else if (!strcmp(a, "--run") && hasNext) runPath = argv[++i];
        else if (!strcmp(a, "--no-browser")) openBrowser = false;
        else if (!strcmp(a, "--explain")) explain = true;
        else if (!strcmp(a, "--cli")) {
            cli = true;
            if (hasNext) folder = argv[++i];
        } else if (!strcmp(a, "--bench")) {
            bench = true;
            if (hasNext) benchRows = atoi(argv[++i]);
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

    if (cli || !runPath.empty()) {
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

    Str webDir = findWebDir(argv[0], web);
    if (webDir.empty()) {
        fprintf(stderr, "can't find the web/ folder; run from the project folder or pass --web <dir>\n");
        return 1;
    }
    apiInit(&cat, &ex, webDir, plat::join(plat::parent(webDir), "examples"));
    if (!serveHttp(port, apiHandle, onReady)) {
        fprintf(stderr, "couldn't listen on ports %d-%d\n", port, port + 19);
        return 1;
    }
    return 0;
}
