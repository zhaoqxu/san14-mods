#ifndef SAN14_COMPANION_TACTICAL_AI_POLICY_H
#define SAN14_COMPANION_TACTICAL_AI_POLICY_H

#include "bt_tactical_modules.h"

namespace companion_mod {
namespace tactical_ai_policy {

constexpr int32_t WATER_DEFENSE_MAX_DISTANCE = 15;
constexpr int32_t LAND_DEFENSE_ENGAGE_RADIUS = 10;
constexpr int32_t CITY_FRONT_APPROACH_RADIUS = 6;
constexpr int32_t EMERGENCY_DEFENSE_SUPPORT_RADIUS = 18;
constexpr int32_t EMERGENCY_DEFENSE_SUPPORT_CAP = 45000;
constexpr int32_t EMERGENCY_CAPITAL_FLOOR = 15000;
constexpr int32_t EMERGENCY_SURPLUS_QUOTA_CAP = 65000;
constexpr int32_t EMERGENCY_WEALTH_GOLD_MIN = 10000;
constexpr int32_t EMERGENCY_WEALTH_FOOD_MIN = 40000;
constexpr int32_t EMERGENCY_DISTANT_TRANSPORT_HEXES = 6;
constexpr int32_t EMERGENCY_RECALL_MIN_THREAT = 1000;
constexpr int32_t EMERGENCY_RECALL_TROOP_FLOOR = 1500;
constexpr int32_t LOCAL_DEFENSE_CITY_RESERVE = 5000;
constexpr int32_t DEFENSE_SORTIE_MIN_TROOPS = 1000;
constexpr uint32_t DEFENSE_SORTIE_MAX_UNITS_PER_WAVE = 4;
constexpr int32_t DEFENSE_SORTIE_MAX_TROOPS_PER_WAVE = 16000;
constexpr double DEFENSE_SUPPLY_RATIO_MIN = 3.0;
constexpr double DEFENSE_SUPPLY_RATIO_COMFORTABLE = 6.0;
constexpr double DEFENSE_NORMAL_FIELD_RATIO = 0.85;
constexpr double DEFENSE_LOW_SUPPLY_FIELD_RATIO = 0.50;
constexpr double DEFENSE_SORTIE_FOOD_BASE_PER_TROOP = 2.0;
constexpr double DEFENSE_SORTIE_FOOD_PER_HEX = 0.35;
constexpr uint32_t DEFENSE_SORTIE_COOLDOWN_MS = 15000;
constexpr int32_t DEFENSE_ENGAGEMENT_BUFFER_HEXES = 2;

constexpr int32_t SIEGE_PREVIEW_TICKS = 384;
constexpr int32_t MOVEMENT_TICKS_PER_DAY = 12;
constexpr int32_t TURN_DAYS = 10;
constexpr int32_t SIEGE_INTERCEPT_BUFFER = 12;

inline uint32_t preferred_land_formation(const San14TacticalOfficer& off, int32_t dist) {
    const uint32_t mask = off.land_formation_mask;
    if (dist <= 4 && (mask & (1u << 2))) return 2;
    if (mask & (1u << 1)) return 1;
    if (mask & (1u << 3)) return 3;
    return 1;
}

struct SupplyState {
    int32_t food = 0;
    int32_t garrison_troops = 0;
    int32_t deployed_troops = 0;
    int32_t troop_burden = 1;
    double ratio = 0.0;
};

inline SupplyState compute_supply_state(const San14TacticalWorld* w, const San14TacticalCity& c) {
    SupplyState s{};
    s.food = (std::max)(0, c.food);
    s.garrison_troops = (std::max)(0, c.troops);
    if (w && w->units && c.home_global_id > 0) {
        for (uint32_t i = 0; i < w->unit_count; ++i) {
            const auto& u = w->units[i];
            if ((u.kind == 1 || u.kind == 2) && u.troops > 0 && u.home_global_id == c.home_global_id) {
                s.deployed_troops += u.troops;
            }
        }
    }
    s.troop_burden = (std::max)(1, s.garrison_troops + s.deployed_troops);
    s.ratio = static_cast<double>(s.food) / static_cast<double>(s.troop_burden);
    return s;
}

inline bool assess_water_route(const San14TacticalHostOps* ops, uint32_t city_tile, uint32_t enemy_tile) {
    if (!ops || city_tile > MAX_TILE_ID || enemy_tile > MAX_TILE_ID) return false;
    San14TacticalTile et{};
    if (read_tile(ops, enemy_tile, et) && is_water_kind(et.terrain_kind, et.is_water)) return true;

    const int32_t dist = hex_dist(city_tile, enemy_tile);
    if (dist <= 0 || dist > WATER_DEFENSE_MAX_DISTANCE) return false;

    const int32_t cx = static_cast<int32_t>(city_tile % GRID_WIDTH);
    const int32_t cy = static_cast<int32_t>(city_tile / GRID_WIDTH);
    const int32_t ex = static_cast<int32_t>(enemy_tile % GRID_WIDTH);
    const int32_t ey = static_cast<int32_t>(enemy_tile / GRID_WIDTH);
    int water_hits = 0;
    const int steps = (std::max)(2, dist);
    for (int s = 1; s < steps; ++s) {
        const int32_t sx = cx + ((ex - cx) * s) / steps;
        const int32_t sy = cy + ((ey - cy) * s) / steps;
        if (sx < 0 || sx >= GRID_WIDTH || sy < 0 || sy >= GRID_WIDTH) continue;
        const uint32_t tid = static_cast<uint32_t>(sy * GRID_WIDTH + sx);
        San14TacticalTile t{};
        if (read_tile(ops, tid, t) && is_water_kind(t.terrain_kind, t.is_water)) {
            ++water_hits;
            if (water_hits >= 2) return true;
        }
    }
    return false;
}

inline uint32_t choose_water_intercept_tile(
    const San14TacticalHostOps* ops,
    uint32_t city_tile,
    uint32_t enemy_tile,
    const std::unordered_set<uint32_t>& occupied
) {
    if (!ops || city_tile > MAX_TILE_ID || enemy_tile > MAX_TILE_ID) return SAN14_TACTICAL_INVALID_TILE_ID;
    uint32_t best = SAN14_TACTICAL_INVALID_TILE_ID;
    int32_t best_score = -1000000;

    std::array<uint32_t, 6> ring1{};
    const int n1 = collect_neighbors(enemy_tile, ring1);
    auto consider_tile = [&](uint32_t tid) {
        if (tid > MAX_TILE_ID || occupied.count(tid)) return;
        San14TacticalTile t{};
        if (!read_tile(ops, tid, t) || is_impassable_kind(t.terrain_kind)) return;
        if (t.occupant_unit_slot != 0) return;
        const int32_t dc = hex_dist(tid, city_tile);
        const int32_t de = hex_dist(tid, enemy_tile);
        int32_t score = (is_water_kind(t.terrain_kind, t.is_water) ? 200 : 40) - de * 15 - dc * 4;
        if (score > best_score) {
            best_score = score;
            best = tid;
        }
    };

    for (int i = 0; i < n1; ++i) {
        consider_tile(ring1[i]);
        std::array<uint32_t, 6> ring2{};
        const int n2 = collect_neighbors(ring1[i], ring2);
        for (int j = 0; j < n2; ++j) consider_tile(ring2[j]);
    }
    return best;
}

inline int32_t naval_officer_score(const San14TacticalOfficer& off, const San14TacticalHostOps* ops = nullptr) {
    int32_t score = off.leadership * 2 + off.strength + off.max_troops / 100;
    if (off.naval_formation == 12) score += 300;
    else if (off.naval_formation == 13) score += 260;
    else if (off.naval_formation == 11) score += 140;
    for (int i = 0; i < 5; ++i) {
        if (off.traits[i] == 27) score += 220;
        else if (off.traits[i] == 11) score += 120;
    }
    if (ops && ops->struct_size >= offsetof(San14TacticalHostOps, get_officer_traits) + sizeof(ops->get_officer_traits) &&
        ops->get_officer_traits) {
        San14OfficerTraits traits{sizeof(traits)};
        if (ops->get_officer_traits(ops->ops_context, off.officer_id, &traits) == SAN14_OK &&
            traits.struct_size >= sizeof(traits) && traits.officer_id == off.officer_id) {
            const uint32_t slots = (std::min)(traits.active_slots, 9u);
            for (uint32_t slot = 5; slot < slots; ++slot) {
                if (traits.ids[slot] == 27) score += 220;
                else if (traits.ids[slot] == 11) score += 120;
            }
        }
    }
    return score;
}

inline int32_t emergency_threat_for_city(
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    const San14TacticalCity& city
) {
    int32_t total = 0;
    if (!w || !w->units) return 0;
    for (uint32_t i = 0; i < w->unit_count; ++i) {
        const auto& u = w->units[i];
        if (u.kind != 1 || u.troops <= 0) continue;
        if (!is_enemy_force(w, ops, u.force_id, city.force_id)) continue;
        const int32_t d = hex_dist(u.tile_id, city.tile_id);
        const bool direct = (u.target_id == city.city_id) ||
                            (u.target_tile_id <= MAX_TILE_ID && hex_dist(u.target_tile_id, city.tile_id) <= 2);
        if (d <= LAND_DEFENSE_ENGAGE_RADIUS || (direct && d <= WATER_DEFENSE_MAX_DISTANCE)) {
            total += u.troops;
        }
    }
    return total;
}

inline bool cities_adjacent(
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    const San14TacticalCity& a,
    const San14TacticalCity& b
) {
    if (a.city_id == b.city_id) return false;
    const int32_t direct = hex_dist(a.tile_id, b.tile_id);
    if (direct > EMERGENCY_DEFENSE_SUPPORT_RADIUS) return false;
    if (!ops || !ops->get_tile || a.tile_id > MAX_TILE_ID || b.tile_id > MAX_TILE_ID) return true;

    const int32_t ax = static_cast<int32_t>(a.tile_id % GRID_WIDTH);
    const int32_t ay = static_cast<int32_t>(a.tile_id / GRID_WIDTH);
    const int32_t bx = static_cast<int32_t>(b.tile_id % GRID_WIDTH);
    const int32_t by = static_cast<int32_t>(b.tile_id / GRID_WIDTH);
    const int steps = (std::max)(2, direct);
    for (int s = 1; s < steps; ++s) {
        const int32_t sx = ax + ((bx - ax) * s) / steps;
        const int32_t sy = ay + ((by - ay) * s) / steps;
        const uint32_t tid = static_cast<uint32_t>(sy * GRID_WIDTH + sx);
        San14TacticalTile t{};
        if (read_tile(ops, tid, t) && t.area_city_id != 0 &&
            t.area_city_id != a.city_id && t.area_city_id != b.city_id) {
            const auto* mid = find_city(w, t.area_city_id);
            if (mid && is_enemy_force(w, ops, mid->force_id, a.force_id)) return false;
        }
    }
    return true;
}

inline int32_t compute_city_reserve_floor(
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    const San14TacticalCity& city
) {
    if (emergency_threat_for_city(w, ops, city) >= EMERGENCY_RECALL_MIN_THREAT) {
        return EMERGENCY_CAPITAL_FLOOR;
    }
    for (uint32_t i = 0; i < w->city_count; ++i) {
        const auto& other = w->cities[i];
        if (other.city_id == city.city_id || other.force_id != city.force_id) continue;
        if (emergency_threat_for_city(w, ops, other) >= EMERGENCY_RECALL_MIN_THREAT &&
            cities_adjacent(w, ops, city, other)) {
            return EMERGENCY_CAPITAL_FLOOR;
        }
    }
    return LOCAL_DEFENSE_CITY_RESERVE;
}

struct ThreatDesc {
    const San14TacticalUnit* unit = nullptr;
    int32_t distance = 9999;
    int32_t urgency = 0;
    bool naval = false;
    bool siege = false;
};

inline std::vector<ThreatDesc> collect_city_threats(
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    const San14TacticalCity& city,
    bool include_naval,
    bool include_siege
) {
    std::vector<ThreatDesc> out;
    if (!w || !w->units) return out;
    for (uint32_t i = 0; i < w->unit_count; ++i) {
        const auto& u = w->units[i];
        if (u.kind != 1 || u.troops <= 0) continue;
        if (!is_enemy_force(w, ops, u.force_id, city.force_id)) continue;
        const int32_t dist = hex_dist(u.tile_id, city.tile_id);
        const bool is_siege = is_siege_formation(u.land_formation);
        const bool is_naval = (dist <= WATER_DEFENSE_MAX_DISTANCE) &&
                              assess_water_route(ops, city.tile_id, u.tile_id);
        const bool matches_naval = include_naval && is_naval;
        const bool matches_siege = include_siege && is_siege && dist <= WATER_DEFENSE_MAX_DISTANCE;
        const bool matches_land = dist <= LAND_DEFENSE_ENGAGE_RADIUS;
        if (!matches_naval && !matches_siege && !matches_land) continue;

        ThreatDesc td{};
        td.unit = &u;
        td.distance = dist;
        td.naval = is_naval;
        td.siege = is_siege;
        td.urgency = u.troops + (is_siege ? 4000 : 0) + (is_naval ? 2000 : 0) - dist * 250;
        out.push_back(td);
    }
    std::sort(out.begin(), out.end(), [](const ThreatDesc& a, const ThreatDesc& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        if (a.urgency != b.urgency) return a.urgency > b.urgency;
        return a.unit->slot < b.unit->slot;
    });
    return out;
}

inline void run_emergency_recall_and_relief(
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    State& st
) {
    if (!w || !ops) return;
    if (ops->return_unit) {
        for (uint32_t i = 0; i < w->unit_count; ++i) {
            const auto& u = w->units[i];
            if (u.kind != 1 || u.troops < EMERGENCY_RECALL_TROOP_FLOOR || is_player_unit(w, u)) continue;
            if (!force_has_module(st, w, ops, u.force_id, SAN14_TACTICAL_MOD_LEGACY_REINFORCE)) continue;
            if (u.order_code == 11 || u.order_code == 12 || u.task_locked) continue;
            // Do not pull back units actively managed by BT defensive/siege modules.
            const auto oit = st.order_mem.find(u.slot);
            if (oit != st.order_mem.end() && oit->second.mod) continue;
            for (uint32_t c = 0; c < w->city_count; ++c) {
                const auto& home = w->cities[c];
                if (home.home_global_id != u.home_global_id || home.force_id != u.force_id) continue;
                if (hex_dist(u.tile_id, home.tile_id) <= LAND_DEFENSE_ENGAGE_RADIUS) break;
                const int32_t threat = emergency_threat_for_city(w, ops, home);
                if (threat >= EMERGENCY_RECALL_MIN_THREAT && home.troops < threat) {
                    if (ops->return_unit(ops->ops_context, u.slot, 0, 0, 1) == SAN14_OK) {
                        record_telemetry(st, u.force_id, SAN14_TACTICAL_MOD_LEGACY_REINFORCE);
                    }
                }
                break;
            }
        }
    }

    if (!ops->deploy_unit || !ops->list_city_officers) return;
    for (uint32_t c = 0; c < w->city_count; ++c) {
        const auto& victim = w->cities[c];
        if (is_player_city(w, victim)) continue;
        if (!force_has_module(st, w, ops, victim.force_id, SAN14_TACTICAL_MOD_LEGACY_REINFORCE)) continue;
        const int32_t threat = emergency_threat_for_city(w, ops, victim);
        if (threat < EMERGENCY_RECALL_MIN_THREAT || victim.troops >= threat) continue;

        auto threats = collect_city_threats(w, ops, victim, false, true);
        if (threats.empty()) continue;

        for (uint32_t s = 0; s < w->city_count; ++s) {
            const auto& src = w->cities[s];
            if (src.city_id == victim.city_id || src.force_id != victim.force_id || is_player_city(w, src)) continue;
            const auto* dist = find_district(w, src.district_id);
            if (!dist || dist->remaining_commands == 0) continue;
            if (src.troops <= EMERGENCY_CAPITAL_FLOOR + DEFENSE_SORTIE_MIN_TROOPS) continue;
            if (src.food < 10000 || src.gold < 500) continue;
            if (!cities_adjacent(w, ops, src, victim)) continue;
            if (emergency_threat_for_city(w, ops, src) >= EMERGENCY_RECALL_MIN_THREAT) continue;

            std::array<San14TacticalOfficer, 64> offs{};
            uint32_t off_count = 0;
            if (ops->list_city_officers(ops->ops_context, src.city_id, offs.data(), 64, &off_count) != SAN14_OK ||
                off_count == 0) {
                continue;
            }
            const uint32_t n = (std::min)(off_count, uint32_t(64));
            std::sort(offs.begin(), offs.begin() + n, [](const San14TacticalOfficer& a, const San14TacticalOfficer& b) {
                if (a.max_troops != b.max_troops) return a.max_troops > b.max_troops;
                return a.officer_id < b.officer_id;
            });
            const auto& off = offs[0];
            const int32_t surplus = src.troops - EMERGENCY_CAPITAL_FLOOR;
            const int32_t troops = (std::min)({surplus, off.max_troops, DEFENSE_SORTIE_MAX_TROOPS_PER_WAVE});
            if (troops < DEFENSE_SORTIE_MIN_TROOPS) continue;

            San14TacticalDeployRequest req{};
            req.struct_size = sizeof(req);
            std::fill_n(req.waypoint_tiles, 5, SAN14_TACTICAL_INVALID_TILE_ID);
            req.officer_id = off.officer_id;
            req.formation_id = preferred_land_formation(off, hex_dist(src.tile_id, victim.tile_id));
            req.naval_formation_id = off.naval_formation ? off.naval_formation : 11;
            req.troops = troops;
            req.order_code = 7;
            req.target_type = 0x1b;
            req.target_id = threats[0].unit->slot;
            San14TacticalDeployResult dep{sizeof(dep)};
            if (ops->deploy_unit(ops->ops_context, &req, &dep) == SAN14_OK && dep.unit_slot > 0) {
                st.assignments[dep.unit_slot] = {
                    dep.unit_slot, off.officer_id, victim.city_id, victim.force_id,
                    threats[0].unit->slot, SAN14_TACTICAL_INVALID_TILE_ID, false, false
                };
                record_telemetry(st, victim.force_id, SAN14_TACTICAL_MOD_LEGACY_REINFORCE);
                break;
            }
        }
    }
}

inline void mobilize_city_waves(
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    State& st,
    bool enable_naval,
    bool enable_siege
) {
    if (!w || !ops || !ops->deploy_unit || !ops->list_city_officers) return;
    std::unordered_set<uint32_t> occupied;
    for (uint32_t i = 0; i < w->unit_count; ++i) {
        if (w->units[i].tile_id <= MAX_TILE_ID) occupied.insert(w->units[i].tile_id);
    }

    for (uint32_t c = 0; c < w->city_count; ++c) {
        const auto& city = w->cities[c];
        if (is_player_city(w, city)) continue;
        const bool city_naval = enable_naval && force_has_module(st, w, ops, city.force_id, SAN14_TACTICAL_MOD_LEGACY_NAVAL);
        const bool city_siege = enable_siege && force_has_module(st, w, ops, city.force_id, SAN14_TACTICAL_MOD_LEGACY_SIEGE_INT);
        if (!city_naval && !city_siege) continue;

        const auto* dist = find_district(w, city.district_id);
        if (!dist || dist->remaining_commands == 0) continue;

        auto threats = collect_city_threats(w, ops, city, city_naval, city_siege);
        threats.erase(std::remove_if(threats.begin(), threats.end(), [&](const ThreatDesc& td) {
            return !(city_naval && td.naval) && !(city_siege && td.siege);
        }), threats.end());
        if (threats.empty()) continue;

        const auto cd_it = st.city_cooldown_ms.find(city.city_id);
        if (cd_it != st.city_cooldown_ms.end() &&
            w->now_ms >= cd_it->second &&
            (w->now_ms - cd_it->second) < DEFENSE_SORTIE_COOLDOWN_MS) {
            continue;
        }

        const SupplyState sup = compute_supply_state(w, city);
        if (sup.ratio < DEFENSE_SUPPLY_RATIO_MIN) continue;

        const bool use_emerg = force_has_module(st, w, ops, city.force_id, SAN14_TACTICAL_MOD_LEGACY_REINFORCE);
        const int32_t reserve = use_emerg
            ? compute_city_reserve_floor(w, ops, city)
            : LOCAL_DEFENSE_CITY_RESERVE;
        int32_t surplus = city.troops - reserve;
        if (surplus < DEFENSE_SORTIE_MIN_TROOPS) continue;

        std::array<San14TacticalOfficer, 64> offs{};
        uint32_t off_count = 0;
        if (ops->list_city_officers(ops->ops_context, city.city_id, offs.data(), 64, &off_count) != SAN14_OK ||
            off_count == 0) {
            continue;
        }
        const uint32_t n_offs = (std::min)(off_count, uint32_t(64));
        const bool lead_naval = threats[0].naval;
        std::sort(offs.begin(), offs.begin() + n_offs, [&](const San14TacticalOfficer& a, const San14TacticalOfficer& b) {
            if (lead_naval) {
                const int32_t sa = naval_officer_score(a, ops);
                const int32_t sb = naval_officer_score(b, ops);
                if (sa != sb) return sa > sb;
            }
            if (a.max_troops != b.max_troops) return a.max_troops > b.max_troops;
            return a.officer_id < b.officer_id;
        });

        const double field_ratio = sup.ratio >= DEFENSE_SUPPLY_RATIO_COMFORTABLE
            ? DEFENSE_NORMAL_FIELD_RATIO : DEFENSE_LOW_SUPPLY_FIELD_RATIO;
        const int32_t opposing = threats[0].unit->troops;
        const int32_t target_troops = static_cast<int32_t>(std::ceil(opposing * field_ratio));

        int32_t drafted = 0;
        uint32_t launched = 0;
        uint32_t off_idx = 0;
        for (const auto& td : threats) {
            if (launched >= DEFENSE_SORTIE_MAX_UNITS_PER_WAVE ||
                launched >= dist->remaining_commands ||
                off_idx >= n_offs ||
                surplus < DEFENSE_SORTIE_MIN_TROOPS ||
                drafted >= DEFENSE_SORTIE_MAX_TROOPS_PER_WAVE) {
                break;
            }
            const auto& off = offs[off_idx++];
            const uint32_t hex_d = static_cast<uint32_t>((std::max)(1, (std::min)(WATER_DEFENSE_MAX_DISTANCE, td.distance)));
            const double food_per_troop = DEFENSE_SORTIE_FOOD_BASE_PER_TROOP + hex_d * DEFENSE_SORTIE_FOOD_PER_HEX;
            const double food_budget = sup.food - sup.troop_burden * DEFENSE_SUPPLY_RATIO_MIN;
            const int32_t food_cap = food_budget > 0.0 ? static_cast<int32_t>(food_budget / food_per_troop) : 0;
            const int32_t troops = (std::min)({
                off.max_troops, surplus, food_cap,
                DEFENSE_SORTIE_MAX_TROOPS_PER_WAVE - drafted,
                (std::max)(DEFENSE_SORTIE_MIN_TROOPS, target_troops)
            });
            if (troops < DEFENSE_SORTIE_MIN_TROOPS) continue;

            San14TacticalDeployRequest req{};
            req.struct_size = sizeof(req);
            std::fill_n(req.waypoint_tiles, 5, SAN14_TACTICAL_INVALID_TILE_ID);
            req.officer_id = off.officer_id;
            req.formation_id = preferred_land_formation(off, td.distance);
            req.naval_formation_id = off.naval_formation ? off.naval_formation : 11;
            req.troops = troops;

            uint32_t station = SAN14_TACTICAL_INVALID_TILE_ID;
            if (td.naval) {
                station = choose_water_intercept_tile(ops, city.tile_id, td.unit->tile_id, occupied);
                if (station <= MAX_TILE_ID) {
                    req.order_code = 1;
                    req.target_type = 9;
                    req.target_id = station;
                } else {
                    req.order_code = 7;
                    req.target_type = 0x1b;
                    req.target_id = td.unit->slot;
                }
            } else {
                req.order_code = 7;
                req.target_type = 0x1b;
                req.target_id = td.unit->slot;
            }

            San14TacticalDeployResult res{sizeof(res)};
            if (ops->deploy_unit(ops->ops_context, &req, &res) == SAN14_OK && res.unit_slot > 0) {
                ++launched;
                drafted += res.deployed_troops;
                surplus -= res.deployed_troops;
                if (station <= MAX_TILE_ID) occupied.insert(station);
                st.assignments[res.unit_slot] = {
                    res.unit_slot, off.officer_id, city.city_id, city.force_id,
                    td.unit->slot, station, td.naval, td.siege
                };
                record_telemetry(st, city.force_id, td.naval ? SAN14_TACTICAL_MOD_LEGACY_NAVAL : SAN14_TACTICAL_MOD_LEGACY_SIEGE_INT);
            }
        }
        if (launched > 0) {
            st.city_cooldown_ms[city.city_id] = w->now_ms;
        }
    }
}

inline San14Result SAN14_MOD_CALL on_eval_target_strength(
    void* plugin_context, const San14TacticalWorld* w, const San14TacticalHostOps* ops,
    uint32_t district_id, uint32_t target_city_id, int32_t base_strength, int32_t, int32_t* out_corrected_strength
) {
    if (!plugin_context || !w || !out_corrected_strength) return SAN14_E_INVALID_ARGUMENT;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    cache_world_metadata(*st, w);

    const auto* dist = find_district(w, district_id);
    const uint32_t att_force = dist ? dist->force_id : 0u;
    const auto* target = find_city(w, target_city_id);
    if (!target) return SAN14_E_UNSUPPORTED;

    const bool do_siegeplan = (att_force != 0) && force_has_module(*st, w, ops, att_force, SAN14_TACTICAL_MOD_SIEGEPLAN);
    const bool do_legacy = (att_force != 0)
        ? force_has_module(*st, w, ops, att_force, SAN14_TACTICAL_MOD_LEGACY_REINFORCE)
        : (st->config.emergency_reinforcement && !st->ab.active);

    if (do_siegeplan) {
        const auto& p = st->params;
        float ratio = 1.0f;
        if (ops && ops->struct_size >= offsetof(San14TacticalHostOps, eval_area_bonus) + sizeof(ops->eval_area_bonus) &&
            ops->eval_area_bonus && target->tile_id <= MAX_TILE_ID && target->force_id != 0 && att_force != target->force_id) {
            float md = 0.0f, ma = 0.0f;
            int32_t nh = 0;
            const int32_t cx = static_cast<int32_t>(target->tile_id % GRID_WIDTH);
            const int32_t cy = static_cast<int32_t>(target->tile_id / GRID_WIDTH);
            for (int32_t dy = -3; dy <= 3; ++dy) {
                for (int32_t dx = -3; dx <= 3; ++dx) {
                    const int32_t nx = cx + dx, ny = cy + dy;
                    if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
                    const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
                    const int32_t dd = hex_dist(tid, target->tile_id);
                    if (dd < 1 || dd > 3) continue;
                    San14TacticalTile tile{};
                    if (ops->get_tile && (!read_tile(ops, tid, tile) || tile.area_id == 0)) continue;
                    float a1 = 0.0f, a2 = 0.0f;
                    int32_t s1 = 0, s2 = 0;
                    if (ops->eval_area_bonus(ops->ops_context, tid, target->force_id, &a1, &s1) != SAN14_OK ||
                        ops->eval_area_bonus(ops->ops_context, tid, att_force, &a2, &s2) != SAN14_OK) continue;
                    if (a1 >= 0.5f && a1 <= 4.0f && a2 >= 0.5f && a2 <= 4.0f) {
                        md += a1;
                        ma += a2;
                        ++nh;
                    }
                }
            }
            if (nh > 0) {
                md /= static_cast<float>(nh);
                ma /= static_cast<float>(nh);
                const float md_f = 1.0f + 0.77f * (md - 1.0f);
                const float ma_f = (std::max)(ma, 1.0f + 0.20f * (md - 1.0f));
                if (ma_f > 0.01f) {
                    const float min_r = static_cast<float>(p.siege_fu_min_pct) / 100.0f;
                    const float max_r = static_cast<float>(p.siege_fu_max_pct) / 100.0f;
                    ratio = (std::max)(min_r, (std::min)(max_r, md_f / ma_f));
                }
            }
        }

        int32_t relief_sum = 0;
        for (uint32_t i = 0; i < w->city_count; ++i) {
            const auto& c = w->cities[i];
            if (c.city_id == target->city_id || c.force_id != target->force_id || c.gold <= 0 || c.troops <= 5000) continue;
            const int32_t spare = c.troops - 5000;
            const int32_t d = hex_dist(c.tile_id, target->tile_id);
            int32_t weight = 0;
            if (d <= 18) weight = p.siege_relief_w18;
            else if (d <= 30) weight = p.siege_relief_w30;
            else if (d <= 45) weight = p.siege_relief_w45;
            relief_sum += (spare * weight) / 100;
        }
        relief_sum = (std::min)(relief_sum, p.siege_relief_cap);
        *out_corrected_strength = static_cast<int32_t>(static_cast<float>(base_strength) * ratio + 0.5f) + relief_sum;
        record_telemetry(*st, att_force, SAN14_TACTICAL_MOD_SIEGEPLAN);
        return SAN14_OK;
    }

    if (!do_legacy) return SAN14_E_UNSUPPORTED;

    int32_t supplement = 0;
    for (uint32_t i = 0; i < w->unit_count; ++i) {
        const auto& u = w->units[i];
        if (u.kind != 1 || u.troops <= 0 || u.force_id != target->force_id) continue;
        if (u.target_id == target->city_id ||
            hex_dist(u.tile_id, target->tile_id) <= CITY_FRONT_APPROACH_RADIUS) {
            supplement += u.troops;
        }
    }
    for (uint32_t i = 0; i < w->city_count; ++i) {
        const auto& c = w->cities[i];
        if (c.city_id == target->city_id || c.force_id != target->force_id || is_player_city(w, c)) continue;
        if (hex_dist(c.tile_id, target->tile_id) <= EMERGENCY_DEFENSE_SUPPORT_RADIUS &&
            c.troops > EMERGENCY_CAPITAL_FLOOR) {
            supplement += (c.troops - EMERGENCY_CAPITAL_FLOOR);
        }
    }
    supplement = (std::min)(supplement, EMERGENCY_DEFENSE_SUPPORT_CAP);
    *out_corrected_strength = base_strength + supplement;
    return SAN14_OK;
}

inline San14Result SAN14_MOD_CALL on_eval_reserve(
    void* plugin_context, const San14TacticalWorld* w, const San14TacticalHostOps* ops,
    uint32_t source_city_id, int32_t current_base_reserve, int32_t current_quota,
    int32_t* out_new_reserve, int32_t* out_new_quota
) {
    if (!plugin_context || !w || !out_new_reserve || !out_new_quota) return SAN14_E_INVALID_ARGUMENT;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);

