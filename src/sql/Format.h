#pragma once
#include "sql/Executor.h"

// Text output for a result, shared by the terminal tool and the app's terminal.

// ASCII table like the mysql client prints; empty if the statement has no
// columns. At most maxRows rows, with a note when some are left out.
Str formatTable(const Result& r, int maxRows);

// "3 rows  (0.120 ms)  [B+ Tree lookup id = 3]"
Str formatSummary(const Result& r);
