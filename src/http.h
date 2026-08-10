// The small amount of HTTP this app needs: read a request, write a reply, and
// hold a Server-Sent Events stream open so alerts arrive without polling.
//
// Winsock headers must land before <windows.h>, so this header goes first in
// any file that includes it.
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "json.h"

namespace on {

struct HttpRequest {
    std::string method;
    std::string path;    // percent-decoded, query stripped
    std::string query;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    std::string header(const std::string& name) const;
};

// One accepted connection. Replies always close the socket afterwards: with a
// handful of office PCs there is nothing to gain from keep-alive, and it
// removes a whole class of framing bugs.
class Conn {
public:
    explicit Conn(SOCKET s) : sock_(s) {}
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    bool readRequest(HttpRequest& req);
    void sendRaw(const std::string& bytes);
    void send(int code, const std::string& contentType, const std::string& body);
    void sendJson(int code, const js::Value& value);
    void sendText(int code, const std::string& text);

    SOCKET release();  // hand the socket to an SSE stream
    SOCKET sock() const { return sock_; }

private:
    SOCKET sock_;
};

// A browser sitting on /api/events. Every write goes through one mutex so a
// broadcast from another station can never interleave halfway through a frame.
struct SseClient {
    SOCKET sock = INVALID_SOCKET;
    // Which station this browser is sitting at, or empty when nobody has
    // picked one yet. Set once before the client is shared with the hub and
    // never touched again, so it is safe to read without the mutex.
    std::string station;
    std::mutex mu;
    std::atomic<bool> dead{false};

    bool sendFrame(const std::string& frame);
    void shutdownNow();
};

std::string sseFrame(const std::string& event, const std::string& data);

// Percent-decoding, as used on the request target.
std::string urlDecode(const std::string& s);

// Pulls one parameter out of a raw query string ("station=front&x=1"),
// percent-decoded and with '+' read as a space. Empty when it isn't there.
std::string queryParam(const std::string& query, const std::string& name);

bool netStartup(std::string& error);
void netCleanup();

// Binds without SO_REUSEADDR on purpose: if another program already holds the
// port we want to fail loudly, exactly as the Node version did, so the message
// about port.txt still makes sense.
//
// With localOnly set, this binds to 127.0.0.1 rather than every interface, so
// the machine itself can use Dental Flow while nothing on the network can even
// open a connection. That is enforced by the operating system, not by us
// deciding what to answer.
SOCKET listenOn(int port, bool localOnly, std::string& error, bool& portInUse);

std::vector<std::string> localIPv4();

// The address on this computer that a given connection actually arrived on.
// A name that resolves somewhere else cannot match it, which is what makes it
// worth checking the Host header against.
std::string localAddressOf(SOCKET s);

}  // namespace on
