// Dental Comms - simple intraoffice messaging.
//
// A C++ port of the original Node server. The browser side is untouched: this
// serves the very same public/ folder and speaks the same API, so staff
// machines need no changes and no Node.js has to be installed anywhere.
#include "http.h"  // must precede <windows.h> users

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "gui.h"
#include "network.h"
#include "routes.h"
#include "state.h"
#include "util.h"
#include "version.h"

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

// Called from the UI thread when the user turns Dental Comms off. Dropping the
// listener is what unblocks the accept thread; without it the process would
// sit in accept() forever and never exit.
void stopServing() {
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
    g_hub.stop();
}

// The accept loop, on its own thread. It has to be off the UI thread: a window
// being dragged, or a menu left open, blocks the message loop, and a message
// nobody could deliver because someone was holding a menu open would be
// exactly the kind of failure this program promises not to have.
void acceptLoop() {
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
}

// Started with --tray (or /tray), Dental Comms goes straight to the
// notification area. That is how the Run key launches it, so a computer that
// switches itself on in the morning does not greet whoever unlocks it with a
// window across their screen.
bool wantsTray(const std::string& cmdLine) {
    const std::string c = lower(cmdLine);
    return icontains(c, "--tray") || icontains(c, "/tray") ||
           icontains(c, "--minimized") || icontains(c, "--minimised");
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int) {
    std::string err;
    if (!on::netStartup(err)) {
        on::showProblem("Dental Comms could not start: " + err);
        return 1;
    }

    // Where Dental Comms is willing to answer depends on the kind of network
    // this computer is on, so the settings have to be read before binding.
    on::Config cfg = on::loadConfig();
    const on::NetProfile profile = on::currentProfile();
    const on::NetMode mode = cfg.networks.modeFor(profile);

    if (mode == on::NetMode::Off) {
        on::showProblem(
            std::string("Dental Comms did not start.\n\nWindows says this "
                        "computer is on a ") +
            on::profileName(profile) +
            " network, and Dental Comms is set to stay off on those.\n\n"
            "If this is your office network, tell Windows so: Settings > "
            "Network & internet > (your connection) > set the network profile "
            "to Private. Then start Dental Comms again.\n\n"
            "To change the rule itself, edit \"networks\" in "
            "data\\config.json.");
        on::netCleanup();
        return 1;
    }

    const bool localOnly = (mode == on::NetMode::LocalOnly);
    const int port = pickPort();
    bool inUse = false;
    g_listen = on::listenOn(port, localOnly, err, inUse);
    if (g_listen == INVALID_SOCKET) {
        if (inUse) {
            char msg[512];
            _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                        "Another program on this computer is already using "
                        "port %d.\n\nFix: create a file named port.txt in the "
                        "Dental Comms folder containing a different number (for "
                        "example 8123), then start Dental Comms again.\n\n"
                        "Remember to update the bookmarks on the other "
                        "computers to the new number.",
                        port);
            on::showProblem(msg);
        } else {
            on::showProblem("Dental Comms could not start: " + err);
        }
        on::netCleanup();
        return 1;
    }

    g_hub.start(cfg);

    // What the window shows. Worked out here, beside the code that decided
    // where to bind, rather than in the window - which should not have to know
    // anything about networks.
    on::GuiInfo info;
    info.version = DC_VERSION_STRING;
    info.port = port;
    info.networkName = on::profileName(profile);
    info.localOnly = localOnly;
    info.officeName = cfg.officeName;
    if (!localOnly) {
        // The computer's own name goes first, and is the one to bookmark: it
        // keeps working when the router hands this machine a different number
        // after a restart, which is the failure that breaks every bookmark in
        // the office at once and looks like Dental Comms having died.
        info.computer = computerName();
        if (!info.computer.empty()) info.addresses.push_back(info.computer);
        for (const auto& ip : on::localIPv4()) info.addresses.push_back(ip);
    }

    std::thread server(acceptLoop);
    on::migrateRunAtSignIn();

    // Either the switch on this launch or the setting in config.json will do
    // it: the switch is what the run-at-sign-in entry passes, and the setting
    // is for an office that wants it that way however it was started.
    const bool hidden = cfg.startMinimized || wantsTray(narrow(cmdLine));
    const int rc = on::runGui(inst, info, hidden, &stopServing);

    // runGui only returns once the user has quit and stopServing has closed
    // the listener, so this joins rather than hangs.
    if (server.joinable()) server.join();
    on::netCleanup();
    return rc;
}
