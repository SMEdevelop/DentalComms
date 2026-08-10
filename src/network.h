// Which kind of network is this computer on right now?
//
// Windows labels every network Domain, Private or Public, and that label is
// the best signal available for "is it safe to let other computers reach me".
// Dental Flow has no passwords, so on an untrusted network it should not be
// answering strangers.
#pragma once
#include <string>

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

}  // namespace on
