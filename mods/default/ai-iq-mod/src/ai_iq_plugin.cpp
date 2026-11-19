#include <cstddef>
#include <cstring>

#include "san14_mod.h"
#include "diplomacy_policy.h"
#include "tactical_ai_policy.h"
#include "government_policy.h"

namespace {
const San14HostApi* host = nullptr;
bool active = false;
bool diplomacy_registered = false;
bool tactical_registered = false;
bool government_registered = false;
bool tactical_rule_enabled = false;

companion_mod::diplomacy_policy::State diplomacy_state;
companion_mod::tactical_ai_policy::State tactical_state;

San14StringView sv(const char* text) {
    return {text, static_cast<uint32_t>(std::strlen(text))};
}

enum class Service { Rule, Diplomacy, Tactical, Government };

struct Feature {
    const char* key;
    const char* label;
    const char* help;
    const char* rule;
    Service service;
    int32_t default_value;
    const char* category;
};

const Feature features[] = {
    {"fix_ai_gate_siege", u8"优化电脑攻城编组：更常使用攻城器械，并提高单支器械队兵力", u8"提高电脑单一出征队列上限并优化攻城队配比。", "ai.siege.composition", Service::Rule, 0, u8"常规"},
    {"dynamic_diplomacy", u8"动态外交（类似群雄策 MOD）：电脑会选择一个主要敌人进攻，并尽量与其他势力结盟", u8"依据威胁与敌意动态判定主敌；对共敌势力放宽同盟与停战条件，对互敌势力提高结盟阻力。", "diplomacy.ai.dynamic", Service::Diplomacy, 0, u8"常规"},
    {"smart_district_ai", u8"优化孙权等南方势力的水战逻辑：防守时主动出击在水上解决战斗", u8"检测水域来袭与河道威胁，择优推举水战/操舵武将并调度舰队拦截或机动防御。", "ai.tactical.policy", Service::Tactical, 0, u8"常规"},
    {"emergency_reinforcement_ai", u8"优化电脑出兵、回防和邻城救援逻辑", u8"重估支援兵力并保障前线安全留兵，在战局吃紧时触发远征召回、邻城强援及防止过早撤退。", "ai.tactical.policy", Service::Tactical, 0, u8"常规"},
    {"siege_interception_ai", u8"电脑防守时会主动出击攻击攻城器械", u8"基于行军时间窗口预警敌方攻城器械并统筹前线迎击。", "ai.tactical.policy", Service::Tactical, 0, u8"常规"},
    {"weighted_government_auto_assign", u8"优化电脑施政", u8"根据兵力、战线、粮草和水域威胁调整自动施政排序。", "government.ai.optimized", Service::Government, 0, u8"常规"},
    {"bt_deploy", u8"出兵编成：主力分兵与侧翼锋矢", u8"迎击出兵时按强将数量平分兵力并自动配属锋矢侧翼与雁行辅助，避免单将满兵孤军送死。", "ai.tactical.policy", Service::Tactical, 1, u8"行为树优化"},
    {"bt_rally", u8"集结齐进：迎击会合与攻城同步防落单", u8"迎击与攻城多路军势同步会合、近处晚出与防先头部队落单送死，绝不往回拉扯。", "ai.tactical.policy", Service::Tactical, 1, u8"行为树优化"},
    {"bt_engage", u8"会战协同：分流集火与贴敌还手", u8"近战按负载分流避免扎堆、锋矢优先突击远程/器械，并令贴敌待命部队立即还手。", "ai.tactical.policy", Service::Tactical, 1, u8"行为树优化"},
    {"bt_retreat", u8"野战撤退：劣势整军势撤退与同向联动", u8"迎击军势在野外被敌力明显压制时整军势协同撤退并统一回城目标，避免各自溃灭。", "ai.tactical.policy", Service::Tactical, 1, u8"行为树优化"},
    {"bt_stance", u8"雁行站位：保持两格射程与友军掩护", u8"电脑雁行部队自动保持2格射击距离、被贴脸时优先撤至友军盾后安全格。", "ai.tactical.policy", Service::Tactical, 1, u8"行为树优化"},
    {"bt_walls", u8"城下迎击：器械拦截、半渡而击、水战守港与援军协防", u8"被围城下放开迎击限制，整合水上战力折算、半渡而击、港口交换比决策与援军留守。", "ai.tactical.policy", Service::Tactical, 1, u8"行为树优化"},
    {"bt_siegeplan", u8"攻城评估：援军府分折算、先占周边府与防断粮", u8"攻城决策折算邻城援军与府加成、兵力不足不强攻，并在攻城前先占周边府及保护粮道。", "ai.tactical.policy", Service::Tactical, 1, u8"行为树优化"},
    {"bt_gate", u8"守关调度：关后雁行布防、近战回城与隔关撤退豁免", u8"受攻关隘仅派雁行据关射击、阻止关后排队与跨关送死，并豁免隔关不可达敌军触发的误撤退。", "ai.tactical.policy", Service::Tactical, 1, u8"行为树优化"}
};
constexpr size_t feature_count = sizeof(features) / sizeof(features[0]);
static_assert(feature_count == 14, "AI IQ mod's 6 legacy + 8 BT module feature set");
bool applied[feature_count]{};

bool eval_disable_global_rules() {
    const auto v = companion_mod::tactical_ai_policy::read_env_ascii("SAN14_AI_EVAL_DISABLE_GLOBAL");
    return v == "1" || v == "true" || v == "TRUE";
}

bool government_available() {
    return host && host->struct_size >= offsetof(San14HostApi, government) + sizeof(host->government) &&
           host->government && host->government->struct_size >= sizeof(San14GovernmentApi) &&
           host->government->register_policy && host->government->unregister_policy;
}

bool diplomacy_available() {
    return host &&
           host->struct_size >= offsetof(San14HostApi, diplomacy) + sizeof(host->diplomacy) &&
           host->diplomacy &&
           host->diplomacy->struct_size >= sizeof(San14DiplomacyApi) &&
           host->diplomacy->register_policy &&
           host->diplomacy->unregister_policy;
}

bool tactical_ai_available() {
    return host &&
           host->struct_size >= offsetof(San14HostApi, tactical_ai) + sizeof(host->tactical_ai) &&
           host->tactical_ai &&
           host->tactical_ai->struct_size >= sizeof(San14TacticalAiApi) &&
           host->tactical_ai->register_policy &&
           host->tactical_ai->unregister_policy;
}

companion_mod::tactical_ai_policy::Config tactical_config_with(size_t override_index, bool override_value) {
    companion_mod::tactical_ai_policy::Config cfg{};
    for (size_t i = 0; i < feature_count; ++i) {
        const bool v = (i == override_index) ? override_value : applied[i];
        if (!v) continue;
        if (std::strcmp(features[i].key, "smart_district_ai") == 0) cfg.naval_defense = true;
        else if (std::strcmp(features[i].key, "emergency_reinforcement_ai") == 0) cfg.emergency_reinforcement = true;
        else if (std::strcmp(features[i].key, "siege_interception_ai") == 0) cfg.siege_interception = true;
        else if (std::strcmp(features[i].key, "bt_deploy") == 0) cfg.bt_deploy = true;
        else if (std::strcmp(features[i].key, "bt_rally") == 0) cfg.bt_rally = true;
        else if (std::strcmp(features[i].key, "bt_engage") == 0) cfg.bt_engage = true;
        else if (std::strcmp(features[i].key, "bt_retreat") == 0) cfg.bt_retreat = true;
        else if (std::strcmp(features[i].key, "bt_stance") == 0) cfg.bt_stance = true;
        else if (std::strcmp(features[i].key, "bt_walls") == 0) cfg.bt_walls = true;
        else if (std::strcmp(features[i].key, "bt_siegeplan") == 0) cfg.bt_siegeplan = true;
        else if (std::strcmp(features[i].key, "bt_gate") == 0) cfg.bt_gate = true;
    }
    return cfg;
}

San14Result ensure_policy(Service service) {
    if (service == Service::Diplomacy && !diplomacy_registered) {
        if (!diplomacy_available()) return SAN14_E_UNSUPPORTED;
        const auto spec = companion_mod::diplomacy_policy::make_policy(diplomacy_state);
        const auto result = host->diplomacy->register_policy(host->host_context, &spec);
        if (result != SAN14_OK) return result;
        diplomacy_registered = true;
    } else if (service == Service::Tactical && !tactical_registered) {
        if (!tactical_ai_available()) return SAN14_E_UNSUPPORTED;
        const auto spec = companion_mod::tactical_ai_policy::make_policy(tactical_state);
        const auto result = host->tactical_ai->register_policy(host->host_context, &spec);
        if (result != SAN14_OK) return result;
        tactical_registered = true;
    }
    if (service == Service::Government && !government_registered) {
        if (!government_available()) return SAN14_E_UNSUPPORTED;
        const auto result = host->government->register_policy(host->host_context, companion_government_policy::score, nullptr);
        if (result != SAN14_OK) return result;
        government_registered = true;
    }
    return SAN14_OK;
}

San14Result apply(size_t index, bool value) {
    const auto& feature = features[index];
    if (applied[index] == value) return SAN14_OK;

    if (feature.service == Service::Tactical) {
        const auto prev_cfg = tactical_config_with(feature_count, false);
        const auto next_cfg = tactical_config_with(index, value);
        const bool need_policy = next_cfg.any_enabled() || tactical_state.ab.active;
        if (value) {
            const auto ready = ensure_policy(Service::Tactical);
            if (ready != SAN14_OK) return ready;
        }
        tactical_state.set_config(next_cfg);
        if (tactical_rule_enabled != need_policy) {
            const auto rule_res = host->rules.set_bool(host->host_context, sv(feature.rule), need_policy ? 1 : 0);
            if (rule_res != SAN14_OK) {
                tactical_state.set_config(prev_cfg);
                return rule_res;
            }
            tactical_rule_enabled = need_policy;
        }
        applied[index] = value;
        return SAN14_OK;
    }

    if (value) {
        const auto ready = ensure_policy(feature.service);
        if (ready != SAN14_OK) return ready;
    }
    if (feature.service == Service::Diplomacy) diplomacy_state.set_enabled(value);

    const auto result = host->rules.set_bool(host->host_context, sv(feature.rule), value ? 1 : 0);
    if (result != SAN14_OK) {
        if (feature.service == Service::Diplomacy) diplomacy_state.set_enabled(applied[index]);
        return result;
    }
    applied[index] = value;
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL changed(void*, San14StringView key, int32_t value) {
    if (!active) return SAN14_E_BUSY;
    if (!key.data || (value != 0 && value != 1)) return SAN14_E_INVALID_ARGUMENT;
    for (size_t i = 0; i < feature_count; ++i) {
        if (key.length == std::strlen(features[i].key) &&
            std::memcmp(key.data, features[i].key, key.length) == 0) {
            return apply(i, value != 0);
        }
    }
    return SAN14_E_NOT_FOUND;
}

San14Result SAN14_MOD_CALL load(San14ModContext*) {
    for (const auto& feature : features) {
        const San14BoolSetting spec{
            sizeof(spec),
            sv(feature.key),
            sv(feature.label),
            sv(feature.help),
            feature.default_value,
            changed,
            nullptr,
            sv(feature.category),
            0
        };
        const auto result = host->settings.register_bool(host->host_context, &spec);
        if (result != SAN14_OK) return result;
    }
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL enable(San14ModContext*) {
    active = true;
    tactical_state.set_config(tactical_config_with(feature_count, false));
    const bool suppress_global = eval_disable_global_rules();

    for (size_t i = 0; i < feature_count; ++i) {
        int32_t value = 0;
        const auto result = host->settings.get_bool(host->host_context, sv(features[i].key), &value);
        if (result != SAN14_OK) return result;
        if (suppress_global && features[i].service != Service::Tactical) {
            value = 0;
        }
        if (value) {
            const auto applied_result = apply(i, true);
            if (applied_result != SAN14_OK) {
                if (applied_result == SAN14_E_UNSUPPORTED && features[i].default_value != 0) {
                    continue;  // Graceful degradation on older Hosts/Adapters lacking tactical_ai.
                }
                return applied_result;
            }
        }
    }

    // If an A/B harness env override is active, ensure the tactical policy is registered
    // even when settings.ini leaves individual toggles off.
    if (tactical_state.ab.active && !tactical_registered && tactical_ai_available()) {
        if (ensure_policy(Service::Tactical) == SAN14_OK && !tactical_rule_enabled) {
            if (host->rules.set_bool(host->host_context, sv("ai.tactical.policy"), 1) == SAN14_OK) {
                tactical_rule_enabled = true;
            }
        }
    }
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL disable(San14ModContext*) {
    bool busy = false;
    for (size_t i = feature_count; i > 0; --i) {
        if (applied[i - 1] && apply(i - 1, false) != SAN14_OK) busy = true;
    }
    bool diplomacy_applied = false;
    bool tactical_applied = false;
    bool government_applied = false;
    for (size_t i = 0; i < feature_count; ++i) {
        if (features[i].service == Service::Diplomacy) diplomacy_applied = applied[i];
        else if (features[i].service == Service::Tactical && applied[i]) tactical_applied = true;
        else if (features[i].service == Service::Government) government_applied = applied[i];
    }
    if (!tactical_applied && tactical_rule_enabled) {
        if (host->rules.set_bool(host->host_context, sv("ai.tactical.policy"), 0) == SAN14_OK) {
            tactical_rule_enabled = false;
        } else {
            busy = true;
        }
    }
    if (diplomacy_registered && !diplomacy_applied) {
        if (host->diplomacy->unregister_policy(host->host_context) == SAN14_OK) diplomacy_registered = false;
        else busy = true;
    }
    if (tactical_registered && !tactical_applied) {
        if (host->tactical_ai->unregister_policy(host->host_context) == SAN14_OK) tactical_registered = false;
        else busy = true;
    }
    if (government_registered && !government_applied) {
        if (host->government->unregister_policy(host->host_context) == SAN14_OK) government_registered = false;
        else busy = true;
    }
    if (busy) return SAN14_E_BUSY;
    active = false;
    return SAN14_OK;
}

void SAN14_MOD_CALL unload(San14ModContext*) {
    active = false;
    host = nullptr;
    diplomacy_registered = false;
    tactical_registered = false;
    government_registered = false;
    tactical_rule_enabled = false;
    diplomacy_state.set_enabled(false);
    tactical_state.set_config({});
    for (auto& value : applied) value = false;
}
}  // namespace

extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(
    const San14HostApi* h,
    San14ModApi* out
) {
    if (!h || !out || h->abi_version != SAN14_HOST_ABI_V1 || out->struct_size < sizeof(*out) ||
        h->struct_size < offsetof(San14HostApi, settings) + sizeof(h->settings) ||
        h->rules.struct_size < sizeof(San14RuleApi) || h->settings.struct_size < sizeof(San14SettingsApi) ||
        !h->rules.set_bool || !h->settings.register_bool || !h->settings.get_bool) {
        return SAN14_E_UNSUPPORTED;
    }
    if (active) return SAN14_E_BUSY;
    host = h;
    *out = {sizeof(*out), SAN14_HOST_ABI_V1, load, enable, disable, unload};
    return SAN14_OK;
}
