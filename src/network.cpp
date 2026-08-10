#include "network.h"

#include <objbase.h>
#include <netlistmgr.h>

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

NetProfile currentProfile() {
    // COM may already be running in another mode on this thread; only undo
    // what this call actually set up.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool weInitialised = SUCCEEDED(init);

    NetProfile result = NetProfile::Unknown;
    INetworkListManager* nlm = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL,
                                   IID_INetworkListManager, (void**)&nlm)) &&
        nlm) {
        IEnumNetworks* nets = nullptr;
        if (SUCCEEDED(nlm->GetNetworks(NLM_ENUM_NETWORK_CONNECTED, &nets)) &&
            nets) {
            bool sawAny = false;
            while (true) {
                INetwork* net = nullptr;
                ULONG got = 0;
                if (FAILED(nets->Next(1, &net, &got)) || got == 0 || !net) break;
                NLM_NETWORK_CATEGORY cat = NLM_NETWORK_CATEGORY_PUBLIC;
                if (SUCCEEDED(net->GetCategory(&cat))) {
                    const NetProfile p = fromCategory(cat);
                    if (!sawAny || trustRank(p) < trustRank(result)) result = p;
                    sawAny = true;
                }
                net->Release();
            }
            nets->Release();
        }
        nlm->Release();
    }

    if (weInitialised) CoUninitialize();
    return result;
}

}  // namespace on
