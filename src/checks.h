// The checklist the server window shows: what Dental Comms found out about
// this computer, in plain words, with a button beside anything that can be
// put right from here.
//
// Every problem the server can run into ends up as a row here rather than as
// a dialog that closes the program. The words live in one place, built from a
// plain list of facts, so the self-test can walk every combination without a
// network, a firewall or a window.
#pragma once
#include <string>
#include <vector>

#include "config.h"
#include "firewall.h"
#include "network.h"

namespace on {

// Everything the window needs to know, gathered by main.cpp.
struct Facts {
    std::string version;
    std::string officeName;
    std::string computer;                 // this machine's name
    std::vector<std::string> addresses;   // best first; empty when staff can't connect
    int port = 0;
    int portWanted = 0;    // the usual port, when the first start had to use another
    bool serving = true;   // listening (false only while it cannot get its port back)

    ConfigReport config;

    NetProfile profile = NetProfile::Unknown;
    NetMode mode = NetMode::Office;
    std::string networkNames;   // the networks behind `profile`, for a sentence

    FirewallState firewall = FirewallState::Unknown;
    bool runAtSignIn = false;

    int staffScreens = 0;        // screens open on other computers right now
    long long lastStaffSeen = 0; // when another computer last reached us (ms); 0 = not yet
    long long now = 0;           // ms, the same clock as lastStaffSeen
};

enum class CheckState {
    Working,   // grey: waiting for something
    Ok,        // green tick
    Note,      // blue: worth knowing, nothing to do
    NeedsYou   // amber: something to put right
};

// What a row's button does. The window carries it out; nothing here acts.
enum class Fix {
    None,
    Firewall,          // elevated: DentalComms.exe --fix-firewall
    TrustNetwork,      // elevated: DentalComms.exe --trust-network
    StartWithWindows   // the same as ticking it in the tray menu
};

struct Check {
    CheckState state = CheckState::Ok;
    std::string text;       // one line
    std::string help;       // a sentence or two under it; empty when all is well
    std::string fixLabel;   // the button's words, empty for no button
    Fix fix = Fix::None;
};

std::vector<Check> buildChecks(const Facts& f);

// True when any row needs somebody to do something.
bool needsAttention(const std::vector<Check>& checks);

// The address staff type in, e.g. "http://RECEPTION-PC:3272", or empty when
// no other computer can reach this one right now.
std::string staffAddress(const Facts& f);

// The same address by number, e.g. "http://192.168.1.25:3272", for computers
// that can't find this one by name. Empty when there is no address at all, or
// when the computer has no usable number.
std::string staffIpAddress(const Facts& f);

// "just now", "4 minutes ago", "at 9:14".
std::string sinceText(long long thenMs, long long nowMs);

}  // namespace on
