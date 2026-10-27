// The office's own settings: what it's called, which stations exist, and the
// quick-message buttons. Persisted as data/config.json - the same file the
// Node version wrote, so an existing office keeps its setup.
#pragma once
#include <string>
#include <vector>

#include "json.h"
#include "network.h"

namespace on {

// What Dental Comms should do on a given kind of network.
enum class NetMode {
    Office,     // listen on every interface - staff computers can reach it
    LocalOnly,  // listen on this computer only; nobody else can connect
    Off         // refuse to start at all
};

const char* netModeName(NetMode m);
NetMode netModeFromName(const std::string& s, NetMode fallback);

// Dental Comms has no passwords, so where it answers matters. The defaults trust
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
    // station whose Dental Comms window is behind the practice software.
    // Turning them all off leaves the alert silent and still - it appears on
    // screen and waits.
    bool notifySound = true;
    bool notifyFlash = true;
    bool notifyTaskbar = true;

    // Start straight into the notification area, with no window. What the
    // office wants once it is set up and working: the server computer comes
    // on in the morning and Dental Comms is simply there. The --tray switch
    // does the same thing for one launch, and is what the run-at-sign-in
    // entry uses.
    bool startMinimized = false;

    // Names this server may be reached by, beyond localhost, this computer's
    // own name, and the address a connection actually arrived on. Only needed
    // when the office uses an alias its router hands out.
    std::vector<std::string> allowedHosts;

    // The port every bookmark in the office uses, once the first start has
    // picked one. 0 means "not chosen yet". port.txt and the PORT variable
    // still win over it, so an office that set one by hand keeps it.
    int port = 0;

    // Whether the first-run welcome on the server computer has been finished.
    // Only a brand-new settings file starts false: a file from before this
    // existed belongs to an office that is already set up and working, and
    // greeting them with "Welcome" after an update would be wrong.
    bool welcomed = false;

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

// How the settings came to be what they are this time, so the server window
// can say so in plain words instead of quietly starting with different rooms.
enum class ConfigOrigin {
    Loaded,              // read as usual
    Created,             // no file yet: a brand-new office, defaults written
    RestoredFromBackup,  // the file was damaged; the last good copy was used
    ResetDamaged         // damaged and no usable backup: defaults
};

struct ConfigReport {
    ConfigOrigin origin = ConfigOrigin::Loaded;
    // Could Dental Comms write in its data folder? False when it has been put
    // somewhere Windows protects, which would lose every change made.
    bool writable = true;
    // The name a damaged file was set aside under, so nothing is lost.
    std::string damagedCopy;
};

// Reads data/config.json, never leaving the office with no settings at all:
//   - missing        -> the defaults, written out
//   - damaged        -> set aside as config.damaged-<date>.json, then the
//                       last good copy (config.backup.json), else defaults
// Every successful save also refreshes config.backup.json, so a hand edit
// that breaks the file costs nothing.
Config loadConfig(ConfigReport* report = nullptr);
bool saveConfig(const Config& cfg);

// The same, for a given data folder. Used by the self-test so it never has to
// touch the office's real settings.
Config loadConfigFrom(const std::string& dataDir, ConfigReport* report = nullptr);
bool saveConfigTo(const std::string& dataDir, const Config& cfg);

// <resourceRoot>\data
std::string dataDir();

// Reads a settings file's text. False when it is not a settings object at all
// (not JSON, or not an object) - the "damaged" case.
bool parseConfig(const std::string& text, Config& out);

// Where public/ and data/ live. Normally the folder holding the .exe, but the
// parent is checked too so the server still works when built into build\.
std::string resourceRoot();

// A random id in the same uuid shape the settings page generates, so ids made
// at either end are indistinguishable.
std::string newId();

}  // namespace on
