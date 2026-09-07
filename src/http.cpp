#include "http.h"

#include <iphlpapi.h>

#include <cstdio>

#include "util.h"

namespace on {
namespace {

// Node destroys the connection past a megabyte of body; the same ceiling keeps
// a stuck or hostile client from growing this process without bound.
constexpr size_t kMaxBody = 1000000;
constexpr size_t kMaxHeaders = 64 * 1024;

bool sendAll(SOCKET s, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(s, data + sent, (int)(len - sent), 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

const char* reasonFor(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        default:  return "OK";
    }
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hexVal(s[i + 1]), lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

std::string queryParam(const std::string& query, const std::string& name) {
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t amp = query.find('&', pos);
        const std::string pair =
            query.substr(pos, amp == std::string::npos ? std::string::npos
                                                       : amp - pos);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.compare(0, eq, name) == 0) {
            // '+' means a space in a query string, but not in a path, which is
            // why this does not just hand the whole thing to urlDecode.
            std::string value = pair.substr(eq + 1);
            for (char& c : value)
                if (c == '+') c = ' ';
            return urlDecode(value);
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

std::string HttpRequest::header(const std::string& name) const {
    for (const auto& h : headers)
        if (iequals(h.first, name)) return h.second;
    return "";
}

Conn::~Conn() {
    if (sock_ != INVALID_SOCKET) {
        ::shutdown(sock_, SD_BOTH);
        ::closesocket(sock_);
    }
}

SOCKET Conn::release() {
    SOCKET s = sock_;
    sock_ = INVALID_SOCKET;
    return s;
}

bool Conn::readRequest(HttpRequest& req) {
    std::string buf;
    char chunk[4096];
    size_t headerEnd = std::string::npos;

    while (true) {
        headerEnd = buf.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
        if (buf.size() > kMaxHeaders) return false;
        int n = ::recv(sock_, chunk, sizeof(chunk), 0);
        if (n <= 0) return false;
        buf.append(chunk, (size_t)n);
    }

    // Request line
    const size_t lineEnd = buf.find("\r\n");
    const std::string line = buf.substr(0, lineEnd);
    const size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) return false;
    const size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;
    req.method = line.substr(0, sp1);
    std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);

    const size_t q = target.find('?');
    if (q != std::string::npos) {
        req.query = target.substr(q + 1);
        target = target.substr(0, q);
    }
    req.path = urlDecode(target);

    // Headers
    size_t pos = lineEnd + 2;
    while (pos < headerEnd) {
        const size_t eol = buf.find("\r\n", pos);
        if (eol == std::string::npos || eol > headerEnd) break;
        const std::string h = buf.substr(pos, eol - pos);
        const size_t colon = h.find(':');
        if (colon != std::string::npos)
            req.headers.emplace_back(trim(h.substr(0, colon)),
                                     trim(h.substr(colon + 1)));
        pos = eol + 2;
    }

    // Body
    size_t want = 0;
    const std::string len = req.header("Content-Length");
    if (!len.empty()) {
        try {
            want = (size_t)std::stoull(len);
        } catch (...) {
            return false;
        }
        if (want > kMaxBody) return false;
    }
    req.body = buf.substr(headerEnd + 4);
    while (req.body.size() < want) {
        int n = ::recv(sock_, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        req.body.append(chunk, (size_t)n);
    }
    if (req.body.size() > want) req.body.resize(want);
    return true;
}

void Conn::sendRaw(const std::string& bytes) {
    sendAll(sock_, bytes.data(), bytes.size());
}

void Conn::send(int code, const std::string& contentType,
                const std::string& body, const std::string& extraHeaders) {
    char head[512];
    std::snprintf(head, sizeof(head),
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: %s\r\n"
                  "Content-Length: %zu\r\n"
                  "Cache-Control: no-store\r\n"
                  "Connection: close\r\n",
                  code, reasonFor(code), contentType.c_str(), body.size());
    std::string out(head);
    // Already CRLF-terminated per line by the caller, or empty. The blank line
    // that ends the headers is added here either way.
    out += extraHeaders;
    out += "\r\n";
    out += body;
    sendRaw(out);
}

void Conn::sendJson(int code, const js::Value& value) {
    // Station names and log lines carry arrows and accents. JSON is always
    // UTF-8 by spec and browsers assume it, but saying so keeps simpler
    // clients from guessing an 8-bit codepage and mangling the text.
    send(code, "application/json; charset=utf-8", value.dump(0));
}

void Conn::sendText(int code, const std::string& text) {
    send(code, "text/plain; charset=utf-8", text);
}

// ------------------------------------------------------------------- SSE
std::string sseFrame(const std::string& event, const std::string& data) {
    return "event: " + event + "\ndata: " + data + "\n\n";
}

bool SseClient::sendFrame(const std::string& frame) {
    if (dead.load()) return false;
    std::lock_guard<std::mutex> lk(mu);
    if (dead.load()) return false;
    if (!sendAll(sock, frame.data(), frame.size())) {
        dead.store(true);
        return false;
    }
    return true;
}

void SseClient::shutdownNow() {
    dead.store(true);
    std::lock_guard<std::mutex> lk(mu);
    if (sock != INVALID_SOCKET) {
        ::shutdown(sock, SD_BOTH);
        ::closesocket(sock);
        sock = INVALID_SOCKET;
    }
}

// ------------------------------------------------------------- networking
bool netStartup(std::string& error) {
    WSADATA wsa{};
    const int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        error = "Windows networking failed to start (WSAStartup " +
                std::to_string(rc) + ")";
        return false;
    }
    return true;
}

void netCleanup() { WSACleanup(); }

SOCKET listenOn(int port, bool localOnly, std::string& error, bool& portInUse) {
    portInUse = false;
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = "could not create a socket";
        return INVALID_SOCKET;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = localOnly ? htonl(INADDR_LOOPBACK) : INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);

    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        portInUse = (err == WSAEADDRINUSE);
        error = "could not listen on port " + std::to_string(port);
        ::closesocket(s);
        return INVALID_SOCKET;
    }
    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) {
        error = "could not listen on port " + std::to_string(port);
        ::closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

std::string localAddressOf(SOCKET s) {
    sockaddr_in addr{};
    int len = sizeof(addr);
    if (::getsockname(s, (sockaddr*)&addr, &len) == SOCKET_ERROR) return "";
    char ip[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) return "";
    return ip;
}

bool looksVirtualAdapter(const std::string& nameOrDescription) {
    // Matched against both the friendly name and the description, because
    // which one carries the giveaway varies by product. "vEthernet" alone
    // covers Hyper-V and WSL, which is how a 172.31.x.x address turns up on
    // an ordinary office PC that has never knowingly run a virtual machine.
    static const char* kMarkers[] = {
        "vethernet", "hyper-v", "virtualbox", "vmware", "wsl", "docker",
        "loopback", "tap-windows", "tailscale", "zerotier", "npcap",
        "bluetooth",
    };
    for (const char* m : kMarkers)
        if (icontains(nameOrDescription, m)) return true;
    return false;
}

bool isLinkLocalIPv4(const std::string& ip) {
    return ip.rfind("169.254.", 0) == 0;
}

std::vector<std::string> localIPv4() {
    std::vector<std::string> real;      // worth showing the office
    std::vector<std::string> fallback;  // shown only if nothing else survives

    ULONG size = 15000;
    std::vector<BYTE> buf(size);
    auto* table = (IP_ADAPTER_ADDRESSES*)buf.data();
    // INCLUDE_GATEWAYS is what fills in FirstGatewayAddress below; without it
    // the field is simply never populated and every adapter looks gatewayless.
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                        GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, table, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        table = (IP_ADAPTER_ADDRESSES*)buf.data();
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, table, &size);
    }
    if (rc != NO_ERROR) return real;

