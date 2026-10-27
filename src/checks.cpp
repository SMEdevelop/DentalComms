#include "checks.h"

#include <windows.h>

#include <cstdio>

namespace on {
namespace {

std::string num(int n) { return std::to_string(n); }

std::string quoted(const std::string& names) {
    return names.empty() ? std::string("this network") : "\"" + names + "\"";
}

Check row(CheckState s, std::string text, std::string help = "",
          std::string fixLabel = "", Fix fix = Fix::None) {
    Check c;
    c.state = s;
    c.text = std::move(text);
    c.help = std::move(help);
    c.fixLabel = std::move(fixLabel);
    c.fix = fix;
    return c;
}

}  // namespace

std::string sinceText(long long thenMs, long long nowMs) {
    const long long secs = (nowMs - thenMs) / 1000;
    if (secs < 60) return "just now";
    const long long mins = secs / 60;
    if (mins < 60) return std::to_string(mins) + (mins == 1 ? " minute ago" : " minutes ago");

    // Older than an hour: a clock time reads better than "7 hours ago".
    const long long ft = (thenMs + 11644473600000LL) * 10000LL;  // to FILETIME units
    FILETIME utc{(DWORD)ft, (DWORD)(ft >> 32)}, local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &st))
        return "earlier today";
    char buf[32];
    const int h12 = st.wHour % 12 == 0 ? 12 : st.wHour % 12;
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "at %d:%02d %s", h12, st.wMinute,
                st.wHour < 12 ? "AM" : "PM");
    return buf;
}

std::string staffAddress(const Facts& f) {
    if (f.mode != NetMode::Office || !f.serving || f.addresses.empty()) return "";
    return "http://" + f.addresses[0] + ":" + num(f.port);
}

bool needsAttention(const std::vector<Check>& checks) {
    for (const auto& c : checks)
        if (c.state == CheckState::NeedsYou) return true;
    return false;
}

