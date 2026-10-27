// Dental Comms self-test - every non-networking subsystem, checked against the
// behaviour of the Node server this replaced.
#include "http.h"  // must precede <windows.h> users

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "checks.h"
#include "config.h"
#include "firewall.h"
#include "network.h"
#include "json.h"
#include "routes.h"
#include "shortcut.h"
#include "state.h"
#include "util.h"

static int g_pass = 0, g_fail = 0;

static void section(const char* name) { std::printf("\n%s\n", name); }

static void check(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
        std::printf("  [ok]   %s\n", what.c_str());
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", what.c_str());
    }
}

static void note(const std::string& text) {
    std::printf("         (%s)\n", text.c_str());
}

// A settings edit as the admin page would send it.
static js::Value makeConfigBody(int stations, int presets) {
    js::Value b = js::Value::object();
    b["officeName"] = "Test Office";
    js::Value sts = js::Value::array();
    for (int i = 0; i < stations; ++i) {
        js::Value s = js::Value::object();
        s["id"] = "s" + std::to_string(i);
        s["name"] = "Station " + std::to_string(i);
        sts.push(s);
    }
    b["stations"] = sts;
    js::Value prs = js::Value::array();
    for (int i = 0; i < presets; ++i) {
        js::Value p = js::Value::object();
        p["id"] = "p" + std::to_string(i);
        p["label"] = "Button " + std::to_string(i);
        p["color"] = "#112233";
        p["urgent"] = false;
        prs.push(p);
    }
    b["presets"] = prs;
    return b;
}

