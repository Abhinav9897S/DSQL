#include "sql/Format.h"
#include <cstdio>

// UTF-8 characters, not bytes, so names like "Zoë" line up
static int textWidth(const Str& s) {
    int w = 0;
    for (int i = 0; i < s.size(); i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) w++;
    return w;
}

static void pad(Str& out, const Str& s, int width, bool right) {
    int gap = width - textWidth(s);
    if (right)
        for (int i = 0; i < gap; i++) out += ' ';
    out += s;
    if (!right)
        for (int i = 0; i < gap; i++) out += ' ';
}

static void border(Str& out, const DynArray<int>& w) {
    out += '+';
    for (int i = 0; i < w.size(); i++) {
        for (int k = 0; k < w[i] + 2; k++) out += '-';
        out += '+';
    }
    out += '\n';
}

Str formatTable(const Result& r, int maxRows) {
    Str out;
    if (r.columns.empty()) return out;
    int shown = r.rows.size() < maxRows ? r.rows.size() : maxRows;

    DynArray<int> w;
    for (int c = 0; c < r.columns.size(); c++) {
        int width = textWidth(r.columns[c]);
        for (int i = 0; i < shown; i++) {
            int x = textWidth(r.rows[i][c].toString());
            if (x > width) width = x;
        }
        w.push(width > 40 ? 40 : width);
    }

    border(out, w);
    for (int c = 0; c < r.columns.size(); c++) {
        out += "| ";
        pad(out, r.columns[c], w[c], false);
        out += ' ';
    }
    out += "|\n";
    border(out, w);
    for (int i = 0; i < shown; i++) {
        for (int c = 0; c < r.columns.size(); c++) {
            Str cell = r.rows[i][c].toString();
            if (textWidth(cell) > 40) cell = cell.substr(0, 37) + "...";
            out += "| ";
            pad(out, cell, w[c], r.types[c] == Type::Int || r.types[c] == Type::Float);
            out += ' ';
        }
        out += "|\n";
    }
    border(out, w);
    if (shown < r.rows.size()) out += "(showing " + Str::num(shown) + " of " + Str::num(r.rows.size()) + " rows)\n";
    return out;
}

Str formatSummary(const Result& r) {
    char ms[32];
    snprintf(ms, sizeof ms, "%.3f", r.ms);
    Str s = r.message + "  (" + ms + " ms)";
    if (!r.plan.empty()) s += "  [" + r.plan + "]";
    return s;
}
