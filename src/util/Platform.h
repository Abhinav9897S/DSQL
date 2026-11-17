#pragma once
#include "ds/Str.h"
#include "ds/DynArray.h"

// The few OS calls we need (folders, files, time), behind one header so the
// rest of the code doesn't care whether it's Linux, macOS or Windows.
namespace plat {

struct DirEntry {
    Str name;
    bool dir = false;
    long long size = 0;
};

bool isDir(const Str& path);
bool exists(const Str& path);
bool makeDir(const Str& path);
bool removeFile(const Str& path);
long long fileSize(const Str& path);

// directory listing, sorted with folders first
bool listDir(const Str& path, DynArray<DirEntry>& out);

bool readFile(const Str& path, Str& out);
bool writeFile(const Str& path, const Str& data);

Str home();
Str cwd();
Str absolute(const Str& path);
Str join(const Str& dir, const Str& name);
Str parent(const Str& path);
Str baseName(const Str& path);
char sep();

double nowMs();

}
