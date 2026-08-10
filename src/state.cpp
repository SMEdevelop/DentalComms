#include "state.h"

#include <chrono>

namespace on {
namespace {

// The activity log is written by people, for people, so it carries the same
// arrow and dash the Node version used. Spelled as UTF-8 bytes rather than
// literal characters so the text survives whatever encoding the compiler
// assumes for this file.
const char* kArrow = "\xe2\x86\x92";  // ->
const char* kDash = "\xe2\x80\x94";   // --

// How often to look for alerts nobody has answered. Well under the shortest
// sensible escalation delay, so the sender hears about it close to on time
// without the thread waking up for nothing all day.
constexpr int kSweepSeconds = 10;

}  // namespace

long long nowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
        .count();
}

js::Value Signal::toJson() const {
    js::Value o = js::Value::object();
    o["id"] = id;
    o["from"] = from;
    o["to"] = to;
    if (presetId.empty())
        o["presetId"] = js::Value();  // null, as the browser expects
    else
        o["presetId"] = presetId;
    o["label"] = label;
    o["color"] = color;
    o["urgent"] = urgent;
    o["note"] = note;
    o["time"] = (double)time;
    o["stale"] = escalated;
    return o;
}

namespace {
js::Value entryJson(const RecentEntry& e) {
    js::Value o = js::Value::object();
    o["text"] = e.text;
    o["time"] = (double)e.time;
    return o;
}
}  // namespace

Hub::~Hub() {
    stopping_.store(true);
    sweepCv_.notify_all();
    if (sweeper_.joinable()) sweeper_.join();
}

void Hub::start() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        config_ = loadConfig();
    }
    startSweeper();
}

void Hub::start(const Config& preloaded) {
    // Startup has to read the settings before it can decide where to listen,
    // so it hands them over rather than making us read the file twice.
    {
        std::lock_guard<std::mutex> lk(mu_);
        config_ = preloaded;
    }
    startSweeper();
}

void Hub::stop() {
    // Wake the sweeper so Ctrl+C is not held up waiting for its next tick.
    // Joining happens in the destructor; here we only need it heading home.
    stopping_.store(true);
    sweepCv_.notify_all();
    closeAllClients();
}

void Hub::startSweeper() {
    if (sweeper_.joinable()) return;
    sweeper_ = std::thread([this] {
        while (true) {
            {
                std::unique_lock<std::mutex> lk(sweepMu_);
                sweepCv_.wait_for(lk, std::chrono::seconds(kSweepSeconds),
                                  [this] { return stopping_.load(); });
            }
            if (stopping_.load()) return;
            sweepUnanswered();
        }
    });
}

std::vector<std::string> Hub::allowedHosts() {
    std::lock_guard<std::mutex> lk(mu_);
    return config_.allowedHosts;
}

js::Value Hub::stateJson() {
    // Taken before mu_, never inside it: presence has its own lock and this is
    // the only place that wants both.
    js::Value listening = listeningJson();
    std::lock_guard<std::mutex> lk(mu_);
    js::Value out = js::Value::object();
    out["listening"] = listening;
    out["config"] = config_.toJson();
    js::Value sigs = js::Value::array();
    for (const auto& s : signals_) sigs.push(s.toJson());
    out["signals"] = sigs;
    js::Value rec = js::Value::array();
    for (const auto& r : recent_) rec.push(entryJson(r));
    out["recent"] = rec;
    return out;
}

RecentEntry Hub::logEvent(const std::string& text) {
    const RecentEntry entry{text, nowMillis()};
    recent_.push_front(entry);
    if (recent_.size() > 50) recent_.pop_back();
    return entry;
}

