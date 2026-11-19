#ifndef SAN14_AI_IQ_TACTICAL_COMMON_H
#define SAN14_AI_IQ_TACTICAL_COMMON_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "san14_tactical_ai.h"

namespace companion_mod {
namespace tactical_ai_policy {

constexpr int32_t GRID_WIDTH = 220;
constexpr int32_t MAX_TILE_ID = SAN14_TACTICAL_MAX_TILE_ID;
constexpr uint32_t TILE_COUNT = static_cast<uint32_t>(MAX_TILE_ID) + 1u;
constexpr uint32_t MAX_FORCES = 64u;
constexpr uint32_t MAX_CITIES = 64u;
constexpr uint32_t MAX_AREAS = 512u;
constexpr uint32_t MAX_GATES = 16u;

// Order codes & target types (semantic constants matching SDK tactical ops).
constexpr int32_t ACT_MOVE = 1;
constexpr int32_t ACT_GO_CITY = 2;
constexpr int32_t ACT_ATTACK_CITY = 4;
constexpr int32_t ACT_ATTACK_GATE = 5;
constexpr int32_t ACT_ATTACK_ARMY = 7;
constexpr int32_t ACT_ENTER_CITY = 11;
constexpr int32_t ACT_WAIT = 13;
constexpr int32_t ACT_MARCH_ALT = 14;

constexpr int32_t TT_CITY = 5;
constexpr int32_t TT_GATE = 6;
constexpr int32_t TT_HEX = 9;
constexpr int32_t TT_ARMY = 27;

// Formation & terrain semantic IDs.
constexpr uint32_t FORM_YULIN = 1;
constexpr uint32_t FORM_FENGSHI = 2;
constexpr uint32_t FORM_YANXING = 3;
constexpr uint32_t FORM_FANGYUAN = 4;
constexpr uint32_t FORM_HEYI = 5;
constexpr uint32_t FORM_CHANGSHE = 6;
constexpr uint32_t FORM_ZHUIXING = 7;
constexpr uint32_t FORM_JINGLAN = 8;
constexpr uint32_t FORM_CHONGCHE = 9;
constexpr uint32_t FORM_TOUSHI = 10;
constexpr uint32_t FORM_SIYI = 14;

constexpr uint32_t TER_RIVER = 14;
constexpr uint32_t TER_GATE = 17;
constexpr uint32_t TER_PORT = 18;
constexpr uint32_t TER_WATER2 = 24;

struct PolicyParams {
    // Module 1: deploy (intercept deployment composition)
    int32_t deploy_strong_leadership = 60;
    uint32_t deploy_max_mains = 3;
    int32_t deploy_min_share = 2000;
    int32_t deploy_cheer_cap = 2000;

    // Module 2: rally (intercept + siege meet + siege lone)
    int32_t rally_meet_in = 6;
    int32_t rally_meet_out = 8;
    int32_t rally_meet_far = 24;
    int32_t rally_meet_stall_days = 4;
    int32_t rally_meet_max_days = 12;
    int32_t rally_start_moved = 4;
    int32_t rally_start_far = 60;
    int32_t rally_start_slack = 3;
    int32_t rally_start_max_days = 30;
    int32_t rally_lone_min = 5;
    int32_t rally_lone_max = 8;
    int32_t rally_lone_near = 4;
    int32_t rally_lone_join = 12;
    int32_t rally_lone_ahead = 2;
    int32_t rally_lone_wait_days = 8;
    int32_t rally_lone_block_days = 20;
    int32_t rally_lone_foe = 3;

    // Module 3: engage (target distribution + waitfight)
    int32_t engage_radius = 4;
    int32_t engage_load_ratio_pct = 150;

    // Module 4: retreat (field intercept retreat + cascade + unify dest)
    int32_t retreat_ratio_pct = 130;
    int32_t retreat_scan_radius = 8;
    int32_t retreat_home_safe_dist = 4;
    bool retreat_check_daily = false;

    // Module 5: stance (Yanxing ranged positioning)
    int32_t stance_engage_radius = 4;

    // Module 6: walls (city defense unlock + defend + ford + shore + navaldef + relief)
    int32_t walls_unlock_dist = 3;
    int32_t walls_defend_engine_radius = 3;
    int32_t walls_defend_city_radius = 5;
    int32_t walls_defend_city_ally_radius = 6;
    int32_t walls_defend_min_troops = 1500;
    int32_t walls_defend_odds_pct = 60;
    int32_t walls_shore_reach = 6;
    int32_t walls_shore_max_units = 3;
    int32_t walls_shore_min_troops = 1500;
    int32_t walls_shore_days = 12;
    int32_t walls_naval_edge = 3;
    int32_t walls_naval_leave = 6;
    int32_t walls_naval_reach = 12;
    int32_t walls_naval_max_units = 3;
    int32_t walls_naval_days = 12;
    int32_t walls_naval_wait_days = 5;
    int32_t walls_naval_min_troops = 1500;
    int32_t walls_naval_water_pref_pct = 90;
    int32_t walls_naval_min_ratio_pct = 100;
    int32_t walls_naval_min_half_pct = 50;
    int32_t walls_naval_odds_pct = 120;
    int32_t walls_relief_recent_days = 12;
    int32_t walls_relief_near = 12;
    int32_t walls_relief_threat_radius = 20;
    int32_t walls_relief_close_radius = 6;
    int32_t walls_relief_days = 20;
    int32_t walls_relief_min_troops = 1500;
    int32_t walls_relief_keep_troops = 1000;
    int32_t walls_relief_odds_pct = 60;

