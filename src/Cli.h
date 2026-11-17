#pragma once
#include "storage/Catalog.h"
#include "sql/Executor.h"

// Terminal front ends: the interactive REPL, running a .sql file, and the
// indexed-vs-scan benchmark.
void printResult(const Result& r, bool explain);
int runRepl(Catalog& cat, Executor& ex, bool explain);
int runFile(Catalog& cat, Executor& ex, const Str& path, bool explain);
int runBenchmark(int rows, int frames, int order);
