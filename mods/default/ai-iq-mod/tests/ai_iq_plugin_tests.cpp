#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "san14_mod.h"
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
std::vector<San14BoolSetting> settings;
std::map<std::string, int> values, rules, rule_calls;
std::string fail_rule, fail_get;
int fail_rule_value = -1;
bool fail_diplomacy = false, fail_tactical = false, fail_diplomacy_cleanup = false, fail_tactical_cleanup = false;
int diplomacy_registers = 0, diplomacy_unregisters = 0, tactical_registers = 0, tactical_unregisters = 0;
bool fail_government = false, fail_government_cleanup = false;
San14GovernmentScoreCallback registered_government = nullptr;
San14Result SAN14_MOD_CALL gov_register(void*, San14GovernmentScoreCallback callback, void*) {
    if (fail_government) return SAN14_E_BUSY;
    assert(callback && !registered_government); registered_government = callback; return SAN14_OK;
}
San14Result SAN14_MOD_CALL gov_unregister(void*) {
    if (fail_government_cleanup) return SAN14_E_BUSY;
    registered_government = nullptr; return SAN14_OK;
}
const San14GovernmentApi government_service{sizeof(San14GovernmentApi), gov_register, gov_unregister};
San14DiplomacyPolicy registered_diplomacy{};
San14TacticalAiPolicy registered_tactical{};