    // Module 7: siegeplan (relief + siegefu + reserve + fu + siegeprep + supplyguard + siegeenough)
    int32_t siege_reserve_ratio_pct = 60;
    int32_t siege_relief_w18 = 80;
    int32_t siege_relief_w30 = 50;
    int32_t siege_relief_w45 = 10;
    int32_t siege_relief_cap = 45000;
    int32_t siege_fu_min_pct = 75;
    int32_t siege_fu_max_pct = 160;
    int32_t siege_prep_in = 7;
    int32_t siege_prep_out = 10;
    int32_t siege_prep_max_days = 15;
    int32_t siege_prep_flip_days = 10;
    int32_t siege_prep_reach = 12;
    int32_t siege_guard_near = 14;
    int32_t siege_guard_depth = 40;
    int32_t siege_guard_days = 10;

    // Module 8: gate (pass archers + GateGuard + melee withdraw + ReachBlocked)
    int32_t gate_near = 2;
    int32_t gate_reach = 10;
    int32_t gate_city_radius = 20;
    uint32_t gate_max_archers = 3;
    int32_t gate_walk_steps = 14;
    int32_t gate_reach_steps = 16;
};

struct Config {
    bool naval_defense = false;
    bool emergency_reinforcement = false;
    bool siege_interception = false;

    bool bt_deploy = false;
    bool bt_rally = false;
    bool bt_engage = false;
    bool bt_retreat = false;
    bool bt_stance = false;
    bool bt_walls = false;
    bool bt_siegeplan = false;
    bool bt_gate = false;

    uint32_t to_mask() const {
        uint32_t m = 0;
        if (bt_deploy) m |= SAN14_TACTICAL_MOD_DEPLOY;
        if (bt_rally) m |= SAN14_TACTICAL_MOD_RALLY;
        if (bt_engage) m |= SAN14_TACTICAL_MOD_ENGAGE;
        if (bt_retreat) m |= SAN14_TACTICAL_MOD_RETREAT;
        if (bt_stance) m |= SAN14_TACTICAL_MOD_STANCE;
        if (bt_walls) m |= SAN14_TACTICAL_MOD_WALLS;
        if (bt_siegeplan) m |= SAN14_TACTICAL_MOD_SIEGEPLAN;
        if (bt_gate) m |= SAN14_TACTICAL_MOD_GATE;
        if (naval_defense) m |= SAN14_TACTICAL_MOD_LEGACY_NAVAL;
        if (emergency_reinforcement) m |= SAN14_TACTICAL_MOD_LEGACY_REINFORCE;
        if (siege_interception) m |= SAN14_TACTICAL_MOD_LEGACY_SIEGE_INT;
        return m;
    }

    bool any_new8() const {
        return (to_mask() & SAN14_TACTICAL_MOD_ALL_NEW8) != 0;
    }

    bool any_legacy3() const {
        return (to_mask() & SAN14_TACTICAL_MOD_ALL_LEGACY3) != 0;
    }

    bool any_enabled() const {
        return to_mask() != 0;
    }

