// Dental Comms - simple intraoffice messaging.
//
// A C++ port of the original Node server. The browser side is untouched: this
// serves the very same public/ folder and speaks the same API, so staff
// machines need no changes and no Node.js has to be installed anywhere.
//
// Starting up never ends in a dead end if it can be helped. Each thing that
// can be wrong - a damaged settings file, a busy port, an untrusted network, a
// firewall rule - is either put right on the spot or becomes a line in the
// window's checklist with a button beside it. Only a few failures, the ones
// that leave nothing to show a window for, still stop with a message box.
#include "http.h"  // must precede <windows.h> users

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "checks.h"
#include "config.h"
#include "firewall.h"
#include "gui.h"
#include "network.h"
#include "routes.h"
#include "state.h"
#include "util.h"
#include "version.h"

namespace {

// An uncommon default so it doesn't collide with other software. Overridden by
// a PORT variable, port.txt next to the program, or "port" in config.json.
constexpr int kDefaultPort = 3272;
// How far past a busy port to look for a free one.
constexpr int kPortSearch = 20;

enum class PortSource { Env, File, Config, Default };

int parsePort(const std::string& raw, size_t minDigits) {
    const std::string v = trim(raw);
    if (v.size() < minDigits || v.size() > 5) return 0;
    for (char c : v)
        if (!isdigit((unsigned char)c)) return 0;
    const int p = atoi(v.c_str());
    return (p > 0 && p <= 65535) ? p : 0;
}

std::string portFilePath() { return joinPath(on::resourceRoot(), "port.txt"); }

int pickPort(const on::Config& cfg, PortSource& src) {
    char buf[64] = {0};
    const DWORD n = GetEnvironmentVariableA("PORT", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf))
        if (const int p = parsePort(buf, 1)) {
            src = PortSource::Env;
            return p;
        }
    std::string text;
    if (readFile(portFilePath(), text))
        if (const int p = parsePort(text, 2)) {
            src = PortSource::File;
            return p;
        }
    if (cfg.port > 0) {
        src = PortSource::Config;
        return cfg.port;
    }
    src = PortSource::Default;
    return kDefaultPort;
}

on::Hub g_hub;
std::atomic<SOCKET> g_listen{INVALID_SOCKET};
on::NetworkPolicy g_policy;
on::NetMode g_mode = on::NetMode::Office;
bool g_serving = false;   // UI thread only, once the window is up
int g_port = kDefaultPort;
int g_portWanted = 0;     // the port the office would normally use, if moved
on::ConfigReport g_report;
on::FirewallState g_firewall = on::FirewallState::Unknown;

// ------------------------------------------------------------- listening
// One thread per listener. It ends by itself when that listener is closed,
// which is how a change of network swaps one listener for another.
void acceptLoop(SOCKET ls) {
    while (true) {
        SOCKET client = accept(ls, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (g_listen.load() != ls) break;  // closed: replaced or shutting down
            continue;
        }
        // One thread per connection. An office has tens of browsers, not
        // thousands, and a thread that can block on a slow client without
        // affecting anyone else is far simpler than an event loop.
        std::thread([client] { on::serveConnection(client, g_hub); }).detach();
    }
}

void closeListener() {
    const SOCKET s = g_listen.exchange(INVALID_SOCKET);
    if (s != INVALID_SOCKET) closesocket(s);
}

// Opens the listener `mode` calls for. Off needs none.
bool listenFor(on::NetMode mode, std::string& err, bool& inUse) {
    inUse = false;
    if (mode == on::NetMode::Off) return true;
    const SOCKET s =
        on::listenOn(g_port, mode == on::NetMode::LocalOnly, err, inUse);
    if (s == INVALID_SOCKET) return false;
    g_listen = s;
    std::thread(acceptLoop, s).detach();
    return true;
}

// Called from the UI thread when the user turns Dental Comms off. Dropping the
// listener is what unblocks the accept thread.
void stopServing() {
    closeListener();
    g_hub.stop();
}

// Is `port` free? Asked with bind() alone, never listen(): a listen is what
// makes Windows Firewall ask its question, and this is only a test.
bool portFree(int port) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);
    const bool ok = ::bind(s, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR;
    closesocket(s);
    return ok;
}

int freePortAfter(int port) {
    for (int p = port + 1; p <= port + kPortSearch && p <= 65535; ++p)
        if (portFree(p)) return p;
    return 0;
}