    const auto* city = find_city(w, source_city_id);
    if (!city) return SAN14_E_UNSUPPORTED;

    const bool do_siegeplan = force_has_module(*st, w, ops, city->force_id, SAN14_TACTICAL_MOD_SIEGEPLAN);
    const bool do_legacy = force_has_module(*st, w, ops, city->force_id, SAN14_TACTICAL_MOD_LEGACY_REINFORCE);
    if (!do_siegeplan && !do_legacy) return SAN14_E_UNSUPPORTED;

    const int32_t threat = emergency_threat_for_city(w, ops, *city);
    if (do_siegeplan) {
        int32_t new_reserve = current_base_reserve;
        if (threat == 0) {
            new_reserve = (current_base_reserve * st->params.siege_reserve_ratio_pct) / 100;
        } else if (do_legacy) {
            new_reserve = (std::max)(current_base_reserve, EMERGENCY_CAPITAL_FLOOR);
        }
        int32_t new_quota = current_quota;
        if (do_legacy && threat == 0 && city->gold >= EMERGENCY_WEALTH_GOLD_MIN &&
            city->food >= EMERGENCY_WEALTH_FOOD_MIN && city->troops > new_reserve) {
            const int32_t avail = city->troops - new_reserve;
            new_quota = (std::min)(EMERGENCY_SURPLUS_QUOTA_CAP, (std::max)(current_quota, avail));
        }
        *out_new_reserve = new_reserve;
        *out_new_quota = new_quota;
        record_telemetry(*st, city->force_id, SAN14_TACTICAL_MOD_SIEGEPLAN);
        return SAN14_OK;
    }