    bool operator==(const Config& o) const {
        return to_mask() == o.to_mask();
    }
    bool operator!=(const Config& o) const {
        return !(*this == o);
    }
};

struct AbConfig {
    bool active = false;
    uint32_t force_a = 0;
    uint32_t force_b = 0;
    uint32_t mask_a = 0;
    uint32_t mask_b = 0;
    bool has_mask_a = false;
    bool has_mask_b = false;
    uint32_t default_mask = 0;
    bool has_default_mask = false;
};

inline uint32_t parse_profile_spec(const std::string& spec, uint32_t base_mask) {
    if (spec.empty() || spec == "default") {
        return base_mask ? base_mask : (SAN14_TACTICAL_MOD_ALL_NEW8 | SAN14_TACTICAL_MOD_ALL_LEGACY3);
    }
    if (spec == "fused" || spec == "all") return SAN14_TACTICAL_MOD_ALL_NEW8 | SAN14_TACTICAL_MOD_ALL_LEGACY3;
    if (spec == "vanilla" || spec == "off" || spec == "none" || spec == "0") return 0u;
    if (spec == "legacy") return SAN14_TACTICAL_MOD_ALL_LEGACY3;
    if (spec == "new8") return SAN14_TACTICAL_MOD_ALL_NEW8;
    if (spec.rfind("0x", 0) == 0 || spec.rfind("0X", 0) == 0) {
        return static_cast<uint32_t>(std::strtoul(spec.c_str(), nullptr, 16));
    }
    // Comma/+ separated module tokens
    uint32_t m = 0;
    size_t i = 0;
    while (i < spec.size()) {
        size_t j = spec.find_first_of(",+", i);
        if (j == std::string::npos) j = spec.size();
        const std::string tok = spec.substr(i, j - i);
        if (tok == "deploy") m |= SAN14_TACTICAL_MOD_DEPLOY;
        else if (tok == "rally") m |= SAN14_TACTICAL_MOD_RALLY;
        else if (tok == "engage") m |= SAN14_TACTICAL_MOD_ENGAGE;
        else if (tok == "retreat") m |= SAN14_TACTICAL_MOD_RETREAT;
        else if (tok == "stance") m |= SAN14_TACTICAL_MOD_STANCE;
        else if (tok == "walls") m |= SAN14_TACTICAL_MOD_WALLS;
        else if (tok == "siegeplan") m |= SAN14_TACTICAL_MOD_SIEGEPLAN;
        else if (tok == "gate") m |= SAN14_TACTICAL_MOD_GATE;
        else if (tok == "legacy_naval") m |= SAN14_TACTICAL_MOD_LEGACY_NAVAL;
        else if (tok == "legacy_reinforce") m |= SAN14_TACTICAL_MOD_LEGACY_REINFORCE;
        else if (tok == "legacy_siege_int") m |= SAN14_TACTICAL_MOD_LEGACY_SIEGE_INT;
        else if (tok == "new8") m |= SAN14_TACTICAL_MOD_ALL_NEW8;
        else if (tok == "legacy") m |= SAN14_TACTICAL_MOD_ALL_LEGACY3;
        i = j + 1;
    }
    return m;
}

inline std::string read_env_ascii(const char* name) {
#ifdef _WIN32
    char buf[256]{};
    const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) return std::string(buf, n);
#else
    const char* v = std::getenv(name);
    if (v) return std::string(v);
#endif
    return {};
}

inline AbConfig load_env_ab_config(uint32_t base_mask) {
    AbConfig ab{};
    const std::string fa = read_env_ascii("SAN14_AI_AB_FORCE_A");
    const std::string fb = read_env_ascii("SAN14_AI_AB_FORCE_B");
    const std::string pa = read_env_ascii("SAN14_AI_AB_PROFILE_A");
    const std::string pb = read_env_ascii("SAN14_AI_AB_PROFILE_B");
    const std::string pd = read_env_ascii("SAN14_AI_AB_DEFAULT");
    if (!fa.empty()) {
        ab.force_a = static_cast<uint32_t>(std::strtoul(fa.c_str(), nullptr, 10));
        if (ab.force_a > 0) {
            ab.active = true;
            ab.has_mask_a = true;
            ab.mask_a = pa.empty() ? base_mask : parse_profile_spec(pa, base_mask);
        }
    }
    if (!fb.empty()) {
        ab.force_b = static_cast<uint32_t>(std::strtoul(fb.c_str(), nullptr, 10));
        if (ab.force_b > 0) {
            ab.active = true;
            ab.has_mask_b = true;
            ab.mask_b = pb.empty() ? 0u : parse_profile_spec(pb, base_mask);
        }
    }
    if (!pd.empty()) {
        ab.active = true;
        ab.has_default_mask = true;
        ab.default_mask = parse_profile_spec(pd, base_mask);
    }
    return ab;
}

enum MemOwner : uint8_t {
    OWNER_NONE = 0,
    OWNER_STANCE = 1,
    OWNER_DEFEND = 3,
    OWNER_TARGET = 5,
    OWNER_RALLY = 6,
    OWNER_FU = 7,
    OWNER_SHORE = 10,
    OWNER_GATE = 12,
    OWNER_PREP = 18,
    OWNER_SGUARD = 19,
    OWNER_NAVAL = 20,
    OWNER_RELIEF = 21,
    OWNER_LONE = 22,
    OWNER_RETREAT = 23
};

struct OrderMem {
    uint32_t commander_id = 0;
    uint32_t force_id = 0;
    uint8_t mod = 0;
    uint8_t owner = OWNER_NONE;
    int32_t p_act = 0;
    int32_t p_type = 0;
    int32_t p_id = 0;
    int32_t w_act = 0;
    int32_t w_type = 0;
    int32_t w_id = 0;
};

struct TroopRallyMem {
    uint32_t force_id = 0;
    uint32_t target = 0;
    int32_t dist = 255;
    int32_t progress_day = 0;
    int32_t wait_from = 0;
    int32_t start_dist = 255;
    int32_t start_from = 0;
};

struct SiegeLoneMem {
    uint32_t commander_id = 0;
    uint32_t force_id = 0;
    uint32_t goal = 0;
    int32_t since = 0;
    int32_t block_until = 0;
    uint8_t on = 0;
    int32_t o_act = 0;
    int32_t o_type = 0;
    int32_t o_id = 0;
};

struct FuMem {
    uint32_t commander_id = 0;
    uint32_t force_id = 0;
    int32_t since = 0;
    uint32_t block_tile = SAN14_TACTICAL_INVALID_TILE_ID;
    int32_t block_until = 0;
    uint32_t tile = SAN14_TACTICAL_INVALID_TILE_ID;
    int32_t act = 0;
    int32_t type = 0;
    int32_t id = 0;
    uint8_t zone = 0;
    uint32_t goal = 0;
    uint32_t last_tile = SAN14_TACTICAL_INVALID_TILE_ID;
    uint8_t still = 0;
};

struct SiegePrepMem {
    uint32_t force_id = 0;
    uint32_t goal = 0;
    int32_t from = 0;
    uint8_t done = 0;
};

struct SupplyGuardMem {
    uint32_t commander_id = 0;
    uint32_t force_id = 0;
    uint32_t enemy_slot = 0;
    uint32_t enemy_commander_id = 0;
    int32_t since = 0;
    uint32_t goal = 0;
    uint8_t big = 0;
};

struct ShoreMem {
    uint32_t commander_id = 0;
    uint32_t force_id = 0;
    uint32_t enemy_slot = 0;
    uint32_t enemy_commander_id = 0;
    int32_t since = 0;
    uint32_t tile = SAN14_TACTICAL_INVALID_TILE_ID;
};

struct NavalDefMem {
    uint32_t commander_id = 0;
    uint32_t force_id = 0;
    uint32_t enemy_slot = 0;
    uint32_t enemy_commander_id = 0;
    int32_t since = 0;
    uint8_t mode = 0;
    uint32_t tile = SAN14_TACTICAL_INVALID_TILE_ID;
};

struct NavalSeen {
    uint32_t commander_id = 0;
    int32_t first_day = 0;
    int32_t last_day = 0;
};

struct ReliefMem {
    uint32_t commander_id = 0;
    uint32_t force_id = 0;
    uint32_t city_id = 0;
    int32_t since = 0;
    int32_t act = 0;
    int32_t type = 0;
    int32_t id = 0;
    uint8_t mode = 0;
};

struct ReliefTroopMem {
    uint32_t leader_id = 0;
    int32_t last_intercept_day = -999;
};

struct GateMem {
    uint32_t commander_id = 0;
    uint32_t force_id = 0;
    uint32_t gate_foothold = 0;
    uint32_t tile = SAN14_TACTICAL_INVALID_TILE_ID;
};

struct DeployWaveMem {
    uint32_t date_key = 0;
    uint32_t target_foothold = 0;
    int32_t share_troops = 0;
    bool flank_assigned = false;
};

struct ActiveAssignment {
    uint32_t unit_slot = 0;
    uint32_t commander_id = 0;
    uint32_t city_id = 0;
    uint32_t force_id = 0;
    uint32_t target_slot = 0;
    uint32_t station_tile = SAN14_TACTICAL_INVALID_TILE_ID;
    bool naval = false;
    bool siege = false;
};

struct State {
    std::mutex mutex;
    Config config{};
    PolicyParams params{};
    AbConfig ab{};

