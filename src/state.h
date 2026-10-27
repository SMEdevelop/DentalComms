// Everything the office shares: which alerts are still ringing, the recent
// activity log, and the browsers currently listening for updates.
//
// One mutex guards the lot. The work under it is a handful of vector
// operations on data an office generates by hand, so contention isn't a
// concern and a single lock is far easier to reason about than several.
#pragma once
#include "http.h"  // must precede <windows.h> users

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "config.h"

namespace on {

struct Signal {
    std::string id;
    std::string from;
    std::string to;
    std::string presetId;   // empty when it was a typed note
    std::string label;
    std::string color;
    bool urgent = false;
    std::string note;
    long long time = 0;
    // Set once the sender has been told this went unanswered, so they are told
    // exactly once however long the alert sits there.
    bool escalated = false;

    js::Value toJson() const;
};

struct RecentEntry {
    std::string text;
    long long time = 0;
};

long long nowMillis();

class Hub {
public:
    ~Hub();

    void start();                       // load settings from disk
    void start(const Config& preloaded); // ...or adopt ones already read

    // Shutting down: end the unanswered-alert watch and drop every open
    // stream, so nothing is left blocked when the process goes away.
    void stop();

    js::Value stateJson();

    // Returns false with `error` set when the request can't be honoured; the
    // wording matches the Node server so the browser shows the same message.
    bool createSignals(const js::Value& body, std::vector<Signal>& created,
                       std::string& error);
    void ackSignal(const std::string& id, const std::string& by);
    void cancelSignal(const std::string& id);
    bool updateConfig(const js::Value& body, std::string& error);

    // Extra names the office reaches this server by, beyond localhost, this
    // computer's own name and the address the connection actually arrived on.
    std::vector<std::string> allowedHosts();

    void addClient(const std::shared_ptr<SseClient>& c);
    void removeClient(const std::shared_ptr<SseClient>& c);
    void broadcast(const std::string& event, const js::Value& data);
    void closeAllClients();

    // The station ids with at least one browser sitting on them right now, as
    // a JSON array. Everything else is guesswork: a room with no screen open
    // is a room where an alert will not be seen.
    js::Value listeningJson();

    // A browser on another computer just reached us. That, and nothing the
    // firewall settings say, is proof that staff computers can connect.
    void noteStaffSeen() { lastStaffSeen_.store(nowMillis()); }
    long long lastStaffSeen() const { return lastStaffSeen_.load(); }

    // Message screens open on other computers right now.
    int staffScreens();

    // Windows moved to a network Dental Comms should not serve: shut every
    // stream that came from another computer, so "this computer only" holds
    // for connections already open as well as new ones.
    void dropRemoteClients();

    std::string officeName();

    // The first-run welcome on the server computer is done. Saved at once.
    void setWelcomed();

    // Does one pass of the unanswered-alert check immediately, instead of
    // waiting for the next tick. Exposed for the self-test.
    void sweepNow() { sweepUnanswered(); }

private:
    // Caller holds mu_. Returns the entry it added so the caller can put it on
    // the wire once the lock is gone: every screen then shows the same
    // activity log the server has, rather than each one guessing at its own.
    RecentEntry logEvent(const std::string& text);
    void broadcastPresence();

    // Runs on its own thread: notices alerts nobody has acknowledged and tells
    // the sender, once each.
    void startSweeper();
    void sweepUnanswered();

    std::mutex mu_;
    Config config_;
    std::vector<Signal> signals_;
    std::deque<RecentEntry> recent_;

    std::mutex clientsMu_;
    std::vector<std::shared_ptr<SseClient>> clients_;

    std::thread sweeper_;
    std::mutex sweepMu_;
    std::condition_variable sweepCv_;
    std::atomic<bool> stopping_{false};
    std::atomic<long long> lastStaffSeen_{0};
};

}  // namespace on