bool Hub::createSignals(const js::Value& body, std::vector<Signal>& created,
                        std::string& error) {
    std::vector<js::Value> broadcasts;
    RecentEntry entry;
    {
        std::lock_guard<std::mutex> lk(mu_);

        const std::string from = body.str("from");
        const std::string note = body.str("note");
        const std::string presetId = body.str("presetId");
        const Preset* preset = config_.findPreset(presetId);

        // `to` is one station id or a list of them; unknown ids are dropped
        // rather than failing the whole send.
        std::vector<std::string> targets;
        if (const js::Value* to = body.find("to")) {
            if (to->isArr()) {
                for (const auto& t : to->arr) {
                    const std::string id = t.asStr();
                    if (id == "all" || config_.hasStation(id))
                        targets.push_back(id);
                }
            } else {
                const std::string id = to->asStr();
                if (id == "all" || config_.hasStation(id)) targets.push_back(id);
            }
        }

        if (from.empty() || targets.empty() || (!preset && note.empty())) {
            error = "missing fields";
            return false;
        }

        const long long stamp = nowMillis();
        std::string noteClamped = note.size() > 200 ? note.substr(0, 200) : note;
        for (const auto& t : targets) {
            Signal s;
            s.id = newId();
            s.from = from;
            s.to = t;
            s.presetId = preset ? preset->id : "";
            s.label = preset ? preset->label : "Message";
            s.color = preset ? preset->color : "#1c7ed6";
            s.urgent = preset ? preset->urgent : false;
            s.note = noteClamped;
            s.time = stamp;
            signals_.push_back(s);
            created.push_back(s);
        }

        std::string names;
        for (size_t i = 0; i < targets.size(); ++i) {
            if (i) names += ", ";
            names += config_.stationName(targets[i]);
        }
        const Signal& first = created.front();
        std::string line = config_.stationName(from) + " " + kArrow + " " + names +
                           ": " + first.label;
        if (!first.note.empty()) line += std::string(" ") + kDash + " " + first.note;
        entry = logEvent(line);

        for (const auto& s : created) broadcasts.push_back(s.toJson());
    }
    // Broadcasting reaches out over sockets, so it happens after the state
    // lock is released - a slow browser must never stall the office.
    for (const auto& b : broadcasts) broadcast("signal-new", b);
    // One log line for the whole send, however many rooms it went to, so every
    // screen's activity list matches the server's instead of showing one line
    // per room until the next refresh.
    broadcast("log", entryJson(entry));
    return true;
}

void Hub::ackSignal(const std::string& id, const std::string& by) {
    js::Value payload;
    RecentEntry entry;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (size_t i = 0; i < signals_.size(); ++i) {
            if (signals_[i].id != id) continue;
            Signal s = signals_[i];
            signals_.erase(signals_.begin() + (long)i);
            const std::string who = by.empty() ? s.to : by;
            entry = logEvent(config_.stationName(who) + " saw: \"" + s.label +
                             "\" from " + config_.stationName(s.from));
            payload = s.toJson();
            payload["by"] = who;
            found = true;
            break;
        }
    }
    if (found) {
        broadcast("signal-ack", payload);
        broadcast("log", entryJson(entry));
    }
}

void Hub::cancelSignal(const std::string& id) {
    js::Value payload;
    RecentEntry entry;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (size_t i = 0; i < signals_.size(); ++i) {
            if (signals_[i].id != id) continue;
            Signal s = signals_[i];
            signals_.erase(signals_.begin() + (long)i);
            entry = logEvent(config_.stationName(s.from) + " canceled: \"" +
                             s.label + "\" to " + config_.stationName(s.to));
            payload = s.toJson();
            found = true;
            break;
        }
    }
    if (found) {
        broadcast("signal-cancel", payload);
        broadcast("log", entryJson(entry));
    }
}

bool Hub::updateConfig(const js::Value& body, std::string& error) {
    js::Value snapshot;
    std::vector<js::Value> orphaned;
    {
        std::lock_guard<std::mutex> lk(mu_);
        Config next = config_;
        if (!next.applyFromJson(body, error)) return false;
        config_ = std::move(next);
        // Written while still holding the lock: two people saving settings at
        // once should land on disk in the same order they took effect here,
        // never the reverse.
        saveConfig(config_);
        snapshot = config_.toJson();

        // A station that has just been deleted can never tap "Got it" again,
        // so any alert still waiting on it would sit in the list forever.
        // Clear those out the same way a cancel would.
        for (size_t i = signals_.size(); i-- > 0;) {
            const Signal& s = signals_[i];
            if (s.to == "all" || config_.hasStation(s.to)) continue;
            orphaned.push_back(s.toJson());
            signals_.erase(signals_.begin() + (long)i);
        }
    }
    broadcast("config", snapshot);
    for (const auto& o : orphaned) broadcast("signal-cancel", o);
    return true;
}