    const int32_t floor = compute_city_reserve_floor(w, ops, *city);
    const int32_t new_reserve = (std::max)(current_base_reserve, floor);
    int32_t new_quota = current_quota;
    if (threat == 0 && city->gold >= EMERGENCY_WEALTH_GOLD_MIN &&
        city->food >= EMERGENCY_WEALTH_FOOD_MIN && city->troops > new_reserve) {
        const int32_t avail = city->troops - new_reserve;
        new_quota = (std::min)(EMERGENCY_SURPLUS_QUOTA_CAP, (std::max)(current_quota, avail));
    }
    *out_new_reserve = new_reserve;
    *out_new_quota = new_quota;
    return SAN14_OK;
}

inline San14Result SAN14_MOD_CALL on_eval_command_rollback(
    void* plugin_context, uint32_t district_id, uint32_t remaining_commands, int32_t* out_force_skip
) {
    return on_eval_command_rollback_ex(plugin_context, nullptr, nullptr, district_id, 0, remaining_commands, out_force_skip);
}

inline San14Result SAN14_MOD_CALL on_eval_transport_include(
    void* plugin_context, const San14TacticalWorld* w, const San14TacticalHostOps* ops,
    uint32_t unit_slot, uint32_t dest_city_id, int32_t* out_include
) {
    if (!plugin_context || !w || !out_include) return SAN14_E_INVALID_ARGUMENT;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);

