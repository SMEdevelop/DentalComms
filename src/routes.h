#pragma once
#include "http.h"  // must precede <windows.h> users

#include <string>
#include <vector>

#include "state.h"

namespace on {

// Serves one accepted connection to completion and closes it. Runs on its own
// thread; an /api/events request keeps that thread for as long as the browser
// stays on the page.
void serveConnection(SOCKET raw, Hub& hub);

// What /api/server answers with: { "staffAddress", "addresses": [...],
// "port", "computer" }. Set by main.cpp whenever the picture changes, because
// only it knows which port and network it ended up on.
void publishServerInfo(const js::Value& info);

// Exposed for the self-test: maps a request path to a file inside public/,
// returning false for anything trying to escape that folder.
bool resolveStaticPath(const std::string& urlPath, const std::string& publicDir,
                       std::string& fullPath);
std::string mimeFor(const std::string& path);

// Who Dental Comms is willing to be spoken to by. Exposed for the self-test.
std::string hostOnly(const std::string& hostHeader);
bool hostIsOurs(const std::string& hostHeader, const std::string& arrivedOn,
                const std::string& thisComputer,
                const std::vector<std::string>& extra);
bool originIsOurs(const std::string& origin, const std::string& hostHeader);
bool bodyIsJson(const std::string& contentType);

}  // namespace on