void Hub::addClient(const std::shared_ptr<SseClient>& c) {
    {
        std::lock_guard<std::mutex> lk(clientsMu_);
        clients_.push_back(c);
    }
    broadcastPresence();
}

void Hub::removeClient(const std::shared_ptr<SseClient>& c) {
    bool removed = false;
    {
        std::lock_guard<std::mutex> lk(clientsMu_);
        for (size_t i = 0; i < clients_.size(); ++i) {
            if (clients_[i] == c) {
                clients_.erase(clients_.begin() + (long)i);
                removed = true;
                break;
            }
        }
    }
    if (removed) broadcastPresence();
}

js::Value Hub::listeningJson() {
    // Membership is driven entirely by add/remove: a browser that vanishes is
    // taken off the list by its own stream thread, which notices within a ping
    // interval at the very worst.
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lk(clientsMu_);
        for (const auto& c : clients_) {
            if (c->station.empty()) continue;
            bool already = false;
            for (const auto& id : ids)
                if (id == c->station) { already = true; break; }
            if (!already) ids.push_back(c->station);
        }
    }
    js::Value arr = js::Value::array();
    for (const auto& id : ids) arr.push(id);
    return arr;
}

void Hub::broadcastPresence() {
    js::Value o = js::Value::object();
    o["listening"] = listeningJson();
    broadcast("presence", o);
}

void Hub::sweepUnanswered() {
    std::vector<js::Value> stale, expired;
    std::vector<RecentEntry> entries;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const long long now = nowMillis();
        const int escalateAfter = config_.escalateAfterSeconds;
        const int expireAfter = config_.expireAfterSeconds;

        for (size_t i = signals_.size(); i-- > 0;) {
            Signal& s = signals_[i];
            const long long ageSeconds = (now - s.time) / 1000;

            // Long enough that nobody is coming: an alert sent to an empty
            // room at closing time must not greet the morning shift.
            if (expireAfter > 0 && ageSeconds >= expireAfter) {
                entries.push_back(
                    logEvent(config_.stationName(s.to) + " never saw: \"" +
                             s.label + "\" from " + config_.stationName(s.from)));
                expired.push_back(s.toJson());
                signals_.erase(signals_.begin() + (long)i);
                continue;
            }

            if (escalateAfter > 0 && !s.escalated && ageSeconds >= escalateAfter) {
                s.escalated = true;
                entries.push_back(logEvent(
                    config_.stationName(s.to) + " has not answered: \"" +
                    s.label + "\" from " + config_.stationName(s.from)));
                stale.push_back(s.toJson());
            }
        }
    }
    for (const auto& v : stale) broadcast("signal-stale", v);
    for (const auto& v : expired) broadcast("signal-expired", v);
    for (const auto& e : entries) broadcast("log", entryJson(e));
}

void Hub::broadcast(const std::string& event, const js::Value& data) {
    const std::string frame = sseFrame(event, data.dump(0));
    std::vector<std::shared_ptr<SseClient>> snapshot;
    {
        std::lock_guard<std::mutex> lk(clientsMu_);
        snapshot = clients_;
    }
    for (const auto& c : snapshot) c->sendFrame(frame);
}

void Hub::closeAllClients() {
    std::vector<std::shared_ptr<SseClient>> snapshot;
    {
        std::lock_guard<std::mutex> lk(clientsMu_);
        snapshot.swap(clients_);
    }
    for (const auto& c : snapshot) c->shutdownNow();
}

}  // namespace on