    const auto* unit = find_unit(w, unit_slot);
    const auto* city = find_city(w, dest_city_id);
    if (!unit || !city) return SAN14_E_UNSUPPORTED;
    if (!force_has_module(*st, w, ops, city->force_id, SAN14_TACTICAL_MOD_LEGACY_REINFORCE)) {
        return SAN14_E_UNSUPPORTED;
    }
    if (unit->kind == 2 && hex_dist(unit->tile_id, city->tile_id) > EMERGENCY_DISTANT_TRANSPORT_HEXES) {
        *out_include = 0;
        return SAN14_OK;
    }
    *out_include = 1;
    return SAN14_OK;
}

inline San14Result SAN14_MOD_CALL on_eval_unit_return(
    void* plugin_context, const San14TacticalWorld* w, const San14TacticalHostOps* ops,
    uint32_t unit_slot, int32_t, int32_t, int32_t, int32_t* out_allow
) {
    if (!plugin_context || !w || !out_allow) return SAN14_E_INVALID_ARGUMENT;
    *out_allow = 1;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    const auto* unit = find_unit(w, unit_slot);
    if (!unit || !force_has_module(*st, w, ops, unit->force_id, SAN14_TACTICAL_MOD_LEGACY_REINFORCE)) {
        return SAN14_OK;
    }
    const auto it = st->assignments.find(unit_slot);
    if (it == st->assignments.end()) return SAN14_OK;
    const auto* city = find_city(w, it->second.city_id);
    if (city && emergency_threat_for_city(w, ops, *city) >= EMERGENCY_RECALL_MIN_THREAT) {
        *out_allow = 0;
    }
    return SAN14_OK;
}