// Saves a new port where the old one came from, so the next start agrees.
void rememberPort(on::Config& cfg, PortSource src, int port) {
    if (src == PortSource::File) {
        writeFileAtomic(portFilePath(), std::to_string(port) + "\r\n");
        return;
    }
    cfg.port = port;
    on::saveConfig(cfg);
}

// ----------------------------------------------------------- busy port
enum { kTryAgain = 1001, kNewNumber, kStopOther };

bool isOurExeName(const std::string& name) {
    return iequals(name, "DentalComms.exe") || iequals(name, "DentalFlow.exe");
}

int askAboutBusyPort(int port, const std::string& owner, bool system) {
    const std::wstring n = std::to_wstring(port);
    if (isOurExeName(owner)) {
        return on::askChoice(
            L"Another copy of Dental Comms is already running",
            L"It's an older copy, probably in a different folder. Only one can run "
            L"on a computer at a time.",
            {{kStopOther, L"Turn the other copy off and use this one\n"
                          L"Messages pause for a few seconds while it switches over."},
             {kTryAgain, L"Try again\nAfter turning the other copy off yourself."}});
    }
    std::wstring who = owner.empty() ? L"Another program on this computer"
                       : system      ? L"A part of Windows"
                                     : widen(stripExt(owner));
    return on::askChoice(
        L"Dental Comms can't start yet",
        who + L" is using connection number " + n +
            L", which Dental Comms needs.\n\nClose it and try again, or let Dental "
            L"Comms use a different number.",
        {{kTryAgain, L"Try again"},
         {kNewNumber, L"Use a different number\nEvery other computer will need the "
                      L"new address afterwards. This window will show it."}});
}

// Gets the first listener open, putting a busy port right where it can.
bool openFirstListener(on::Config& cfg, PortSource src) {
    // A server that has never finished its welcome has no bookmarks pointing
    // at it yet, so a busy port can simply be swapped for the next free one.
    // After that the port is in every bookmark in the office, and changes only
    // when somebody says so.
    const bool mayMove =
        !cfg.welcomed && (src == PortSource::Default || src == PortSource::Config);
    while (true) {
        std::string err;
        bool inUse = false;
        if (listenFor(g_mode, err, inUse)) return true;
        if (!inUse) {
            on::showProblem(
                "Dental Comms couldn't start.\n\nWindows wouldn't let it listen for "
                "the other computers. Restarting this computer usually fixes "
                "this.\n\n(" + err + ")");
            return false;
        }
        if (mayMove) {
            if (const int p = freePortAfter(g_port)) {
                if (!g_portWanted) g_portWanted = g_port;
                g_port = p;
                rememberPort(cfg, src, p);
                continue;
            }
        }
        bool system = false;
        const std::string owner = on::portOwner(g_port, &system);
        switch (askAboutBusyPort(g_port, owner, system)) {
            case kTryAgain:
                continue;
            case kStopOther:
                on::stopOtherServers();
                for (int i = 0; i < 40 && !portFree(g_port); ++i) Sleep(250);
                continue;
            case kNewNumber:
                if (const int p = freePortAfter(g_port)) {
                    if (!g_portWanted) g_portWanted = g_port;
                    g_port = p;
                    rememberPort(cfg, src, p);
                    continue;
                }
                on::showProblem(
                    "Dental Comms couldn't find a free connection number near " +
                    std::to_string(g_port) +
                    ". Restart this computer and try again.");
                return false;
            default:
                return false;
        }
    }
}

// ----------------------------------------------------------------- facts
// Windows changed its mind about the network: move the listener to match.
// Going to "this computer only" also drops the screens other computers have
// open, or the restriction would only apply to new arrivals.
void followNetwork(on::NetMode want) {
    if (want == g_mode && g_serving) return;
    if (want != g_mode) {
        closeListener();
        if (want != on::NetMode::Office) g_hub.dropRemoteClients();
        g_mode = want;
    }
    std::string err;
    bool inUse = false;
    g_serving = listenFor(want, err, inUse);
}

void publish(const on::Facts& f) {
    js::Value info = js::Value::object();
    info["staffAddress"] = on::staffAddress(f);
    js::Value all = js::Value::array();
    if (f.mode == on::NetMode::Office)
        for (const auto& a : f.addresses)
            all.push("http://" + a + ":" + std::to_string(f.port));
    info["addresses"] = all;
    info["port"] = f.port;
    info["computer"] = f.computer;
    info["officeName"] = f.officeName;
    on::publishServerInfo(info);
}

