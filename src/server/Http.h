#pragma once
#include "ds/Str.h"

// Bare-bones HTTP/1.1 server: one connection at a time, Connection: close.
// That's plenty for a workbench used by one person on localhost.

struct HttpRequest {
    Str method;
    Str path;    // without the query string
    Str query;
    Str body;

    Str param(const char* name) const;   // URL-decoded query parameter, empty if absent
};

struct HttpResponse {
    int status = 200;
    Str type = "application/json; charset=utf-8";
    Str body;
};

typedef void (*HttpHandler)(const HttpRequest& req, HttpResponse& res);

// Binds 127.0.0.1 on `port` (or the next free one) and serves forever.
// Returns false if no port could be bound. `onReady` gets the real port.
bool serveHttp(int port, HttpHandler handler, void (*onReady)(int port));

Str urlDecode(const Str& s);