inline San14Result SAN14_MOD_CALL on_eval_retreat(
    void* plugin_context, const San14TacticalWorld* w, const San14TacticalHostOps* ops,
    uint32_t unit_slot, int32_t native_result, int32_t* out_override
) {
    if (!plugin_context || !w || !out_override) return SAN14_E_INVALID_ARGUMENT;
    *out_override = -1;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (native_result != 1) return SAN14_OK;
    const auto* unit = find_unit(w, unit_slot);
    if (!unit || unit->troops < DEFENSE_SORTIE_MIN_TROOPS) return SAN14_OK;
    if (!force_has_module(*st, w, ops, unit->force_id, SAN14_TACTICAL_MOD_LEGACY_REINFORCE)) return SAN14_OK;

    const auto it = st->assignments.find(unit_slot);
    if (it != st->assignments.end()) {
        const auto* city = find_city(w, it->second.city_id);
        if (city && emergency_threat_for_city(w, ops, *city) >= EMERGENCY_RECALL_MIN_THREAT) {
            *out_override = 0;
        }
    }
    return SAN14_OK;
}

inline San14Result SAN14_MOD_CALL on_select_defense_officer(
    void* plugin_context, const San14TacticalWorld* w, const San14TacticalHostOps* ops,
    uint32_t source_city_id, int32_t, uint32_t planned_target_tile_id,
    const uint32_t* candidate_officer_ids, uint32_t candidate_count,
    uint32_t* out_promoted_officer_id, uint32_t* out_override_target_tile_id
) {
    if (!plugin_context || !w || !ops || !candidate_officer_ids ||
        !out_promoted_officer_id || !out_override_target_tile_id) {
        return SAN14_E_INVALID_ARGUMENT;
    }
    *out_promoted_officer_id = 0;
    *out_override_target_tile_id = planned_target_tile_id;

    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    const auto* city = find_city(w, source_city_id);
    if (!city || is_player_city(w, *city) || candidate_count == 0) return SAN14_OK;

    const bool do_naval = force_has_module(*st, w, ops, city->force_id, SAN14_TACTICAL_MOD_LEGACY_NAVAL);
    const bool do_siege = force_has_module(*st, w, ops, city->force_id, SAN14_TACTICAL_MOD_LEGACY_SIEGE_INT);
    if (!do_naval && !do_siege) return SAN14_E_UNSUPPORTED;

    const bool water_threat = do_naval &&
        assess_water_route(ops, city->tile_id, planned_target_tile_id);

    uint32_t best_id = 0;
    int32_t best_score = -1000000;
    for (uint32_t i = 0; i < candidate_count; ++i) {
        San14TacticalOfficer off{sizeof(off)};
        if (ops->get_officer(ops->ops_context, candidate_officer_ids[i], &off) != SAN14_OK) continue;
        if (off.has_active_unit || off.has_active_command) continue;
        const int32_t score = water_threat
            ? naval_officer_score(off, ops)
            : (off.leadership * 2 + off.strength + off.max_troops / 100);
        if (score > best_score) {
            best_score = score;
            best_id = off.officer_id;
        }
    }
    *out_promoted_officer_id = best_id;
    if (water_threat) {
        std::unordered_set<uint32_t> occ;
        const uint32_t station = choose_water_intercept_tile(ops, city->tile_id, planned_target_tile_id, occ);
        if (station <= MAX_TILE_ID) *out_override_target_tile_id = station;
    }
    return SAN14_OK;
}

