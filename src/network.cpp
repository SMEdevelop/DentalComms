#include "network.h"

#include <objbase.h>
#include <netlistmgr.h>

#include "util.h"

namespace on {
namespace {

// Ordered least to most trusted, so "the lowest one wins" is just a min().
int trustRank(NetProfile p) {
    switch (p) {
        case NetProfile::Public:  return 0;
        case NetProfile::Private: return 1;
        case NetProfile::Domain:  return 2;
        case NetProfile::Unknown: return 0;  // treated as untrusted
    }
    return 0;
}

NetProfile fromCategory(NLM_NETWORK_CATEGORY c) {
    switch (c) {
        case NLM_NETWORK_CATEGORY_PRIVATE: return NetProfile::Private;
        case NLM_NETWORK_CATEGORY_DOMAIN_AUTHENTICATED: return NetProfile::Domain;
        case NLM_NETWORK_CATEGORY_PUBLIC:
        default: return NetProfile::Public;
    }
}

}  // namespace

const char* profileName(NetProfile p) {
    switch (p) {
        case NetProfile::Domain:  return "Domain";
        case NetProfile::Private: return "Private";
        case NetProfile::Public:  return "Public";
        case NetProfile::Unknown: return "Unknown";
    }
    return "Unknown";
}

namespace {

// COM may already be running in another mode on this thread; only undo what
// this object actually set up.
struct ComScope {
    bool mine;
    ComScope() : mine(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~ComScope() { if (mine) CoUninitialize(); }
};

// Calls `fn` for every connected network. Stops early when fn returns false.
template <class Fn>
void forEachConnected(Fn fn) {
    INetworkListManager* nlm = nullptr;
    if (FAILED(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL,
                                IID_INetworkListManager, (void**)&nlm)) ||
        !nlm)
        return;
    IEnumNetworks* nets = nullptr;
    if (SUCCEEDED(nlm->GetNetworks(NLM_ENUM_NETWORK_CONNECTED, &nets)) && nets) {
        while (true) {
            INetwork* net = nullptr;
            ULONG got = 0;
            if (FAILED(nets->Next(1, &net, &got)) || got == 0 || !net) break;
            const bool more = fn(net);
            net->Release();
            if (!more) break;
        }
        nets->Release();
    }
    nlm->Release();
}

}  // namespace

std::vector<ConnectedNetwork> connectedNetworks() {
    ComScope com;
    std::vector<ConnectedNetwork> out;
    forEachConnected([&](INetwork* net) {
        ConnectedNetwork n;
        NLM_NETWORK_CATEGORY cat = NLM_NETWORK_CATEGORY_PUBLIC;
        n.profile = SUCCEEDED(net->GetCategory(&cat)) ? fromCategory(cat)
                                                      : NetProfile::Public;
        BSTR name = nullptr;
        if (SUCCEEDED(net->GetName(&name)) && name) {
            n.name = narrow(name);
            SysFreeString(name);
        }
        out.push_back(std::move(n));
        return true;
    });
    return out;
}

NetProfile leastTrusted(const std::vector<ConnectedNetwork>& nets) {
    NetProfile result = NetProfile::Unknown;
    bool sawAny = false;
    for (const auto& n : nets) {
        if (!sawAny || trustRank(n.profile) < trustRank(result)) result = n.profile;
        sawAny = true;
    }
    return result;
}

std::string networkNamesAt(const std::vector<ConnectedNetwork>& nets,
                           NetProfile p) {
    std::vector<std::string> names;
    for (const auto& n : nets)
        if (n.profile == p && !n.name.empty()) names.push_back(n.name);
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) out += (i + 1 == names.size()) ? " and " : ", ";
        out += names[i];
    }
    return out;
}

NetProfile currentProfile() { return leastTrusted(connectedNetworks()); }

int trustConnectedNetworks() {
    ComScope com;
    bool failed = false;
    forEachConnected([&](INetwork* net) {
        NLM_NETWORK_CATEGORY cat;
        if (SUCCEEDED(net->GetCategory(&cat)) && cat == NLM_NETWORK_CATEGORY_PUBLIC &&
            FAILED(net->SetCategory(NLM_NETWORK_CATEGORY_PRIVATE)))
            failed = true;
        return true;
    });
    return failed ? 1 : 0;
}

}  // namespace on