    uint32_t last_siege_turn_key = 0;
    uint32_t last_daily_date_key = 0;
    int32_t last_retreat_turn_day = -1;

    std::unordered_map<uint32_t, uint32_t> city_cooldown_ms;
    std::unordered_map<uint32_t, ActiveAssignment> assignments;
    std::unordered_map<uint32_t, uint32_t> district_force_cache;

    // Per-unit / per-troops BT module states
    std::unordered_map<uint32_t, OrderMem> order_mem;
    std::unordered_map<uint32_t, TroopRallyMem> rally_mem;
    std::unordered_map<uint32_t, SiegeLoneMem> siege_lone_mem;
    std::unordered_map<uint32_t, FuMem> fu_mem;
    std::unordered_map<uint32_t, SiegePrepMem> prep_mem;
    std::unordered_map<uint32_t, SupplyGuardMem> sg_mem;
    std::unordered_map<uint32_t, ShoreMem> shore_mem;
    std::unordered_map<uint32_t, NavalDefMem> naval_mem;
    std::unordered_map<uint32_t, NavalSeen> naval_seen;
    std::unordered_map<uint32_t, ReliefMem> relief_mem;
    std::unordered_map<uint32_t, ReliefTroopMem> relief_troop_mem;
    std::unordered_map<uint32_t, GateMem> gate_mem;
    std::unordered_map<uint32_t, DeployWaveMem> deploy_waves;

