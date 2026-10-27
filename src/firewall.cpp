#include "firewall.h"

#include <windows.h>
#include <netfw.h>
#include <objbase.h>
#include <oleauto.h>

#include "util.h"

namespace on {
namespace {

constexpr long kDomain = 1, kPrivate = 2, kPublic = 4;
const wchar_t kRuleName[] = L"Dental Comms";

// Just enough of a smart pointer to keep every Release() paired, without
// pulling in ATL, which the C++ build tools do not always include.
template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
    T** put() {
        if (p) { p->Release(); p = nullptr; }
        return &p;
    }
    explicit operator bool() const { return p != nullptr; }
};

struct ComScope {
    bool mine;
    ComScope() : mine(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~ComScope() { if (mine) CoUninitialize(); }
};

std::string normalisePath(const std::string& p) { return lower(trim(expandEnv(p))); }

bool openPolicy(Com<INetFwPolicy2>& pol) {
    return SUCCEEDED(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      __uuidof(INetFwPolicy2), (void**)pol.put()));
}

FirewallRuleInfo readRule(INetFwRule* r) {
    FirewallRuleInfo i;
    BSTR app = nullptr;
    if (SUCCEEDED(r->get_ApplicationName(&app)) && app) {
        i.app = expandEnv(narrow(app));
        SysFreeString(app);
    }
    VARIANT_BOOL en = VARIANT_FALSE;
    r->get_Enabled(&en);
    i.enabled = en != VARIANT_FALSE;
    NET_FW_RULE_DIRECTION dir = NET_FW_RULE_DIR_OUT;
    r->get_Direction(&dir);
    i.inbound = dir == NET_FW_RULE_DIR_IN;
    NET_FW_ACTION act = NET_FW_ACTION_BLOCK;
    r->get_Action(&act);
    i.allow = act == NET_FW_ACTION_ALLOW;
    r->get_Profiles(&i.profiles);
    return i;
}

// Calls fn(rule, info) for every rule that names this exe. A machine can carry
// a few hundred rules; reading them takes a few tens of milliseconds.
template <class Fn>
void forEachRuleForExe(INetFwPolicy2* pol, const std::string& exe, Fn fn) {
    Com<INetFwRules> rules;
    if (FAILED(pol->get_Rules(rules.put())) || !rules) return;
    Com<IUnknown> unk;
    if (FAILED(rules->get__NewEnum(unk.put())) || !unk) return;
    Com<IEnumVARIANT> it;
    if (FAILED(unk->QueryInterface(__uuidof(IEnumVARIANT), (void**)it.put())) || !it)
        return;
    const std::string me = normalisePath(exe);
    VARIANT v;
    VariantInit(&v);
    while (it->Next(1, &v, nullptr) == S_OK) {
        Com<INetFwRule> r;
        if (v.vt == VT_DISPATCH && v.pdispVal &&
            SUCCEEDED(v.pdispVal->QueryInterface(__uuidof(INetFwRule), (void**)r.put()))) {
            FirewallRuleInfo info = readRule(r.p);
            if (!info.app.empty() && normalisePath(info.app) == me) fn(r.p, info);
        }
        VariantClear(&v);
    }
}

}  // namespace

const char* firewallStateName(FirewallState s) {
    switch (s) {
        case FirewallState::Allowed: return "allowed";
        case FirewallState::Blocked: return "blocked";
        case FirewallState::WillAsk: return "will ask";
        case FirewallState::Managed: return "managed";
        case FirewallState::Off:     return "off";
        case FirewallState::Unknown: return "unknown";
    }
    return "unknown";
}

FirewallState classifyFirewall(bool firewallOn, bool blockAllInbound,
                               bool policyManaged, long currentProfiles,
                               const std::vector<FirewallRuleInfo>& rules,
                               const std::string& exe) {
    if (!firewallOn) return FirewallState::Off;

    FirewallState result = FirewallState::WillAsk;
    if (blockAllInbound) {
        result = FirewallState::Blocked;
    } else {
        const std::string me = normalisePath(exe);
        for (const auto& r : rules) {
            if (!r.enabled || !r.inbound || !(r.profiles & currentProfiles)) continue;
            if (normalisePath(r.app) != me) continue;
            if (!r.allow) {
                result = FirewallState::Blocked;
                break;  // nothing else can override a block
            }
            result = FirewallState::Allowed;
        }
    }
    // Rules made on this computer do not count for much when the office's IT
    // decides from the domain; saying "click here to fix it" would be a lie.
    if (policyManaged && result != FirewallState::Allowed) return FirewallState::Managed;
    return result;
}

