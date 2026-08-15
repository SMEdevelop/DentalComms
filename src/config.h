// The office's own settings: what it's called, which stations exist, and the
// quick-message buttons. Persisted as data/config.json - the same file the
// Node version wrote, so an existing office keeps its setup.
#pragma once
#include <string>
#include <vector>

#include "json.h"
#include "network.h"

namespace on {

// What Dental Flow should do on a given kind of network.
enum class NetMode {
    Office,     // listen on every interface - staff computers can reach it
    LocalOnly,  // listen on this computer only; nobody else can connect
    Off         // refuse to start at all
};

const char* netModeName(NetMode m);
NetMode netModeFromName(const std::string& s, NetMode fallback);

// Dental Flow has no passwords, so where it answers matters. The defaults trust
// the two networks Windows considers yours and hold back on the one it
// doesn't - a laptop that wanders onto cafe Wi-Fi does not start serving the
// office's message board to the room.
struct NetworkPolicy {
    NetMode domain = NetMode::Office;
    NetMode priv = NetMode::Office;
    NetMode pub = NetMode::LocalOnly;

    NetMode modeFor(NetProfile p) const;
    js::Value toJson() const;
    void applyFromJson(const js::Value& v);
};

struct Station {
    std::string id;
    std::string name;
};

struct Preset {
    std::string id;
    std::string label;
    std::string color = "#1c7ed6";
    bool urgent = false;
};

struct Config {
    std::string officeName = "Our Office";
    NetworkPolicy networks;

    // How long an alert may sit unanswered before the sender is told nobody
    // has picked it up, and how long before it is given up on entirely.
    // Either can be set to 0 to switch that behaviour off.
    int escalateAfterSeconds = 120;
    int expireAfterSeconds = 3600;

    // How an arriving alert announces itself on the station receiving it.
    // All on by default, because each one alone has a way of being missed:
    // sound is lost in a noisy surgery or on a tablet somebody muted, a flash
    // is lost on anyone not looking at the screen, and both are lost on a
    // station whose Dental Flow window is behind the practice software.
    // Turning them all off leaves the alert silent and still - it appears on
    // screen and waits.
    bool notifySound = true;
    bool notifyFlash = true;
    bool notifyTaskbar = true;

    // Names this server may be reached by, beyond localhost, this computer's
    // own name, and the address a connection actually arrived on. Only needed
    // when the office uses an alias its router hands out.
    std::vector<std::string> allowedHosts;

    std::vector<Station> stations;
    std::vector<Preset> presets;

    static Config defaults();

    js::Value toJson() const;

    // Applies a settings edit from the admin page. Mirrors the Node server's
    // rules exactly: everything is clamped rather than rejected, so a slightly
    // odd edit still saves instead of erroring. Returns false only when there
    // is nothing usable at all - no stations or no buttons - and then `error`
    // carries the message the admin page shows.
    //
    // The network policy is carried over untouched: the settings page knows
    // nothing about it, so a routine "rename a room" save must not silently
    // reset where the server is willing to answer.
    bool applyFromJson(const js::Value& body, std::string& error);

    std::string stationName(const std::string& id) const;
    bool hasStation(const std::string& id) const;
    const Preset* findPreset(const std::string& id) const;
};

// Reads data/config.json, falling back to the defaults (and writing them out)
// when the file is missing or damaged - the office is never left with no
// settings at all.
Config loadConfig();
bool saveConfig(const Config& cfg);

// Where public/ and data/ live. Normally the folder holding the .exe, but the
// parent is checked too so the server still works when built into build\.
std::string resourceRoot();

// A random id in the same uuid shape the settings page generates, so ids made
// at either end are indistinguishable.
std::string newId();

}  // namespace on