inline San14Result SAN14_MOD_CALL on_filter_order_target(
    void* plugin_context, const San14TacticalWorld* w, const San14TacticalHostOps* ops,
    uint32_t unit_slot, int32_t, int32_t, San14TacticalOrderProposal* out_proposal
) {
    if (!plugin_context || !w || !ops || !out_proposal) return SAN14_E_INVALID_ARGUMENT;
    out_proposal->decision = SAN14_TACTICAL_ORDER_KEEP;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);

    const auto* unit = find_unit(w, unit_slot);
    if (!unit || unit->kind != 1 || unit->troops < DEFENSE_SORTIE_MIN_TROOPS || is_player_unit(w, *unit)) {
        return SAN14_OK;
    }
    if (!force_has_module(*st, w, ops, unit->force_id, SAN14_TACTICAL_MOD_LEGACY_SIEGE_INT)) {
        return st->config.any_enabled() ? SAN14_OK : SAN14_E_UNSUPPORTED;
    }
    // Yield if a BT module (e.g., gate / stance) holds this unit.
    const auto oit = st->order_mem.find(unit_slot);
    if (oit != st->order_mem.end() && oit->second.mod && oit->second.owner != OWNER_NONE) {
        return SAN14_OK;
    }

    for (uint32_t i = 0; i < w->unit_count; ++i) {
        const auto& enemy = w->units[i];
        if (enemy.kind != 1 || enemy.troops <= 0 || !is_siege_formation(enemy.land_formation)) continue;
        if (!is_enemy_force(w, ops, enemy.force_id, unit->force_id)) continue;
        if (hex_dist(unit->tile_id, enemy.tile_id) <= LAND_DEFENSE_ENGAGE_RADIUS) {
            std::fill_n(out_proposal->waypoints, 5, SAN14_TACTICAL_INVALID_TILE_ID);
            out_proposal->decision = SAN14_TACTICAL_ORDER_REPLACE_ROUTE;
            out_proposal->order_code = 7;
            out_proposal->target_type = 0x1b;
            out_proposal->target_id = static_cast<int32_t>(enemy.slot);
            out_proposal->refresh_route = 1;
            record_telemetry(*st, unit->force_id, SAN14_TACTICAL_MOD_LEGACY_SIEGE_INT);
            return SAN14_OK;
        }
    }
    return SAN14_OK;
}