    for (auto* a = table; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (a->IfType == IF_TYPE_TUNNEL) continue;

        const std::string friendly =
            a->FriendlyName ? narrow(a->FriendlyName) : std::string();
        const std::string desc =
            a->Description ? narrow(a->Description) : std::string();

        // Two independent signals, either of which marks an adapter as
        // internal-only:
        //
        //  - its name says so, which catches the named products above;
        //  - it has no default gateway, which catches the ones it doesn't.
        //    A virtual switch has none because this computer IS the gateway
        //    for it, whereas anything reaching a real office network was
        //    given a router to talk through.
        const bool virtualByName =
            looksVirtualAdapter(friendly) || looksVirtualAdapter(desc);
        const bool noGateway = a->FirstGatewayAddress == nullptr;

        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            auto* sa = (sockaddr_in*)u->Address.lpSockaddr;
            char ip[INET_ADDRSTRLEN] = {0};
            if (!inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip))) continue;

            // Never worth printing under any circumstance: nothing is on the
            // other end of one of these.
            if (isLinkLocalIPv4(ip)) continue;

            if (virtualByName || noGateway)
                fallback.emplace_back(ip);
            else
                real.emplace_back(ip);
        }
    }

    // The gateway test is the one that could be wrong: an office running a
    // bare switch with static addresses and no router has no gateway either,
    // and hiding its only address would leave nobody able to reach the server.
    // So a filter that removes everything is treated as a filter that was
    // mistaken, and the unfiltered list is handed back instead.
    return real.empty() ? fallback : real;
}

}  // namespace on
