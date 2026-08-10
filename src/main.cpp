// Dental Flow - simple intraoffice messaging.
//
// A C++ port of the original Node server. The browser side is untouched: this
// serves the very same public/ folder and speaks the same API, so staff
// machines need no changes and no Node.js has to be installed anywhere.
#include "http.h"  // must precede <windows.h> users

#include <cstdio>
#include <string>
#include <thread>

#include "config.h"
#include "network.h"
#include "routes.h"
#include "state.h"
#include "util.h"

namespace {

// An uncommon default so it doesn't collide with other software. Overridden by
// a PORT variable, or by putting a number in port.txt next to the program.
constexpr int kDefaultPort = 3272;

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!isdigit((unsigned char)c)) return false;
    return true;
}

int pickPort() {
    char buf[64] = {0};
    DWORD n = GetEnvironmentVariableA("PORT", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        const std::string v = trim(buf);
        if (allDigits(v)) {
            const int p = atoi(v.c_str());
            if (p > 0 && p <= 65535) return p;
        }
    }
    std::string text;
    if (readFile(joinPath(on::resourceRoot(), "port.txt"), text)) {
        const std::string v = trim(text);
        if (allDigits(v) && v.size() >= 2 && v.size() <= 5) {
            const int p = atoi(v.c_str());
            if (p > 0 && p <= 65535) return p;
        }
    }
    return kDefaultPort;
}

on::Hub g_hub;
SOCKET g_listen = INVALID_SOCKET;

BOOL WINAPI onConsoleSignal(DWORD) {
    // Ctrl+C or the window closing: drop the listener and every open event
    // stream so the process can exit instead of hanging on a blocked accept.
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
    g_hub.stop();
    return FALSE;  // let the default handler finish the job
}

}  // namespace

int main() {
    // The activity log and station names can hold non-ASCII text; without this
    // the console renders them as mojibake.
    SetConsoleOutputCP(CP_UTF8);

    std::string err;
    if (!on::netStartup(err)) {
        std::printf("\n  PROBLEM: %s\n", err.c_str());
        return 1;
    }

    // Where Dental Flow is willing to answer depends on the kind of network
    // this computer is on, so the settings have to be read before binding.
    on::Config cfg = on::loadConfig();
    const on::NetProfile profile = on::currentProfile();
    const on::NetMode mode = cfg.networks.modeFor(profile);

    if (mode == on::NetMode::Off) {
        std::printf(
            "\n  Dental Flow did not start.\n\n"
            "  Windows says this computer is on a %s network, and Dental Flow is\n"
            "  set to stay off on those.\n\n"
            "  If this is your office network, tell Windows so: Settings ->\n"
            "  Network & internet -> (your connection) -> set the network\n"
            "  profile to Private. Then start Dental Flow again.\n\n"
            "  To change the rule itself, edit \"networks\" in "
            "data\\config.json.\n\n",
            on::profileName(profile));
        on::netCleanup();
        return 1;
    }

    const bool localOnly = (mode == on::NetMode::LocalOnly);
    const int port = pickPort();
    bool inUse = false;
    g_listen = on::listenOn(port, localOnly, err, inUse);
    if (g_listen == INVALID_SOCKET) {
        std::printf("\n");
        if (inUse) {
            std::printf(
                "  PROBLEM: another program on this computer is already using "
                "port %d.\n"
                "  Fix: create a file named port.txt in this folder containing "
                "a different\n"
                "  number (for example 8123), then start Dental Flow again.\n"
                "  Remember to update the bookmarks on the other computers to "
                "the new number.\n",
                port);
        } else {
            std::printf("  PROBLEM: Dental Flow could not start: %s\n",
                        err.c_str());
        }
        on::netCleanup();
        return 1;
    }

    g_hub.start(cfg);
    SetConsoleCtrlHandler(onConsoleSignal, TRUE);

    std::printf("\n  Dental Flow is running.\n");
    std::printf("  On this computer:  http://localhost:%d\n", port);
    if (localOnly) {
        // Say plainly that staff cannot connect, and why, rather than letting
        // someone spend twenty minutes on bookmarks that were never going to
        // work.
        std::printf(
            "\n  Network: %s - this computer only.\n"
            "  Staff computers CANNOT connect right now. Dental Flow holds back\n"
            "  on %s networks because it has no password on it.\n\n"
            "  If this is your office network, tell Windows so: Settings ->\n"
            "  Network & internet -> (your connection) -> set the network\n"
            "  profile to Private, then restart Dental Flow.\n",
            on::profileName(profile), on::profileName(profile));
    } else {
        for (const auto& ip : on::localIPv4())
            std::printf("  Other computers:   http://%s:%d\n", ip.c_str(), port);
        std::printf("\n  Network: %s - staff computers can connect.\n",
                    on::profileName(profile));
    }
    std::printf("\n  Leave this window open. Press Ctrl+C to stop.\n\n");
    std::fflush(stdout);

    while (true) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        SOCKET client = accept(g_listen, (sockaddr*)&from, &fromLen);
        if (client == INVALID_SOCKET) {
            if (g_listen == INVALID_SOCKET) break;  // shutting down
            continue;
        }
        // One thread per connection. An office has tens of browsers, not
        // thousands, and a thread that can block on a slow client without
        // affecting anyone else is far simpler than an event loop.
        std::thread([client] { on::serveConnection(client, g_hub); }).detach();
    }

    on::netCleanup();
    return 0;
}
