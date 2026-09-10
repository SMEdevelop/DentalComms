#include "routes.h"

#include <memory>

#include "shortcut.h"
#include "util.h"

namespace on {
namespace {

// Browsers are told to reconnect on their own, so a dropped stream heals
// without anyone noticing. The ping exists to notice a browser that vanished
// without closing cleanly - a sleeping laptop, a yanked network cable.
constexpr int kPingSeconds = 25;

// How long a connection may say nothing at all before we give up on it. One
// thread is held per connection, so a browser's speculative pre-connect, a
// port scan, or a machine that slept mid-request would otherwise each cost a
// thread for as long as Dental Flow stays up.
constexpr int kIdleSeconds = 20;

std::string lowerExt(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    return lower(path.substr(dot));
}

bool isPathSafe(const std::string& rel) {
    if (rel.empty()) return false;
    if (rel.find(':') != std::string::npos) return false;  // C:\ or a stream
    // Reject a ".." anywhere it forms its own segment, before the path is ever
    // joined to the public folder.
    size_t start = 0;
    while (start <= rel.size()) {
        size_t sep = rel.find_first_of("/\\", start);
        const std::string seg =
            rel.substr(start, sep == std::string::npos ? std::string::npos
                                                       : sep - start);
        if (seg == "..") return false;
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return true;
}

void handleSse(SOCKET raw, Hub& hub, const std::string& station) {
    const std::string head =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n"
        "\r\n";

    auto client = std::make_shared<SseClient>();
    client->sock = raw;
    client->station = station;
    if (!client->sendFrame(head)) {
        client->shutdownNow();
        return;
    }
    // Registered before "hello" goes out, because "hello" is what sends the
    // browser back for a fresh copy of the state - and that copy has to
    // already include this station as one of the ones listening.
    hub.addClient(client);
    if (!client->sendFrame(sseFrame("hello", "{}"))) {
        hub.removeClient(client);
        client->shutdownNow();
        return;
    }

    // A read with a timeout does double duty: it wakes up in time to send the
    // keepalive, and it notices immediately when the browser closes the tab.
    DWORD timeout = kPingSeconds * 1000;
    setsockopt(raw, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout,
               sizeof(timeout));

    char scratch[512];
    while (!client->dead.load()) {
        const int n = ::recv(raw, scratch, sizeof(scratch), 0);
        if (n == 0) break;  // browser closed the stream
        if (n < 0) {
            const int err = WSAGetLastError();
            if (err != WSAETIMEDOUT) break;
            if (!client->sendFrame(": ping\n\n")) break;
        }
        // n > 0 means the browser sent something on an event stream; ignore it.
    }

    hub.removeClient(client);
    client->shutdownNow();
}

}  // namespace

std::string hostOnly(const std::string& hostHeader) {
    const std::string h = trim(hostHeader);
    if (!h.empty() && h[0] == '[') {  // an IPv6 literal: [::1]:3272
        const size_t close = h.find(']');
        return close == std::string::npos ? h : h.substr(1, close - 1);
    }
    const size_t colon = h.find(':');
    return colon == std::string::npos ? h : h.substr(0, colon);
}

bool hostIsOurs(const std::string& hostHeader, const std::string& arrivedOn,
                const std::string& thisComputer,
                const std::vector<std::string>& extra) {
    const std::string h = lower(hostOnly(hostHeader));
    if (h.empty()) return false;
    if (h == "localhost" || h == "127.0.0.1" || h == "::1") return true;
    // The address the connection genuinely came in on. A name in someone
    // else's DNS pointed at this office cannot match it, which is what stops a
    // web page from reading the message board through a member of staff.
    if (!arrivedOn.empty() && h == lower(arrivedOn)) return true;
    const std::string me = lower(thisComputer);
    if (!me.empty() && (h == me || h.rfind(me + ".", 0) == 0)) return true;
    for (const auto& e : extra)
        if (h == lower(e)) return true;
    return false;
}

bool originIsOurs(const std::string& origin, const std::string& hostHeader) {
    // Absent means the browser considered this same-origin, which is the
    // normal case for the office's own page.
    if (origin.empty()) return true;
    if (iequals(trim(origin), "null")) return false;
    const size_t scheme = origin.find("://");
    if (scheme == std::string::npos) return false;
    return iequals(trim(origin.substr(scheme + 3)), trim(hostHeader));
}

bool bodyIsJson(const std::string& contentType) {
    // A plain HTML form can only ever send urlencoded, multipart or text/plain
    // bodies, and anything else makes the browser ask permission first - which
    // Dental Flow never grants. Insisting on JSON is therefore what keeps a
    // page on the internet from posting to the office through a staff browser.
    return lower(trim(contentType)).rfind("application/json", 0) == 0;
}

std::string mimeFor(const std::string& path) {
    const std::string ext = lowerExt(path);
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".json") return "application/json";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

bool resolveStaticPath(const std::string& urlPath, const std::string& publicDir,
                       std::string& fullPath) {
    std::string rel = urlPath;
    if (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
    if (rel.empty()) rel = "index.html";
    if (!isPathSafe(rel)) return false;

    fullPath = joinPath(publicDir, rel);
    // Belt and braces: whatever the joining did, the result must still sit
    // inside public/.
    const std::string base = lower(publicDir);
    if (lower(fullPath).compare(0, base.size(), base) != 0) return false;
    return true;
}

void serveConnection(SOCKET raw, Hub& hub) {
    const std::string publicDir = joinPath(resourceRoot(), "public");

    // Nothing here waits on a client forever. The event stream replaces these
    // with its own, longer, read timeout once it takes the socket over.
    DWORD idle = kIdleSeconds * 1000;
    setsockopt(raw, SOL_SOCKET, SO_RCVTIMEO, (const char*)&idle, sizeof(idle));
    setsockopt(raw, SOL_SOCKET, SO_SNDTIMEO, (const char*)&idle, sizeof(idle));

    // Peek at the request before deciding whether this socket becomes a
    // long-lived event stream or an ordinary request/response.
    Conn conn(raw);
    HttpRequest req;
    if (!conn.readRequest(req)) return;

    const std::string& p = req.path;
    const bool isPost = iequals(req.method, "POST");

    // Dental Flow has no passwords, which is a fair trade on the office's own
    // network. It is not a reason to also answer a web page that a member of
    // staff happens to have open, so requests are held to arriving under a
    // name that means this computer, and to not announcing themselves as
    // coming from somewhere else.
    const std::string host = req.header("Host");
    if (!hostIsOurs(host, localAddressOf(raw), computerName(),
                    hub.allowedHosts())) {
        conn.sendText(403,
                      "Dental Flow does not answer to that address.\r\n\r\n"
                      "Use the address shown in the Dental Flow window. If your "
                      "office reaches it by\r\nsome other name, add that name "
                      "to \"allowedHosts\" in data\\config.json.\r\n");
        return;
    }
    if (!originIsOurs(req.header("Origin"), host)) {
        conn.sendText(403, "Dental Flow does not take requests from other web "
                           "pages.\r\n");
        return;
    }
    if (isPost && !bodyIsJson(req.header("Content-Type"))) {
        conn.sendText(403, "Dental Flow only accepts application/json here.\r\n");
        return;
    }

    if (p == "/api/events") {
        handleSse(conn.release(), hub, queryParam(req.query, "station"));
        return;
    }

    if (p == "/api/state" && iequals(req.method, "GET")) {
        conn.sendJson(200, hub.stateJson());
        return;
    }

    if (p == "/api/signals" && isPost) {
        std::string err;
        js::Value body = js::Value::parse(req.body.empty() ? "{}" : req.body, &err);
        if (!err.empty()) {
            js::Value e = js::Value::object();
            e["error"] = "bad json";
            conn.sendJson(400, e);
            return;
        }
        std::vector<Signal> created;
        std::string why;
        if (!hub.createSignals(body, created, why)) {
            js::Value e = js::Value::object();
            e["error"] = why;
            conn.sendJson(400, e);
            return;
        }
        js::Value out = js::Value::object();
        js::Value arr = js::Value::array();
        for (const auto& s : created) arr.push(s.toJson());
        out["signals"] = arr;
        conn.sendJson(200, out);
        return;
    }

    // /api/signals/<id>/ack  or  /api/signals/<id>/cancel
    if (isPost && p.rfind("/api/signals/", 0) == 0) {
        const std::string rest = p.substr(strlen("/api/signals/"));
        const size_t slash = rest.find('/');
        if (slash != std::string::npos) {
            const std::string id = rest.substr(0, slash);
            const std::string action = rest.substr(slash + 1);
            if (!id.empty() && (action == "ack" || action == "cancel")) {
                if (action == "ack") {
                    std::string by;
                    if (!req.body.empty()) {
                        js::Value b = js::Value::parse(req.body, nullptr);
                        by = b.str("by");
                    }
                    hub.ackSignal(id, by);
                } else {
                    hub.cancelSignal(id);
                }
                // An alert someone else already cleared is not an error - two
                // people tapping "Got it" together is normal.
                js::Value ok = js::Value::object();
                ok["ok"] = true;
                conn.sendJson(200, ok);
                return;
            }
        }
    }

    if (p == "/api/config" && isPost) {
        std::string err;
        js::Value body = js::Value::parse(req.body.empty() ? "{}" : req.body, &err);
        if (!err.empty()) {
            js::Value e = js::Value::object();
            e["error"] = "bad json";
            conn.sendJson(400, e);
            return;
        }
        std::string why;
        if (!hub.updateConfig(body, why)) {
            js::Value e = js::Value::object();
            e["error"] = why;
            conn.sendJson(400, e);
            return;
        }
        js::Value ok = js::Value::object();
        ok["ok"] = true;
        conn.sendJson(200, ok);
        return;
    }

    // --- the startup shortcut ---
    // Built here rather than kept as a file, because it has to carry the
    // address this particular office reaches the server on - which is
    // whatever the browser asked for, so a computer that bookmarked the name
    // gets a shortcut with the name in it and one that used the number gets
    // the number.
    //
    // What is served is the script that makes the shortcut, not the shortcut
    // itself: Chromium refuses to save a .lnk and renames it to .download.
    // See shortcut.cpp.
    if (p == "/setup/shortcut") {
        const bool chrome = icontains(req.query, "chrome");
        // The Host header is the address that actually worked from where the
        // request came from, which is a better answer than anything this
        // process could work out about itself.
        std::string host = req.header("Host");
        if (host.empty()) host = "localhost";
        std::string script, err;
        if (!buildStartupScript("http://" + host, chrome, script, err)) {
            // The address came from the request, so a refusal is about what
            // was asked for rather than anything wrong on this end.
            conn.sendText(400, err);
            return;
        }
        std::string extra =
            "Content-Disposition: attachment; "
            "filename=\"Set up Dental Flow.cmd\"\r\n";
        conn.send(200, "application/octet-stream", script, extra);
        return;
    }

    // --- pages and assets ---
    std::string wanted = p;
    if (p == "/") wanted = "/index.html";
    else if (p == "/admin") wanted = "/admin.html";
    else if (p == "/setup") wanted = "/setup.html";

    std::string full;
    if (!resolveStaticPath(wanted, publicDir, full)) {
        conn.sendText(403, "Forbidden");
        return;
    }
    std::string bytes;
    if (!readFile(full, bytes)) {
        conn.sendText(404, "Not found");
        return;
    }
    conn.send(200, mimeFor(full), bytes);
}

}  // namespace on