inline void SAN14_MOD_CALL on_strategy_update(
    void* plugin_context, const San14TacticalWorld* w, const San14TacticalHostOps* ops,
    San14TacticalStrategyPhase phase
) {
    if (!plugin_context || !w || !ops) return;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    cache_world_metadata(*st, w);
    const auto cfg = st->config;
    const bool may_have_ab = st->ab.active || has_host_ab_provider(ops);
    if (!cfg.any_legacy3() && !may_have_ab) return;

    if (phase == SAN14_TACTICAL_PHASE_PLANNING) {
        if (cfg.emergency_reinforcement || may_have_ab) {
            run_emergency_recall_and_relief(w, ops, *st);
        }
        if (cfg.naval_defense || cfg.siege_interception || may_have_ab) {
            mobilize_city_waves(w, ops, *st, cfg.naval_defense || may_have_ab, cfg.siege_interception || may_have_ab);
        }
        return;
    }

    if (phase == SAN14_TACTICAL_PHASE_POST_PLANNING && (cfg.siege_interception || may_have_ab)) {
        if (w->day != 1 && w->day != 11 && w->day != 21) return;
        const uint32_t turn_key = w->year * 10000u + w->month * 100u + w->day;
        if (st->last_siege_turn_key == turn_key) return;
        st->last_siege_turn_key = turn_key;
        mobilize_city_waves(w, ops, *st, false, true);
    }
}