    // Force-scoped tactical caches
    std::unordered_map<uint64_t, int32_t> prep_step_fc;  // (force<<16)|city -> day
    std::unordered_map<uint32_t, uint32_t> city_gate;    // city_id -> gate foothold (52..61)
    std::unordered_map<uint32_t, uint32_t> gate_arch;    // gate_id (1..10) -> archer count
    std::unordered_map<uint32_t, uint8_t> gate_hot;      // gate_id (1..10) -> 1 if attacked
    std::unordered_map<uint32_t, uint32_t> gate_tile;    // gate_id -> tile_id
    std::unordered_map<uint32_t, uint8_t> gate_side;     // tile_id -> gate_id on defender side
    std::unordered_map<uint32_t, uint32_t> gate_side_force;

    // Terrain & port map caches (learned / built once per session)
    bool walk_seeded = false;
    std::array<bool, 256> walkable_terrains{};
    bool ports_built = false;
    std::vector<uint32_t> port_tiles;
    std::vector<int16_t> tile_nearest_port;
    std::vector<uint8_t> tile_port_water_dist;

    // Per-force telemetry counters
    std::array<San14TacticalForceTelemetry, MAX_FORCES> telemetry{};

    void set_config(const Config& next) {
        std::lock_guard<std::mutex> lock(mutex);
        const bool changed = (config != next);
        config = next;
        ab = load_env_ab_config(config.to_mask());
        if (changed) clear_locked();
    }

    void refresh_ab_locked() {
        ab = load_env_ab_config(config.to_mask());
    }

    Config get_config() {
        std::lock_guard<std::mutex> lock(mutex);
        return config;
    }