int main() {
    std::printf("Dental Comms self-test\n");
    std::printf("resource root: %s\n", on::resourceRoot().c_str());

    // ---------------------------------------------------------------------
    section("Default settings");
    {
        on::Config c = on::Config::defaults();
        check(c.officeName == "Our Office", "office has a default name");
        check(c.stations.size() == 6, "six default stations");
        check(c.presets.size() == 6, "six default quick messages");
        check(c.stations[0].id == "front" && c.stations[0].name == "Front Desk",
              "the front desk is first");
        const on::Preset* doc = c.findPreset("doctor");
        check(doc && doc->urgent, "'Doctor needed' is marked urgent");
        check(doc && doc->color == "#d64545", "it kept its red");
        check(c.findPreset("nope") == nullptr, "an unknown button is not found");
        check(c.hasStation("lab"), "the lab is a station");
        check(!c.hasStation("moon"), "the moon is not");
        check(c.stationName("all") == "Everyone", "'all' reads as Everyone");
        check(c.stationName("room2") == "Room 2", "an id resolves to its name");
        check(c.stationName("ghost") == "ghost",
              "an id we no longer know still prints as something");
    }

    // ---------------------------------------------------------------------
    section("Editing settings");
    {
        on::Config c = on::Config::defaults();
        std::string err;

        js::Value empty = js::Value::object();
        empty["stations"] = js::Value::array();
        empty["presets"] = js::Value::array();
        check(!c.applyFromJson(empty, err), "refuses an office with no stations");
        check(err == "need at least one station and one quick message",
              "and says exactly why");

        js::Value noPresets = makeConfigBody(2, 0);
        err.clear();
        check(!c.applyFromJson(noPresets, err),
              "refuses an office with no quick messages");

        check(c.stations.size() == 6,
              "a rejected edit leaves the old settings alone");

        js::Value good = makeConfigBody(3, 2);
        err.clear();
        check(c.applyFromJson(good, err), "accepts a sensible edit");
        check(c.officeName == "Test Office", "keeps the office name");
        check(c.stations.size() == 3 && c.presets.size() == 2,
              "keeps the stations and buttons");

        // Clamping, exactly as the Node version did it.
        js::Value big = makeConfigBody(60, 40);
        big["officeName"] = std::string(200, 'x');
        err.clear();
        c.applyFromJson(big, err);
        check(c.stations.size() == 40, "caps stations at 40");
        check(c.presets.size() == 20, "caps quick messages at 20");
        check(c.officeName.size() == 60, "caps the office name at 60");

        js::Value odd = makeConfigBody(1, 1);
        odd["presets"].arr[0]["color"] = "not-a-color";
        odd["presets"].arr[0].erase("label");
        odd["stations"].arr[0].erase("name");
        odd["presets"].arr[0]["urgent"] = 1;  // truthy, but not a bool
        err.clear();
        c.applyFromJson(odd, err);
        check(c.presets[0].color == "#1c7ed6", "a bad colour falls back to blue");
        check(c.presets[0].label == "Message", "a missing label gets a default");
        check(c.stations[0].name == "Station", "a missing station name too");
        check(c.presets[0].urgent, "a truthy non-boolean counts as urgent");

        js::Value noId = makeConfigBody(1, 1);
        noId["stations"].arr[0].erase("id");
        err.clear();
        c.applyFromJson(noId, err);
        check(!c.stations[0].id.empty(), "a station with no id is given one");
        check(c.stations[0].id.size() == 36, "and it looks like a uuid");
    }

    // ---------------------------------------------------------------------
    section("Text handling");
    {
        on::Config c = on::Config::defaults();
        std::string err;
        js::Value body = makeConfigBody(1, 1);
        // 30 three-byte characters = 90 bytes; the 60-byte cap lands mid-run.
        std::string wide;
        for (int i = 0; i < 30; ++i) wide += "\xe4\xb8\xad";
        body["officeName"] = wide;
        c.applyFromJson(body, err);
        check(c.officeName.size() <= 60, "a long name is trimmed to the cap");
        check(c.officeName.size() % 3 == 0,
              "trimming never splits a character in half");
        note("trimmed " + std::to_string(wide.size()) + " bytes to " +
             std::to_string(c.officeName.size()));
    }

    // ---------------------------------------------------------------------
    section("Settings file");
    {
        // The real config.json is restored at the end of this block.
        const std::string path =
            joinPath(joinPath(on::resourceRoot(), "data"), "config.json");
        std::string original;
        const bool had = readFile(path, original);

        on::Config c = on::Config::defaults();
        c.officeName = "Round Trip";
        c.stations = {{"a", "Alpha"}, {"b", "Bravo"}};
        check(on::saveConfig(c), "settings save");

        on::Config back = on::loadConfig();
        check(back.officeName == "Round Trip", "the name survives a reload");
        check(back.stations.size() == 2, "the stations survive");
        check(back.stations[1].name == "Bravo", "in the right order");
        check(back.presets.size() == 6, "so do the quick messages");

        std::string dumped = c.toJson().dump(2);
        std::string parseErr;
        js::Value reparsed = js::Value::parse(dumped, &parseErr);
        check(parseErr.empty() && reparsed.isObj(),
              "the saved file is valid JSON");
        check(reparsed.find("officeName") && reparsed.find("stations") &&
                  reparsed.find("presets"),
              "with the three keys the Node version wrote");

        // The settings only this file can change survive a save and reload.
        on::Config tuned = on::Config::defaults();
        tuned.escalateAfterSeconds = 30;
        tuned.expireAfterSeconds = 300;
        tuned.allowedHosts = {"dentalcomms.local"};
        on::saveConfig(tuned);
        on::Config reread = on::loadConfig();
        check(reread.escalateAfterSeconds == 30 && reread.expireAfterSeconds == 300,
              "the unanswered-alert delays reload");
        check(reread.allowedHosts.size() == 1 &&
                  reread.allowedHosts[0] == "dentalcomms.local",
              "so do the extra host names");

        js::Value silly = js::Value::object();
        silly["escalateAfterSeconds"] = -5;
        silly["expireAfterSeconds"] = 99999999;
        writeFileAtomic(path, silly.dump(2));
        on::Config clamped = on::loadConfig();
        check(clamped.escalateAfterSeconds == 0, "a negative delay reads as off");
        check(clamped.expireAfterSeconds == 86400, "and a silly one is capped");

        if (had) writeFileAtomic(path, original);
        else on::saveConfig(on::Config::defaults());
        check(true, "the office's real settings were put back");
    }

    // ---------------------------------------------------------------------
    section("Sending a message");
    {
        on::Hub hub;
        hub.start();
        std::vector<on::Signal> made;
        std::string err;

        js::Value bad = js::Value::object();
        bad["to"] = "room1";
        check(!hub.createSignals(bad, made, err), "refuses a message from nobody");
        check(err == "missing fields", "with the Node server's wording");

        made.clear();
        err.clear();
        js::Value noTarget = js::Value::object();
        noTarget["from"] = "front";
        noTarget["presetId"] = "doctor";
        check(!hub.createSignals(noTarget, made, err),
              "refuses a message to nobody");

        made.clear();
        err.clear();
        js::Value blank = js::Value::object();
        blank["from"] = "front";
        blank["to"] = "room1";
        check(!hub.createSignals(blank, made, err),
              "refuses an empty message with no button");

        made.clear();
        err.clear();
        js::Value good = js::Value::object();
        good["from"] = "front";
        good["to"] = "room1";
        good["presetId"] = "doctor";
        check(hub.createSignals(good, made, err), "sends a quick message");
        check(made.size() == 1, "one alert for one room");
        check(made[0].label == "Doctor needed", "carries the button's words");
        check(made[0].color == "#d64545", "and its colour");
        check(made[0].urgent, "and its urgency");
        check(made[0].presetId == "doctor", "and which button it was");
        check(made[0].id.size() == 36, "gets an id");
        check(made[0].time > 0, "and a timestamp");

        made.clear();
        err.clear();
        js::Value typed = js::Value::object();
        typed["from"] = "front";
        typed["to"] = "room1";
        typed["note"] = std::string(500, 'z');
        check(hub.createSignals(typed, made, err), "sends a typed note");
        check(made[0].label == "Message", "a note with no button reads 'Message'");
        check(made[0].note.size() == 200, "a long note is trimmed to 200");
        check(made[0].presetId.empty(), "and has no button id");

        made.clear();
        err.clear();
        js::Value many = js::Value::object();
        many["from"] = "front";
        js::Value to = js::Value::array();
        to.push(std::string("room1"));
        to.push(std::string("room2"));
        to.push(std::string("nowhere"));  // dropped, not an error
        many["to"] = to;
        many["presetId"] = "ready";
        check(hub.createSignals(many, made, err), "sends to several rooms");
        check(made.size() == 2, "unknown rooms are quietly dropped");
        check(made[0].to == "room1" && made[1].to == "room2",
              "the real rooms both got one");

        made.clear();
        err.clear();
        js::Value all = js::Value::object();
        all["from"] = "lab";
        all["to"] = "all";
        all["presetId"] = "ready";
        check(hub.createSignals(all, made, err), "sends to everyone");
        check(made.size() == 1 && made[0].to == "all",
              "'Everyone' is one alert, not one per room");
    }

    // ---------------------------------------------------------------------
    section("Acknowledging and cancelling");
    {
        on::Hub hub;
        hub.start();
        std::vector<on::Signal> made;
        std::string err;
        js::Value send = js::Value::object();
        send["from"] = "front";
        send["to"] = "room1";
        send["presetId"] = "doctor";
        hub.createSignals(send, made, err);
        const std::string id = made[0].id;

        js::Value state = hub.stateJson();
        check(state.find("signals")->arr.size() == 1, "the alert is waiting");

        hub.ackSignal(id, "room1");
        state = hub.stateJson();
        check(state.find("signals")->arr.empty(), "tapping Got it clears it");
        check(state.find("recent")->arr.size() == 2,
              "both the send and the ack are logged");

        hub.ackSignal("no-such-id", "room1");
        check(true, "acknowledging a vanished alert is harmless");

        made.clear();
        err.clear();
        hub.createSignals(send, made, err);
        hub.cancelSignal(made[0].id);
        state = hub.stateJson();
        check(state.find("signals")->arr.empty(), "the sender can cancel it");

        // The log is a rolling window, not an archive.
        for (int i = 0; i < 60; ++i) {
            std::vector<on::Signal> tmp;
            std::string e;
            hub.createSignals(send, tmp, e);
        }
        state = hub.stateJson();
        check(state.find("recent")->arr.size() == 50,
              "the activity log stops at 50 entries");
        check(state.find("signals")->arr.size() == 60,
              "but unacknowledged alerts all stay");
    }

    // ---------------------------------------------------------------------
    section("What the browser asks for");
    {
        on::Hub hub;
        hub.start();
        js::Value state = hub.stateJson();
        check(state.find("config") != nullptr, "state carries the settings");
        check(state.find("signals") != nullptr, "and the live alerts");
        check(state.find("recent") != nullptr, "and the recent activity");
        const js::Value* cfg = state.find("config");
        check(cfg->find("officeName") && cfg->find("stations") &&
                  cfg->find("presets"),
              "the settings have the shape the page expects");

        js::Value payload = js::Value::object();
        payload["id"] = "x";
        check(on::sseFrame("signal-new", payload.dump(0)) ==
                  "event: signal-new\ndata: {\"id\":\"x\"}\n\n",
              "an event frame matches the Server-Sent Events format");
    }

    // ---------------------------------------------------------------------
    section("Serving files");
    {
        check(on::mimeFor("a/b/index.html") == "text/html; charset=utf-8",
              "html");
        check(on::mimeFor("style.css") == "text/css; charset=utf-8", "css");
        check(on::mimeFor("app.js") == "text/javascript; charset=utf-8", "js");
        check(on::mimeFor("icon-192.PNG") == "image/png",
              "png, whatever the capitals");
        check(on::mimeFor("manifest.json") == "application/json", "json");
        check(on::mimeFor("odd.xyz") == "application/octet-stream",
              "anything else is downloaded, not run");

        const std::string pub = joinPath(on::resourceRoot(), "public");
        std::string full;
        check(on::resolveStaticPath("/style.css", pub, full), "finds a real file");
        check(icontains(full, "public"), "inside the public folder");
        check(on::resolveStaticPath("/", pub, full) && icontains(full, "index.html"),
              "the bare address serves the front page");

        check(!on::resolveStaticPath("/../data/config.json", pub, full),
              "refuses to climb out of public/");
        check(!on::resolveStaticPath("/..\\data\\config.json", pub, full),
              "including with Windows slashes");
        check(!on::resolveStaticPath("/a/../../secret.txt", pub, full),
              "and when the climb is buried mid-path");
        check(!on::resolveStaticPath("/C:/Windows/win.ini", pub, full),
              "refuses an absolute drive path");

        std::string bytes;
        check(on::resolveStaticPath("/index.html", pub, full) &&
                  readFile(full, bytes) && !bytes.empty(),
              "the real front page is readable");
        check(bytes.find("<") != std::string::npos, "and looks like a web page");
    }

    // ---------------------------------------------------------------------
    section("Who has a screen open");
    {
        on::Hub hub;
        hub.start();
        check(hub.listeningJson().arr.empty(),
              "with no browsers open, no room is listening");

        auto one = std::make_shared<on::SseClient>();
        one->station = "room1";
        auto two = std::make_shared<on::SseClient>();
        two->station = "room1";  // a second browser in the same room
        auto three = std::make_shared<on::SseClient>();
        three->station = "front";
        auto unset = std::make_shared<on::SseClient>();  // hasn't picked yet
        hub.addClient(one);
        hub.addClient(two);
        hub.addClient(three);
        hub.addClient(unset);

        js::Value who = hub.listeningJson();
        check(who.arr.size() == 2, "two rooms have a screen open");
        bool hasRoom1 = false, hasFront = false;
        for (const auto& v : who.arr) {
            if (v.asStr() == "room1") hasRoom1 = true;
            if (v.asStr() == "front") hasFront = true;
        }
        check(hasRoom1 && hasFront, "and they are the right two");
        check(who.arr.size() == 2,
              "a browser that has not picked a station counts for nobody");

        hub.removeClient(one);
        check(hub.listeningJson().arr.size() == 2,
              "closing one of two browsers leaves the room still listening");
        hub.removeClient(two);
        who = hub.listeningJson();
        check(who.arr.size() == 1 && who.arr[0].asStr() == "front",
              "closing the last one marks that room empty");

        js::Value state = hub.stateJson();
        check(state.find("listening") != nullptr,
              "a page loading fresh is told who is listening");
    }

    // ---------------------------------------------------------------------
    section("Alerts nobody answers");
    {
        on::Config cfg = on::Config::defaults();
        cfg.escalateAfterSeconds = 1;
        cfg.expireAfterSeconds = 0;  // not being tested here
        on::Hub hub;
        hub.start(cfg);

        std::vector<on::Signal> made;
        std::string err;
        js::Value send = js::Value::object();
        send["from"] = "front";
        send["to"] = "room1";
        send["presetId"] = "doctor";
        check(hub.createSignals(send, made, err), "an alert goes out");

        hub.sweepNow();
        js::Value state = hub.stateJson();
        check(!state.find("signals")->arr[0].boolean("stale"),
              "a fresh alert is not overdue yet");

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        hub.sweepNow();
        state = hub.stateJson();
        check(state.find("signals")->arr.size() == 1,
              "an overdue alert is still there to be answered");
        check(state.find("signals")->arr[0].boolean("stale"),
              "but it is now marked unanswered");
        check(icontains(state.find("recent")->arr[0].str("text"),
                        "has not answered"),
              "and the activity log says which room");

        hub.sweepNow();
        state = hub.stateJson();
        check(state.find("recent")->arr.size() == 2,
              "the sender is told once, not on every pass");
    }

    // ---------------------------------------------------------------------
    section("Alerts nobody ever answers");
    {
        on::Config cfg = on::Config::defaults();
        cfg.escalateAfterSeconds = 0;
        cfg.expireAfterSeconds = 1;
        on::Hub hub;
        hub.start(cfg);

        std::vector<on::Signal> made;
        std::string err;
        js::Value send = js::Value::object();
        send["from"] = "front";
        send["to"] = "room1";
        send["presetId"] = "doctor";
        hub.createSignals(send, made, err);

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        hub.sweepNow();
        js::Value state = hub.stateJson();
        check(state.find("signals")->arr.empty(),
              "an alert nobody ever saw is eventually given up on");
        check(icontains(state.find("recent")->arr[0].str("text"), "never saw"),
              "and the log records that it went unseen");

        // Turning both off is how an office says "leave my alerts alone".
        on::Config never = on::Config::defaults();
        never.escalateAfterSeconds = 0;
        never.expireAfterSeconds = 0;
        on::Hub patient;
        patient.start(never);
        made.clear();
        patient.createSignals(send, made, err);
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        patient.sweepNow();
        check(patient.stateJson().find("signals")->arr.size() == 1,
              "with both set to 0 an alert waits forever");
    }

    // ---------------------------------------------------------------------
    section("Deleting a station");
    {
        // updateConfig writes to disk, so the office's real file is put back.
        const std::string path =
            joinPath(joinPath(on::resourceRoot(), "data"), "config.json");
        std::string original;
        const bool had = readFile(path, original);

        on::Hub hub;
        hub.start();
        std::vector<on::Signal> made;
        std::string err;
        js::Value send = js::Value::object();
        send["from"] = "front";
        send["to"] = "room1";
        send["presetId"] = "doctor";
        hub.createSignals(send, made, err);
        check(hub.stateJson().find("signals")->arr.size() == 1,
              "an alert is waiting on Room 1");

        // A settings save that no longer has room1 in it.
        js::Value edit = makeConfigBody(2, 2);
        check(hub.updateConfig(edit, err), "the rooms are renamed");
        check(hub.stateJson().find("signals")->arr.empty(),
              "an alert left waiting on a deleted room is cleared, not stranded");

        if (had) writeFileAtomic(path, original);
        else on::saveConfig(on::Config::defaults());
        check(true, "the office's real settings were put back");
    }

    // ---------------------------------------------------------------------
    section("Who Dental Comms will answer");
    {
        const std::vector<std::string> none;
        const std::vector<std::string> extra = {"dentalcomms.local"};

        check(on::hostOnly("192.168.1.25:3272") == "192.168.1.25",
              "the port comes off a Host header");
        check(on::hostOnly("[::1]:3272") == "::1", "and the brackets off IPv6");
        check(on::hostOnly("reception-pc") == "reception-pc",
              "a bare name is left alone");

        check(on::hostIsOurs("localhost:3272", "192.168.1.25", "RECEPTION-PC", none),
              "localhost is us");
        check(on::hostIsOurs("192.168.1.25:3272", "192.168.1.25", "RECEPTION-PC", none),
              "so is the address the connection came in on");
        check(on::hostIsOurs("RECEPTION-PC:3272", "192.168.1.25", "RECEPTION-PC", none),
              "so is this computer's name");
        check(on::hostIsOurs("reception-pc:3272", "192.168.1.25", "RECEPTION-PC", none),
              "whatever the capitals");
        check(on::hostIsOurs("reception-pc.office.lan", "192.168.1.25", "RECEPTION-PC", none),
              "including with the domain tacked on");
        check(on::hostIsOurs("dentalcomms.local", "192.168.1.25", "RECEPTION-PC", extra),
              "and any name the office added to allowedHosts");

        check(!on::hostIsOurs("evil.example", "192.168.1.25", "RECEPTION-PC", none),
              "a name pointed here from somebody else's DNS is refused");
        check(!on::hostIsOurs("192.168.1.99:3272", "192.168.1.25", "RECEPTION-PC", none),
              "so is an address this connection did not arrive on");
        check(!on::hostIsOurs("", "192.168.1.25", "RECEPTION-PC", none),
              "and a request with no Host at all");

        check(on::originIsOurs("", "192.168.1.25:3272"),
              "no Origin means the office's own page");
        check(on::originIsOurs("http://192.168.1.25:3272", "192.168.1.25:3272"),
              "an Origin naming us is fine");
        check(!on::originIsOurs("http://evil.example", "192.168.1.25:3272"),
              "an Origin naming somewhere else is not");
        check(!on::originIsOurs("null", "192.168.1.25:3272"),
              "nor is the one a sandboxed page sends");

        check(on::bodyIsJson("application/json"), "the browser's own posts pass");
        check(on::bodyIsJson("application/json; charset=utf-8"),
              "with a charset on the end too");
        check(!on::bodyIsJson("text/plain"),
              "a form post dressed as text is refused");
        check(!on::bodyIsJson("application/x-www-form-urlencoded"),
              "and so is a plain form post");
        check(!on::bodyIsJson(""), "and one with no content type at all");
    }

    // ---------------------------------------------------------------------
    section("Reading the address bar");
    {
        check(on::urlDecode("/a%20b") == "/a b", "a percent escape decodes");
        check(on::urlDecode("/plain") == "/plain", "plain text is untouched");
        check(on::queryParam("station=room1", "station") == "room1",
              "a parameter is found");
        check(on::queryParam("a=1&station=front&b=2", "station") == "front",
              "even between others");
        check(on::queryParam("station=Back%20Office", "station") == "Back Office",
              "and is decoded");
        check(on::queryParam("station=a+b", "station") == "a b",
              "with + read as a space, as query strings go");
        check(on::queryParam("a=1", "station").empty(),
              "a missing parameter is empty");
        check(on::queryParam("", "station").empty(), "so is an empty query");
    }

    // ---------------------------------------------------------------------
    section("Which networks it will answer on");
    {
        on::NetworkPolicy p;
        check(p.modeFor(on::NetProfile::Domain) == on::NetMode::Office,
              "a domain network is the office - staff can connect");
        check(p.modeFor(on::NetProfile::Private) == on::NetMode::Office,
              "so is a private network");
        check(p.modeFor(on::NetProfile::Public) == on::NetMode::LocalOnly,
              "a public network gets this computer only");
        check(p.modeFor(on::NetProfile::Unknown) == on::NetMode::LocalOnly,
              "and so does a network Windows won't name");

        check(on::netModeFromName("office", on::NetMode::Off) == on::NetMode::Office,
              "'office' parses");
        check(on::netModeFromName("local-only", on::NetMode::Off) ==
                  on::NetMode::LocalOnly,
              "'local-only' parses");
        check(on::netModeFromName("OFF", on::NetMode::Office) == on::NetMode::Off,
              "'off' parses whatever the capitals");
        check(on::netModeFromName("nonsense", on::NetMode::Office) ==
                  on::NetMode::Office,
              "an unreadable value keeps the safe existing setting");

        // Round trip through the settings file.
        on::Config c = on::Config::defaults();
        c.networks.pub = on::NetMode::Off;
        c.networks.priv = on::NetMode::LocalOnly;
        std::string dumped = c.toJson().dump(2);
        on::Config back;
        std::string perr;
        js::Value v = js::Value::parse(dumped, &perr);
        back.networks.applyFromJson(*v.find("networks"));
        check(back.networks.pub == on::NetMode::Off, "a saved rule reloads");
        check(back.networks.priv == on::NetMode::LocalOnly, "all three of them");
        check(back.networks.domain == on::NetMode::Office, "including the default");

        // The settings page has no idea this exists; saving a room rename
        // must not quietly reopen the server to a cafe.
        on::Config guard = on::Config::defaults();
        guard.networks.pub = on::NetMode::Off;
        guard.escalateAfterSeconds = 45;
        guard.expireAfterSeconds = 90;
        guard.allowedHosts = {"dentalcomms.local"};
        std::string err;
        js::Value edit = makeConfigBody(2, 2);
        check(guard.applyFromJson(edit, err), "a settings-page save succeeds");
        check(guard.networks.pub == on::NetMode::Off,
              "and leaves the network rule exactly as it was");
        check(guard.escalateAfterSeconds == 45 && guard.expireAfterSeconds == 90,
              "and the unanswered-alert delays");
        check(guard.allowedHosts.size() == 1,
              "and the extra names the office is reached by");

        check(std::string(on::profileName(on::NetProfile::Public)) == "Public",
              "profiles have readable names");
        note(std::string("this computer is on a ") +
             on::profileName(on::currentProfile()) + " network right now");
    }

    // ---------------------------------------------------------------------
    section("Which addresses the office is told to use");
    {
        // Adapters that exist only inside this computer. Offering one of these
        // as the address to bookmark sends someone chasing a connection that
        // can never work.
        check(on::looksVirtualAdapter("vEthernet (Default Switch)"),
              "a Hyper-V switch is virtual");
        check(on::looksVirtualAdapter("vEthernet (WSL (Hyper-V firewall))"),
              "so is the WSL one");
        check(on::looksVirtualAdapter("VirtualBox Host-Only Network"),
              "so is VirtualBox");
        check(on::looksVirtualAdapter("VMware Network Adapter VMnet1"),
              "so is VMware");
        check(on::looksVirtualAdapter("Bluetooth Network Connection"),
              "so is a Bluetooth link");
        check(on::looksVirtualAdapter("VETHERNET (WSL)"),
              "and the test does not care about capitals");

        // The ones a dental office actually connects through.
        check(!on::looksVirtualAdapter("Wi-Fi"), "Wi-Fi is real");
        check(!on::looksVirtualAdapter("Ethernet"), "Ethernet is real");
        check(!on::looksVirtualAdapter("Realtek PCIe GbE Family Controller"),
              "and so is a plain network card");
        check(!on::looksVirtualAdapter("Intel(R) Wi-Fi 6 AX201 160MHz"),
              "and a plain wireless card");

        // 169.254.x.x means the adapter asked for an address and nothing
        // replied. It reaches nothing, so it is never worth printing.
        check(on::isLinkLocalIPv4("169.254.13.153"), "169.254 is link-local");
        check(on::isLinkLocalIPv4("169.254.0.1"), "so is the bottom of it");
        check(!on::isLinkLocalIPv4("192.168.4.132"), "a home LAN address is not");
        check(!on::isLinkLocalIPv4("172.31.176.1"), "nor is a 172 address");
        check(!on::isLinkLocalIPv4("169.253.1.1"), "nor is a near miss");
        check(!on::isLinkLocalIPv4("10.0.0.1"), "nor is a 10.x address");

        // Whatever this machine happens to have, the rule that matters is that
        // nothing useless is ever offered to the office.
        const auto ips = on::localIPv4();
        bool anyLinkLocal = false;
        for (const auto& ip : ips)
            if (on::isLinkLocalIPv4(ip)) anyLinkLocal = true;
        check(!anyLinkLocal, "no link-local address is offered to the office");
        std::string listed;
        for (const auto& ip : ips) listed += (listed.empty() ? "" : ", ") + ip;
        note("this computer would offer: " + (listed.empty() ? "(none)" : listed));

        // The startup window offers three ways in: localhost, this computer's
        // name, and each address above. Every one of them has to be a name the
        // server will actually answer to - printing an address that is then
        // refused with "Dental Comms does not answer to that address" is worse
        // than not printing it, because it looks like the program is broken.
        const std::vector<std::string> none;
        const std::string me = computerName();
        check(!me.empty(), "this computer knows what it is called");
        check(on::hostIsOurs("localhost:3272", "127.0.0.1", me, none),
              "localhost, the first thing it offers, is answered");
        check(on::hostIsOurs(me + ":3272", "192.168.1.25", me, none),
              "the computer name it offers is answered");
        bool everyAddressAnswered = true;
        for (const auto& ip : ips)
            if (!on::hostIsOurs(ip + ":3272", ip, me, none))
                everyAddressAnswered = false;
        check(everyAddressAnswered,
              "and so is every address it offers, reached on that address");
    }

    // ---------------------------------------------------------------------
    section("How alerts announce themselves");
    {
        on::Config c = on::Config::defaults();
        check(c.notifySound && c.notifyFlash && c.notifyTaskbar,
              "a new office gets the chime, the flash and the taskbar blink");

        // Round trip through the file format.
        c.notifyFlash = false;
        js::Value v = c.toJson();
        check(v.boolean("notifySound", false) && !v.boolean("notifyFlash", true) &&
              v.boolean("notifyTaskbar", false),
              "all three settings are written out");

        // A settings-page save carries them.
        std::string err;
        js::Value body = makeConfigBody(2, 2);
        body["notifySound"] = false;
        body["notifyFlash"] = true;
        body["notifyTaskbar"] = false;
        on::Config saved = on::Config::defaults();
        check(saved.applyFromJson(body, err), "a save with all three flags succeeds");
        check(!saved.notifySound, "turning the chime off sticks");
        check(saved.notifyFlash, "and the flash stays on independently");
        check(!saved.notifyTaskbar, "and the taskbar blink is its own switch");

        // The case that matters: an admin page opened before these options
        // existed posts no such keys. Reading those as "off" would mute the
        // office the next time somebody renamed a room.
        on::Config older = on::Config::defaults();
        older.notifySound = true;
        older.notifyFlash = false;
        older.notifyTaskbar = false;
        js::Value oldBody = makeConfigBody(2, 2);   // no notify* keys at all
        check(older.applyFromJson(oldBody, err), "an older page can still save");
        check(older.notifySound && !older.notifyFlash && !older.notifyTaskbar,
              "a save that omits them changes none of them");

        // Reordering: the presets come back in exactly the order they arrived,
        // because that order is the order of the buttons on every screen.
        js::Value reordered = js::Value::object();
        reordered["officeName"] = "Test Office";
        reordered["stations"] = makeConfigBody(1, 1).find("stations") ?
                                *makeConfigBody(1, 1).find("stations") :
                                js::Value::array();
        js::Value prs = js::Value::array();
        for (const char* id : {"third", "first", "second"}) {
            js::Value p = js::Value::object();
            p["id"] = id;
            p["label"] = std::string("Button ") + id;
            p["color"] = "#112233";
            prs.push(p);
        }
        reordered["presets"] = prs;
        on::Config ord = on::Config::defaults();
        check(ord.applyFromJson(reordered, err), "a reordered save succeeds");
        check(ord.presets.size() == 3, "keeps every button");
        if (ord.presets.size() == 3) {
            check(ord.presets[0].id == "third" && ord.presets[1].id == "first" &&
                      ord.presets[2].id == "second",
                  "and keeps them in the order the page sent them");
        }
    }

    // ---------------------------------------------------------------------
    section("Ids");
    {
        std::string a = on::newId(), b = on::newId();
        check(a != b, "two ids differ");
        check(a.size() == 36, "36 characters");
        check(a[8] == '-' && a[13] == '-' && a[18] == '-' && a[23] == '-',
              "dashes in the uuid positions");
        check(a[14] == '4', "version 4");
    }

    // ---------------------------------------------------------------------
    section("Starting minimised");
    {
        on::Config c = on::Config::defaults();
        check(!c.startMinimized,
              "a fresh office starts with a window, not hidden away");

        c.startMinimized = true;
        js::Value out = c.toJson();
        const js::Value* v = out.find("startMinimized");
        check(v && v->isBool() && v->b,
              "the setting is written to config.json");

        // An office that has set it must keep it when someone renames a room
        // from the settings page, which knows nothing about this option.
        js::Value body = makeConfigBody(2, 2);
        std::string err;
        check(c.applyFromJson(body, err),
              "a settings edit that never mentions it still saves");
        check(c.startMinimized,
              "and leaves it alone rather than turning it back off");

        on::Config off = on::Config::defaults();
        js::Value body2 = makeConfigBody(2, 2);
        check(off.applyFromJson(body2, err) && !off.startMinimized,
              "an office that never set it is not switched on by accident");
    }

    // ---------------------------------------------------------------------
    section("The startup shortcut");
    {
        // Why this exists at all: the page's pop-out button calls
        // window.open(), which browsers only honour inside a real click, so it
        // can never fire by itself at sign-in. A browser started with --app=
        // makes the same window with nobody pressing anything.
        //
        // What is handed over is the script that makes the shortcut. Serving
        // the .lnk itself cannot work - Chromium renames it to .download - and
        // the script runs where the answer about which browser is installed is
        // actually true, which is the staff computer rather than this one.
        std::string script, err;
        const bool built = on::buildStartupScript("http://RECEPTION-PC:3272",
                                                  false, script, err);
        check(built, "a script is produced" + (built ? "" : ": " + err));
        if (built) {
            check(script.compare(0, 9, "@echo off") == 0,
                  "that cmd.exe will recognise as a batch file");
            // cmd.exe is not reliable on LF-only files: a goto into a label
            // block is exactly where it comes apart, and this file is all
            // labels.
            check(script.find("\r\n") != std::string::npos &&
                      script.find("\n\n") == std::string::npos,
                  "with CRLF line endings throughout");
            // The console reads this in an OEM code page, so anything above
            // ASCII arrives as mojibake on a staff computer.
            bool ascii = true;
            for (size_t i = 0; i < script.size(); ++i)
                if ((unsigned char)script[i] > 0x7F) ascii = false;
            check(ascii, "and nothing in it above plain ASCII");

            // The address has to be in there, or the shortcut opens the wrong
            // office - and every staff computer would need doing again.
            check(script.find("http://RECEPTION-PC:3272") != std::string::npos,
                  "carrying the address it was asked for");
            check(script.find("--app=%DC_URL%") != std::string::npos,
                  "opening it in its own window rather than a tab");
            check(script.find("Start Menu\\Programs\\Startup") !=
                      std::string::npos,
                  "and putting the shortcut where Windows starts things from");
            check(script.find("del \"%DC_STARTUP%\\Dental Flow.lnk\"") !=
                      std::string::npos,
                  "removing the one made before the rename, so only one "
                  "window opens");

            // A computer with only the other browser still has to end up set
            // up, so both are looked for whichever was asked for.
            check(script.find("msedge.exe") != std::string::npos &&
                      script.find("chrome.exe") != std::string::npos,
                  "looking for either browser, not only the one asked for");
            check(script.find(":nobrowser") != std::string::npos,
                  "and saying so plainly when neither is there");
        }

        std::string chromeFirst;
        check(on::buildStartupScript("http://x:1", true, chromeFirst, err),
              "asking for Chrome also produces a script");
        if (!chromeFirst.empty())
            check(chromeFirst.find("chrome.exe") < chromeFirst.find("msedge.exe"),
                  "with Chrome tried before Edge when Chrome was asked for");

        // The address comes out of the Host header, which is whatever the
        // client chose to send, and it is pasted into a batch file where & is
        // a command separator. Refusing the shape is the whole defence.
        const char* hostile[] = {
            "http://x&calc",
            "http://x|calc",
            "http://x\"&calc&\"",
            "http://x%CD%",
            "http://x\r\nrem",
            "http://x^&calc",
            "https://x",
            "x:1",
            "http://",
        };
        bool allRefused = true;
        for (size_t i = 0; i < sizeof(hostile) / sizeof(*hostile); ++i) {
            std::string ignored, why;
            if (on::buildStartupScript(hostile[i], false, ignored, why))
                allRefused = false;
        }
        check(allRefused,
              "an address that would run commands in the batch file is "
              "refused, not escaped");
    }

    // ---------------------------------------------------------------------
    section("Settings that repair themselves");
    {
        // A scratch folder of its own, so the office's real settings are
        // never touched.
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        const std::string dir =
            joinPath(narrow(tmp), "dc-selftest-" + std::to_string(GetCurrentProcessId()));
        const std::string cfgPath = joinPath(dir, "config.json");
        const std::string bakPath = joinPath(dir, "config.backup.json");

        on::ConfigReport r;
        on::Config fresh = on::loadConfigFrom(dir, &r);
        check(r.origin == on::ConfigOrigin::Created, "a missing file is a new office");
        check(r.writable, "and the folder can be written to");
        check(fileExists(cfgPath), "the defaults are written out");
        check(fileExists(bakPath), "with a backup beside them");
        check(!fresh.welcomed, "a new office has not been welcomed yet");
        check(fresh.port == 0, "and has not picked a port yet");

        fresh.officeName = "Backed Up";
        fresh.port = 3273;
        fresh.welcomed = true;
        check(on::saveConfigTo(dir, fresh), "a save works");
        on::Config again = on::loadConfigFrom(dir, &r);
        check(r.origin == on::ConfigOrigin::Loaded, "the next start just loads it");
        check(again.port == 3273, "the chosen port survives a reload");
        check(again.welcomed, "and so does finishing the welcome");

        // A file from before "welcomed" existed belongs to an office that is
        // already up and running: no Welcome screen after an update.
        writeFileAtomic(cfgPath, "{\"officeName\":\"Old Office\",\"stations\":[],\"presets\":[]}");
        on::Config old = on::loadConfigFrom(dir, &r);
        check(old.welcomed, "an older settings file counts as already welcomed");
        check(old.port == 0, "and has no port of its own until one is picked");

        writeFileAtomic(cfgPath, "{\"port\": 99999}");
        check(on::loadConfigFrom(dir, &r).port == 0, "a port out of range is ignored");

        // Damage it, with a good backup in place - one saved before the
        // welcome was finished, so the repair cannot lean on it for that.
        fresh.welcomed = false;
        on::saveConfigTo(dir, fresh);
        writeFileAtomic(cfgPath, "{ this is not json");
        on::Config restored = on::loadConfigFrom(dir, &r);
        check(r.origin == on::ConfigOrigin::RestoredFromBackup,
              "a damaged file is replaced by the last good copy");
        check(restored.officeName == "Backed Up", "with the office's own rooms in it");
        check(restored.welcomed, "and no Welcome screen for an office already set up");
        check(!r.damagedCopy.empty() && fileExists(joinPath(dir, r.damagedCopy)),
              "and the damaged file is kept under its own name, not overwritten");
        std::string now;
        on::Config reparsed;
        check(readFile(cfgPath, now) && on::parseConfig(now, reparsed),
              "config.json is a good file again");

        // Damage it with no backup to fall back on.
        DeleteFileW(widen(bakPath).c_str());
        writeFileAtomic(cfgPath, "[1, 2, 3]");
        on::Config reset = on::loadConfigFrom(dir, &r);
        check(r.origin == on::ConfigOrigin::ResetDamaged,
              "with no backup either, the defaults are used");
        check(reset.stations.size() == 6, "so the screens still have rooms");
        check(reset.welcomed,
              "and it is not mistaken for a new office - only the file broke");

        // The settings page knows nothing about these two.
        on::Config server = on::Config::defaults();
        server.port = 4000;
        server.welcomed = true;
        std::string err;
        check(server.applyFromJson(makeConfigBody(2, 2), err), "a settings edit saves");
        check(server.port == 4000 && server.welcomed,
              "and leaves the port and the welcome alone");

        for (const auto& f : listFiles(dir, ".json"))
            DeleteFileW(widen(joinPath(dir, f)).c_str());
        RemoveDirectoryW(widen(dir).c_str());
    }

    // ---------------------------------------------------------------------
    section("Firewall judgement");
    {
        const std::string exe = expandEnv("%WINDIR%") + "\\DentalComms.exe";
        auto rule = [](const std::string& app, bool allow, long profiles,
                       bool enabled = true, bool inbound = true) {
            on::FirewallRuleInfo r;
            r.app = app;
            r.allow = allow;
            r.profiles = profiles;
            r.enabled = enabled;
            r.inbound = inbound;
            return r;
        };
        const long priv = 2, pub = 4, all = 0x7FFFFFFF;
        using FS = on::FirewallState;

        check(on::classifyFirewall(false, false, false, priv, {}, exe) == FS::Off,
              "Windows Firewall off reads as off");
        check(on::classifyFirewall(true, false, false, priv, {}, exe) == FS::WillAsk,
              "no rule yet: Windows will ask");
        check(on::classifyFirewall(true, false, false, priv,
                                   {rule(exe, true, priv)}, exe) == FS::Allowed,
              "an allow rule for this network lets it in");
        check(on::classifyFirewall(true, false, false, priv,
                                   {rule(exe, true, pub)}, exe) == FS::WillAsk,
              "one for a different kind of network does not count");
        check(on::classifyFirewall(true, false, false, priv,
                                   {rule(exe, true, priv), rule(exe, false, all)},
                                   exe) == FS::Blocked,
              "a block rule beats an allow rule, as it does in Windows");
        check(on::classifyFirewall(true, false, false, priv,
                                   {rule(exe, true, priv), rule(exe, false, all, false)},
                                   exe) == FS::Allowed,
              "a switched-off block rule is ignored");
        check(on::classifyFirewall(true, false, false, priv,
                                   {rule(exe, false, all, true, false)}, exe) ==
                  FS::WillAsk,
              "an outbound rule is not about staff reaching us");
        check(on::classifyFirewall(true, false, false, priv,
                                   {rule("C:\\Other\\Thing.exe", false, all)},
                                   exe) == FS::WillAsk,
              "another program's rules are left out of it");
        check(on::classifyFirewall(true, false, false, priv,
                                   {rule("%windir%\\DENTALCOMMS.EXE", false, priv)},
                                   exe) == FS::Blocked,
              "paths match whatever their case or %variables%");
        check(on::classifyFirewall(true, true, false, priv,
                                   {rule(exe, true, priv)}, exe) == FS::Blocked,
              "\"block all incoming connections\" blocks it too");
        check(on::classifyFirewall(true, false, true, priv, {}, exe) == FS::Managed,
              "on an IT-managed computer an unasked rule is IT's to decide");
        check(on::classifyFirewall(true, false, true, priv,
                                   {rule(exe, true, priv)}, exe) == FS::Allowed,
              "but an allow rule that is there still counts");
    }

    // ---------------------------------------------------------------------
    section("Network names");
    {
        using NP = on::NetProfile;
        std::vector<on::ConnectedNetwork> nets = {{"OfficeWiFi", NP::Private},
                                                  {"Phone hotspot", NP::Public}};
        check(on::leastTrusted(nets) == NP::Public,
              "office Ethernet plus a hotspot counts as the hotspot");
        check(on::networkNamesAt(nets, NP::Public) == "Phone hotspot",
              "and it is the hotspot that gets named");
        check(on::leastTrusted({}) == NP::Unknown, "nothing connected is unknown");
        nets.push_back({"Guest", NP::Public});
        check(on::networkNamesAt(nets, NP::Public) == "Phone hotspot and Guest",
              "two names read as a sentence");
    }

    // ---------------------------------------------------------------------
    section("The server window's checklist");
    {
        auto healthy = [] {
            on::Facts f;
            f.version = "test";
            f.computer = "RECEPTION-PC";
            f.addresses = {"RECEPTION-PC", "192.168.1.25"};
            f.port = 3272;
            f.profile = on::NetProfile::Private;
            f.mode = on::NetMode::Office;
            f.networkNames = "OfficeWiFi";
            f.firewall = on::FirewallState::Allowed;
            f.runAtSignIn = true;
            f.now = 10'000'000;
            return f;
        };
        auto find = [](const std::vector<on::Check>& cs, on::Fix fix) -> const on::Check* {
            for (const auto& c : cs)
                if (c.fix == fix) return &c;
            return nullptr;
        };
        auto has = [](const std::vector<on::Check>& cs, const std::string& text) {
            for (const auto& c : cs)
                if (c.text.find(text) != std::string::npos) return true;
            return false;
        };

        on::Facts f = healthy();
        auto cs = on::buildChecks(f);
        check(!on::needsAttention(cs), "a healthy server needs nobody");
        check(on::staffAddress(f) == "http://RECEPTION-PC:3272",
              "staff are given the computer's name, not its number");
        check(has(cs, "OfficeWiFi"), "and are told which network it is on");
        check(has(cs, "No other computer has connected yet"),
              "and that no staff computer has tried it yet");

        f.staffScreens = 3;
        check(has(on::buildChecks(f), "3 staff screens are connected"),
              "staff screens that are open are counted");

        f = healthy();
        f.mode = on::NetMode::LocalOnly;
        f.profile = on::NetProfile::Public;
        f.networkNames = "Network 2";
        cs = on::buildChecks(f);
        check(on::needsAttention(cs), "a public network needs somebody");
        check(on::staffAddress(f).empty(), "and offers no address that cannot work");
        const on::Check* trust = find(cs, on::Fix::TrustNetwork);
        check(trust && trust->help.find("\"Network 2\"") != std::string::npos,
              "it names the network and offers to trust it");

        f = healthy();
        f.mode = on::NetMode::Off;
        f.profile = on::NetProfile::Public;
        f.networkNames = "Cafe";
        cs = on::buildChecks(f);
        check(has(cs, "Waiting for your office network") &&
                  find(cs, on::Fix::TrustNetwork),
              "set to stay off on a public network, it waits rather than quitting");
        f.profile = on::NetProfile::Unknown;
        f.networkNames.clear();
        check(!find(on::buildChecks(f), on::Fix::TrustNetwork),
              "with no network at all there is nothing to trust");

        f = healthy();
        f.firewall = on::FirewallState::Blocked;
        cs = on::buildChecks(f);
        const on::Check* fw = find(cs, on::Fix::Firewall);
        check(fw && fw->state == on::CheckState::NeedsYou,
              "a firewall block needs somebody and has a fix button");
        f.lastStaffSeen = f.now - 30'000;
        cs = on::buildChecks(f);
        check(!find(cs, on::Fix::Firewall) && has(cs, "just now"),
              "but once a staff computer has got in, that proof wins");

        f = healthy();
        f.firewall = on::FirewallState::Managed;
        cs = on::buildChecks(f);
        check(on::needsAttention(cs) && !find(cs, on::Fix::Firewall),
              "IT-managed: says who to ask, offers no button that cannot work");

        f = healthy();
        f.firewall = on::FirewallState::WillAsk;
        cs = on::buildChecks(f);
        check(!on::needsAttention(cs) && find(cs, on::Fix::Firewall),
              "not asked yet: a heads-up, with the option to allow it now");

        f = healthy();
        f.config.writable = false;
        check(has(on::buildChecks(f), "can't save its settings"),
              "a folder it cannot save in is reported");

        f = healthy();
        f.config.origin = on::ConfigOrigin::RestoredFromBackup;
        f.config.damagedCopy = "config.damaged-x.json";
        cs = on::buildChecks(f);
        check(has(cs, "repaired") && !on::needsAttention(cs),
              "repaired settings are mentioned, not alarmed about");

        f = healthy();
        f.port = 3273;
        f.portWanted = 3272;
        check(has(on::buildChecks(f), "Using connection number 3273"),
              "a port picked because the usual one was busy is explained");

        f = healthy();
        f.runAtSignIn = false;
        check(find(on::buildChecks(f), on::Fix::StartWithWindows),
              "not starting with Windows offers to");

        f = healthy();
        f.serving = false;
        check(on::needsAttention(on::buildChecks(f)) && on::staffAddress(f).empty(),
              "a listener it could not get back is a problem, with no address");

        // Every way the checklist can go wrong, at once: every problem row
        // explains itself, and none of it reads like an error code.
        std::vector<on::Facts> worst;
        for (int i = 0; i < 4; ++i) worst.push_back(healthy());
        worst[0].mode = on::NetMode::LocalOnly;
        worst[0].profile = on::NetProfile::Public;
        worst[1].firewall = on::FirewallState::Blocked;
        worst[1].config.writable = false;
        worst[2].firewall = on::FirewallState::Managed;
        worst[2].serving = false;
        worst[3].mode = on::NetMode::LocalOnly;
        worst[3].profile = on::NetProfile::Unknown;
        worst[3].networkNames.clear();
        bool explained = true, plain = true;
        for (const auto& w : worst)
            for (const auto& c : on::buildChecks(w)) {
                if (c.state == on::CheckState::NeedsYou && c.help.empty()) explained = false;
                for (const char* bad : {"0x", "rror", "errno", "WSA", "HRESULT", "exception"})
                    if (c.text.find(bad) != std::string::npos ||
                        c.help.find(bad) != std::string::npos)
                        plain = false;
            }
        check(explained, "every problem says what to do about it");
        check(plain, "and none of it is an error code");

        check(on::sinceText(1000, 30'000) == "just now", "half a minute ago is just now");
        check(on::sinceText(0, 5 * 60'000) == "5 minutes ago", "then minutes");
        check(on::sinceText(0, 60'000) == "1 minute ago", "one minute is singular");
    }

    // ---------------------------------------------------------------------
    section("Staff computers reaching the server");
    {
        on::Hub hub;
        hub.start(on::Config::defaults());
        check(hub.lastStaffSeen() == 0, "nobody has connected at first");
        hub.noteStaffSeen();
        check(hub.lastStaffSeen() > 0, "a connection from another computer is noted");

        // These sockets are not real, so the hub's sends to them fail and mark
        // them dead; what matters here is which ones stay on the hub's list.
        auto local = std::make_shared<on::SseClient>();
        local->station = "front";
        auto remote = std::make_shared<on::SseClient>();
        remote->station = "room1";
        remote->remote = true;
        hub.addClient(local);
        hub.addClient(remote);
        check(hub.staffScreens() == 1, "only screens on other computers count as staff");
        hub.dropRemoteClients();
        const std::string listening = hub.listeningJson().dump(0);
        check(hub.staffScreens() == 0 && listening.find("room1") == std::string::npos,
              "moving to an untrusted network drops other computers' screens");
        check(listening.find("front") != std::string::npos,
              "but leaves this computer's own alone");
        hub.removeClient(local);
        hub.stop();
    }

    // ---------------------------------------------------------------------
    section("Files the client setup needs");
    {
        const std::string pub = joinPath(on::resourceRoot(), "public");
        check(fileExists(joinPath(pub, "setup.html")),
              "the Set up this computer page is present");
        check(fileExists(joinPath(pub, "sw.js")),
              "the service worker is present");
        std::string idx;
        if (readFile(joinPath(pub, "index.html"), idx))
            check(idx.find("sw.js") == std::string::npos,
                  "and is NOT registered from the page staff use all day - it "
                  "sits in front of the alert stream and cannot be un-installed "
                  "by deleting it from the server");
        std::string sw;
        if (readFile(joinPath(pub, "sw.js"), sw)) {
            // It exists only so Chrome offers to install the app. If it ever
            // starts caching, a station could show a working-looking page
            // while the server is off - the one failure this program must not
            // have.
            check(sw.find("caches.open") == std::string::npos &&
                      sw.find("cache.put") == std::string::npos &&
                      sw.find("cache.addAll") == std::string::npos,
                  "and stores nothing, so a dead server can never look alive");
            check(sw.find("addEventListener('fetch'") != std::string::npos,
                  "while still having the fetch handler installability needs");
        }
    }

    std::printf("\n=====================================\n");
    std::printf("  passed: %d    failed: %d\n", g_pass, g_fail);
    std::printf("=====================================\n");
    return g_fail == 0 ? 0 : 1;
}