inline void SAN14_MOD_CALL on_reset(void* plugin_context) {
    if (!plugin_context) return;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    st->clear_locked();
    st->refresh_ab_locked();
}

inline San14TacticalAiPolicy make_policy(State& state) {
    San14TacticalAiPolicy p{};
    p.struct_size = sizeof(p);
    p.plugin_context = &state;
    p.on_reset = &on_reset;
    p.on_strategy_update = &on_strategy_update;
    p.on_filter_order_target = &on_filter_order_target;
    p.on_eval_retreat = &on_eval_retreat;
    p.on_eval_unit_return = &on_eval_unit_return;
    p.on_eval_transport_include = &on_eval_transport_include;
    p.on_eval_target_strength = &on_eval_target_strength;
    p.on_eval_candidate_reserve = &on_eval_reserve;
    p.on_eval_draft_reserve = &on_eval_reserve;
    p.on_eval_command_rollback = &on_eval_command_rollback;
    p.on_select_defense_officer = &on_select_defense_officer;
    p.on_daily_tactical_tick = &on_daily_tactical_tick;
    p.on_eval_intercept_deploy = &on_eval_intercept_deploy;
    p.on_eval_intercept_gate = &on_eval_intercept_gate;
    p.on_eval_command_rollback_ex = &on_eval_command_rollback_ex;
    p.on_post_order_target = &on_post_order_target;
    p.on_query_telemetry = &on_query_telemetry;
    return p;
}

}  // namespace tactical_ai_policy
}  // namespace companion_mod

#endif