on::Facts gatherFacts(bool deep) {
    on::Facts f;
    const auto nets = on::connectedNetworks();
    f.profile = on::leastTrusted(nets);
    followNetwork(g_policy.modeFor(f.profile));
    f.mode = g_mode;
    f.serving = g_serving;
    f.networkNames = on::networkNamesAt(nets, f.profile);

    if (deep && f.mode == on::NetMode::Office) g_firewall = on::firewallState();
    f.firewall = g_firewall;

    f.version = DC_VERSION_STRING;
    f.officeName = g_hub.officeName();
    f.port = g_port;
    f.portWanted = g_portWanted;
    f.config = g_report;
    f.runAtSignIn = on::runAtSignInEnabled();
    f.staffScreens = g_hub.staffScreens();
    f.lastStaffSeen = g_hub.lastStaffSeen();
    f.now = on::nowMillis();

    if (f.mode == on::NetMode::Office) {
        // The computer's own name goes first, and is the one to bookmark: it
        // keeps working when the router hands this machine a different number
        // after a restart, which is the failure that breaks every bookmark in
        // the office at once and looks like Dental Comms having died.
        f.computer = computerName();
        if (!f.computer.empty()) f.addresses.push_back(f.computer);
        for (const auto& ip : on::localIPv4()) f.addresses.push_back(ip);
    }
    publish(f);
    return f;
}

// Started with --tray (or /tray), Dental Comms goes straight to the
// notification area. That is how the Run key launches it, so a computer that
// switches itself on in the morning does not greet whoever unlocks it with a
// window across their screen.
bool wantsTray(const std::string& args) {
    return icontains(args, "--tray") || icontains(args, "/tray") ||
           icontains(args, "--minimized") || icontains(args, "--minimised");
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int) {
    const std::string args = lower(narrow(cmdLine ? cmdLine : L""));

    // One-shot jobs the window asks for through a UAC prompt. Handled before
    // the single-instance check, because the copy that asked is still running.
    if (icontains(args, "--fix-firewall")) return on::fixFirewall();
    if (icontains(args, "--trust-network")) return on::trustConnectedNetworks();

    // Already running - most often hidden by the clock. Show that copy rather
    // than failing on the port it holds. The Run entry's quiet --tray start
    // just leaves, and --quit asks the running copy to turn off.
    const bool quit = icontains(args, "--quit");
    if (!on::claimSingleInstance()) {
        if (quit) on::signalRunningCopy(on::InstanceRequest::Quit);
        else if (!wantsTray(args)) on::signalRunningCopy(on::InstanceRequest::Show);
        return 0;
    }
    if (quit) return 0;  // nothing running to turn off

    std::string err;
    if (!on::netStartup(err)) {
        on::showProblem(
            "Dental Comms couldn't start its networking. Restart this computer "
            "and try again.\n\n(" + err + ")");
        return 1;
    }

    // Created, loaded, or repaired - never missing.
    on::Config cfg = on::loadConfig(&g_report);

    // Where Dental Comms is willing to answer depends on the kind of network
    // this computer is on. A network it is set to stay off on no longer stops
    // it: it waits in the tray and starts by itself on a better one.
    g_policy = cfg.networks;
    g_mode = g_policy.modeFor(on::currentProfile());

    PortSource src;
    g_port = pickPort(cfg, src);
    if (!openFirstListener(cfg, src)) {
        on::netCleanup();
        return 1;
    }
    g_serving = true;

    // Pin the port a new office starts on, so a later version with a
    // different default can never quietly move everyone's bookmarks.
    if (src == PortSource::Default && cfg.port == 0) {
        cfg.port = g_port;
        on::saveConfig(cfg);
    }

    // A brand-new server should come back after a restart without anyone
    // having to know there is a setting for it. The installer does the same;
    // this covers the portable zip. Untick it in the tray menu to undo.
    if (g_report.origin == on::ConfigOrigin::Created && !on::runAtSignInEnabled()) {
        std::string ignored;
        on::setRunAtSignIn(true, ignored);
    }

    g_hub.start(cfg);
    on::migrateRunAtSignIn();

    const on::Facts facts = gatherFacts(true);

    on::GuiHooks hooks;
    hooks.onQuit = &stopServing;
    hooks.refresh = gatherFacts;
    hooks.markWelcomed = [] { g_hub.setWelcomed(); };

    // Either the switch on this launch or the setting in config.json will do
    // it: the switch is what the run-at-sign-in entry passes, and the setting
    // is for an office that wants it that way however it was started.
    const bool hidden = cfg.startMinimized || wantsTray(args);
    const int rc = on::runGui(inst, facts, !cfg.welcomed, hidden, hooks);

    on::netCleanup();
    return rc;
}
