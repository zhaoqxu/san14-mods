#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <array>
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "../src/tactical_ai_policy.h"

namespace tac = companion_mod::tactical_ai_policy;

namespace {

uint32_t tile_at(uint32_t x, uint32_t y) { return y * 220u + x; }

struct RouteCmd {
    uint32_t slot = 0;
    int32_t order_code = 0;
    int32_t target_type = 0;
    int32_t target_id = 0;
};

struct TroopsCmd {
    uint32_t troops_id = 0;
    int32_t purpose = 0;
    uint32_t goal_id = 0;
};

struct MockBtEnv {
    std::unordered_map<uint32_t, San14TacticalTile> tiles;
    std::unordered_map<uint32_t, San14TacticalOfficer> officers;
    std::unordered_map<uint32_t, float> area_bonus_by_force;
    std::unordered_map<uint64_t, float> exchange_ratios;  // ((uint64_t)atk_form << 32) | def_form
    std::unordered_map<uint32_t, San14TacticalForceAbOverride> ab_overrides;
    std::vector<RouteCmd> route_cmds;
    std::vector<TroopsCmd> troops_cmds;
    std::vector<San14TacticalUnit>* live_units = nullptr;
    std::vector<San14TacticalUnitExt>* live_exts = nullptr;
};

San14Result SAN14_MOD_CALL mock_get_tile(void* ctx, uint32_t id, San14TacticalTile* out) {
    auto* env = static_cast<MockBtEnv*>(ctx);
    auto it = env->tiles.find(id);
    if (it != env->tiles.end()) {
        *out = it->second;
    } else {
        *out = {};
        out->struct_size = sizeof(*out);
        out->tile_id = id;
        out->terrain_kind = 1;  // passable plain
    }
    return SAN14_OK;
}

int32_t SAN14_MOD_CALL mock_hex_dist(void*, uint32_t a, uint32_t b) { return tac::hex_dist(a, b); }

San14Result SAN14_MOD_CALL mock_get_officer(void* ctx, uint32_t id, San14TacticalOfficer* out) {
    auto* env = static_cast<MockBtEnv*>(ctx);
    auto it = env->officers.find(id);
    if (it == env->officers.end()) return SAN14_E_NOT_FOUND;
    *out = it->second;
    return SAN14_OK;
}

int32_t SAN14_MOD_CALL mock_enemy(void*, uint32_t a, uint32_t b) { return (a && b && a != b) ? 1 : 0; }

San14Result SAN14_MOD_CALL mock_set_route(
    void* ctx, uint32_t slot, int32_t order_code, int32_t target_type, int32_t target_id,
    const uint32_t[5], uint8_t
) {
    auto* env = static_cast<MockBtEnv*>(ctx);
    env->route_cmds.push_back({slot, order_code, target_type, target_id});
    if (env->live_units) {
        for (auto& u : *env->live_units) {
            if (u.slot == slot) {
                u.order_code = static_cast<uint32_t>(order_code);
                u.target_type = static_cast<uint32_t>(target_type);
                u.target_id = static_cast<uint32_t>(target_id);
                break;
            }
        }
    }
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL mock_eval_area_bonus(
    void* ctx, uint32_t, uint32_t force_id, float* out_mult, int32_t* out_sum
) {
    auto* env = static_cast<MockBtEnv*>(ctx);
    auto it = env->area_bonus_by_force.find(force_id);
    *out_mult = (it != env->area_bonus_by_force.end()) ? it->second : 1.0f;
    *out_sum = static_cast<int32_t>((*out_mult - 1.0f) * 100.0f);
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL mock_eval_exchange(
    void* ctx, uint32_t, uint32_t atk_form, uint32_t, uint32_t def_form, float* out_ratio
) {
    auto* env = static_cast<MockBtEnv*>(ctx);
    const uint64_t k = (static_cast<uint64_t>(atk_form) << 32) | def_form;
    auto it = env->exchange_ratios.find(k);
    *out_ratio = (it != env->exchange_ratios.end()) ? it->second : 1.0f;
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL mock_set_troops_purpose(
    void* ctx, uint32_t troops_id, int32_t purpose, uint32_t goal_id
) {
    auto* env = static_cast<MockBtEnv*>(ctx);
    env->troops_cmds.push_back({troops_id, purpose, goal_id});
    if (env->live_exts) {
        for (auto& e : *env->live_exts) {
            if (e.troops_id == troops_id) {
                e.troops_purpose = static_cast<uint32_t>(purpose);
                e.troops_goal_id = goal_id;
            }
        }
    }
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL mock_get_ab(void* ctx, uint32_t force_id, San14TacticalForceAbOverride* out) {
    auto* env = static_cast<MockBtEnv*>(ctx);
    auto it = env->ab_overrides.find(force_id);
    if (it != env->ab_overrides.end()) {
        *out = it->second;
    } else {
        *out = {sizeof(*out), 0, {}, SAN14_TACTICAL_AB_ALL_ACTIVE, 0};
    }
    return SAN14_OK;
}

San14TacticalHostOps make_ops(MockBtEnv& env) {
    San14TacticalHostOps ops{};
    ops.struct_size = sizeof(ops);
    ops.ops_context = &env;
    ops.get_tile = &mock_get_tile;
    ops.hex_distance = &mock_hex_dist;
    ops.get_officer = &mock_get_officer;
    ops.is_enemy_force = &mock_enemy;
    ops.set_unit_route = &mock_set_route;
    ops.eval_area_bonus = &mock_eval_area_bonus;
    ops.eval_unit_exchange_ratio = &mock_eval_exchange;
    ops.set_troops_purpose = &mock_set_troops_purpose;
    ops.get_force_ab_override = &mock_get_ab;
    return ops;
}

tac::Config all_new8_cfg() {
    tac::Config c{};
    c.bt_deploy = true;
    c.bt_rally = true;
    c.bt_engage = true;
    c.bt_retreat = true;
    c.bt_stance = true;
    c.bt_walls = true;
    c.bt_siegeplan = true;
    c.bt_gate = true;
    return c;
}

San14TacticalUnit make_unit(uint32_t slot, uint32_t force, uint32_t cmd, int32_t troops, uint32_t form, uint32_t tile, int32_t act = 1) {
    San14TacticalUnit u{};
    u.struct_size = sizeof(u);
    u.slot = slot;
    u.force_id = force;
    u.home_global_id = force * 100u;
    u.kind = 1;
    u.commander_id = cmd;
    u.troops = troops;
    u.morale = 100;
    u.land_formation = form;
    u.naval_formation = 11;
    u.tile_id = tile;
    u.order_code = static_cast<uint32_t>(act);
    u.strength = static_cast<float>(troops);
    return u;
}

San14TacticalUnitExt make_ext(uint32_t slot, uint32_t tid, uint32_t purpose, uint32_t goal, uint32_t lead_off, float fstr = 0.0f) {
    San14TacticalUnitExt e{};
    e.struct_size = sizeof(e);
    e.unit_slot = slot;
    e.troops_id = tid;
    e.troops_purpose = purpose;
    e.troops_goal_id = goal;
    e.troops_leader_officer_id = lead_off;
    e.troops_fighting_strength = fstr;
    return e;
}

void test_deploy_module() {
    tac::State st;
    st.set_config(all_new8_cfg());
    auto policy = tac::make_policy(st);
    MockBtEnv env;
    env.officers[1] = {sizeof(San14TacticalOfficer), 1, 1, 1, 100, 100, 85, 80, 70, (1u << 1) | (1u << 2), 11, 12000, {}, 0, 0, 0, 0};
    env.officers[2] = {sizeof(San14TacticalOfficer), 2, 1, 1, 100, 100, 70, 70, 60, (1u << 1) | (1u << 2), 11, 10000, {}, 0, 0, 0, 0};
    env.officers[3] = {sizeof(San14TacticalOfficer), 3, 1, 1, 100, 100, 65, 65, 50, (1u << 1), 11, 9000, {}, 0, 0, 0, 0};
    env.officers[4] = {sizeof(San14TacticalOfficer), 4, 1, 1, 100, 100, 45, 45, 40, (1u << 1) | (1u << 3), 11, 6000, {}, 0, 0, 0, 0};
    const uint32_t cands[4] = {1, 2, 3, 4};

    San14TacticalDistrict dist{sizeof(San14TacticalDistrict), 1, 1, 5, 0, {}};
    San14TacticalWorld w{}; w.struct_size = sizeof(w); w.year = 220; w.month = 1; w.day = 1;
    w.districts = &dist; w.district_count = 1;
    auto ops = make_ops(env);

    San14TacticalInterceptDeployContext ctx{sizeof(ctx), 1, 1, 1, 0, 1, 1, 11, 9000, 12000, 9000, 0, 99, cands, 4};
    San14TacticalInterceptDeployDecision dec{sizeof(dec)};
    assert(policy.on_eval_intercept_deploy(policy.plugin_context, &w, &ops, &ctx, &dec) == SAN14_OK);
    assert(dec.skip_deploy == 0 && dec.override_troops == 3000 && dec.override_formation_id == 0);

    // 2nd strong officer gets flank Fengshi (2) and share 3000.
    ctx.officer_id = 2; ctx.sent_count = 1; ctx.max_troops = 10000; ctx.troops = 6000;
    assert(policy.on_eval_intercept_deploy(policy.plugin_context, &w, &ops, &ctx, &dec) == SAN14_OK);
    assert(dec.override_troops == 3000 && dec.override_formation_id == tac::FORM_FENGSHI);

    // Weak officer (< 60 leadership) capped at 2000 and switched to Yanxing (3).
    ctx.officer_id = 4; ctx.sent_count = 3; ctx.max_troops = 6000; ctx.troops = 5000;
    assert(policy.on_eval_intercept_deploy(policy.plugin_context, &w, &ops, &ctx, &dec) == SAN14_OK);
    assert(dec.override_troops == 2000 && dec.override_formation_id == tac::FORM_YANXING);
}

void test_stance_engage_rally_modules() {
    tac::State st;
    st.set_config(all_new8_cfg());
    auto policy = tac::make_policy(st);
    MockBtEnv env;
    auto ops = make_ops(env);

    std::vector<San14TacticalCity> cities = {
        {sizeof(San14TacticalCity), 1, 1, 1, 5000, 20000, 100000, 100, tile_at(10, 10), 100, 0, {}},
        {sizeof(San14TacticalCity), 2, 2, 2, 5000, 20000, 100000, 100, tile_at(50, 50), 200, 0, {}},
    };
    // Unit 1: Yanxing touching enemy 10 -> should back off behind friendly shield unit 2.
    // Unit 2, 3: Melee in troops 1 attacking enemy 10/11 (load split).
    // Unit 4: Waiting melee touching enemy 11 -> waitfight attacks enemy 11.
    // Unit 5 (lead) & Unit 6 (lagging 6 hexes behind toward home): intercept rally holds 5 and advances 6.
    std::vector<San14TacticalUnit> units = {
        make_unit(1, 1, 101, 4000, tac::FORM_YANXING, tile_at(20, 20), tac::ACT_ATTACK_ARMY),
        make_unit(2, 1, 102, 7000, tac::FORM_YULIN, tile_at(19, 20), tac::ACT_ATTACK_ARMY),
        make_unit(3, 1, 103, 7000, tac::FORM_FENGSHI, tile_at(20, 22), tac::ACT_ATTACK_ARMY),
        make_unit(4, 1, 104, 5000, tac::FORM_YULIN, tile_at(21, 21), tac::ACT_WAIT),
        make_unit(5, 1, 105, 5000, tac::FORM_YULIN, tile_at(17, 10), tac::ACT_MOVE),
        make_unit(6, 1, 106, 4000, tac::FORM_YULIN, tile_at(11, 10), tac::ACT_MOVE),
        make_unit(10, 2, 201, 6000, tac::FORM_YULIN, tile_at(21, 20), tac::ACT_ATTACK_ARMY),
        make_unit(11, 2, 202, 5000, tac::FORM_YANXING, tile_at(22, 21), tac::ACT_WAIT),
    };
    units[1].target_type = tac::TT_ARMY; units[1].target_id = 10;
    units[2].target_type = tac::TT_ARMY; units[2].target_id = 10;

    std::vector<San14TacticalUnitExt> exts = {
        make_ext(1, 1, 1, 0, 102),
        make_ext(2, 1, 1, 0, 102),
        make_ext(3, 1, 1, 0, 102),
        make_ext(4, 2, 1, 0, 104),
        make_ext(5, 3, 1, 0, 105),
        make_ext(6, 3, 1, 0, 105),
    };
    env.live_units = &units;
    env.live_exts = &exts;

    San14TacticalWorld w{};
    w.struct_size = sizeof(w);
    w.year = 220; w.month = 1; w.day = 2;
    w.cities = cities.data(); w.city_count = static_cast<uint32_t>(cities.size());
    w.units = units.data(); w.unit_count = static_cast<uint32_t>(units.size());
    w.unit_exts = exts.data(); w.unit_ext_count = static_cast<uint32_t>(exts.size());

    policy.on_daily_tactical_tick(policy.plugin_context, &w, &ops);

    // Yanxing unit 1 backed off (>= 2 hexes from enemies 10/11).
    assert(units[0].order_code == tac::ACT_MOVE);
    assert(tac::hex_dist(units[0].target_id, units[6].tile_id) >= 2);
    assert(tac::hex_dist(units[0].target_id, units[7].tile_id) >= 2);

    // Fengshi unit 3 prioritized Yanxing enemy 11 over overloaded 10.
    assert(units[2].order_code == tac::ACT_ATTACK_ARMY && units[2].target_id == 11);

    // Waitfight unit 4 attacked adjacent enemy 10/11.
    assert(units[3].order_code == tac::ACT_ATTACK_ARMY && (units[3].target_id == 10 || units[3].target_id == 11));

    // Intercept rally held lead 5 and moved lagging 6 to lead 5's tile.
    assert(units[4].order_code == tac::ACT_WAIT);
    assert(units[5].order_code == tac::ACT_MOVE && units[5].target_id == units[4].tile_id);
}

void test_retreat_and_gate_modules() {
    tac::State st;
    st.set_config(all_new8_cfg());
    auto policy = tac::make_policy(st);
    MockBtEnv env;
    auto ops = make_ops(env);

    // Place a gate at (20, 20) owned by force 1, with wall of mountain tiles (terrain 19) on y != 20.
    const uint32_t gate_tile = tile_at(20, 20);
    env.tiles[gate_tile] = {sizeof(San14TacticalTile), gate_tile, tac::TER_GATE, 1, 1, 1, 1, 0, 0, {}};
    for (uint32_t y = 10; y <= 30; ++y) {
        if (y == 20) continue;
        const uint32_t wt = tile_at(20, y);
        env.tiles[wt] = {sizeof(San14TacticalTile), wt, 19, 99, 1, 1, 0, 0, 0, {}};
    }

    std::vector<San14TacticalGate> gates = {
        {sizeof(San14TacticalGate), 1, 52, gate_tile, 1, 1}
    };
    std::vector<San14TacticalCity> cities = {
        {sizeof(San14TacticalCity), 1, 1, 1, 5000, 20000, 100000, 100, tile_at(12, 20), 100, 0, {}},
        {sizeof(San14TacticalCity), 2, 2, 2, 5000, 20000, 100000, 100, tile_at(30, 20), 200, 0, {}},
    };
    // Force 1: Yanxing 1 behind gate at (17, 20), melee 2 behind gate at (18, 20) -> melee 2 sent into city 1, Yanxing 1 moves to firing hex (19, 20).
    // Force 2: Melee attacker 10 at (21, 20) outside gate (8000 troops) + extra enemy 11 at (22, 20) (8000 troops).
    // Because enemy 10/11 are behind Force 1's own gate... wait: Force 1 OWNS the gate, so enemy 10/11 CANNOT cross it!
    // Therefore ReachBlocked zeroes 10/11, so Force 1 troops 1 DOES NOT retreat despite 16000 > 1.3 * 4000!
    std::vector<San14TacticalUnit> units = {
        make_unit(1, 1, 101, 4000, tac::FORM_YANXING, tile_at(17, 20), tac::ACT_MOVE),
        make_unit(2, 1, 102, 4000, tac::FORM_YULIN, tile_at(18, 20), tac::ACT_MOVE),
        make_unit(10, 2, 201, 8000, tac::FORM_YULIN, tile_at(21, 20), tac::ACT_ATTACK_GATE),
        make_unit(11, 2, 202, 8000, tac::FORM_YULIN, tile_at(22, 20), tac::ACT_MOVE),
    };
    std::vector<San14TacticalUnitExt> exts = {
        make_ext(1, 1, 1, 0, 101, 4000.0f),
        make_ext(2, 2, 1, 0, 102, 4000.0f),
    };
    env.live_units = &units;
    env.live_exts = &exts;

    San14TacticalWorld w{};
    w.struct_size = sizeof(w);
    w.year = 220; w.month = 1; w.day = 1;  // Turn start!
    w.cities = cities.data(); w.city_count = static_cast<uint32_t>(cities.size());
    w.gates = gates.data(); w.gate_count = static_cast<uint32_t>(gates.size());
    w.units = units.data(); w.unit_count = static_cast<uint32_t>(units.size());
    w.unit_exts = exts.data(); w.unit_ext_count = static_cast<uint32_t>(exts.size());

    policy.on_daily_tactical_tick(policy.plugin_context, &w, &ops);

    // Gate placed Yanxing 1 at a firing hex on our side (x < 20, distance 2 from attacker (21,20)), sent melee 2 into city 1.
    assert(units[0].order_code == tac::ACT_MOVE && tac::hex_dist(units[0].target_id, units[2].tile_id) == 2 &&
           (units[0].target_id % 220u) < 20u);
    assert(units[1].order_code == tac::ACT_ENTER_CITY && units[1].target_id == 1);
    // GateGuard blocks leaving through the gate!
    units[0].tile_id = units[0].target_id;
    San14TacticalOrderProposal prop{sizeof(prop)};
    assert(policy.on_post_order_target(policy.plugin_context, &w, &ops, 1, tac::TT_HEX, static_cast<int32_t>(tile_at(21, 20)), &prop) == SAN14_OK);
    assert(prop.decision == SAN14_TACTICAL_ORDER_REPLACE_ROUTE && prop.order_code == tac::ACT_WAIT);
    // Retreat did NOT falsely fire across own gate!
    assert(env.troops_cmds.empty());

    // Now test genuine field retreat on day 11 (open plain far from home, no gate blocking).
    w.day = 11;
    units[0].tile_id = tile_at(30, 30);
    units[2].tile_id = tile_at(32, 30);
    units[3].tile_id = tile_at(33, 30);
    policy.on_daily_tactical_tick(policy.plugin_context, &w, &ops);
    assert(env.troops_cmds.size() == 1 && env.troops_cmds[0].troops_id == 1 && env.troops_cmds[0].purpose == 0);
}

void test_walls_siegeplan_ab_and_telemetry() {
    tac::State st;
    auto cfg = all_new8_cfg();
    cfg.emergency_reinforcement = true;  // Fused config!
    st.set_config(cfg);
    auto policy = tac::make_policy(st);
    MockBtEnv env;
    auto ops = make_ops(env);

    // Force 1 = FUSED, Force 2 = VANILLA via A/B override.
    env.ab_overrides[1] = {sizeof(San14TacticalForceAbOverride), 1, {}, SAN14_TACTICAL_AB_FUSED, 0};
    env.ab_overrides[2] = {sizeof(San14TacticalForceAbOverride), 1, {}, SAN14_TACTICAL_AB_VANILLA, 0};
    env.area_bonus_by_force[1] = 1.0f;
    env.area_bonus_by_force[2] = 1.3f;

    std::vector<San14TacticalDistrict> dists = {
        {sizeof(San14TacticalDistrict), 1, 1, 3, 0, {}},
        {sizeof(San14TacticalDistrict), 2, 2, 3, 0, {}},
    };
    std::vector<San14TacticalCity> cities = {
        {sizeof(San14TacticalCity), 1, 1, 1, 5000, 30000, 100000, 100, tile_at(10, 10), 100, 0, {}},
        {sizeof(San14TacticalCity), 2, 2, 2, 5000, 12000, 100000, 100, tile_at(30, 10), 200, 0, {}},
        {sizeof(San14TacticalCity), 3, 2, 2, 5000, 15000, 100000, 100, tile_at(45, 10), 300, 0, {}},  // 15 hexes from city 2 (w18=80%)
    };
    // Make city 2 surrounding tiles have area 10.
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            const uint32_t tid = tile_at(30u + dx, 10u + dy);
            env.tiles[tid] = {sizeof(San14TacticalTile), tid, 1, 1, 10, 2, 2, 0, 0, {}};
        }
    }
    std::vector<San14TacticalUnit> units = {
        make_unit(10, 2, 201, 5000, tac::FORM_YULIN, tile_at(12, 10), tac::ACT_ATTACK_CITY),
    };
    San14TacticalWorld w{};
    w.struct_size = sizeof(w);
    w.year = 220; w.month = 1; w.day = 1;
    w.districts = dists.data(); w.district_count = 2;
    w.cities = cities.data(); w.city_count = 3;
    w.units = units.data(); w.unit_count = 1;

    // 1. Force 1 (FUSED) evaluates target city 2: uses siegefu + 80% relief of (15000-5000)=8000 (not double-counted!).
    int32_t corrected = 0;
    assert(policy.on_eval_target_strength(policy.plugin_context, &w, &ops, 1, 2, 10000, 5000, &corrected) == SAN14_OK);
    // md=1.3 -> mdF=1+0.77*0.3=1.231, maF=1+0.2*0.3=1.06 -> ratio=1.231/1.06=1.1613 -> 11613 + 8000 = 19613.
    assert(corrected > 19000 && corrected < 20500);

    // Force 2 (VANILLA) stays UNSUPPORTED!
    assert(policy.on_eval_target_strength(policy.plugin_context, &w, &ops, 2, 1, 10000, 5000, &corrected) == SAN14_E_UNSUPPORTED);

    // 2. Reserve: Force 1 city 1 under threat (unit 10 at dist 2) keeps EMERGENCY_CAPITAL_FLOOR (15000) because legacy_reinforce is fused!
    int32_t nr = 0, nq = 0;
    assert(policy.on_eval_candidate_reserve(policy.plugin_context, &w, &ops, 1, 8000, 10000, &nr, &nq) == SAN14_OK);
    assert(nr == 15000);
    // Without threat, Force 1 scales 10000 -> 6000 (0.6x).
    units[0].tile_id = tile_at(40, 40);
    assert(policy.on_eval_candidate_reserve(policy.plugin_context, &w, &ops, 1, 10000, 10000, &nr, &nq) == SAN14_OK);
    assert(nr == 6000);

    // 3. Command rollback: Force 1 skips (siegeenough), Force 2 is UNSUPPORTED.
    int32_t fskip = 0;
    assert(policy.on_eval_command_rollback_ex(policy.plugin_context, &w, &ops, 1, 1, 0, &fskip) == SAN14_OK && fskip == 1);
    assert(policy.on_eval_command_rollback_ex(policy.plugin_context, &w, &ops, 2, 2, 0, &fskip) == SAN14_E_UNSUPPORTED);

    // 4. Intercept gate: Force 1 unlocks native block when enemy lead <= 3 hexes from city 1, halves water.
    units[0].tile_id = tile_at(12, 10);
    int32_t allow = -1, halve = 0;
    assert(policy.on_eval_intercept_gate(policy.plugin_context, &w, &ops, 1, 1, 10, 1, &allow, &halve) == SAN14_OK);
    assert(allow == 1 && halve == 1);
    assert(policy.on_eval_intercept_gate(policy.plugin_context, &w, &ops, 2, 2, 10, 1, &allow, &halve) == SAN14_OK);
    assert(allow == -1 && halve == 0);

    // 5. Telemetry recorded for Force 1, zero for Force 2.
    San14TacticalForceTelemetry tel1{sizeof(tel1)}, tel2{sizeof(tel2)};
    assert(policy.on_query_telemetry(policy.plugin_context, 1, &tel1) == SAN14_OK && tel1.siegeplan_adjusts >= 3 && tel1.walls_actions >= 1);
    assert(policy.on_query_telemetry(policy.plugin_context, 2, &tel2) == SAN14_OK && tel2.siegeplan_adjusts == 0 && tel2.walls_actions == 0);

    // 6. Old-Adapter ABI v1 degradation: small struct_size world & ops never crash!
    San14TacticalWorld old_w = w;
    old_w.struct_size = SAN14_TACTICAL_WORLD_V1_SIZE;
    San14TacticalHostOps old_ops = ops;
    old_ops.struct_size = SAN14_TACTICAL_HOST_OPS_V1_SIZE;
    policy.on_daily_tactical_tick(policy.plugin_context, &old_w, &old_ops);
    assert(policy.on_eval_target_strength(policy.plugin_context, &old_w, &old_ops, 1, 2, 10000, 5000, &corrected) == SAN14_OK);
}

void test_navaldef_and_shore_and_fu() {
    // Part A: navaldef chooses ND_WATER at port edge when LouChuan (13) exchange >= 1.0 and outside shore reach (or water-surrounded).
    {
        tac::State st;
        st.set_config(all_new8_cfg());
        auto policy = tac::make_policy(st);
        MockBtEnv env;
        auto ops = make_ops(env);

        const uint32_t port_tile = tile_at(20, 20);
        const uint32_t water1 = tile_at(21, 20);
        const uint32_t water2 = tile_at(22, 20);
        const uint32_t fu_tile = tile_at(32, 32);
        env.tiles[port_tile] = {sizeof(San14TacticalTile), port_tile, tac::TER_PORT, 1, 10, 1, 1, 0, 0, {}};
        env.tiles[water1] = {sizeof(San14TacticalTile), water1, tac::TER_RIVER, 1, 10, 1, 1, 0, 1, {}};
        env.tiles[water2] = {sizeof(San14TacticalTile), water2, tac::TER_RIVER, 1, 10, 1, 1, 0, 1, {}};
        env.tiles[fu_tile] = {sizeof(San14TacticalTile), fu_tile, 1, 1, 20, 2, 2, 0, 0, {}};

        env.exchange_ratios[(13ull << 32) | 11ull] = 1.8f;
        env.exchange_ratios[(1ull << 32) | 11ull] = 1.2f;

        std::vector<San14TacticalForceExt> fexts = {
            {sizeof(San14TacticalForceExt), 1, 13},
            {sizeof(San14TacticalForceExt), 2, 11},
        };
        std::vector<San14TacticalArea> areas = {
            {sizeof(San14TacticalArea), 10, 1, tile_at(15, 20), 1, 10, 10, {}, 1, {}},
            {sizeof(San14TacticalArea), 20, 2, fu_tile, 2, 10, 10, {10}, 0, {}},
        };
        std::vector<San14TacticalCity> cities = {
            {sizeof(San14TacticalCity), 1, 1, 1, 5000, 20000, 100000, 100, tile_at(15, 20), 100, 0, {}},
            {sizeof(San14TacticalCity), 2, 2, 2, 5000, 20000, 100000, 100, tile_at(35, 35), 200, 0, {}},
        };
        // Unit 1 is 8 hexes from enemy (outside shore reach 6, inside navaldef reach 12).
        std::vector<San14TacticalUnit> units = {
            make_unit(1, 1, 101, 5000, tac::FORM_YULIN, tile_at(14, 20), tac::ACT_MOVE),
            make_unit(10, 2, 201, 5000, tac::FORM_YULIN, water2, tac::ACT_MOVE),
            make_unit(2, 1, 102, 4000, tac::FORM_YULIN, tile_at(30, 32), tac::ACT_MOVE),
        };
        std::vector<San14TacticalUnitExt> exts = {
            make_ext(1, 1, 1, 0, 101),
            make_ext(10, 2, 1, 0, 201),
            make_ext(2, 3, 8, 2, 103),
        };
        exts[1].ship_kind = 11;
        env.live_units = &units;
        env.live_exts = &exts;

        San14TacticalWorld w{};
        w.struct_size = sizeof(w);
        w.year = 220; w.month = 1; w.day = 5;
        w.cities = cities.data(); w.city_count = 2;
        w.units = units.data(); w.unit_count = static_cast<uint32_t>(units.size());
        w.unit_exts = exts.data(); w.unit_ext_count = static_cast<uint32_t>(exts.size());
        w.force_exts = fexts.data(); w.force_ext_count = 2;
        w.areas = areas.data(); w.area_count = 2;

        policy.on_daily_tactical_tick(policy.plugin_context, &w, &ops);

        assert(units[0].order_code == tac::ACT_ATTACK_ARMY && units[0].target_id == 10);
        assert(units[2].order_code == tac::ACT_MOVE && units[2].target_id == fu_tile);
    }

    // Part B: shore strikes river-crossing enemy from bank without boarding water.
    {
        tac::State st;
        st.set_config(all_new8_cfg());
        auto policy = tac::make_policy(st);
        MockBtEnv env;
        auto ops = make_ops(env);

        const uint32_t water_hex = tile_at(22, 20);
        env.tiles[water_hex] = {sizeof(San14TacticalTile), water_hex, tac::TER_RIVER, 1, 10, 1, 0, 0, 1, {}};
        std::vector<San14TacticalCity> cities = {
            {sizeof(San14TacticalCity), 1, 1, 1, 5000, 20000, 100000, 100, tile_at(15, 20), 100, 0, {}},
        };
        std::vector<San14TacticalUnit> units = {
            make_unit(1, 1, 101, 4000, tac::FORM_YULIN, tile_at(19, 20), tac::ACT_MOVE),
            make_unit(10, 2, 201, 4000, tac::FORM_YULIN, water_hex, tac::ACT_MOVE),
        };
        std::vector<San14TacticalUnitExt> exts = {
            make_ext(1, 1, 1, 0, 101),
            make_ext(10, 2, 1, 0, 201),
        };
        env.live_units = &units;
        env.live_exts = &exts;

        San14TacticalWorld w{};
        w.struct_size = sizeof(w);
        w.year = 220; w.month = 1; w.day = 6;
        w.cities = cities.data(); w.city_count = 1;
        w.units = units.data(); w.unit_count = 2;
        w.unit_exts = exts.data(); w.unit_ext_count = 2;

        policy.on_daily_tactical_tick(policy.plugin_context, &w, &ops);
        assert(units[0].order_code == tac::ACT_MOVE && tac::hex_dist(units[0].target_id, water_hex) == 1);
    }
}

}  // namespace

int main() {
    test_deploy_module();
    test_stance_engage_rally_modules();
    test_retreat_and_gate_modules();
    test_walls_siegeplan_ab_and_telemetry();
    test_navaldef_and_shore_and_fu();
    std::cout << "bt_tactical_modules_tests OK\n";
    return 0;
}