std::vector<Check> buildChecks(const Facts& f) {
    std::vector<Check> out;
    const bool isPublic =
        f.profile == NetProfile::Public || f.profile == NetProfile::Unknown;
    const bool noNetwork = f.profile == NetProfile::Unknown && f.networkNames.empty();

    // ---- is it running at all
    if (f.mode == NetMode::Off) {
        const bool canTrust = isPublic && !noNetwork;
        out.push_back(row(
            CheckState::Working, "Waiting for your office network",
            noNetwork ? "This computer isn't connected to a network. Dental Comms "
                        "will start by itself as soon as it is."
            : isPublic ? "Dental Comms stays off on public networks like " +
                             quoted(f.networkNames) +
                             ". It will start by itself when this computer is back "
                             "on your office network."
                       : "The settings file says to stay off on this kind of "
                         "network. Dental Comms will start by itself on a network "
                         "it is allowed to use.",
            canTrust ? "This is our office network" : "",
            canTrust ? Fix::TrustNetwork : Fix::None));
    } else if (!f.serving) {
        out.push_back(row(
            CheckState::NeedsYou, "Dental Comms can't be reached right now",
            "Another program took its connection number (" + num(f.port) +
                ") while the network was changing. Dental Comms keeps trying. "
                "If this doesn't clear in a minute, restart this computer."));
    } else {
        out.push_back(row(CheckState::Ok, "Dental Comms is running"));
    }

    if (f.portWanted && f.portWanted != f.port) {
        out.push_back(row(
            CheckState::Note, "Using connection number " + num(f.port),
            "Number " + num(f.portWanted) +
                " was already taken by another program on this computer, so "
                "Dental Comms picked the next free one. Nothing for you to do."));
    }

    // ---- settings
    if (!f.config.writable) {
        out.push_back(row(
            CheckState::NeedsYou, "Dental Comms can't save its settings",
            "It's in a folder Windows protects, so any rooms or buttons you change "
            "would be lost. Run the Dental Comms installer instead; it puts "
            "everything somewhere it can save."));
    } else if (f.config.origin == ConfigOrigin::RestoredFromBackup) {
        out.push_back(row(
            CheckState::Note, "Your settings were repaired",
            "The settings file was damaged, so your rooms and buttons were put "
            "back from the last good copy. The damaged file was kept as " +
                f.config.damagedCopy + "."));
    } else if (f.config.origin == ConfigOrigin::ResetDamaged) {
        out.push_back(row(
            CheckState::Note, "Your settings were reset",
            "The settings file was damaged and there was no copy to restore, so "
            "Dental Comms started with the standard rooms and buttons. The damaged "
            "file was kept as " + f.config.damagedCopy + " in the data folder."));
    } else {
        out.push_back(row(CheckState::Ok, "Your settings are saved on this computer"));
    }

    // ---- the network
    if (f.mode == NetMode::Office) {
        out.push_back(row(CheckState::Ok,
                          f.networkNames.empty()
                              ? std::string("Connected to your office network")
                              : "Connected to your office network (" +
                                    f.networkNames + ")"));
    } else if (f.mode == NetMode::LocalOnly) {
        if (noNetwork) {
            out.push_back(row(
                CheckState::NeedsYou, "This computer isn't connected to a network",
                "Other computers can't reach Dental Comms until it is. Check the "
                "network cable or the Wi-Fi."));
        } else if (isPublic) {
            out.push_back(row(
                CheckState::NeedsYou, "Other computers can't connect yet",
                "Windows thinks " + quoted(f.networkNames) +
                    " is a public network, like coffee-shop Wi-Fi, so Dental Comms "
                    "keeps other computers out. If this computer is at your office "
                    "right now, tell Windows so:",
                "Yes, this is our office network", Fix::TrustNetwork));
        } else {
            out.push_back(row(
                CheckState::Note, "Set to this computer only",
                "The settings file says to serve only this computer on this kind "
                "of network, so staff computers can't connect."));
        }
    }

    // ---- can staff actually get in
    if (f.mode == NetMode::Office && f.serving) {
        const bool proven = f.staffScreens > 0 || f.lastStaffSeen > 0;
        if (!proven) {
            switch (f.firewall) {
                case FirewallState::Allowed:
                    out.push_back(row(CheckState::Ok,
                                      "Windows Firewall lets other computers in"));
                    break;
                case FirewallState::WillAsk:
                    out.push_back(row(
                        CheckState::Note, "Windows may ask about Dental Comms",
                        "If a Windows Security box asks whether to allow Dental Comms "
                        "on your network, click Allow. Or let it in now:",
                        "Allow it now", Fix::Firewall));
                    break;
                case FirewallState::Blocked:
                    out.push_back(row(
                        CheckState::NeedsYou,
                        "Windows Firewall is keeping other computers out",
                        "This usually happens when someone clicks Cancel on Windows' "
                        "\"allow access\" box. It's easy to undo:",
                        "Let them in", Fix::Firewall));
                    break;
                case FirewallState::Managed:
                    out.push_back(row(
                        CheckState::NeedsYou,
                        "Your office's IT settings may be keeping other computers out",
                        "Ask whoever looks after your computers to allow Dental Comms "
                        "(TCP port " + num(f.port) + ") through the firewall."));
                    break;
                case FirewallState::Off:
                case FirewallState::Unknown:
                    break;  // nothing worth saying either way
            }
        }

        if (f.staffScreens > 0) {
            out.push_back(row(CheckState::Ok,
                              f.staffScreens == 1
                                  ? std::string("1 staff screen is connected")
                                  : num(f.staffScreens) + " staff screens are connected"));
        } else if (f.lastStaffSeen > 0) {
            out.push_back(row(CheckState::Ok, "Reached from another computer " +
                                                  sinceText(f.lastStaffSeen, f.now)));
        } else {
            out.push_back(row(CheckState::Note, "No other computer has connected yet",
                              "Open the address below on a staff computer to try it."));
        }
    }

    // ---- after a restart
    if (f.runAtSignIn) {
        out.push_back(row(CheckState::Ok, "Starts by itself when this computer starts"));
    } else {
        out.push_back(row(CheckState::Note, "Doesn't start by itself yet",
                          "If this computer restarts, messages stop until someone "
                          "opens Dental Comms again.",
                          "Start it with Windows", Fix::StartWithWindows));
    }
    return out;
}

}  // namespace on
