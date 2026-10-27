// Which kind of network is this computer on right now?
//
// Windows labels every network Domain, Private or Public, and that label is
// the best signal available for "is it safe to let other computers reach me".
// Dental Comms has no passwords, so on an untrusted network it should not be
// answering strangers.
#pragma once
#include <string>
#include <vector>

namespace on {

enum class NetProfile {
    Domain,   // joined to the company domain - a managed office network
    Private,  // a network marked trusted, e.g. the office Wi-Fi at a clinic
    Public,   // Windows' label for untrusted: cafes, hotels, airports
    Unknown   // nothing connected, or Windows wouldn't say
};

// Reports the *least* trusted of every network currently connected. A laptop
// on the office Ethernet and a phone hotspot at the same time counts as
// Public, because anything it serves is reachable from the hotspot too.
NetProfile currentProfile();

const char* profileName(NetProfile p);

// One network Windows says this computer is connected to: its name as shown
// in Settings ("OfficeWiFi", "Network 2") and how far Windows trusts it.
struct ConnectedNetwork {
    std::string name;
    NetProfile profile = NetProfile::Unknown;
};
std::vector<ConnectedNetwork> connectedNetworks();

// The least trusted of a set of networks - the rule currentProfile() applies.
// Unknown when the list is empty.
NetProfile leastTrusted(const std::vector<ConnectedNetwork>& nets);

// The names of the networks that hold the profile at `p`, joined for a
// sentence: "OfficeWiFi" or "OfficeWiFi and Network 2".
std::string networkNamesAt(const std::vector<ConnectedNetwork>& nets, NetProfile p);

// Marks every connected Public network Private. Needs administrator rights,
// so it runs in the copy of Dental Comms started with --trust-network through
// a UAC prompt. Returns the process exit code: 0 when it worked.
int trustConnectedNetworks();

}  // namespace on
