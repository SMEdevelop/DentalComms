#include "config.h"

#include <random>

#include "util.h"

namespace on {
namespace {

// ---- JavaScript-shaped helpers -------------------------------------------
// The Node server leaned on JS coercion (`String(x || 'default')`, `!!x`).
// Reproducing those rules here keeps a settings edit behaving identically,
// which matters because the same admin page drives both.

std::string numToStr(double v) {
    if (v == (long long)v) return std::to_string((long long)v);
    std::string s = std::to_string(v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

bool jsTruthy(const js::Value* v) {
    if (!v) return false;
    switch (v->type) {
        case js::Type::Bool: return v->b;
        case js::Type::Num:  return v->n != 0;
        case js::Type::Str:  return !v->s.empty();
        case js::Type::Null: return false;
        default:             return true;  // objects and arrays are truthy
    }
}

// String(x || def) - an absent, null, empty or zero value falls back.
std::string jsStringOr(const js::Value* v, const std::string& def) {
    if (!jsTruthy(v)) return def;
    if (v->type == js::Type::Str) return v->s;
    if (v->type == js::Type::Num) return numToStr(v->n);
    if (v->type == js::Type::Bool) return v->b ? "true" : "false";
    return def;
}

// JS slices by character; doing it by byte could cut a UTF-8 sequence in half
// and produce mojibake in the middle of an office name. Back up to a
// character boundary instead.
std::string clampUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
    return s.substr(0, cut);
}

bool isHexColor(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return false;
    for (size_t i = 1; i < 7; ++i)
        if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

// A hand-edited file should never be able to set a delay of minus a week or of
// a century; 0 means "off" and a day is as long as any of this is useful for.
int clampSeconds(int v) {
    if (v < 0) return 0;
    if (v > 86400) return 86400;
    return v;
}

std::string parentDir(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(0, slash);
}

}  // namespace

const char* netModeName(NetMode m) {
    switch (m) {
        case NetMode::Office:    return "office";
        case NetMode::LocalOnly: return "local-only";
        case NetMode::Off:       return "off";
    }
    return "office";
}

NetMode netModeFromName(const std::string& s, NetMode fallback) {
    const std::string v = lower(trim(s));
    if (v == "office") return NetMode::Office;
    if (v == "local-only" || v == "localonly" || v == "local")
        return NetMode::LocalOnly;
    if (v == "off" || v == "none") return NetMode::Off;
    return fallback;
}

NetMode NetworkPolicy::modeFor(NetProfile p) const {
    switch (p) {
        case NetProfile::Domain:  return domain;
        case NetProfile::Private: return priv;
        case NetProfile::Public:  return pub;
        // If Windows won't say what it's on, use the untrusted answer rather
        // than guessing generously.
        case NetProfile::Unknown: return pub;
    }
    return pub;
}

js::Value NetworkPolicy::toJson() const {
    js::Value o = js::Value::object();
    o["domain"] = netModeName(domain);
    o["private"] = netModeName(priv);
    o["public"] = netModeName(pub);
    return o;
}

void NetworkPolicy::applyFromJson(const js::Value& v) {
    if (!v.isObj()) return;
    domain = netModeFromName(v.str("domain"), domain);
    priv = netModeFromName(v.str("private"), priv);
    pub = netModeFromName(v.str("public"), pub);
}

std::string newId() {
    static std::mt19937_64 rng{std::random_device{}()};
    static const char* hex = "0123456789abcdef";
    auto nib = [&] { return hex[rng() & 0xF]; };
    std::string out;
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) out += '-';
        else if (i == 14) out += '4';                       // version 4
        else if (i == 19) out += hex[8 + (rng() & 0x3)];     // variant
        else out += nib();
    }
    return out;
}

std::string resourceRoot() {
    // Built into build\ during development, but shipped next to public\ -
    // accept both rather than making the folder layout load-bearing.
    const std::string here = exeDir();
    if (dirExists(joinPath(here, "public"))) return here;
    const std::string up = parentDir(here);
    if (dirExists(joinPath(up, "public"))) return up;
    return here;
}

namespace {

struct MessageColour {
    const char* hex;
    int r, g, b;
};
// msg-red, msg-green, msg-blue, msg-ochre, msg-plum, msg-spruce, msg-walnut.
const MessageColour kMessageColours[] = {
    {"#b4483a", 0xb4, 0x48, 0x3a}, {"#4f7a52", 0x4f, 0x7a, 0x52},
    {"#3d6b8e", 0x3d, 0x6b, 0x8e}, {"#9a6a2e", 0x9a, 0x6a, 0x2e},
    {"#7a5a7e", 0x7a, 0x5a, 0x7e}, {"#3f7a74", 0x3f, 0x7a, 0x74},
    {"#6f5a48", 0x6f, 0x5a, 0x48},
};
// The colours of the buttons Dental Comms shipped before, and the colour
// with the same meaning now.
const char* const kOldColours[][2] = {
    {"#d64545", "#b4483a"}, {"#c0392b", "#b4483a"}, {"#2f9e44", "#4f7a52"},
    {"#1c7ed6", "#3d6b8e"}, {"#e8890c", "#9a6a2e"}, {"#7048b6", "#7a5a7e"},
    {"#0ca678", "#3f7a74"},
};

}  // namespace

std::string nearestMessageColour(const std::string& in) {
    if (!isHexColor(in)) return "#3d6b8e";   // msg-blue, the default
    const std::string hex = lower(in);
    for (const auto& c : kMessageColours)
        if (hex == c.hex) return c.hex;
    for (const auto& o : kOldColours)
        if (hex == o[0]) return o[1];
    const int r = std::stoi(hex.substr(1, 2), nullptr, 16);
    const int g = std::stoi(hex.substr(3, 2), nullptr, 16);
    const int b = std::stoi(hex.substr(5, 2), nullptr, 16);
    // "Redmean": a cheap distance that weighs the channels roughly the way
    // eyes do, so a pinkish red lands on terracotta rather than plum.
    const char* best = kMessageColours[0].hex;
    long bestD = -1;
    for (const auto& c : kMessageColours) {
        const long rm = (r + c.r) / 2, dr = r - c.r, dg = g - c.g, db = b - c.b;
        const long d = (((512 + rm) * dr * dr) >> 8) + 4 * dg * dg + (((767 - rm) * db * db) >> 8);
        if (bestD < 0 || d < bestD) {
            bestD = d;
            best = c.hex;
        }
    }
    return best;
}

Config Config::defaults() {
    Config c;
    c.officeName = "Our Office";
    c.stations = {{"front", "Front Desk"}, {"room1", "Room 1"},
                  {"room2", "Room 2"},     {"room3", "Room 3"},
                  {"office", "Back Office"}, {"lab", "Lab"}};
    c.presets = {{"doctor", "Doctor needed", "#b4483a", true},          // msg-red
                 {"ready", "Patient is ready", "#4f7a52", false},       // msg-green
                 {"phone", "Phone call for you", "#3d6b8e", false},     // msg-blue
                 {"assist", "Please assist", "#9a6a2e", false},         // msg-ochre
                 {"checkout", "Ready for checkout", "#7a5a7e", false},  // msg-plum
                 {"comehere", "Please come here", "#3f7a74", false}};   // msg-spruce
    return c;
}

js::Value Config::toJson() const {
    js::Value out = js::Value::object();
    out["officeName"] = officeName;
    out["networks"] = networks.toJson();
    out["escalateAfterSeconds"] = escalateAfterSeconds;
    out["expireAfterSeconds"] = expireAfterSeconds;
    out["notifySound"] = notifySound;
    out["notifyFlash"] = notifyFlash;
    out["notifyTaskbar"] = notifyTaskbar;
    out["startMinimized"] = startMinimized;
    out["welcomed"] = welcomed;
    if (port > 0) out["port"] = port;
    js::Value hosts = js::Value::array();
    for (const auto& h : allowedHosts) hosts.push(h);
    out["allowedHosts"] = hosts;
    js::Value sts = js::Value::array();
    for (const auto& s : stations) {
        js::Value o = js::Value::object();
        o["id"] = s.id;
        o["name"] = s.name;
        sts.push(o);
    }
    out["stations"] = sts;
    js::Value prs = js::Value::array();
    for (const auto& p : presets) {
        js::Value o = js::Value::object();
        o["id"] = p.id;
        o["label"] = p.label;
        o["color"] = p.color;
        o["urgent"] = p.urgent;
        prs.push(o);
    }
    out["presets"] = prs;
    return out;
}

bool Config::applyFromJson(const js::Value& body, std::string& error) {
    const js::Value* sts = body.find("stations");
    const js::Value* prs = body.find("presets");
    if (!sts || !sts->isArr() || sts->arr.empty() || !prs || !prs->isArr() ||
        prs->arr.empty()) {
        error = "need at least one station and one quick message";
        return false;
    }

    // The settings page knows about none of these, so they are carried over
    // rather than reset by a routine "rename a room" save.
    Config next;
    next.networks = networks;
    next.escalateAfterSeconds = escalateAfterSeconds;
    next.expireAfterSeconds = expireAfterSeconds;
    next.allowedHosts = allowedHosts;
    next.port = port;
    next.welcomed = welcomed;
    next.officeName = clampUtf8(jsStringOr(body.find("officeName"), "Our Office"), 60);

    // The settings page does send these, but a key that is absent must mean
    // "carry it over", not "off". An admin page left open in a browser tab
    // since before this option existed would otherwise mute the whole office
    // the next time someone renamed a room.
    next.notifySound = body.find("notifySound") ? jsTruthy(body.find("notifySound"))
                                                : notifySound;
    next.notifyFlash = body.find("notifyFlash") ? jsTruthy(body.find("notifyFlash"))
                                                : notifyFlash;
    next.notifyTaskbar = body.find("notifyTaskbar")
                             ? jsTruthy(body.find("notifyTaskbar"))
                             : notifyTaskbar;
    // Carried over the same way, and for the same reason: this one belongs to
    // the server computer, and the settings page has no business resetting it
    // because somebody renamed a room from a different machine.
    next.startMinimized = body.find("startMinimized")
                              ? jsTruthy(body.find("startMinimized"))
                              : startMinimized;

    for (size_t i = 0; i < sts->arr.size() && i < 40; ++i) {
        const js::Value& s = sts->arr[i];
        Station st;
        st.id = clampUtf8(jsStringOr(s.find("id"), newId()), 40);
        st.name = clampUtf8(jsStringOr(s.find("name"), "Station"), 40);
        next.stations.push_back(std::move(st));
    }
    for (size_t i = 0; i < prs->arr.size() && i < 20; ++i) {
        const js::Value& p = prs->arr[i];
        Preset pr;
        pr.id = clampUtf8(jsStringOr(p.find("id"), newId()), 40);
        pr.label = clampUtf8(jsStringOr(p.find("label"), "Message"), 60);
        const std::string color = p.str("color");
        pr.color = nearestMessageColour(color);
        pr.urgent = jsTruthy(p.find("urgent"));
        next.presets.push_back(std::move(pr));
    }

    *this = std::move(next);
    return true;
}

std::string Config::stationName(const std::string& id) const {
    if (id == "all") return "Everyone";
    for (const auto& s : stations)
        if (s.id == id) return s.name;
    return id;  // an id we no longer know still reads as something
}

bool Config::hasStation(const std::string& id) const {
    for (const auto& s : stations)
        if (s.id == id) return true;
    return false;
}

const Preset* Config::findPreset(const std::string& id) const {
    for (const auto& p : presets)
        if (p.id == id) return &p;
    return nullptr;
}

std::string dataDir() { return joinPath(resourceRoot(), "data"); }

bool parseConfig(const std::string& text, Config& c) {
    std::string err;
    js::Value v = js::Value::parse(text, &err);
    if (!err.empty() || !v.isObj()) return false;

    c = Config();
    c.officeName = v.str("officeName", "Our Office");
    if (const js::Value* nets = v.find("networks")) c.networks.applyFromJson(*nets);
    c.escalateAfterSeconds = clampSeconds(v.integer("escalateAfterSeconds", 120));
    c.expireAfterSeconds = clampSeconds(v.integer("expireAfterSeconds", 3600));
    // Absent in files written before these existed, and on is what those
    // offices had: the chime always rang.
    c.notifySound = v.boolean("notifySound", true);
    c.notifyFlash = v.boolean("notifyFlash", true);
    c.notifyTaskbar = v.boolean("notifyTaskbar", true);
    c.startMinimized = v.boolean("startMinimized", false);
    // A file without it was written by an office already up and running.
    c.welcomed = v.boolean("welcomed", true);
    const int port = v.integer("port", 0);
    c.port = (port > 0 && port <= 65535) ? port : 0;
    if (const js::Value* hosts = v.find("allowedHosts")) {
        for (const auto& h : hosts->arr) {
            const std::string name = lower(trim(h.asStr()));
            if (!name.empty()) c.allowedHosts.push_back(name);
        }
    }
    if (const js::Value* sts = v.find("stations")) {
        for (const auto& s : sts->arr) {
            Station st;
            st.id = s.str("id");
            st.name = s.str("name", "Station");
            if (!st.id.empty()) c.stations.push_back(std::move(st));
        }
    }
    if (const js::Value* prs = v.find("presets")) {
        for (const auto& p : prs->arr) {
            Preset pr;
            pr.id = p.str("id");
            pr.label = p.str("label", "Message");
            const std::string color = p.str("color");
            pr.color = nearestMessageColour(color);
            pr.urgent = p.boolean("urgent");
            if (!pr.id.empty()) c.presets.push_back(std::move(pr));
        }
    }
    // An office with no stations or no buttons has an unusable screen. The
    // settings page can't create that, so it means the file was hand-edited.
    if (c.stations.empty()) c.stations = Config::defaults().stations;
    if (c.presets.empty()) c.presets = Config::defaults().presets;
    return true;
}

namespace {

// "2026-10-26-143005", for naming a damaged file that is being set aside.
std::string fileStamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%04d-%02d-%02d-%02d%02d%02d",
                t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

// Can anything be saved here at all? Asked by writing, because nothing else
// gives a straight answer: folder permissions, a read-only USB stick and
// Controlled Folder Access all only show up when a write fails.
bool canWriteIn(const std::string& dir) {
    ensureDir(dir);
    const std::string probe = joinPath(dir, ".write-test");
    if (!writeFileAtomic(probe, "ok")) return false;
    DeleteFileW(widen(probe).c_str());
    return true;
}

}  // namespace

Config loadConfigFrom(const std::string& dir, ConfigReport* report) {
    ConfigReport local;
    ConfigReport& r = report ? *report : local;
    r = ConfigReport();

    const std::string path = joinPath(dir, "config.json");
    const std::string backup = joinPath(dir, "config.backup.json");

    std::string text;
    if (!readFile(path, text)) {
        Config c = Config::defaults();
        r.origin = ConfigOrigin::Created;
        r.writable = saveConfigTo(dir, c);
        return c;
    }

    Config c;
    if (parseConfig(text, c)) {
        r.origin = ConfigOrigin::Loaded;
        r.writable = canWriteIn(dir);
        return c;
    }

    // Damaged. Set it aside under a dated name rather than leaving it where
    // the next save would overwrite it - whatever was in it stays findable.
    r.damagedCopy = "config.damaged-" + fileStamp() + ".json";
    MoveFileExW(widen(path).c_str(), widen(joinPath(dir, r.damagedCopy)).c_str(),
                MOVEFILE_REPLACE_EXISTING);

    std::string saved;
    if (readFile(backup, saved) && parseConfig(saved, c)) {
        r.origin = ConfigOrigin::RestoredFromBackup;
    } else {
        c = Config::defaults();
        r.origin = ConfigOrigin::ResetDamaged;
    }
    // Not a new office either way: it was set up once, and only the file
    // broke. The window's checklist says what happened instead of a Welcome.
    c.welcomed = true;
    r.writable = saveConfigTo(dir, c);
    return c;
}

bool saveConfigTo(const std::string& dir, const Config& cfg) {
    ensureDir(dir);
    const std::string data = cfg.toJson().dump(2);
    if (!writeFileAtomic(joinPath(dir, "config.json"), data)) return false;
    // A copy of the last file Dental Comms itself wrote, so a hand edit that
    // breaks config.json - or a power cut mid-write on an old disk - is undone
    // by the next start rather than costing the office its rooms.
    writeFileAtomic(joinPath(dir, "config.backup.json"), data);
    return true;
}

Config loadConfig(ConfigReport* report) { return loadConfigFrom(dataDir(), report); }

bool saveConfig(const Config& cfg) { return saveConfigTo(dataDir(), cfg); }

}  // namespace on
