// Will Windows Firewall let staff computers reach this one?
//
// The first time Dental Comms listens on the network, Windows asks whether to
// allow it. Click Cancel - or be a front-desk account that cannot say yes -
// and Windows quietly writes a rule that blocks it, for good. Staff browsers
// then fail to connect while the server looks perfectly healthy, which is the
// worst kind of failure this program can have: one nobody can see.
//
// So the window checks, and offers to put it right. Reading the rules needs no
// special rights. Changing them does, which is why the fix runs in a second
// copy of Dental Comms started with --fix-firewall through a UAC prompt.
#pragma once
#include <string>
#include <vector>

namespace on {

enum class FirewallState {
    Allowed,   // a rule lets it in on the network Windows says it is on
    Blocked,   // a rule (usually from a Cancel on Windows' prompt) keeps it out
    WillAsk,   // no rule yet: Windows will ask the first time it listens
    Managed,   // blocked or unasked, and the office's IT policy decides
    Off,       // Windows Firewall is off (or another firewall replaced it)
    Unknown    // Windows would not say
};

const char* firewallStateName(FirewallState s);

// One rule as far as this check cares. Profiles are the NET_FW_PROFILE2 bits:
// 1 domain, 2 private, 4 public.
struct FirewallRuleInfo {
    std::string app;   // program path, environment variables already expanded
    bool enabled = false;
    bool inbound = false;
    bool allow = false;
    long profiles = 0;
};

// The judgement itself, separate from Windows so the self-test can try every
// combination. A Block rule beats any Allow rule - that is how Windows Firewall
// works, and why one Cancel locks staff out however many times anyone clicks
// Allow afterwards.
FirewallState classifyFirewall(bool firewallOn, bool blockAllInbound,
                               bool policyManaged, long currentProfiles,
                               const std::vector<FirewallRuleInfo>& rules,
                               const std::string& exe);

// Asks Windows about this exe on the networks it is connected to right now.
FirewallState firewallState();

// Run elevated, as `DentalComms.exe --fix-firewall`. Switches off the rules
// that block this exe on Domain and Private networks and makes sure one rule
// named "Dental Comms" allows it there. Public stays shut, as Dental Comms
// already keeps itself to this computer there. Returns 0 when it worked.
int fixFirewall();

}  // namespace on