std::string key(San14StringView s) { return {s.data, s.length}; }
San14Result SAN14_MOD_CALL reg(void*, const San14BoolSetting* s) { settings.push_back(*s); return SAN14_OK; }
San14Result SAN14_MOD_CALL get(void*, San14StringView k, int32_t* out) {
    const auto name = key(k);
    if (name == fail_get) return SAN14_E_BUSY;
    auto it = values.find(name);
    if (it != values.end()) {
        *out = it->second;
    } else {
        int32_t def = 0;
        for (const auto& s : settings) {
            if (key(s.key) == name) { def = s.default_value; break; }
        }
        *out = def;
    }
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL rule(void*, San14StringView k, int32_t v) {
    const auto name = key(k);
    ++rule_calls[name];
    if (name == fail_rule && (fail_rule_value < 0 || v == fail_rule_value)) return SAN14_E_BUSY;
    if (v && name == "government.ai.optimized") assert(registered_government);
    if (v && name == "diplomacy.ai.dynamic") assert(registered_diplomacy.on_tick);
    if (v && name == "ai.tactical.policy") assert(registered_tactical.on_strategy_update);
    rules[name] = v;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL dip_register(void*, const San14DiplomacyPolicy* p) {
    ++diplomacy_registers;
    if (fail_diplomacy) return SAN14_E_BUSY;
    assert(p && p->struct_size >= sizeof(San14DiplomacyPolicy) && p->on_tick);
    registered_diplomacy = *p;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL dip_unregister(void*) {
    ++diplomacy_unregisters;
    if (fail_diplomacy_cleanup) return SAN14_E_BUSY;
    registered_diplomacy = {};
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL tac_register(void*, const San14TacticalAiPolicy* p) {
    ++tactical_registers;
    if (fail_tactical) return SAN14_E_BUSY;
    assert(p && p->struct_size >= sizeof(San14TacticalAiPolicy) && p->on_strategy_update && p->on_daily_tactical_tick);
    registered_tactical = *p;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL tac_unregister(void*) {
    ++tactical_unregisters;
    if (fail_tactical_cleanup) return SAN14_E_BUSY;
    registered_tactical = {};
    return SAN14_OK;
}

const San14DiplomacyApi diplomacy_service{sizeof(San14DiplomacyApi), dip_register, dip_unregister};
const San14TacticalAiApi tactical_service{sizeof(San14TacticalAiApi), tac_register, tac_unregister};

void reset_mocks() {
    settings.clear(); values.clear(); rules.clear(); rule_calls.clear(); fail_rule.clear(); fail_get.clear();
    fail_rule_value = -1;
    fail_diplomacy = fail_tactical = fail_diplomacy_cleanup = fail_tactical_cleanup = false;
    diplomacy_registers = diplomacy_unregisters = tactical_registers = tactical_unregisters = 0;
    registered_diplomacy = {}; registered_tactical = {};
    fail_government = fail_government_cleanup = false; registered_government = nullptr;
    SetEnvironmentVariableA("SAN14_AI_EVAL_DISABLE_GLOBAL", nullptr);
    SetEnvironmentVariableA("SAN14_AI_AB_FORCE_A", nullptr);
    SetEnvironmentVariableA("SAN14_AI_AB_PROFILE_A", nullptr);
}

void set_all_bt(int v) {
    for (const char* k : {"bt_deploy", "bt_rally", "bt_engage", "bt_retreat", "bt_stance", "bt_walls", "bt_siegeplan", "bt_gate"}) {
        values[k] = v;
    }
}

San14HostApi fake_host() {
    San14HostApi host{};
    host.struct_size = sizeof(host);
    host.abi_version = SAN14_HOST_ABI_V1;
    host.rules = {sizeof(host.rules), rule};
    host.settings = {sizeof(host.settings), reg, get};
    host.diplomacy = &diplomacy_service;
    host.tactical_ai = &tactical_service;
    host.government = &government_service;
    return host;
}

San14BoolSetting& find_setting(const std::string& name) {
    for (auto& s : settings) if (key(s.key) == name) return s;
    assert(false);
    return settings[0];
}
San14Result change(const std::string& name, int32_t value) {
    auto& s = find_setting(name);
    return s.changed(s.plugin_context, s.key, value);
}

using Initialize = San14Result (SAN14_MOD_CALL*)(const San14HostApi*, San14ModApi*);
}  // namespace

int main() {
    wchar_t exe_path[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path beside_exe = (len > 0 && len < MAX_PATH)
        ? (std::filesystem::path(exe_path).parent_path() / L"ai_iq.dll")
        : std::filesystem::path(L"ai_iq.dll");
    auto dll = LoadLibraryExW(beside_exe.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) dll = LoadLibraryExW(std::filesystem::absolute(L"ai_iq.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(dll);
    auto init = reinterpret_cast<Initialize>(GetProcAddress(dll, "SAN14Mod_Initialize"));
    assert(init);

    // 1. Default registration: 14 settings (6 legacy default=0 + 8 new BT default=1).
    reset_mocks();
    auto host = fake_host();
    San14ModApi api{}; api.struct_size = sizeof(api);
    San14ModContext context{}; context.struct_size = sizeof(context);
    assert(init(&host, &api) == SAN14_OK);
    assert(api.on_load(&context) == SAN14_OK && settings.size() == 14);
    assert(find_setting("bt_deploy").default_value == 1);
    assert(find_setting("bt_gate").default_value == 1);
    assert(find_setting("weighted_government_auto_assign").default_value == 0);
    assert(api.on_enable(&context) == SAN14_OK);
    assert(rules.at("ai.tactical.policy") == 1 && registered_tactical.on_daily_tactical_tick);
    assert(api.on_disable(&context) == SAN14_OK);
    assert(rules.at("ai.tactical.policy") == 0 && !registered_tactical.on_strategy_update);
    api.on_unload(&context);

    // 2. Shared tactical refcount across all 3 legacy + 8 BT toggles (starting with all off).
    reset_mocks();
    set_all_bt(0);
    host = fake_host();
    api = {}; api.struct_size = sizeof(api);
    assert(init(&host, &api) == SAN14_OK);
    assert(api.on_load(&context) == SAN14_OK && settings.size() == 14);
    assert(api.on_enable(&context) == SAN14_OK && rules.empty());

    assert(key(find_setting("weighted_government_auto_assign").label) == u8"优化电脑施政");
    fail_government = true;
    assert(change("weighted_government_auto_assign", 1) == SAN14_E_BUSY);
    fail_government = false;
    fail_rule = "government.ai.optimized";
    assert(change("weighted_government_auto_assign", 1) == SAN14_E_BUSY);
    fail_rule.clear();
    assert(change("weighted_government_auto_assign", 1) == SAN14_OK);
    San14GovernmentSnapshot snapshot{}; snapshot.struct_size = sizeof(snapshot);
    San14GovernmentCandidate candidate{sizeof(candidate), 10, 46, 5, 1, 0}; int32_t score = 0;
    assert(registered_government(nullptr, &snapshot, &candidate, &score) == SAN14_OK && score == 35200);
    assert(change("fix_ai_gate_siege", 1) == SAN14_OK && rules.at("ai.siege.composition") == 1);
    assert(change("dynamic_diplomacy", 1) == SAN14_OK && rules.at("diplomacy.ai.dynamic") == 1 && registered_diplomacy.on_tick);
    assert(change("smart_district_ai", 1) == SAN14_OK && rules.at("ai.tactical.policy") == 1 && registered_tactical.on_strategy_update);
    const int tac_calls = rule_calls.at("ai.tactical.policy");
    assert(change("emergency_reinforcement_ai", 1) == SAN14_OK && rule_calls.at("ai.tactical.policy") == tac_calls);
    assert(change("siege_interception_ai", 1) == SAN14_OK && rule_calls.at("ai.tactical.policy") == tac_calls);
    assert(change("bt_deploy", 1) == SAN14_OK && rule_calls.at("ai.tactical.policy") == tac_calls);
    assert(change("bt_gate", 1) == SAN14_OK && rule_calls.at("ai.tactical.policy") == tac_calls);
    assert(change("smart_district_ai", 0) == SAN14_OK && rules.at("ai.tactical.policy") == 1 && rule_calls.at("ai.tactical.policy") == tac_calls);
    assert(change("emergency_reinforcement_ai", 0) == SAN14_OK && rules.at("ai.tactical.policy") == 1 && rule_calls.at("ai.tactical.policy") == tac_calls);
    assert(change("siege_interception_ai", 0) == SAN14_OK && rules.at("ai.tactical.policy") == 1 && rule_calls.at("ai.tactical.policy") == tac_calls);
    assert(change("bt_deploy", 0) == SAN14_OK && rules.at("ai.tactical.policy") == 1 && rule_calls.at("ai.tactical.policy") == tac_calls);
    assert(change("bt_gate", 0) == SAN14_OK && rules.at("ai.tactical.policy") == 0 && rule_calls.at("ai.tactical.policy") == tac_calls + 1);

    assert(change("emergency_reinforcement_ai", 1) == SAN14_OK && rules.at("ai.tactical.policy") == 1);
    fail_rule = "government.ai.optimized"; fail_rule_value = 0;
    assert(api.on_disable(&context) == SAN14_E_BUSY && registered_government && rules.at(fail_rule) == 1);
    fail_rule.clear(); fail_government_cleanup = true;
    assert(api.on_disable(&context) == SAN14_E_BUSY && registered_government && rules.at("government.ai.optimized") == 0);
    fail_government_cleanup = false;
    assert(api.on_disable(&context) == SAN14_OK && !registered_government);
    assert(rules.at("ai.siege.composition") == 0 && rules.at("diplomacy.ai.dynamic") == 0 && rules.at("ai.tactical.policy") == 0);
    assert(!registered_diplomacy.on_tick && !registered_tactical.on_strategy_update);
    api.on_unload(&context);

    // 3. Lazy missing service checks & graceful degradation when Host lacks tactical_ai.
    reset_mocks();
    host = fake_host();
    host.struct_size = static_cast<uint32_t>(offsetof(San14HostApi, government));
    host.government = nullptr;
    host.diplomacy = nullptr;
    host.tactical_ai = nullptr;
    api = {}; api.struct_size = sizeof(api);
    // Even with default=1 BT toggles, on_enable gracefully degrades so fix_ai_gate_siege still works!
    assert(init(&host, &api) == SAN14_OK && api.on_load(&context) == SAN14_OK && api.on_enable(&context) == SAN14_OK);
    assert(change("fix_ai_gate_siege", 1) == SAN14_OK && rules.at("ai.siege.composition") == 1);
    assert(change("weighted_government_auto_assign", 1) == SAN14_E_UNSUPPORTED);
    assert(change("dynamic_diplomacy", 1) == SAN14_E_UNSUPPORTED);
    assert(change("smart_district_ai", 1) == SAN14_E_UNSUPPORTED);
    assert(change("bt_deploy", 1) == SAN14_E_UNSUPPORTED);
    assert(api.on_disable(&context) == SAN14_OK);
    api.on_unload(&context);

    // 4. SAN14_AI_EVAL_DISABLE_GLOBAL suppresses global non-tactical rules while keeping tactical AI active.
    reset_mocks();
    values["fix_ai_gate_siege"] = 1;
    values["dynamic_diplomacy"] = 1;
    values["weighted_government_auto_assign"] = 1;
    SetEnvironmentVariableA("SAN14_AI_EVAL_DISABLE_GLOBAL", "1");
    host = fake_host();
    api = {}; api.struct_size = sizeof(api);
    assert(init(&host, &api) == SAN14_OK && api.on_load(&context) == SAN14_OK && api.on_enable(&context) == SAN14_OK);
    assert(rules.find("ai.siege.composition") == rules.end());
    assert(!registered_diplomacy.on_tick && !registered_government);
    assert(rules.at("ai.tactical.policy") == 1 && registered_tactical.on_daily_tactical_tick);
    assert(api.on_disable(&context) == SAN14_OK);
    api.on_unload(&context);

    // 5. Env A/B harness activates tactical policy even when all toggles are 0.
    reset_mocks();
    set_all_bt(0);
    SetEnvironmentVariableA("SAN14_AI_AB_FORCE_A", "1");
    SetEnvironmentVariableA("SAN14_AI_AB_PROFILE_A", "new8");
    host = fake_host();
    api = {}; api.struct_size = sizeof(api);
    assert(init(&host, &api) == SAN14_OK && api.on_load(&context) == SAN14_OK && api.on_enable(&context) == SAN14_OK);
    assert(rules.at("ai.tactical.policy") == 1 && registered_tactical.on_daily_tactical_tick);
    assert(api.on_disable(&context) == SAN14_OK && rules.at("ai.tactical.policy") == 0);
    api.on_unload(&context);

    reset_mocks();
    host = fake_host();
    San14GovernmentApi short_service = government_service; short_service.struct_size = sizeof(uint32_t);
    host.government = &short_service; api = {}; api.struct_size = sizeof(api);
    assert(init(&host, &api) == SAN14_OK && api.on_load(&context) == SAN14_OK && api.on_enable(&context) == SAN14_OK);
    assert(change("weighted_government_auto_assign", 1) == SAN14_E_UNSUPPORTED);
    assert(api.on_disable(&context) == SAN14_OK); api.on_unload(&context);

    reset_mocks(); host = fake_host(); values["weighted_government_auto_assign"] = 1;
    api = {}; api.struct_size = sizeof(api);
    assert(init(&host, &api) == SAN14_OK && api.on_load(&context) == SAN14_OK && api.on_enable(&context) == SAN14_OK);
    assert(registered_government && rules.at("government.ai.optimized") == 1);
    assert(api.on_disable(&context) == SAN14_OK); api.on_unload(&context);

    std::cout << "AI IQ plugin: ABI, 14 features (6 legacy + 8 BT), shared tactical refcount, eval isolation and graceful degradation passed\n";
    FreeLibrary(dll);
    return 0;
}