FirewallState firewallState() {
    ComScope com;
    Com<INetFwPolicy2> pol;
    if (!openPolicy(pol)) return FirewallState::Unknown;

    long profiles = 0;
    if (FAILED(pol->get_CurrentProfileTypes(&profiles))) return FirewallState::Unknown;

    bool on = false, blockAll = false;
    for (long bit : {kDomain, kPrivate, kPublic}) {
        if (!(profiles & bit)) continue;
        VARIANT_BOOL v = VARIANT_FALSE;
        if (SUCCEEDED(pol->get_FirewallEnabled((NET_FW_PROFILE_TYPE2)bit, &v)) && v)
            on = true;
        v = VARIANT_FALSE;
        if (SUCCEEDED(pol->get_BlockAllInboundTraffic((NET_FW_PROFILE_TYPE2)bit, &v)) && v)
            blockAll = true;
    }

    NET_FW_MODIFY_STATE modify = NET_FW_MODIFY_STATE_OK;
    pol->get_LocalPolicyModifyState(&modify);
    const bool managed = modify == NET_FW_MODIFY_STATE_GP_OVERRIDE;

    const std::string exe = exePath();
    std::vector<FirewallRuleInfo> rules;
    forEachRuleForExe(pol.p, exe,
                      [&](INetFwRule*, const FirewallRuleInfo& i) { rules.push_back(i); });
    return classifyFirewall(on, blockAll, managed, profiles, rules, exe);
}

int fixFirewall() {
    ComScope com;
    Com<INetFwPolicy2> pol;
    if (!openPolicy(pol)) return 1;

    const std::string exe = exePath();
    bool haveOurs = false, failed = false;

    // Rules are switched off rather than deleted. Deleting goes by name, and a
    // name is not unique - another program's rule could share it - whereas
    // switching off this exact rule touches nothing else, and can be undone in
    // Windows' own firewall settings.
    forEachRuleForExe(pol.p, exe, [&](INetFwRule* r, const FirewallRuleInfo& i) {
        if (!i.inbound) return;
        BSTR name = nullptr;
        r->get_Name(&name);
        const bool ours = name && wcscmp(name, kRuleName) == 0;
        if (name) SysFreeString(name);
        if (ours) {
            haveOurs = true;
            if (FAILED(r->put_Action(NET_FW_ACTION_ALLOW)) ||
                FAILED(r->put_Profiles(kDomain | kPrivate)) ||
                FAILED(r->put_Enabled(VARIANT_TRUE)))
                failed = true;
        } else if (!i.allow && i.enabled && (i.profiles & (kDomain | kPrivate))) {
            if (FAILED(r->put_Enabled(VARIANT_FALSE))) failed = true;
        }
    });

    if (!haveOurs) {
        Com<INetFwRules> rules;
        Com<INetFwRule> r;
        if (FAILED(pol->get_Rules(rules.put())) ||
            FAILED(CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                                    __uuidof(INetFwRule), (void**)r.put())))
            return 1;
        BSTR name = SysAllocString(kRuleName);
        BSTR desc = SysAllocString(L"Lets the office's computers reach Dental Comms.");
        BSTR app = SysAllocString(widen(exe).c_str());
        r->put_Name(name);
        r->put_Description(desc);
        r->put_ApplicationName(app);
        r->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
        r->put_Direction(NET_FW_RULE_DIR_IN);
        r->put_Action(NET_FW_ACTION_ALLOW);
        r->put_Profiles(kDomain | kPrivate);
        r->put_Enabled(VARIANT_TRUE);
        if (FAILED(rules->Add(r.p))) failed = true;
        SysFreeString(name);
        SysFreeString(desc);
        SysFreeString(app);
    }
    return failed ? 1 : 0;
}

}  // namespace on
