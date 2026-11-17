#pragma once

// What each data structure is, where it lives in the code and what it costs.
// Shared by the welcome page, the Learn tab and the Explain tab. The keys
// match the `ds` tags the engine puts on its trace steps.
struct DsInfo {
    const char* key;
    const char* name;
    const char* color;
    const char* file;
    const char* role;
    const char* how;
    const char* cost[4][2];   // operation, cost; unused rows are null
    const char* tryIt;
};

extern const DsInfo DS_INFO[];
extern const int DS_INFO_COUNT;   // the last entry is the parser/planner

const DsInfo& dsInfo(const char* key);   // unknown keys get the parser entry
