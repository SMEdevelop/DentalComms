// Dental Flow self-test - every non-networking subsystem, checked against the
// behaviour of the Node server this replaced.
#include "http.h"  // must precede <windows.h> users

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "config.h"
#include "json.h"
#include "routes.h"
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
    std::printf("Dental Flow self-test\n");
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
        tuned.allowedHosts = {"dentalflow.local"};
        on::saveConfig(tuned);
        on::Config reread = on::loadConfig();
        check(reread.escalateAfterSeconds == 30 && reread.expireAfterSeconds == 300,
              "the unanswered-alert delays reload");
        check(reread.allowedHosts.size() == 1 &&
                  reread.allowedHosts[0] == "dentalflow.local",
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
    section("Who Dental Flow will answer");
    {
        const std::vector<std::string> none;
        const std::vector<std::string> extra = {"dentalflow.local"};

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
        check(on::hostIsOurs("dentalflow.local", "192.168.1.25", "RECEPTION-PC", extra),
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
        guard.allowedHosts = {"dentalflow.local"};
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
    }

    // ---------------------------------------------------------------------
    section("How alerts announce themselves");
    {
        on::Config c = on::Config::defaults();
        check(c.notifySound && c.notifyFlash,
              "a new office gets both the chime and the flash");

        // Round trip through the file format.
        c.notifyFlash = false;
        js::Value v = c.toJson();
        check(v.boolean("notifySound", false) && !v.boolean("notifyFlash", true),
              "both settings are written out");

        // A settings-page save carries them.
        std::string err;
        js::Value body = makeConfigBody(2, 2);
        body["notifySound"] = false;
        body["notifyFlash"] = true;
        on::Config saved = on::Config::defaults();
        check(saved.applyFromJson(body, err), "a save with both flags succeeds");
        check(!saved.notifySound, "turning the chime off sticks");
        check(saved.notifyFlash, "and the flash stays on independently");

        // The case that matters: an admin page opened before this option
        // existed posts no such keys. Reading those as "off" would mute the
        // office the next time somebody renamed a room.
        on::Config older = on::Config::defaults();
        older.notifySound = true;
        older.notifyFlash = false;
        js::Value oldBody = makeConfigBody(2, 2);   // no notify* keys at all
        check(older.applyFromJson(oldBody, err), "an older page can still save");
        check(older.notifySound && !older.notifyFlash,
              "a save that omits them changes neither");

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

    std::printf("\n=====================================\n");
    std::printf("  passed: %d    failed: %d\n", g_pass, g_fail);
    std::printf("=====================================\n");
    return g_fail == 0 ? 0 : 1;
}
