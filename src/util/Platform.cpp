#include "util/Platform.h"
#include "ds/Sort.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <dirent.h>
  #include <unistd.h>
  #include <climits>
#endif

namespace plat {

#ifdef _WIN32

bool isDir(const Str& path) {
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool exists(const Str& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool makeDir(const Str& path) { return CreateDirectoryA(path.c_str(), nullptr) != 0; }

bool listDir(const Str& path, DynArray<DirEntry>& out) {
    WIN32_FIND_DATAA fd;
    Str pattern = join(path, "*");
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        Str name(fd.cFileName);
        if (name == "." || name == "..") continue;
        DirEntry e;
        e.name = name;
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        out.push(e);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    mergeSort(out, [](const DirEntry& a, const DirEntry& b) {
        if (a.dir != b.dir) return a.dir;
        return a.name.lower() < b.name.lower();
    });
    return true;
}

void listRoots(DynArray<DirEntry>& out) {
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1u << i))) continue;
        DirEntry e;
        e.name += (char)('A' + i);
        e.name += ":\\";
        e.dir = true;
        out.push(e);
    }
}

Str home() {
    const char* h = getenv("USERPROFILE");
    return h ? Str(h) : Str("C:\\");
}

Str cwd() {
    char buf[MAX_PATH];
    return GetCurrentDirectoryA(MAX_PATH, buf) ? Str(buf) : Str(".");
}

Str absolute(const Str& path) {
    char buf[MAX_PATH];
    return _fullpath(buf, path.c_str(), MAX_PATH) ? Str(buf) : path;
}

char sep() { return '\\'; }

void openBrowser(const Str& url) {
    Str cmd = "start \"\" \"" + url + "\"";
    system(cmd.c_str());
}

#else

bool isDir(const Str& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool exists(const Str& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool makeDir(const Str& path) { return mkdir(path.c_str(), 0755) == 0; }

bool listDir(const Str& path, DynArray<DirEntry>& out) {
    DIR* d = opendir(path.c_str());
    if (!d) return false;
    while (dirent* ent = readdir(d)) {
        Str name(ent->d_name);
        if (name == "." || name == "..") continue;
        DirEntry e;
        e.name = name;
        Str full = join(path, name);
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            e.dir = S_ISDIR(st.st_mode);
            e.size = (long long)st.st_size;
        }
        out.push(e);
    }
    closedir(d);
    mergeSort(out, [](const DirEntry& a, const DirEntry& b) {
        if (a.dir != b.dir) return a.dir;
        return a.name.lower() < b.name.lower();
    });
    return true;
}

void listRoots(DynArray<DirEntry>&) {}

Str home() {
    const char* h = getenv("HOME");
    return h ? Str(h) : Str("/");
}

Str cwd() {
    char buf[PATH_MAX];
    return getcwd(buf, sizeof buf) ? Str(buf) : Str(".");
}

Str absolute(const Str& path) {
    char buf[PATH_MAX];
    return realpath(path.c_str(), buf) ? Str(buf) : path;
}

char sep() { return '/'; }

void openBrowser(const Str& url) {
#ifdef __APPLE__
    Str cmd = "open '" + url + "' >/dev/null 2>&1 &";
#else
    Str cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
#endif
    int rc = system(cmd.c_str());
    (void)rc;
}

#endif

bool removeFile(const Str& path) { return remove(path.c_str()) == 0; }

long long fileSize(const Str& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long long n = ftell(f);
    fclose(f);
    return n;
}

bool readFile(const Str& path, Str& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, (int)n);
    fclose(f);
    return true;
}

bool writeFile(const Str& path, const Str& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t n = data.size() ? fwrite(data.c_str(), 1, data.size(), f) : 0;
    fclose(f);
    return (int)n == data.size();
}

Str join(const Str& dir, const Str& name) {
    if (dir.empty()) return name;
    char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + name;
    Str r(dir);
    r += sep();
    r += name;
    return r;
}

static int lastSep(const Str& p) {
    int a = p.findLast('/'), b = p.findLast('\\');
    return a > b ? a : b;
}

Str parent(const Str& path) {
    Str p(path);
    while (p.size() > 1 && (p[p.size() - 1] == '/' || p[p.size() - 1] == '\\')) p = p.substr(0, p.size() - 1);
    int i = lastSep(p);
    if (i < 0) return Str();
    if (i == 0) return Str("/");
    Str r = p.substr(0, i);
    if (r.size() == 2 && r[1] == ':') r += '\\';   // "C:" -> "C:\"
    return r;
}

Str baseName(const Str& path) {
    Str p(path);
    while (p.size() > 1 && (p[p.size() - 1] == '/' || p[p.size() - 1] == '\\')) p = p.substr(0, p.size() - 1);
    int i = lastSep(p);
    return i < 0 ? p : p.substr(i + 1, -1);
}

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

}
