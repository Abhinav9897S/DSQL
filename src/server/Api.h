#pragma once
#include "server/Http.h"
#include "storage/Catalog.h"
#include "sql/Executor.h"

// JSON endpoints used by the workbench, plus static files from web/.
//
//   GET  /api/state               open workspace, tables, scripts, pool stats
//   GET  /api/browse?path=        folders, for the open/create dialog
//   POST /api/open?path=          open a folder as the workspace
//   POST /api/create?parent=&name=
//   POST /api/memory              in-memory session, nothing saved
//   POST /api/close
//   POST /api/query?limit=&line=  body = SQL text; line = editor line it starts on
//   GET  /api/script?name=        read a .sql file of the workspace
//   POST /api/script?name=        body = new contents
//   POST /api/script/delete?name=
//   GET  /api/inspect?table=      tree / pool / catalog snapshots
//   GET  /api/sample              the getting-started script
void apiInit(Catalog* cat, Executor* exec, const Str& webDir, const Str& examplesDir);
void apiHandle(const HttpRequest& req, HttpResponse& res);
