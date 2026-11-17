#pragma once
#include "ds/Str.h"
#include "ds/DynArray.h"

// One line of the "how did this query run" story in the side panel.
// `ds` is a short tag the UI turns into a coloured badge:
//   sql queue stack array list hash avl bptree pool disk sort
struct TraceStep {
    Str ds;
    Str title;
    Str detail;
    int repeat = 1;   // identical steps in a row are folded into one
};

struct Trace {
    DynArray<TraceStep> steps;
    int pageNotesLeft = 6;     // the buffer pool narrates only the first few page touches
    int pageNotesSkipped = 0;

    void add(const char* ds, const Str& title, const Str& detail) {
        if (!steps.empty()) {
            TraceStep& last = steps.back();
            if (last.ds == ds && last.title == title && last.detail == detail) {
                last.repeat++;
                return;
            }
        }
        TraceStep s;
        s.ds = ds;
        s.title = title;
        s.detail = detail;
        steps.push(std::move(s));
    }
};

// Points at the running statement's trace, null when nothing is running.
extern Trace* g_trace;

inline void note(const char* ds, const Str& title, const Str& detail) {
    if (g_trace) g_trace->add(ds, title, detail);
}
