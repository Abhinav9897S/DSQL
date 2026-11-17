#include "server/Http.h"
#include "ds/DynArray.h"
#include "util/Platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define BAD_SOCK INVALID_SOCKET
  static void closeSock(sock_t s) { closesocket(s); }
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <sys/time.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <signal.h>
  typedef int sock_t;
  #define BAD_SOCK (-1)
  static void closeSock(sock_t s) { close(s); }
#endif

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Str urlDecode(const Str& s) {
    Str out;
    for (int i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size() && hexVal(s[i + 1]) >= 0 && hexVal(s[i + 2]) >= 0) {
            out += (char)(hexVal(s[i + 1]) * 16 + hexVal(s[i + 2]));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

Str HttpRequest::param(const char* name) const {
    Str want(name);
    int i = 0;
    while (i < query.size()) {
        int amp = query.find('&', i);
        if (amp < 0) amp = query.size();
        Str pair = query.substr(i, amp - i);
        int eq = pair.find('=');
        Str key = eq < 0 ? pair : pair.substr(0, eq);
        if (urlDecode(key) == want) return eq < 0 ? Str() : urlDecode(pair.substr(eq + 1, -1));
        i = amp + 1;
    }
    return Str();
}

static const char* statusText(int code) {
    switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 413: return "Payload Too Large";
    default: return "Internal Server Error";
    }
}

static bool sendAll(sock_t s, const char* data, int n) {
    while (n > 0) {
        int sent = (int)send(s, data, n, 0);
        if (sent <= 0) return false;
        data += sent;
        n -= sent;
    }
    return true;
}

static int findHeaderEnd(const Str& buf) {
    for (int i = 0; i + 3 < buf.size(); i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') return i;
    return -1;
}

static long headerNumber(const Str& head, const char* name) {
    Str lower = head.lower();
    Str want = Str("\n") + name + ":";
    for (int i = 0; i + want.size() <= lower.size(); i++) {
        if (memcmp(lower.c_str() + i, want.c_str(), want.size()) == 0)
            return strtol(lower.c_str() + i + want.size(), nullptr, 10);
    }
    return 0;
}

static void handleClient(sock_t client, HttpHandler handler) {
    const long maxBody = 32L * 1024 * 1024;
    Str buf;
    char chunk[16384];
    int headerEnd = -1;
    while (headerEnd < 0) {
        int n = (int)recv(client, chunk, sizeof chunk, 0);
        if (n <= 0) return;   // browser pre-connect that never sent anything, or timeout
        buf.append(chunk, n);
        headerEnd = findHeaderEnd(buf);
        if (headerEnd < 0 && buf.size() > 64 * 1024) return;
    }

    Str head = buf.substr(0, headerEnd);
    long length = headerNumber(head, "content-length");
    HttpResponse res;
    HttpRequest req;
    if (length > maxBody) {
        res.status = 413;
        res.body = "{\"error\":\"request too large\"}";
    } else {
        int bodyStart = headerEnd + 4;
        while (buf.size() - bodyStart < length) {
            int n = (int)recv(client, chunk, sizeof chunk, 0);
            if (n <= 0) return;
            buf.append(chunk, n);
        }

        // request line: METHOD /path?query HTTP/1.1
        int sp1 = head.find(' ');
        int sp2 = sp1 < 0 ? -1 : head.find(' ', sp1 + 1);
        if (sp1 < 0 || sp2 < 0) return;
        req.method = head.substr(0, sp1);
        Str target = head.substr(sp1 + 1, sp2 - sp1 - 1);
        int q = target.find('?');
        req.path = urlDecode(q < 0 ? target : target.substr(0, q));
        req.query = q < 0 ? Str() : target.substr(q + 1, -1);
        req.body = buf.substr(bodyStart, (int)length);
        handler(req, res);
    }

    Str out = "HTTP/1.1 " + Str::num(res.status) + " " + statusText(res.status) + "\r\n";
    out += "Content-Type: " + res.type + "\r\n";
    out += "Content-Length: " + Str::num(res.body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    if (sendAll(client, out.c_str(), out.size())) sendAll(client, res.body.c_str(), res.body.size());
}

bool serveHttp(int port, HttpHandler handler, void (*onReady)(int)) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#else
    signal(SIGPIPE, SIG_IGN);   // a closed browser tab shouldn't kill the server
#endif

    sock_t server = BAD_SOCK;
    int bound = -1;
    for (int p = port; p < port + 20 && bound < 0; p++) {
        server = socket(AF_INET, SOCK_STREAM, 0);
        if (server == BAD_SOCK) return false;
        int yes = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
        sockaddr_in addr;
        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)p);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // local only
        if (bind(server, (sockaddr*)&addr, sizeof addr) == 0 && listen(server, 16) == 0) {
            bound = p;
        } else {
            closeSock(server);
            server = BAD_SOCK;
        }
    }
    if (bound < 0) return false;
    if (onReady) onReady(bound);

    // Browsers open spare connections and may leave them idle, so we can't
    // just accept() and wait on one. select() tells us which socket actually
    // has a request; idle ones get closed after a while.
    struct Conn {
        sock_t s;
        double since;
    };
    DynArray<Conn> conns;
    for (;;) {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(server, &ready);
        sock_t top = server;
        for (int i = 0; i < conns.size(); i++) {
            FD_SET(conns[i].s, &ready);
            if (conns[i].s > top) top = conns[i].s;
        }
        timeval wait;
        wait.tv_sec = 1;
        wait.tv_usec = 0;
        if (select((int)top + 1, &ready, nullptr, nullptr, &wait) < 0) continue;

        if (FD_ISSET(server, &ready)) {
            sock_t client = accept(server, nullptr, nullptr);
            if (client != BAD_SOCK && conns.size() < 60) {
#ifdef _WIN32
                DWORD timeout = 5000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
#else
                timeval tv;
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
                conns.push(Conn{client, plat::nowMs()});
            } else if (client != BAD_SOCK) {
                closeSock(client);
            }
        }

        double now = plat::nowMs();
        for (int i = conns.size() - 1; i >= 0; i--) {
            bool hasData = FD_ISSET(conns[i].s, &ready);
            if (!hasData && now - conns[i].since < 30000) continue;
            if (hasData) handleClient(conns[i].s, handler);
            closeSock(conns[i].s);
            conns.removeAt(i);
        }
    }
}