    void clear_locked() {
        last_siege_turn_key = 0;
        last_daily_date_key = 0;
        last_retreat_turn_day = -1;
        city_cooldown_ms.clear();
        assignments.clear();
        district_force_cache.clear();
        order_mem.clear();
        rally_mem.clear();
        siege_lone_mem.clear();
        fu_mem.clear();
        prep_mem.clear();
        sg_mem.clear();
        shore_mem.clear();
        naval_mem.clear();
        naval_seen.clear();
        relief_mem.clear();
        relief_troop_mem.clear();
        gate_mem.clear();
        deploy_waves.clear();
        prep_step_fc.clear();
        city_gate.clear();
        gate_arch.clear();
        gate_hot.clear();
        gate_tile.clear();
        gate_side.clear();
        gate_side_force.clear();
        ports_built = false;
        port_tiles.clear();
        tile_nearest_port.clear();
        tile_port_water_dist.clear();
        for (uint32_t f = 0; f < MAX_FORCES; ++f) {
            telemetry[f] = {};
            telemetry[f].struct_size = sizeof(San14TacticalForceTelemetry);
            telemetry[f].force_id = f;
        }
    }
};

inline int32_t day_number(uint32_t year, uint32_t month, uint32_t day) {
    return static_cast<int32_t>(year) * 372 + static_cast<int32_t>(month) * 31 + static_cast<int32_t>(day);
}

inline int32_t hex_dist(uint32_t a, uint32_t b) {
    if (a > MAX_TILE_ID || b > MAX_TILE_ID) return 9999;
    const int32_t ax = static_cast<int32_t>(a % GRID_WIDTH);
    const int32_t ay = static_cast<int32_t>(a / GRID_WIDTH);
    const int32_t bx = static_cast<int32_t>(b % GRID_WIDTH);
    const int32_t by = static_cast<int32_t>(b / GRID_WIDTH);
    const int32_t aq = ax;
    const int32_t ar = ay - ((ax - (ax & 1)) / 2);
    const int32_t bq = bx;
    const int32_t br = by - ((bx - (bx & 1)) / 2);
    const int32_t dq = aq - bq;
    const int32_t dr = ar - br;
    const int32_t ds = -dq - dr;
    return (std::max)({std::abs(dq), std::abs(dr), std::abs(ds)});
}

inline int collect_neighbors(uint32_t tile, std::array<uint32_t, 6>& out) {
    if (tile > MAX_TILE_ID) return 0;
    const int32_t x = static_cast<int32_t>(tile % GRID_WIDTH);
    const int32_t y = static_cast<int32_t>(tile / GRID_WIDTH);
    const int32_t stagger = (x & 1) ? -1 : 1;
    const int32_t dx[6] = {0, 0, -1, 1, -1, 1};
    const int32_t dy[6] = {-1, 1, 0, 0, stagger, stagger};
    int count = 0;
    for (int i = 0; i < 6; ++i) {
        const int32_t nx = x + dx[i];
        const int32_t ny = y + dy[i];
        if (nx >= 0 && nx < GRID_WIDTH && ny >= 0 && ny < GRID_WIDTH) {
            out[count++] = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
        }
    }
    return count;
}

inline bool is_water_kind(uint32_t kind, uint8_t is_water) {
    return is_water != 0 || kind == 12 || kind == 13 || kind == TER_RIVER || kind == TER_PORT || kind == 21 || kind == TER_WATER2;
}

inline bool is_pure_water_terrain(uint32_t kind) {
    return kind == TER_RIVER || kind == TER_WATER2;
}

inline bool is_impassable_kind(uint32_t kind) {
    return kind == 0 || kind == 19 || kind == 20 || kind == 22 || kind == 23;
}

inline bool is_ship_formation(uint32_t form) {
    return form == 11 || form == 12 || form == 13 || form == 17 || form == 18 || form == 20;
}

inline bool is_transport_ship(uint32_t ship_kind) {
    return ship_kind == 17 || ship_kind == 18 || ship_kind == 19 || ship_kind == 20;
}

inline bool is_melee_formation(uint32_t form) {
    return (form >= 1 && form <= 7 && form != FORM_YANXING) || form == FORM_SIYI;
}

inline bool is_fast_formation(uint32_t form) {
    return form == FORM_FENGSHI || form == FORM_ZHUIXING || form == FORM_SIYI;
}

inline bool is_siege_formation(uint32_t formation_id) {
    return formation_id == FORM_JINGLAN || formation_id == FORM_CHONGCHE || formation_id == FORM_TOUSHI;
}

inline int32_t siege_fire_radius(uint32_t formation_id) {
    if (formation_id == FORM_TOUSHI) return 4;
    if (formation_id == FORM_CHONGCHE) return 2;
    if (formation_id == FORM_JINGLAN) return 3;
    return 1;
}

inline bool is_fighting_task(uint32_t task) {
    return task == 1 || task == 2 || task == 3 || task == 8 || task == 9 || task == 10 ||
           task == 13 || task == 14 || task == 15 || task == 16 || task == 21 || task == 22;
}

inline bool is_siege_task(uint32_t task) {
    return task == 2 || task == 8 || task == 9 || task == 10 || task == 22;
}

inline bool is_shore_task(uint32_t task) {
    return task == 1 || task == 3 || task == 13 || task == 14 || task == 15 || task == 16 || task == 21 || task == 23;
}

inline bool is_field_order(int32_t act) {
    return (act >= 1 && act <= 7) || act == ACT_WAIT || act == ACT_MARCH_ALT;
}

inline bool read_tile(const San14TacticalHostOps* ops, uint32_t tile_id, San14TacticalTile& out) {
    out = {};
    out.struct_size = sizeof(out);
    if (!ops || !ops->get_tile || tile_id > MAX_TILE_ID) return false;
    return ops->get_tile(ops->ops_context, tile_id, &out) == SAN14_OK;
}

inline bool has_world_tail(const San14TacticalWorld* w) {
    return w && w->struct_size >= sizeof(San14TacticalWorld);
}

inline bool has_host_ops_tail(const San14TacticalHostOps* ops) {
    return ops && ops->struct_size > SAN14_TACTICAL_HOST_OPS_V1_SIZE;
}

inline const San14TacticalCity* find_city(const San14TacticalWorld* w, uint32_t city_id) {
    if (!w || !w->cities || city_id == 0) return nullptr;
    for (uint32_t i = 0; i < w->city_count; ++i) {
        if (w->cities[i].city_id == city_id) return &w->cities[i];
    }
    return nullptr;
}

inline const San14TacticalDistrict* find_district(const San14TacticalWorld* w, uint32_t district_id) {
    if (!w || !w->districts || district_id == 0) return nullptr;
    for (uint32_t i = 0; i < w->district_count; ++i) {
        if (w->districts[i].district_id == district_id) return &w->districts[i];
    }
    return nullptr;
}

inline const San14TacticalUnit* find_unit(const San14TacticalWorld* w, uint32_t slot) {
    if (!w || !w->units || slot == 0) return nullptr;
    for (uint32_t i = 0; i < w->unit_count; ++i) {
        if (w->units[i].slot == slot) return &w->units[i];
    }
    return nullptr;
}

inline const San14TacticalFormation* find_formation(const San14TacticalWorld* w, uint32_t formation_id) {
    if (!w || !w->formations) return nullptr;
    for (uint32_t i = 0; i < w->formation_count; ++i) {
        if (w->formations[i].formation_id == formation_id) return &w->formations[i];
    }
    return nullptr;
}

inline const San14TacticalUnitExt* find_unit_ext(const San14TacticalWorld* w, uint32_t slot) {
    if (!w || slot == 0 || w->struct_size < offsetof(San14TacticalWorld, unit_ext_count) + sizeof(w->unit_ext_count) ||
        !w->unit_exts) {
        return nullptr;
    }
    for (uint32_t i = 0; i < w->unit_ext_count; ++i) {
        if (w->unit_exts[i].struct_size >= sizeof(San14TacticalUnitExt) && w->unit_exts[i].unit_slot == slot) {
            return &w->unit_exts[i];
        }
    }
    return nullptr;
}

inline const San14TacticalForceExt* find_force_ext(const San14TacticalWorld* w, uint32_t force_id) {
    if (!w || force_id == 0 || w->struct_size < offsetof(San14TacticalWorld, force_ext_count) + sizeof(w->force_ext_count) ||
        !w->force_exts) {
        return nullptr;
    }
    for (uint32_t i = 0; i < w->force_ext_count; ++i) {
        if (w->force_exts[i].struct_size >= sizeof(San14TacticalForceExt) && w->force_exts[i].force_id == force_id) {
            return &w->force_exts[i];
        }
    }
    return nullptr;
}

inline const San14TacticalArea* find_area(const San14TacticalWorld* w, uint32_t area_id) {
    if (!w || area_id == 0 || w->struct_size < offsetof(San14TacticalWorld, area_count) + sizeof(w->area_count) ||
        !w->areas) {
        return nullptr;
    }
    for (uint32_t i = 0; i < w->area_count; ++i) {
        if (w->areas[i].struct_size >= sizeof(San14TacticalArea) && w->areas[i].area_id == area_id) {
            return &w->areas[i];
        }
    }
    return nullptr;
}

inline const San14TacticalGate* find_gate(const San14TacticalWorld* w, uint32_t foothold_id) {
    if (!w || foothold_id == 0 || w->struct_size < offsetof(San14TacticalWorld, gate_count) + sizeof(w->gate_count) ||
        !w->gates) {
        return nullptr;
    }
    for (uint32_t i = 0; i < w->gate_count; ++i) {
        if (w->gates[i].struct_size >= sizeof(San14TacticalGate) &&
            (w->gates[i].foothold_id == foothold_id || w->gates[i].gate_id == foothold_id)) {
            return &w->gates[i];
        }
    }
    return nullptr;
}

inline uint32_t foothold_tile_id(const San14TacticalWorld* w, uint32_t foothold_id) {
    if (foothold_id == 0) return SAN14_TACTICAL_INVALID_TILE_ID;
    if (const auto* c = find_city(w, foothold_id)) return c->tile_id;
    if (const auto* g = find_gate(w, foothold_id)) return g->tile_id;
    return SAN14_TACTICAL_INVALID_TILE_ID;
}

inline uint32_t foothold_owner_force(const San14TacticalWorld* w, uint32_t foothold_id) {
    if (foothold_id == 0) return 0;
    if (const auto* c = find_city(w, foothold_id)) return c->force_id;
    if (const auto* g = find_gate(w, foothold_id)) return g->owner_force_id;
    return 0;
}

inline bool is_enemy_force(const San14TacticalWorld* w, const San14TacticalHostOps* ops, uint32_t a, uint32_t b) {
    if (a == 0 || b == 0 || a == b) return false;
    if (ops && ops->is_enemy_force) return ops->is_enemy_force(ops->ops_context, a, b) != 0;
    if (!w || !w->treaties || !w->treaty_stride || a >= w->treaty_stride || b >= w->treaty_stride) {
        return true;
    }
    return (w->treaties[a * w->treaty_stride + b] & 0x7fu) == 0;
}

inline bool is_player_city(const San14TacticalWorld* w, const San14TacticalCity& c) {
    return (w && !w->is_spectator_mode && w->player_force_id != 0 &&
            c.force_id == w->player_force_id && c.is_player_direct != 0);
}

inline bool is_player_unit(const San14TacticalWorld* w, const San14TacticalUnit& u) {
    return (w && !w->is_spectator_mode && w->player_force_id != 0 &&
            u.force_id == w->player_force_id && u.is_player_direct != 0);
}

inline bool is_player_force_id(const San14TacticalWorld* w, uint32_t force_id) {
    if (!w || w->is_spectator_mode || w->player_force_id == 0 || force_id != w->player_force_id) return false;
    for (uint32_t i = 0; i < w->district_count; ++i) {
        if (w->districts[i].force_id == force_id && w->districts[i].is_player_direct == 0) return false;
    }
    return true;
}

inline bool has_host_ab_provider(const San14TacticalHostOps* ops) {
    return ops &&
           ops->struct_size >= offsetof(San14TacticalHostOps, get_force_ab_override) + sizeof(ops->get_force_ab_override) &&
           ops->get_force_ab_override != nullptr;
}

inline uint32_t effective_mask_for_force(
    const State& st,
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    uint32_t force_id
) {
    if (force_id == 0 || is_player_force_id(w, force_id)) return 0u;
    const uint32_t base = st.config.to_mask();

    // 1. Check Host-provided per-force A/B override first (adapter env or harness).
    if (has_host_ab_provider(ops)) {
        San14TacticalForceAbOverride ov{sizeof(ov)};
        if (ops->get_force_ab_override(ops->ops_context, force_id, &ov) == SAN14_OK && ov.has_override) {
            switch (ov.mode) {
                case SAN14_TACTICAL_AB_VANILLA: return 0u;
                case SAN14_TACTICAL_AB_LEGACY: return SAN14_TACTICAL_MOD_ALL_LEGACY3;
                case SAN14_TACTICAL_AB_NEW8: return SAN14_TACTICAL_MOD_ALL_NEW8;
                case SAN14_TACTICAL_AB_FUSED: return SAN14_TACTICAL_MOD_ALL_NEW8 | SAN14_TACTICAL_MOD_ALL_LEGACY3;
                case SAN14_TACTICAL_AB_CUSTOM_MASK: return ov.enabled_module_mask;
                case SAN14_TACTICAL_AB_ALL_ACTIVE:
                default: break;
            }
        }
    }

    // 2. Check plugin-side env A/B configuration.
    if (st.ab.active) {
        if (st.ab.has_mask_a && force_id == st.ab.force_a) return st.ab.mask_a;
        if (st.ab.has_mask_b && force_id == st.ab.force_b) return st.ab.mask_b;
        if (st.ab.has_default_mask) return st.ab.default_mask;
        return 0u;
    }

    // 3. Normal player mode: 100% active for all AI forces.
    return base;
}

inline bool force_has_module(
    const State& st,
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    uint32_t force_id,
    uint32_t module_bit
) {
    return (effective_mask_for_force(st, w, ops, force_id) & module_bit) != 0;
}

inline bool issue_unit_order(
    const San14TacticalHostOps* ops,
    uint32_t unit_slot,
    int32_t order_code,
    int32_t target_type,
    int32_t target_id
) {
    if (!ops || !ops->set_unit_route) return false;
    const uint32_t waypoints[5] = {
        SAN14_TACTICAL_INVALID_TILE_ID,
        SAN14_TACTICAL_INVALID_TILE_ID,
        SAN14_TACTICAL_INVALID_TILE_ID,
        SAN14_TACTICAL_INVALID_TILE_ID,
        SAN14_TACTICAL_INVALID_TILE_ID
    };
    const uint8_t refresh = (order_code == ACT_WAIT) ? 0 : 1;
    return ops->set_unit_route(
        ops->ops_context, unit_slot, order_code, target_type, target_id, waypoints, refresh
    ) == SAN14_OK;
}

inline void seed_walkable_terrains(State& st, const San14TacticalWorld* w, const San14TacticalHostOps* ops) {
    if (!st.walk_seeded) {
        static constexpr uint32_t seed[] = {1, 2, 3, 5, 9, 10, 11, 12, 14, 15, 18};
        for (uint32_t t : seed) st.walkable_terrains[t] = true;
        st.walk_seeded = true;
    }
    if (!w || !ops || !ops->get_tile) return;
    for (uint32_t i = 0; i < w->unit_count; ++i) {
        const auto& u = w->units[i];
        if (u.kind != 1 || u.troops <= 0 || u.tile_id > MAX_TILE_ID || is_ship_formation(u.land_formation)) continue;
        San14TacticalTile tile{};
        if (read_tile(ops, u.tile_id, tile) && tile.terrain_kind < 256) {
            st.walkable_terrains[tile.terrain_kind] = true;
        }
    }
}

inline bool is_walkable_land_kind(const State& st, uint32_t kind) {
    if (kind >= 256 || is_impassable_kind(kind)) return false;
    return st.walkable_terrains[kind];
}

inline void record_telemetry(State& st, uint32_t force_id, uint32_t module_bit) {
    if (force_id == 0 || force_id >= MAX_FORCES) return;
    auto& t = st.telemetry[force_id];
    t.struct_size = sizeof(San14TacticalForceTelemetry);
    t.force_id = force_id;
    switch (module_bit) {
        case SAN14_TACTICAL_MOD_DEPLOY: ++t.deploy_overrides; break;
        case SAN14_TACTICAL_MOD_RALLY: ++t.rally_holds; break;
        case SAN14_TACTICAL_MOD_ENGAGE: ++t.engage_retargets; break;
        case SAN14_TACTICAL_MOD_RETREAT: ++t.retreat_triggers; break;
        case SAN14_TACTICAL_MOD_STANCE: ++t.stance_adjusts; break;
        case SAN14_TACTICAL_MOD_WALLS: ++t.walls_actions; break;
        case SAN14_TACTICAL_MOD_SIEGEPLAN: ++t.siegeplan_adjusts; break;
        case SAN14_TACTICAL_MOD_GATE: ++t.gate_actions; break;
        default: ++t.legacy_actions; break;
    }
}

}  // namespace tactical_ai_policy
}  // namespace companion_mod

#endif
