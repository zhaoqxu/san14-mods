#ifndef SAN14_AI_IQ_BT_TACTICAL_MODULES_H
#define SAN14_AI_IQ_BT_TACTICAL_MODULES_H

#include "tactical_ai_common.h"

namespace companion_mod {
namespace tactical_ai_policy {

struct DailyUnit {
    const San14TacticalUnit* u = nullptr;
    uint32_t troops_id = 0;
    uint32_t troops_purpose = 0;
    uint32_t troops_goal_id = 0;
    uint32_t troops_leader_officer_id = 0;
    uint32_t ship_kind = 0;
    float troops_fighting_strength = 0.0f;
    bool is_lead = false;
    bool is_ship = false;
    bool is_melee = false;
    bool is_engine = false;
    bool is_yanxing = false;
    bool on_water = false;
    uint32_t area_id = 0;
};

struct DailyContext {
    const San14TacticalWorld* w = nullptr;
    const San14TacticalHostOps* ops = nullptr;
    State* st = nullptr;
    int32_t today = 0;
    uint32_t date_key = 0;

    std::vector<DailyUnit> units;
    std::unordered_map<uint32_t, size_t> slot_to_idx;
    std::unordered_set<uint32_t> occupied_tiles;
    std::unordered_map<uint32_t, uint32_t> troops_count;
    std::unordered_map<uint32_t, San14TacticalTile> tile_cache;
    std::unordered_map<uint32_t, const San14TacticalArea*> area_by_id;
    std::unordered_map<uint32_t, const San14TacticalCity*> city_by_id;
    std::unordered_map<uint32_t, const San14TacticalGate*> gate_by_foothold;
    std::unordered_set<uint32_t> area_has_foothold;
    std::unordered_map<uint32_t, uint32_t> force_masks;
    std::unordered_map<uint32_t, uint32_t> force_ships;

    const San14TacticalTile* get_tile(uint32_t tile_id) {
        if (tile_id > MAX_TILE_ID || !ops || !ops->get_tile) return nullptr;
        auto it = tile_cache.find(tile_id);
        if (it != tile_cache.end()) return &it->second;
        San14TacticalTile t{};
        if (!read_tile(ops, tile_id, t)) return nullptr;
        auto ins = tile_cache.emplace(tile_id, t);
        return &ins.first->second;
    }

    uint32_t terrain_at(uint32_t tile_id) {
        const auto* t = get_tile(tile_id);
        return t ? t->terrain_kind : 0u;
    }

    uint32_t area_at(uint32_t tile_id) {
        const auto* t = get_tile(tile_id);
        return t ? t->area_id : 0u;
    }

    uint32_t tile_owner(uint32_t tile_id) {
        const auto* t = get_tile(tile_id);
        return t ? t->owner_force_id : 0u;
    }

    bool is_water_tile(uint32_t tile_id) {
        return is_pure_water_terrain(terrain_at(tile_id));
    }

    bool is_walkable_free(uint32_t tile_id) {
        if (tile_id > MAX_TILE_ID || occupied_tiles.count(tile_id)) return false;
        const auto* t = get_tile(tile_id);
        if (!t) return false;
        return is_walkable_land_kind(*st, t->terrain_kind);
    }

    uint32_t mask_of(uint32_t force_id) {
        auto it = force_masks.find(force_id);
        if (it != force_masks.end()) return it->second;
        const uint32_t m = effective_mask_for_force(*st, w, ops, force_id);
        force_masks[force_id] = m;
        return m;
    }

    bool has_mod(uint32_t force_id, uint32_t mod_bit) {
        return (mask_of(force_id) & mod_bit) != 0;
    }

    bool is_enemy(uint32_t a, uint32_t b) const {
        return is_enemy_force(w, ops, a, b);
    }

    uint32_t foothold_tile(uint32_t foothold_id) const {
        if (foothold_id == 0) return SAN14_TACTICAL_INVALID_TILE_ID;
        auto cit = city_by_id.find(foothold_id);
        if (cit != city_by_id.end()) return cit->second->tile_id;
        auto git = gate_by_foothold.find(foothold_id);
        if (git != gate_by_foothold.end()) return git->second->tile_id;
        return SAN14_TACTICAL_INVALID_TILE_ID;
    }

    uint32_t foothold_force(uint32_t foothold_id) const {
        if (foothold_id == 0) return 0;
        auto cit = city_by_id.find(foothold_id);
        if (cit != city_by_id.end()) return cit->second->force_id;
        auto git = gate_by_foothold.find(foothold_id);
        if (git != gate_by_foothold.end()) return git->second->owner_force_id;
        return 0;
    }

    uint32_t unit_home_city_id(const DailyUnit& du) const {
        if (du.u->home_global_id == 0) return 0;
        for (uint32_t i = 0; i < w->city_count; ++i) {
            if (w->cities[i].home_global_id == du.u->home_global_id) return w->cities[i].city_id;
        }
        return 0;
    }

    uint32_t force_ship(uint32_t force_id) const {
        auto it = force_ships.find(force_id);
        if (it != force_ships.end()) return it->second;
        return 11u;
    }
};

inline OrderMem* track_unit(DailyContext& ctx, const DailyUnit& du) {
    auto& m = ctx.st->order_mem[du.u->slot];
    if (m.commander_id != du.u->commander_id || m.force_id != du.u->force_id) {
        m = OrderMem{};
        m.commander_id = du.u->commander_id;
        m.force_id = du.u->force_id;
    }
    const int32_t act = static_cast<int32_t>(du.u->order_code);
    const int32_t tt = static_cast<int32_t>(du.u->target_type);
    const int32_t tid = static_cast<int32_t>(du.u->target_id);
    if (m.mod && (act != m.w_act || tt != m.w_type || tid != m.w_id)) {
        m.mod = 0;
        m.owner = OWNER_NONE;
    }
    if (!m.mod) {
        m.p_act = act;
        m.p_type = tt;
        m.p_id = tid;
    }
    return &m;
}

inline bool is_mine(const OrderMem* m, MemOwner owner) {
    return m && (!m->mod || m->owner == owner);
}

inline bool order_unit(
    DailyContext& ctx,
    const DailyUnit& du,
    OrderMem& m,
    MemOwner owner,
    int32_t act,
    int32_t tt,
    int32_t tid,
    uint32_t telemetry_mod = 0
) {
    if (!issue_unit_order(ctx.ops, du.u->slot, act, tt, tid)) return false;
    m.mod = 1;
    m.owner = owner;
    m.w_act = act;
    m.w_type = tt;
    m.w_id = tid;
    if (telemetry_mod != 0) record_telemetry(*ctx.st, du.u->force_id, telemetry_mod);
    return true;
}

inline void restore_unit(DailyContext& ctx, const DailyUnit& du, OrderMem& m) {
    if (!m.mod) return;
    issue_unit_order(ctx.ops, du.u->slot, m.p_act, m.p_type, m.p_id);
    m.mod = 0;
    m.owner = OWNER_NONE;
}

inline int32_t nearest_enemy_dist(const DailyContext& ctx, uint32_t tile_id, uint32_t force_id, int32_t* out_idx = nullptr) {
    int32_t best = 999;
    int32_t best_idx = -1;
    for (size_t i = 0; i < ctx.units.size(); ++i) {
        const auto& e = ctx.units[i];
        if (!ctx.is_enemy(force_id, e.u->force_id)) continue;
        const int32_t d = hex_dist(tile_id, e.u->tile_id);
        if (d < best) {
            best = d;
            best_idx = static_cast<int32_t>(i);
        }
    }
    if (out_idx) *out_idx = best_idx;
    return best;
}

inline bool has_land_foe_near(DailyContext& ctx, uint32_t force_id, uint32_t tile_id, int32_t radius) {
    for (const auto& e : ctx.units) {
        if (!ctx.is_enemy(force_id, e.u->force_id)) continue;
        if (hex_dist(tile_id, e.u->tile_id) <= radius && !e.on_water) return true;
    }
    return false;
}

inline bool has_war_foe_near(const DailyContext& ctx, uint32_t force_id, uint32_t tile_id, int32_t radius) {
    for (const auto& e : ctx.units) {
        if (e.is_ship || !ctx.is_enemy(force_id, e.u->force_id)) continue;
        if (hex_dist(tile_id, e.u->tile_id) <= radius) return true;
    }
    return false;
}

inline bool other_side_of_gate(const State& st, uint32_t from_tile, uint32_t to_tile) {
    const auto fit = st.gate_side.find(from_tile);
    if (fit == st.gate_side.end() || fit->second == 0) return false;
    const uint8_t sg = fit->second;
    const auto git = st.gate_tile.find(sg);
    if (git == st.gate_tile.end()) return false;
    const auto tit = st.gate_side.find(to_tile);
    const uint8_t tg = (tit != st.gate_side.end()) ? tit->second : 0;
    return tg != sg && hex_dist(to_tile, git->second) <= 6;
}

inline void cache_world_metadata(State& st, const San14TacticalWorld* w) {
    if (!w) return;
    for (uint32_t i = 0; i < w->district_count; ++i) {
        st.district_force_cache[w->districts[i].district_id] = w->districts[i].force_id;
    }
}

inline DailyContext build_daily_context(State& st, const San14TacticalWorld* w, const San14TacticalHostOps* ops) {
    DailyContext ctx{};
    ctx.w = w;
    ctx.ops = ops;
    ctx.st = &st;
    ctx.today = day_number(w->year, w->month, w->day);
    ctx.date_key = w->year * 10000u + w->month * 100u + w->day;

    cache_world_metadata(st, w);
    seed_walkable_terrains(st, w, ops);

    // Build lookup tables for cities, areas, gates, and force ship levels.
    for (uint32_t i = 0; i < w->city_count; ++i) {
        ctx.city_by_id[w->cities[i].city_id] = &w->cities[i];
    }
    if (w->struct_size >= offsetof(San14TacticalWorld, area_count) + sizeof(w->area_count) && w->areas) {
        for (uint32_t i = 0; i < w->area_count; ++i) {
            const auto& ar = w->areas[i];
            if (ar.struct_size >= sizeof(San14TacticalArea) && ar.area_id > 0) {
                ctx.area_by_id[ar.area_id] = &ar;
                if (ar.has_foothold) ctx.area_has_foothold.insert(ar.area_id);
            }
        }
    }
    if (w->struct_size >= offsetof(San14TacticalWorld, gate_count) + sizeof(w->gate_count) && w->gates) {
        for (uint32_t i = 0; i < w->gate_count; ++i) {
            const auto& g = w->gates[i];
            if (g.struct_size >= sizeof(San14TacticalGate) && g.foothold_id > 0) {
                ctx.gate_by_foothold[g.foothold_id] = &g;
                if (g.tile_id <= MAX_TILE_ID) {
                    const uint32_t ga = ctx.area_at(g.tile_id);
                    if (ga > 0) ctx.area_has_foothold.insert(ga);
                }
            }
        }
    }
    for (uint32_t i = 0; i < w->city_count; ++i) {
        if (w->cities[i].tile_id <= MAX_TILE_ID) {
            const uint32_t ca = ctx.area_at(w->cities[i].tile_id);
            if (ca > 0) ctx.area_has_foothold.insert(ca);
        }
    }
    if (w->struct_size >= offsetof(San14TacticalWorld, force_ext_count) + sizeof(w->force_ext_count) && w->force_exts) {
        for (uint32_t i = 0; i < w->force_ext_count; ++i) {
            const auto& fe = w->force_exts[i];
            if (fe.struct_size >= sizeof(San14TacticalForceExt) && fe.force_id > 0) {
                ctx.force_ships[fe.force_id] = (fe.naval_ship_kind >= 11 && fe.naval_ship_kind <= 13)
                    ? fe.naval_ship_kind : 11u;
            }
        }
    }

    std::unordered_map<uint32_t, const San14TacticalUnitExt*> ext_by_slot;
    if (w->struct_size >= offsetof(San14TacticalWorld, unit_ext_count) + sizeof(w->unit_ext_count) && w->unit_exts) {
        ext_by_slot.reserve(w->unit_ext_count);
        for (uint32_t i = 0; i < w->unit_ext_count; ++i) {
            const auto& ue = w->unit_exts[i];
            if (ue.struct_size >= sizeof(San14TacticalUnitExt) && ue.unit_slot > 0) {
                ext_by_slot[ue.unit_slot] = &ue;
            }
        }
    }

    ctx.units.reserve(w->unit_count);
    for (uint32_t i = 0; i < w->unit_count; ++i) {
        const auto& u = w->units[i];
        if (u.tile_id <= MAX_TILE_ID) ctx.occupied_tiles.insert(u.tile_id);
        if (u.kind != 1 || u.troops <= 0 || u.tile_id > MAX_TILE_ID) continue;

        DailyUnit du{};
        du.u = &u;
        auto eit = ext_by_slot.find(u.slot);
        if (eit != ext_by_slot.end()) {
            const auto* ue = eit->second;
            du.troops_id = ue->troops_id;
            du.troops_purpose = ue->troops_purpose;
            du.troops_goal_id = ue->troops_goal_id;
            du.troops_leader_officer_id = ue->troops_leader_officer_id;
            du.ship_kind = ue->ship_kind;
            du.troops_fighting_strength = ue->troops_fighting_strength;
            du.is_lead = (ue->troops_leader_officer_id != 0 && ue->troops_leader_officer_id == u.commander_id);
        }
        du.is_ship = is_ship_formation(u.land_formation);
        du.is_melee = is_melee_formation(u.land_formation);
        du.is_engine = is_siege_formation(u.land_formation);
        du.is_yanxing = (u.land_formation == FORM_YANXING);
        du.on_water = ctx.is_water_tile(u.tile_id);
        du.area_id = ctx.area_at(u.tile_id);

        if (du.troops_id > 0) ++ctx.troops_count[du.troops_id];
        ctx.slot_to_idx[u.slot] = ctx.units.size();
        ctx.units.push_back(du);
    }
    return ctx;
}

// ---- Module 5: stance (Yanxing ranged positioning) -------------------------------------------------

inline uint32_t pick_yanxing_firing_hex(DailyContext& ctx, const DailyUnit& a, bool retreat) {
    const int32_t cx = static_cast<int32_t>(a.u->tile_id % GRID_WIDTH);
    const int32_t cy = static_cast<int32_t>(a.u->tile_id / GRID_WIDTH);
    uint32_t best = SAN14_TACTICAL_INVALID_TILE_ID;
    int32_t best_score = -1000000;

    for (int32_t dy = -2; dy <= 2; ++dy) {
        for (int32_t dx = -2; dx <= 2; ++dx) {
            const int32_t nx = cx + dx;
            const int32_t ny = cy + dy;
            if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
            const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
            const int32_t move = hex_dist(a.u->tile_id, tid);
            if (move < 1 || move > 2 || !ctx.is_walkable_free(tid)) continue;
            if (other_side_of_gate(*ctx.st, a.u->tile_id, tid)) continue;

            int32_t ei = -1;
            const int32_t de = nearest_enemy_dist(ctx, tid, a.u->force_id, &ei);
            if (de < 2) continue;

            int32_t friends = 0;
            bool shield = false;
            for (const auto& f : ctx.units) {
                if (f.u->force_id != a.u->force_id || f.u->slot == a.u->slot) continue;
                if (hex_dist(tid, f.u->tile_id) != 1) continue;
                ++friends;
                if (ei >= 0 && hex_dist(f.u->tile_id, ctx.units[static_cast<size_t>(ei)].u->tile_id) < de) {
                    shield = true;
                }
            }
            const int32_t fr = (std::min)(friends, 2);
            const int32_t score = retreat
                ? (60 * (shield ? 1 : 0) + 15 * fr + (de >= 3 ? 20 : 10) - 8 * move)
                : ((de == 2 ? 100 : (de == 3 ? 50 : 10)) + 30 * (shield ? 1 : 0) + 15 * fr - 8 * move);
            if (score > best_score) {
                best_score = score;
                best = tid;
            }
        }
    }
    return best;
}

inline void run_day_stance(DailyContext& ctx) {
    for (const auto& a : ctx.units) {
        if (!a.is_yanxing || a.u->task_locked != 0) continue;
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_STANCE)) continue;
        OrderMem* m = track_unit(ctx, a);
        if (!is_mine(m, OWNER_STANCE)) continue;

        if (!is_field_order(m->p_act)) {
            if (m->mod) restore_unit(ctx, a, *m);
            continue;
        }
        const int32_t de = nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id);
        auto hold_here = [&]() {
            if (!(m->mod && m->w_act == ACT_WAIT && static_cast<uint32_t>(m->w_id) == a.u->tile_id)) {
                order_unit(ctx, a, *m, OWNER_STANCE, ACT_WAIT, TT_HEX, static_cast<int32_t>(a.u->tile_id), SAN14_TACTICAL_MOD_STANCE);
            }
        };

        if (de <= 1) {
            uint32_t h = SAN14_TACTICAL_INVALID_TILE_ID;
            if (m->mod && m->owner == OWNER_STANCE && m->w_act == ACT_MOVE && m->w_type == TT_HEX &&
                m->w_id >= 0 && static_cast<uint32_t>(m->w_id) != a.u->tile_id &&
                ctx.is_walkable_free(static_cast<uint32_t>(m->w_id)) &&
                nearest_enemy_dist(ctx, static_cast<uint32_t>(m->w_id), a.u->force_id) >= 2 &&
                !other_side_of_gate(*ctx.st, a.u->tile_id, static_cast<uint32_t>(m->w_id))) {
                h = static_cast<uint32_t>(m->w_id);
            } else {
                h = pick_yanxing_firing_hex(ctx, a, true);
            }
            if (h <= MAX_TILE_ID) {
                if (!(m->mod && m->w_act == ACT_MOVE && static_cast<uint32_t>(m->w_id) == h)) {
                    order_unit(ctx, a, *m, OWNER_STANCE, ACT_MOVE, TT_HEX, static_cast<int32_t>(h), SAN14_TACTICAL_MOD_STANCE);
                }
            } else {
                hold_here();
            }
        } else if (de == 2) {
            hold_here();
        } else if (de <= ctx.st->params.stance_engage_radius) {
            const uint32_t h = pick_yanxing_firing_hex(ctx, a, false);
            if (h <= MAX_TILE_ID && nearest_enemy_dist(ctx, h, a.u->force_id) == 2) {
                if (!(m->mod && m->w_act == ACT_MOVE && static_cast<uint32_t>(m->w_id) == h)) {
                    order_unit(ctx, a, *m, OWNER_STANCE, ACT_MOVE, TT_HEX, static_cast<int32_t>(h), SAN14_TACTICAL_MOD_STANCE);
                }
            } else if (m->mod) {
                restore_unit(ctx, a, *m);
            }
        } else if (m->mod) {
            restore_unit(ctx, a, *m);
        }
    }
}

// ---- Module 6a: walls.defend (engine intercept + keep defending city) -----------------------------

inline void run_day_defend(DailyContext& ctx) {
    const auto& p = ctx.st->params;
    for (const auto& a : ctx.units) {
        if (!a.is_melee || a.u->task_locked != 0) continue;
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_WALLS)) continue;
        OrderMem* m = track_unit(ctx, a);
        if (!is_mine(m, OWNER_DEFEND)) continue;

        int32_t target_idx = -1;
        const bool going_home_city =
            (m->p_act == ACT_GO_CITY || m->p_act == ACT_ENTER_CITY) &&
            m->p_type == TT_CITY && m->p_id > 0 &&
            ctx.foothold_force(static_cast<uint32_t>(m->p_id)) == a.u->force_id;

        if (going_home_city) {
            const uint32_t ch = ctx.foothold_tile(static_cast<uint32_t>(m->p_id));
            if (ch <= MAX_TILE_ID) {
                int32_t foes = 0, ours = 0;
                int32_t eng_idx = -1, closest_idx = -1;
                int32_t eng_d = 999, closest_d = 999;
                for (size_t j = 0; j < ctx.units.size(); ++j) {
                    const auto& e = ctx.units[j];
                    const int32_t dc = hex_dist(e.u->tile_id, ch);
                    if (e.u->force_id == a.u->force_id) {
                        if (dc <= p.walls_defend_city_ally_radius) ours += e.u->troops;
                        continue;
                    }
                    if (dc > p.walls_defend_city_radius || e.is_ship || !ctx.is_enemy(a.u->force_id, e.u->force_id)) continue;
                    foes += e.u->troops;
                    const int32_t d = hex_dist(a.u->tile_id, e.u->tile_id);
                    if (e.is_engine && d < eng_d) { eng_d = d; eng_idx = static_cast<int32_t>(j); }
                    if (d < closest_d) { closest_d = d; closest_idx = static_cast<int32_t>(j); }
                }
                if (foes > 0 && a.u->troops >= p.walls_defend_min_troops &&
                    ours * 100 >= foes * p.walls_defend_odds_pct) {
                    target_idx = (eng_idx >= 0) ? eng_idx : closest_idx;
                }
            }
        } else if (is_field_order(m->p_act) && nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id) > 1) {
            int32_t best_d = 999;
            for (size_t j = 0; j < ctx.units.size(); ++j) {
                const auto& e = ctx.units[j];
                if (!e.is_engine || !ctx.is_enemy(a.u->force_id, e.u->force_id)) continue;
                const int32_t d = hex_dist(a.u->tile_id, e.u->tile_id);
                if (d > p.walls_defend_engine_radius || d >= best_d) continue;
                int32_t ours = 0, theirs = 0;
                for (const auto& z : ctx.units) {
                    const int32_t dz = hex_dist(z.u->tile_id, e.u->tile_id);
                    if (z.u->force_id == a.u->force_id) {
                        if (dz <= 3) ours += z.u->troops;
                    } else if (dz <= 2 && !z.is_ship && ctx.is_enemy(a.u->force_id, z.u->force_id)) {
                        theirs += z.u->troops;
                    }
                }
                if (ours >= theirs) {
                    best_d = d;
                    target_idx = static_cast<int32_t>(j);
                }
            }
        }

        if (target_idx >= 0) {
            const uint32_t slot = ctx.units[static_cast<size_t>(target_idx)].u->slot;
            if (!(m->mod && m->w_act == ACT_ATTACK_ARMY && m->w_type == TT_ARMY && static_cast<uint32_t>(m->w_id) == slot)) {
                order_unit(ctx, a, *m, OWNER_DEFEND, ACT_ATTACK_ARMY, TT_ARMY, static_cast<int32_t>(slot), SAN14_TACTICAL_MOD_WALLS);
            }
        } else if (m->mod) {
            restore_unit(ctx, a, *m);
        }
    }
}

// ---- Module 8: gate (pass defense, Yanxing placement, melee return, GateGuard) ------------------

inline bool is_gate_task(uint32_t t) {
    return t == 0 || t == 1 || t == 3 || t == 13 || t == 14 || t == 15 || t == 16 || t == 21 || t == 23;
}

inline void gate_walk_bfs(
    DailyContext& ctx,
    uint32_t from_tile,
    int32_t max_steps,
    std::unordered_map<uint32_t, int32_t>& out_dist
) {
    out_dist.clear();
    if (from_tile > MAX_TILE_ID) return;
    std::vector<uint32_t> q;
    q.reserve(256);
    out_dist[from_tile] = 0;
    q.push_back(from_tile);
    size_t head = 0;
    while (head < q.size()) {
        const uint32_t cur = q[head++];
        const int32_t d = out_dist[cur];
        if (d >= max_steps) continue;
        std::array<uint32_t, 6> nbs{};
        const int n = collect_neighbors(cur, nbs);
        for (int i = 0; i < n; ++i) {
            const uint32_t nxt = nbs[i];
            if (out_dist.count(nxt)) continue;
            const uint32_t ter = ctx.terrain_at(nxt);
            if (ter == TER_GATE || is_pure_water_terrain(ter) || !is_walkable_land_kind(*ctx.st, ter)) continue;
            out_dist[nxt] = d + 1;
            q.push_back(nxt);
        }
    }
}

inline uint32_t nearest_own_city(const DailyContext& ctx, uint32_t force_id, uint32_t tile_id) {
    uint32_t best = 0;
    int32_t bd = 1 << 30;
    for (uint32_t i = 0; i < ctx.w->city_count; ++i) {
        const auto& c = ctx.w->cities[i];
        if (c.force_id != force_id || c.tile_id > MAX_TILE_ID) continue;
        const int32_t d = hex_dist(c.tile_id, tile_id);
        if (d < bd) { bd = d; best = c.city_id; }
    }
    return best;
}

inline void run_day_gate(DailyContext& ctx) {
    auto& st = *ctx.st;
    const auto& p = st.params;
    st.gate_hot.clear();
    st.gate_side.clear();
    st.gate_side_force.clear();
    st.city_gate.clear();
    st.gate_arch.clear();

    if (!ctx.w->gates || ctx.w->gate_count == 0) return;

    std::unordered_map<uint32_t, uint8_t> reserved_tiles;
    for (uint32_t gi_idx = 0; gi_idx < ctx.w->gate_count; ++gi_idx) {
        const auto& gate = ctx.w->gates[gi_idx];
        if (gate.struct_size < sizeof(San14TacticalGate) || gate.gate_id == 0 || gate.tile_id > MAX_TILE_ID) continue;
        const uint32_t owner = gate.owner_force_id;
        if (owner == 0 || !ctx.has_mod(owner, SAN14_TACTICAL_MOD_GATE)) continue;

        const uint32_t gid = gate.foothold_id;
        const uint32_t gh = gate.tile_id;
        st.gate_tile[gate.gate_id] = gh;

        std::vector<size_t> atk;
        for (size_t k = 0; k < ctx.units.size(); ++k) {
            const auto& e = ctx.units[k];
            if (!e.is_ship && ctx.is_enemy(owner, e.u->force_id) && hex_dist(e.u->tile_id, gh) <= p.gate_near) {
                atk.push_back(k);
            }
        }

        // Release units tied to this gate when no attacker stands at it.
        for (const auto& a : ctx.units) {
            auto& gm = st.gate_mem[a.u->slot];
            if (gm.commander_id != a.u->commander_id || gm.force_id != a.u->force_id) {
                gm = GateMem{a.u->commander_id, a.u->force_id, 0, SAN14_TACTICAL_INVALID_TILE_ID};
            }
            if (gm.gate_foothold != gid) continue;
            OrderMem* m = track_unit(ctx, a);
            if (!m || !m->mod || m->owner != OWNER_GATE) { gm.gate_foothold = 0; continue; }
            if (atk.empty()) {
                if (m->w_act == ACT_ENTER_CITY) {
                    m->mod = 0;
                    m->owner = OWNER_NONE;
                } else {
                    restore_unit(ctx, a, *m);
                }
                gm.gate_foothold = 0;
            }
        }

        auto threatens_gate = [&](const DailyUnit& e) {
            return hex_dist(e.u->tile_id, gh) <= p.gate_near + 1 ||
                   (e.u->target_type == TT_GATE && e.u->target_id == gate.gate_id) ||
                   (e.u->target_type == TT_HEX && e.u->target_id == gh);
        };
        bool threatened = !atk.empty();
        for (size_t k = 0; k < ctx.units.size() && !threatened; ++k) {
            const auto& e = ctx.units[k];
            if (!e.is_ship && ctx.is_enemy(owner, e.u->force_id) && threatens_gate(e)) threatened = true;
        }
        if (threatened) {
            uint32_t arch_count = 0;
            for (const auto& a : ctx.units) {
                if (a.u->force_id == owner && a.is_yanxing && hex_dist(a.u->tile_id, gh) <= p.gate_reach) {
                    ++arch_count;
                }
            }
            st.gate_arch[gate.gate_id] = arch_count;
            for (uint32_t c = 0; c < ctx.w->city_count; ++c) {
                const auto& city = ctx.w->cities[c];
                if (city.force_id != owner || city.tile_id > MAX_TILE_ID) continue;
                if (hex_dist(city.tile_id, gh) > p.gate_city_radius) continue;
                int32_t best_d = 1 << 30;
                int32_t best_k = -1;
                for (size_t k = 0; k < ctx.units.size(); ++k) {
                    const auto& e = ctx.units[k];
                    if (e.is_ship || !ctx.is_enemy(owner, e.u->force_id)) continue;
                    const int32_t d = hex_dist(e.u->tile_id, city.tile_id);
                    if (d < best_d) { best_d = d; best_k = static_cast<int32_t>(k); }
                }
                if (best_k >= 0 && threatens_gate(ctx.units[static_cast<size_t>(best_k)])) {
                    st.city_gate[city.city_id] = gid;
                }
            }
        }

        if (atk.empty()) continue;
        st.gate_hot[gate.gate_id] = 1;

        auto dist_atk = [&](uint32_t tile) {
            int32_t b = 999;
            for (size_t idx : atk) {
                const int32_t d = hex_dist(tile, ctx.units[idx].u->tile_id);
                if (d < b) b = d;
            }
            return b;
        };

        // Keep en-route Yanxing firing hexes reserved.
        for (const auto& a : ctx.units) {
            if (a.u->force_id != owner || !a.is_yanxing) continue;
            const auto& gm = st.gate_mem[a.u->slot];
            if (gm.commander_id != a.u->commander_id || gm.gate_foothold != gid || gm.tile > MAX_TILE_ID) continue;
            OrderMem* m = track_unit(ctx, a);
            if (m && m->mod && m->owner == OWNER_GATE && m->w_act == ACT_MOVE && static_cast<uint32_t>(m->w_id) == gm.tile &&
                (dist_atk(gm.tile) == 2 || hex_dist(gm.tile, gh) <= 2) &&
                nearest_enemy_dist(ctx, gm.tile, a.u->force_id) >= 2) {
                reserved_tiles[gm.tile] = 2;
            }
        }

        std::unordered_map<uint32_t, int32_t> walk_dist;
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& a : ctx.units) {
                if (a.u->force_id != owner || a.u->task_locked != 0 || a.is_ship) continue;
                if (hex_dist(a.u->tile_id, gh) > p.gate_reach) continue;
                if (pass == 0 && !a.is_yanxing) continue;
                if (pass == 1 && (a.is_yanxing || a.is_engine)) continue;
                if (!is_gate_task(a.troops_purpose)) continue;

                OrderMem* m = track_unit(ctx, a);
                if (!m || (pass == 1 && !is_mine(m, OWNER_GATE)) || !is_field_order(static_cast<int32_t>(a.u->order_code))) {
                    continue;
                }
                const int32_t de = nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id);
                auto& gm = st.gate_mem[a.u->slot];

                if (pass == 0) {
                    if (de < 2) continue;
                    gate_walk_bfs(ctx, a.u->tile_id, p.gate_walk_steps, walk_dist);

                    bool reach_atk = false;
                    for (size_t idx : atk) {
                        if (walk_dist.count(ctx.units[idx].u->tile_id)) { reach_atk = true; break; }
                    }
                    if (!reach_atk) {
                        for (const auto& kv : walk_dist) {
                            if (hex_dist(kv.first, gh) <= 6) {
                                st.gate_side[kv.first] = static_cast<uint8_t>(gate.gate_id);
                                st.gate_side_force[kv.first] = owner;
                            }
                        }
                    }

                    auto order_tile = [&]() -> uint32_t {
                        if (a.u->target_type == TT_HEX && a.u->target_id <= MAX_TILE_ID) return a.u->target_id;
                        if (a.u->target_type == TT_ARMY) {
                            auto it = ctx.slot_to_idx.find(a.u->target_id);
                            if (it != ctx.slot_to_idx.end()) return ctx.units[it->second].u->tile_id;
                        }
                        return SAN14_TACTICAL_INVALID_TILE_ID;
                    };
                    auto stay_here = [&]() {
                        gm.gate_foothold = gid;
                        gm.tile = a.u->tile_id;
                        if (m->mod && m->owner == OWNER_GATE && m->w_act == ACT_WAIT &&
                            static_cast<uint32_t>(m->w_id) == a.u->tile_id) return;
                        order_unit(ctx, a, *m, OWNER_GATE, ACT_WAIT, TT_HEX, static_cast<int32_t>(a.u->tile_id), SAN14_TACTICAL_MOD_GATE);
                    };

                    // Step back if stronger enemy ranged units can shoot over the gate.
                    auto ranged_reach = [](uint32_t f) -> int32_t {
                        return (f == FORM_TOUSHI) ? 3 : ((f == FORM_YANXING || f == FORM_JINGLAN) ? 2 : 0);
                    };
                    int32_t enemy_ranged_troops = 0;
                    for (const auto& e : ctx.units) {
                        const int32_t rr = ranged_reach(e.u->land_formation);
                        if (rr > 0 && ctx.is_enemy(owner, e.u->force_id) && hex_dist(e.u->tile_id, a.u->tile_id) <= rr) {
                            enemy_ranged_troops += e.u->troops;
                        }
                    }
                    if (enemy_ranged_troops * 10 > a.u->troops * 13) {
                        uint32_t bh = SAN14_TACTICAL_INVALID_TILE_ID;
                        int32_t bdd = 1 << 30;
                        const int32_t ax = static_cast<int32_t>(a.u->tile_id % GRID_WIDTH);
                        const int32_t ay = static_cast<int32_t>(a.u->tile_id / GRID_WIDTH);
                        for (int32_t dy = -4; dy <= 4; ++dy) {
                            for (int32_t dx = -4; dx <= 4; ++dx) {
                                const int32_t nx = ax + dx, ny = ay + dy;
                                if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
                                const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
                                auto wit = walk_dist.find(tid);
                                if (tid == a.u->tile_id || wit == walk_dist.end() || reserved_tiles[tid] || !ctx.is_walkable_free(tid)) continue;
                                bool safe = true;
                                for (const auto& e : ctx.units) {
                                    const int32_t rr = ranged_reach(e.u->land_formation);
                                    if (rr > 0 && ctx.is_enemy(owner, e.u->force_id) && hex_dist(e.u->tile_id, tid) <= rr) {
                                        safe = false; break;
                                    }
                                }
                                if (safe && nearest_enemy_dist(ctx, tid, a.u->force_id) >= 2 && wit->second < bdd) {
                                    bdd = wit->second; bh = tid;
                                }
                            }
                        }
                        if (bh <= MAX_TILE_ID) {
                            reserved_tiles[bh] = 1;
                            gm.gate_foothold = gid;
                            gm.tile = bh;
                            if (!(m->mod && m->owner == OWNER_GATE && m->w_act == ACT_MOVE && static_cast<uint32_t>(m->w_id) == bh)) {
                                order_unit(ctx, a, *m, OWNER_GATE, ACT_MOVE, TT_HEX, static_cast<int32_t>(bh), SAN14_TACTICAL_MOD_GATE);
                            }
                            continue;
                        }
                    }

                    if (de == 2 && dist_atk(a.u->tile_id) == 2) {
                        reserved_tiles[a.u->tile_id] = 1;
                        const uint32_t th = order_tile();
                        if (th <= MAX_TILE_ID && (th == a.u->tile_id ? a.u->order_code != static_cast<uint32_t>(ACT_WAIT)
                                                                     : (!walk_dist.count(th) && hex_dist(th, gh) <= p.gate_near + 3))) {
                            stay_here();
                        }
                        continue;
                    }

                    uint32_t best = SAN14_TACTICAL_INVALID_TILE_ID;
                    int32_t bd = 1 << 30;
                    for (size_t idx : atk) {
                        const uint32_t eh = ctx.units[idx].u->tile_id;
                        const int32_t ex = static_cast<int32_t>(eh % GRID_WIDTH);
                        const int32_t ey = static_cast<int32_t>(eh / GRID_WIDTH);
                        for (int32_t dy = -2; dy <= 2; ++dy) {
                            for (int32_t dx = -2; dx <= 2; ++dx) {
                                const int32_t nx = ex + dx, ny = ey + dy;
                                if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
                                const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
                                auto wit = walk_dist.find(tid);
                                const bool mine = (reserved_tiles[tid] == 2 && tid == gm.tile && gm.gate_foothold == gid);
                                if (hex_dist(eh, tid) != 2 || (reserved_tiles[tid] && !mine) || wit == walk_dist.end()) continue;
                                if (tid != a.u->tile_id && !ctx.is_walkable_free(tid)) continue;
                                if (nearest_enemy_dist(ctx, tid, a.u->force_id) < 2) continue;
                                const int32_t sc = mine ? -1 : wit->second;
                                if (sc < bd) { bd = sc; best = tid; }
                            }
                        }
                    }
                    if (best > MAX_TILE_ID) {
                        // Gate post within 2 hexes of gate on our side.
                        const int32_t gx = static_cast<int32_t>(gh % GRID_WIDTH);
                        const int32_t gy = static_cast<int32_t>(gh / GRID_WIDTH);
                        for (int32_t dy = -2; dy <= 2; ++dy) {
                            for (int32_t dx = -2; dx <= 2; ++dx) {
                                const int32_t nx = gx + dx, ny = gy + dy;
                                if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
                                const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
                                if (tid == gh || hex_dist(tid, gh) > 2 || ctx.terrain_at(tid) == TER_GATE) continue;
                                auto wit = walk_dist.find(tid);
                                const bool mine = (reserved_tiles[tid] == 2 && tid == gm.tile && gm.gate_foothold == gid);
                                if ((reserved_tiles[tid] && !mine) || wit == walk_dist.end()) continue;
                                if (tid != a.u->tile_id && !ctx.is_walkable_free(tid)) continue;
                                if (nearest_enemy_dist(ctx, tid, a.u->force_id) < 2) continue;
                                const int32_t sc = (tid == a.u->tile_id) ? -2 : (mine ? -1 : hex_dist(tid, gh) * 100 + wit->second);
                                if (sc < bd) { bd = sc; best = tid; }
                            }
                        }
                    }
                    if (best > MAX_TILE_ID) {
                        const uint32_t th = order_tile();
                        if (th <= MAX_TILE_ID && !walk_dist.count(th) && hex_dist(th, gh) <= p.gate_near + 3) {
                            stay_here();
                        }
                        continue;
                    }
                    reserved_tiles[best] = 1;
                    gm.gate_foothold = gid;
                    gm.tile = best;
                    if (best == a.u->tile_id) {
                        if (!(a.u->order_code == static_cast<uint32_t>(ACT_WAIT) &&
                              a.u->target_type == static_cast<uint32_t>(TT_HEX) &&
                              a.u->target_id == a.u->tile_id)) {
                            order_unit(ctx, a, *m, OWNER_GATE, ACT_WAIT, TT_HEX, static_cast<int32_t>(a.u->tile_id), SAN14_TACTICAL_MOD_GATE);
                        }
                    } else if (!(m->mod && m->w_act == ACT_MOVE && static_cast<uint32_t>(m->w_id) == best)) {
                        order_unit(ctx, a, *m, OWNER_GATE, ACT_MOVE, TT_HEX, static_cast<int32_t>(best), SAN14_TACTICAL_MOD_GATE);
                    }
                } else {
                    if (de < 2) continue;
                    if (gm.gate_foothold == gid && m->mod && m->owner == OWNER_GATE && m->w_act == ACT_ENTER_CITY) continue;
                    gate_walk_bfs(ctx, a.u->tile_id, p.gate_walk_steps, walk_dist);
                    bool reach = false;
                    for (size_t idx : atk) {
                        if (walk_dist.count(ctx.units[idx].u->tile_id)) { reach = true; break; }
                    }
                    if (reach) continue;
                    const uint32_t cid = nearest_own_city(ctx, owner, a.u->tile_id);
                    if (cid == 0) continue;
                    gm.gate_foothold = gid;
                    gm.tile = SAN14_TACTICAL_INVALID_TILE_ID;
                    order_unit(ctx, a, *m, OWNER_GATE, ACT_ENTER_CITY, TT_CITY, static_cast<int32_t>(cid), SAN14_TACTICAL_MOD_GATE);
                }
            }
        }
    }
}

// ---- Module 6b/6c: walls.shore & walls.navaldef -------------------------------------------------

inline bool land_reach_bfs(DailyContext& ctx, uint32_t from_tile, uint32_t to_tile, int32_t max_steps = 10) {
    if (from_tile == to_tile) return true;
    if (from_tile > MAX_TILE_ID || to_tile > MAX_TILE_ID) return false;
    std::unordered_map<uint32_t, int32_t> dist;
    std::vector<uint32_t> q;
    dist[from_tile] = 0;
    q.push_back(from_tile);
    size_t head = 0;
    while (head < q.size()) {
        const uint32_t cur = q[head++];
        const int32_t d = dist[cur];
        if (d >= max_steps) continue;
        std::array<uint32_t, 6> nbs{};
        const int n = collect_neighbors(cur, nbs);
        for (int i = 0; i < n; ++i) {
            const uint32_t nxt = nbs[i];
            if (nxt == to_tile) return true;
            if (dist.count(nxt)) continue;
            const uint32_t ter = ctx.terrain_at(nxt);
            if (is_pure_water_terrain(ter) || !is_walkable_land_kind(*ctx.st, ter)) continue;
            dist[nxt] = d + 1;
            q.push_back(nxt);
        }
    }
    return false;
}

inline uint32_t pick_shore_hex(
    DailyContext& ctx,
    const DailyUnit& a,
    const DailyUnit& e,
    const std::unordered_set<uint32_t>& reserved
) {
    const int32_t want = a.is_yanxing ? 2 : 1;
    const int32_t ex = static_cast<int32_t>(e.u->tile_id % GRID_WIDTH);
    const int32_t ey = static_cast<int32_t>(e.u->tile_id / GRID_WIDTH);
    uint32_t best = SAN14_TACTICAL_INVALID_TILE_ID;
    int32_t best_score = 1 << 30;
    for (int32_t dy = -3; dy <= 3; ++dy) {
        for (int32_t dx = -3; dx <= 3; ++dx) {
            const int32_t nx = ex + dx, ny = ey + dy;
            if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
            const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
            if (hex_dist(e.u->tile_id, tid) != want || ctx.is_water_tile(tid)) continue;
            if (tid != a.u->tile_id && (!ctx.is_walkable_free(tid) || reserved.count(tid))) continue;
            if (has_land_foe_near(ctx, a.u->force_id, tid, 1)) continue;
            const int32_t sc = hex_dist(a.u->tile_id, tid);
            if (sc >= best_score) continue;
            if (tid != a.u->tile_id && !land_reach_bfs(ctx, a.u->tile_id, tid)) continue;
            best_score = sc;
            best = tid;
        }
    }
    return best;
}

inline uint32_t pick_bank_hex(
    DailyContext& ctx,
    const DailyUnit& a,
    const std::unordered_set<uint32_t>& reserved
) {
    const int32_t ax = static_cast<int32_t>(a.u->tile_id % GRID_WIDTH);
    const int32_t ay = static_cast<int32_t>(a.u->tile_id / GRID_WIDTH);
    uint32_t best = SAN14_TACTICAL_INVALID_TILE_ID;
    int32_t bd = 999;
    for (int32_t dy = -2; dy <= 2; ++dy) {
        for (int32_t dx = -2; dx <= 2; ++dx) {
            const int32_t nx = ax + dx, ny = ay + dy;
            if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
            const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
            const int32_t d = hex_dist(a.u->tile_id, tid);
            if (d < 1 || d > 2 || d >= bd || ctx.is_water_tile(tid) ||
                !ctx.is_walkable_free(tid) || reserved.count(tid)) continue;
            if (has_land_foe_near(ctx, a.u->force_id, tid, 1)) continue;
            bd = d;
            best = tid;
        }
    }
    return best;
}

inline void run_day_shore(DailyContext& ctx, std::unordered_set<uint32_t>& shore_reserved) {
    auto& st = *ctx.st;
    const auto& p = st.params;
    shore_reserved.clear();
    std::unordered_map<uint32_t, int32_t> sent_troops;
    std::unordered_map<uint32_t, int32_t> sent_count;

    // Ongoing shore assignments.
    for (const auto& a : ctx.units) {
        auto& sm = st.shore_mem[a.u->slot];
        if (sm.commander_id != a.u->commander_id || sm.force_id != a.u->force_id) {
            sm = ShoreMem{a.u->commander_id, a.u->force_id, 0, 0, 0, SAN14_TACTICAL_INVALID_TILE_ID};
        }
        if (sm.enemy_slot == 0) continue;
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_WALLS)) { sm.enemy_slot = 0; continue; }
        OrderMem* m = track_unit(ctx, a);
        if (!m) { sm.enemy_slot = 0; continue; }

        if (!m->mod || m->owner != OWNER_SHORE) {
            const bool wet_order =
                (a.u->order_code == static_cast<uint32_t>(ACT_ATTACK_ARMY) &&
                 a.u->target_type == static_cast<uint32_t>(TT_ARMY) &&
                 ctx.slot_to_idx.count(a.u->target_id) &&
                 ctx.units[ctx.slot_to_idx[a.u->target_id]].on_water) ||
                (a.u->order_code == static_cast<uint32_t>(ACT_MOVE) &&
                 a.u->target_type == static_cast<uint32_t>(TT_HEX) &&
                 ctx.is_water_tile(a.u->target_id));
            if (!wet_order || !is_mine(m, OWNER_SHORE)) { sm.enemy_slot = 0; continue; }
            sm.tile = SAN14_TACTICAL_INVALID_TILE_ID;
        }

        auto eit = ctx.slot_to_idx.find(sm.enemy_slot);
        bool release = false;
        if (eit == ctx.slot_to_idx.end() || ctx.units[eit->second].u->commander_id != sm.enemy_commander_id) {
            release = true;
        } else if (!ctx.units[eit->second].on_water || (ctx.today - sm.since) > p.walls_shore_days ||
                   (!a.on_water && has_land_foe_near(ctx, a.u->force_id, a.u->tile_id, 1))) {
            release = true;
        }

        uint32_t h = SAN14_TACTICAL_INVALID_TILE_ID;
        if (!release) {
            if (a.on_water) {
                h = pick_bank_hex(ctx, a, shore_reserved);
            } else {
                h = pick_shore_hex(ctx, a, ctx.units[eit->second], shore_reserved);
            }
            if (h > MAX_TILE_ID) release = true;
        }
        if (release) {
            restore_unit(ctx, a, *m);
            sm.enemy_slot = 0;
            continue;
        }
        shore_reserved.insert(h);
        sent_troops[sm.enemy_slot] += a.u->troops;
        ++sent_count[sm.enemy_slot];
        if (h != sm.tile) {
            order_unit(ctx, a, *m, OWNER_SHORE, ACT_MOVE, TT_HEX, static_cast<int32_t>(h), SAN14_TACTICAL_MOD_WALLS);
            sm.tile = h;
        }
    }

    // New shore interceptions against enemy fighting units on water.
    for (const auto& e : ctx.units) {
        if (!e.on_water || !is_fighting_task(e.troops_purpose)) continue;
        while (sent_count[e.u->slot] < p.walls_shore_max_units && sent_troops[e.u->slot] < e.u->troops) {
            int32_t best_idx = -1, best_d = 999;
            uint32_t best_h = SAN14_TACTICAL_INVALID_TILE_ID;
            for (size_t j = 0; j < ctx.units.size(); ++j) {
                const auto& a = ctx.units[j];
                if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_WALLS)) continue;
                if (a.u->force_id == e.u->force_id || (!a.is_melee && !a.is_yanxing) ||
                    a.u->task_locked != 0 || st.shore_mem[a.u->slot].enemy_slot != 0) continue;
                if (a.u->troops < p.walls_shore_min_troops || !ctx.is_enemy(a.u->force_id, e.u->force_id)) continue;
                const int32_t d = hex_dist(a.u->tile_id, e.u->tile_id);
                if (d > p.walls_shore_reach || d >= best_d || a.on_water) continue;
                if (!is_shore_task(a.troops_purpose) || !is_field_order(static_cast<int32_t>(a.u->order_code))) continue;
                if (has_land_foe_near(ctx, a.u->force_id, a.u->tile_id, 2)) continue;
                OrderMem* m = track_unit(ctx, a);
                if (!is_mine(m, OWNER_SHORE)) continue;
                const uint32_t h = pick_shore_hex(ctx, a, e, shore_reserved);
                if (h > MAX_TILE_ID) continue;
                best_idx = static_cast<int32_t>(j);
                best_d = d;
                best_h = h;
            }
            if (best_idx < 0) break;
            const auto& a = ctx.units[static_cast<size_t>(best_idx)];
            OrderMem* m = track_unit(ctx, a);
            auto& sm = st.shore_mem[a.u->slot];
            sm.enemy_slot = e.u->slot;
            sm.enemy_commander_id = e.u->commander_id;
            sm.since = ctx.today;
            sm.tile = best_h;
            order_unit(ctx, a, *m, OWNER_SHORE, ACT_MOVE, TT_HEX, static_cast<int32_t>(best_h), SAN14_TACTICAL_MOD_WALLS);
            shore_reserved.insert(best_h);
            sent_troops[e.u->slot] += a.u->troops;
            ++sent_count[e.u->slot];
        }
    }

    // No boarding to chase an enemy on the water.
    for (const auto& a : ctx.units) {
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_WALLS)) continue;
        if (st.shore_mem[a.u->slot].enemy_slot != 0 || a.is_ship || a.u->task_locked != 0 || a.on_water) continue;
        OrderMem* m = track_unit(ctx, a);
        if (!is_mine(m, OWNER_SHORE)) continue;
        const bool holding = (m->mod && m->owner == OWNER_SHORE);
        const int32_t act = holding ? m->p_act : static_cast<int32_t>(a.u->order_code);
        const int32_t tt = holding ? m->p_type : static_cast<int32_t>(a.u->target_type);
        const uint32_t tid = static_cast<uint32_t>(holding ? m->p_id : static_cast<int32_t>(a.u->target_id));
        bool chase = false;
        if (act == ACT_ATTACK_ARMY && tt == TT_ARMY) {
            auto eit = ctx.slot_to_idx.find(tid);
            if (eit != ctx.slot_to_idx.end()) {
                const auto& e = ctx.units[eit->second];
                chase = e.on_water && hex_dist(a.u->tile_id, e.u->tile_id) > 1 && is_shore_task(a.troops_purpose);
            }
        }
        if (holding) {
            if (!chase) restore_unit(ctx, a, *m);
        } else if (chase) {
            order_unit(ctx, a, *m, OWNER_SHORE, ACT_WAIT, TT_HEX, static_cast<int32_t>(a.u->tile_id), SAN14_TACTICAL_MOD_WALLS);
        }
    }
}

inline void ensure_port_map_built(DailyContext& ctx) {
    auto& st = *ctx.st;
    if (st.ports_built || !ctx.ops || !ctx.ops->get_tile) return;
    st.ports_built = true;
    st.port_tiles.clear();
    st.tile_nearest_port.assign(TILE_COUNT, -1);
    st.tile_port_water_dist.assign(TILE_COUNT, 255);

    std::unordered_map<uint32_t, int16_t> port_of_area;
    // Scan areas first if available, else scan tile grid once.
    for (uint32_t tid = 0; tid <= MAX_TILE_ID; ++tid) {
        San14TacticalTile t{};
        if (!read_tile(ctx.ops, tid, t)) continue;
        if (t.terrain_kind == TER_PORT && st.port_tiles.size() < 64u) {
            const int16_t pidx = static_cast<int16_t>(st.port_tiles.size());
            st.port_tiles.push_back(tid);
            if (t.area_id > 0 && !port_of_area.count(t.area_id)) {
                port_of_area[t.area_id] = pidx;
            }
        }
    }
    if (st.port_tiles.empty()) return;

    std::vector<uint32_t> q;
    q.reserve(4096);
    for (uint32_t tid = 0; tid <= MAX_TILE_ID; ++tid) {
        const auto* t = ctx.get_tile(tid);
        if (!t || !is_pure_water_terrain(t->terrain_kind)) continue;
        int16_t pidx = -1;
        if (t->area_id > 0) {
            auto pit = port_of_area.find(t->area_id);
            if (pit != port_of_area.end()) pidx = pit->second;
        }
        if (pidx < 0) {
            for (size_t i = 0; i < st.port_tiles.size(); ++i) {
                if (hex_dist(tid, st.port_tiles[i]) <= 1) { pidx = static_cast<int16_t>(i); break; }
            }
        }
        if (pidx >= 0) {
            st.tile_nearest_port[tid] = pidx;
            st.tile_port_water_dist[tid] = 0;
            q.push_back(tid);
        }
    }

    size_t head = 0;
    const uint8_t max_d = static_cast<uint8_t>(st.params.walls_naval_leave);
    while (head < q.size()) {
        const uint32_t cur = q[head++];
        const uint8_t d = st.tile_port_water_dist[cur];
        if (d >= max_d) continue;
        std::array<uint32_t, 6> nbs{};
        const int n = collect_neighbors(cur, nbs);
        for (int i = 0; i < n; ++i) {
            const uint32_t nxt = nbs[i];
            if (st.tile_port_water_dist[nxt] != 255) continue;
            if (!ctx.is_water_tile(nxt)) continue;
            st.tile_port_water_dist[nxt] = static_cast<uint8_t>(d + 1);
            st.tile_nearest_port[nxt] = st.tile_nearest_port[cur];
            q.push_back(nxt);
        }
    }
}

inline float eval_exchange_ratio(
    DailyContext& ctx,
    const DailyUnit& atk,
    uint32_t atk_form,
    const DailyUnit& def,
    uint32_t def_form
) {
    if (!ctx.ops || ctx.ops->struct_size < offsetof(San14TacticalHostOps, eval_unit_exchange_ratio) + sizeof(ctx.ops->eval_unit_exchange_ratio) ||
        !ctx.ops->eval_unit_exchange_ratio) {
        return -1.0f;
    }
    float ratio = -1.0f;
    if (ctx.ops->eval_unit_exchange_ratio(ctx.ops->ops_context, atk.u->slot, atk_form, def.u->slot, def_form, &ratio) == SAN14_OK) {
        return ratio;
    }
    return -1.0f;
}

enum NavalMode : uint8_t { ND_NONE = 0, ND_WATER = 1, ND_BANK = 2, ND_HOLD = 3 };

struct NavalPick {
    NavalMode mode = ND_NONE;
    uint32_t tile = SAN14_TACTICAL_INVALID_TILE_ID;
    float rw = -1.0f;
    float rb = -1.0f;
    float rh = -1.0f;
};

inline NavalPick choose_naval_mode(
    DailyContext& ctx,
    const DailyUnit& d,
    const DailyUnit& e,
    int32_t at_port_days,
    const std::unordered_set<uint32_t>& shore_reserved
) {
    const auto& p = ctx.st->params;
    NavalPick pk{};
    const uint32_t e_ship = (e.ship_kind >= 11 && e.ship_kind <= 13) ? e.ship_kind : 11u;
    const uint32_t d_ship = ctx.force_ship(d.u->force_id);
    pk.rw = eval_exchange_ratio(ctx, d, d_ship, e, e_ship);
    const uint32_t bank = d.on_water ? SAN14_TACTICAL_INVALID_TILE_ID : pick_shore_hex(ctx, d, e, shore_reserved);
    if (bank <= MAX_TILE_ID) {
        pk.rb = eval_exchange_ratio(ctx, d, d.u->land_formation, e, e_ship);
    }
    if (at_port_days < p.walls_naval_wait_days) {
        pk.rh = eval_exchange_ratio(ctx, d, d.u->land_formation, e, e.u->land_formation);
    }

    const float best = (std::max)({pk.rw, pk.rb, pk.rh});
    const float go_min = static_cast<float>(p.walls_naval_min_ratio_pct) / 100.0f;
    const float water_pref = static_cast<float>(p.walls_naval_water_pref_pct) / 100.0f;
    if (best < go_min) return pk;
    if (pk.rw >= go_min && pk.rw >= water_pref * best) {
        pk.mode = ND_WATER;
        return pk;
    }
    if (pk.rb >= best) {
        pk.mode = ND_BANK;
        pk.tile = bank;
        return pk;
    }
    if (pk.rh < go_min) {
        if (pk.rb >= go_min) { pk.mode = ND_BANK; pk.tile = bank; }
        return pk;
    }
    pk.mode = ND_HOLD;
    pk.tile = d.on_water ? pick_bank_hex(ctx, d, shore_reserved) : d.u->tile_id;
    if (pk.tile > MAX_TILE_ID) {
        pk.mode = (pk.rw >= go_min) ? ND_WATER : ND_NONE;
    }
    return pk;
}

inline void run_day_navaldef(DailyContext& ctx, std::unordered_set<uint32_t>& shore_reserved) {
    auto& st = *ctx.st;
    const auto& p = st.params;
    if (!ctx.ops || ctx.ops->struct_size < offsetof(San14TacticalHostOps, eval_unit_exchange_ratio) + sizeof(ctx.ops->eval_unit_exchange_ratio) ||
        !ctx.ops->eval_unit_exchange_ratio) {
        return;
    }
    ensure_port_map_built(ctx);
    if (st.port_tiles.empty()) return;

    auto port_owner = [&](int16_t pidx) -> uint32_t {
        if (pidx < 0 || static_cast<size_t>(pidx) >= st.port_tiles.size()) return 0;
        const uint32_t ar = ctx.area_at(st.port_tiles[static_cast<size_t>(pidx)]);
        auto ait = ctx.area_by_id.find(ar);
        if (ait != ctx.area_by_id.end()) return ait->second->owner_force_id;
        return ctx.tile_owner(st.port_tiles[static_cast<size_t>(pidx)]);
    };

    std::vector<int16_t> threat_port(ctx.units.size(), -1);
    std::unordered_map<uint32_t, int32_t> sent_troops;
    std::unordered_map<uint32_t, int32_t> sent_count;

    for (size_t k = 0; k < ctx.units.size(); ++k) {
        const auto& e = ctx.units[k];
        if (!e.on_water || is_transport_ship(e.ship_kind) || !is_fighting_task(e.troops_purpose)) continue;
        const int16_t pidx = st.tile_nearest_port[e.u->tile_id];
        if (pidx < 0 || st.tile_port_water_dist[e.u->tile_id] > static_cast<uint8_t>(p.walls_naval_edge)) continue;
        const uint32_t owner = port_owner(pidx);
        if (owner == 0 || owner == e.u->force_id || !ctx.has_mod(owner, SAN14_TACTICAL_MOD_WALLS) ||
            !ctx.is_enemy(owner, e.u->force_id)) continue;
        threat_port[k] = pidx;
        auto& sn = st.naval_seen[e.u->slot];
        if (sn.commander_id != e.u->commander_id || (ctx.today - sn.last_day) > 3) {
            sn = NavalSeen{e.u->commander_id, ctx.today, ctx.today};
        }
        sn.last_day = ctx.today;
    }

    // Ongoing navaldef assignments.
    for (const auto& a : ctx.units) {
        auto& nm = st.naval_mem[a.u->slot];
        if (nm.commander_id != a.u->commander_id || nm.force_id != a.u->force_id) {
            nm = NavalDefMem{a.u->commander_id, a.u->force_id, 0, 0, 0, ND_NONE, SAN14_TACTICAL_INVALID_TILE_ID};
        }
        if (nm.enemy_slot == 0) continue;
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_WALLS)) { nm.enemy_slot = 0; continue; }
        OrderMem* m = track_unit(ctx, a);
        if (!m) { nm.enemy_slot = 0; continue; }

        if (!m->mod || m->owner != OWNER_NAVAL) {
            const bool wet =
                (a.u->order_code == static_cast<uint32_t>(ACT_ATTACK_ARMY) &&
                 a.u->target_type == static_cast<uint32_t>(TT_ARMY) &&
                 ctx.slot_to_idx.count(a.u->target_id) &&
                 ctx.units[ctx.slot_to_idx[a.u->target_id]].on_water) ||
                (a.u->order_code == static_cast<uint32_t>(ACT_MOVE) &&
                 a.u->target_type == static_cast<uint32_t>(TT_HEX) &&
                 ctx.is_water_tile(a.u->target_id));
            if (nm.mode == ND_WATER || !wet || !is_mine(m, OWNER_NAVAL)) {
                nm.enemy_slot = 0;
                continue;
            }
        }

        auto eit = ctx.slot_to_idx.find(nm.enemy_slot);
        bool release = false;
        NavalPick pk{};
        if (eit == ctx.slot_to_idx.end() || ctx.units[eit->second].u->commander_id != nm.enemy_commander_id) {
            release = true;
        } else {
            const auto& e = ctx.units[eit->second];
            if (!e.on_water || st.tile_port_water_dist[e.u->tile_id] > static_cast<uint8_t>(p.walls_naval_leave) ||
                (ctx.today - nm.since) > p.walls_naval_days ||
                (!a.on_water && has_land_foe_near(ctx, a.u->force_id, a.u->tile_id, 1))) {
                release = true;
            } else {
                pk = choose_naval_mode(ctx, a, e, ctx.today - st.naval_seen[e.u->slot].first_day, shore_reserved);
                if (pk.mode == ND_NONE) release = true;
            }
        }

        if (release) {
            if (m->mod && m->owner == OWNER_NAVAL) restore_unit(ctx, a, *m);
            nm.enemy_slot = 0;
            continue;
        }
        const auto& e = ctx.units[eit->second];
        nm.mode = pk.mode;
        nm.tile = pk.tile;
        if (pk.mode == ND_WATER) {
            if (!(m->mod && m->owner == OWNER_NAVAL && m->w_act == ACT_ATTACK_ARMY && static_cast<uint32_t>(m->w_id) == e.u->slot)) {
                order_unit(ctx, a, *m, OWNER_NAVAL, ACT_ATTACK_ARMY, TT_ARMY, static_cast<int32_t>(e.u->slot), SAN14_TACTICAL_MOD_WALLS);
            }
        } else {
            const int32_t want_act = (pk.mode == ND_HOLD && pk.tile == a.u->tile_id) ? ACT_WAIT : ACT_MOVE;
            if (!(m->mod && m->owner == OWNER_NAVAL && m->w_type == TT_HEX && static_cast<uint32_t>(m->w_id) == pk.tile && m->w_act == want_act)) {
                order_unit(ctx, a, *m, OWNER_NAVAL, want_act, TT_HEX, static_cast<int32_t>(pk.tile), SAN14_TACTICAL_MOD_WALLS);
            }
            if (pk.mode == ND_BANK) shore_reserved.insert(pk.tile);
        }
        sent_troops[e.u->slot] += a.u->troops;
        ++sent_count[e.u->slot];
    }

    // New naval threats, strongest first.
    std::vector<size_t> order;
    for (size_t k = 0; k < ctx.units.size(); ++k) {
        if (threat_port[k] >= 0) order.push_back(k);
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return ctx.units[a].u->troops > ctx.units[b].u->troops;
    });

    std::unordered_set<uint32_t> tried;
    for (size_t k : order) {
        const auto& e = ctx.units[k];
        const uint32_t f = port_owner(threat_port[k]);
        int32_t cluster = 0;
        for (const auto& x : ctx.units) {
            if (x.u->force_id == e.u->force_id && x.on_water && hex_dist(x.u->tile_id, e.u->tile_id) <= 2) {
                cluster += x.u->troops;
            }
        }
        const int32_t need = (cluster * p.walls_naval_odds_pct) / 100;
        struct Cand { size_t idx; NavalPick pk; };
        std::vector<Cand> cands;
        int32_t could = sent_troops[e.u->slot];

        while (sent_count[e.u->slot] + static_cast<int32_t>(cands.size()) < p.walls_naval_max_units && could < need) {
            int32_t best = -1, best_d = 999;
            for (size_t j = 0; j < ctx.units.size(); ++j) {
                const auto& a = ctx.units[j];
                if (tried.count(a.u->slot)) continue;
                if (a.u->force_id != f || a.is_ship || (!a.is_melee && !a.is_yanxing) ||
                    a.u->task_locked != 0 || st.naval_mem[a.u->slot].enemy_slot != 0) continue;
                if (a.u->troops < p.walls_naval_min_troops || !is_shore_task(a.troops_purpose) ||
                    !is_field_order(static_cast<int32_t>(a.u->order_code))) continue;
                const int32_t d = hex_dist(a.u->tile_id, e.u->tile_id);
                if (d > p.walls_naval_reach || d >= best_d) continue;
                if (!a.on_water && has_land_foe_near(ctx, a.u->force_id, a.u->tile_id, 1)) continue;
                OrderMem* m = track_unit(ctx, a);
                if (!is_mine(m, OWNER_NAVAL)) continue;
                best = static_cast<int32_t>(j);
                best_d = d;
            }
            if (best < 0) break;
            const auto& a = ctx.units[static_cast<size_t>(best)];
            tried.insert(a.u->slot);
            const NavalPick pk = choose_naval_mode(ctx, a, e, ctx.today - st.naval_seen[e.u->slot].first_day, shore_reserved);
            if (pk.mode == ND_NONE) continue;
            if (pk.mode == ND_BANK) shore_reserved.insert(pk.tile);
            cands.push_back({static_cast<size_t>(best), pk});
            could += a.u->troops;
        }

        if (!cands.empty() && could * 100 < cluster * p.walls_naval_min_half_pct) {
            for (const auto& c : cands) {
                if (c.pk.mode == ND_BANK) shore_reserved.erase(c.pk.tile);
            }
            continue;
        }
        for (const auto& c : cands) {
            const auto& a = ctx.units[c.idx];
            OrderMem* m = track_unit(ctx, a);
            auto& nm = st.naval_mem[a.u->slot];
            nm = NavalDefMem{a.u->commander_id, a.u->force_id, e.u->slot, e.u->commander_id, ctx.today, c.pk.mode, c.pk.tile};
            if (c.pk.mode == ND_WATER) {
                order_unit(ctx, a, *m, OWNER_NAVAL, ACT_ATTACK_ARMY, TT_ARMY, static_cast<int32_t>(e.u->slot), SAN14_TACTICAL_MOD_WALLS);
            } else {
                const int32_t want_act = (c.pk.mode == ND_HOLD && c.pk.tile == a.u->tile_id) ? ACT_WAIT : ACT_MOVE;
                order_unit(ctx, a, *m, OWNER_NAVAL, want_act, TT_HEX, static_cast<int32_t>(c.pk.tile), SAN14_TACTICAL_MOD_WALLS);
            }
            sent_troops[e.u->slot] += a.u->troops;
            ++sent_count[e.u->slot];
        }
    }
}

// ---- Module 6d: walls.relief (keep relieving adjacent threatened cities) -------------------------

inline void run_day_relief(DailyContext& ctx) {
    auto& st = *ctx.st;
    const auto& p = st.params;
    for (const auto& a : ctx.units) {
        if (a.troops_id > 0) {
            auto& rt = st.relief_troop_mem[a.troops_id];
            if (rt.leader_id != a.troops_leader_officer_id) {
                rt = ReliefTroopMem{a.troops_leader_officer_id, -999};
            }
            if (a.troops_purpose == 1u && a.troops_leader_officer_id != 0) {
                rt.last_intercept_day = ctx.today;
            }
        }
    }

    auto is_attacker_of = [&](const DailyUnit& e, const San14TacticalCity& c) -> bool {
        if (e.u->force_id == c.force_id || !ctx.is_enemy(c.force_id, e.u->force_id) ||
            (e.on_water && is_transport_ship(e.ship_kind))) {
            return false;
        }
        const int32_t d = hex_dist(e.u->tile_id, c.tile_id);
        if (d > p.walls_relief_threat_radius) return false;
        if (is_siege_task(e.troops_purpose) && e.troops_goal_id == c.city_id) return true;
        return d <= p.walls_relief_close_radius && is_fighting_task(e.troops_purpose);
    };

    auto city_has_threat = [&](const San14TacticalCity& c) -> bool {
        for (const auto& e : ctx.units) {
            if (is_attacker_of(e, c)) return true;
        }
        return false;
    };

    auto pick_target_for = [&](const DailyUnit& a, const San14TacticalCity& c) -> int32_t {
        int32_t best = -1, bd = 999;
        for (size_t k = 0; k < ctx.units.size(); ++k) {
            const auto& e = ctx.units[k];
            if (!is_attacker_of(e, c)) continue;
            const int32_t d = hex_dist(a.u->tile_id, e.u->tile_id) - (e.is_engine ? 3 : 0);
            if (d < bd) { bd = d; best = static_cast<int32_t>(k); }
        }
        return best;
    };

    auto should_attack = [&](const DailyUnit& a, int32_t ti, const San14TacticalCity& c) -> bool {
        if (ti < 0) return false;
        const auto& e = ctx.units[static_cast<size_t>(ti)];
        if (hex_dist(e.u->tile_id, a.u->tile_id) <= 4 || hex_dist(e.u->tile_id, c.tile_id) <= 4) return true;
        int32_t foe = 0;
        int32_t ours = (hex_dist(a.u->tile_id, e.u->tile_id) > 6) ? a.u->troops : 0;
        for (const auto& x : ctx.units) {
            const int32_t d = hex_dist(x.u->tile_id, e.u->tile_id);
            if (x.u->force_id == a.u->force_id) {
                if (d <= 6) ours += x.u->troops;
            } else if (d <= 3 && ctx.is_enemy(a.u->force_id, x.u->force_id)) {
                foe += x.u->troops;
            }
        }
        return ours * 100 >= foe * p.walls_relief_odds_pct;
    };

    auto homebound = [](const DailyUnit& a) {
        return a.troops_purpose == 17u || a.troops_purpose == 18u;
    };

    for (auto it = st.relief_mem.begin(); it != st.relief_mem.end(); ) {
        if (it->second.city_id != 0 && !ctx.slot_to_idx.count(it->first)) {
            it = st.relief_mem.erase(it);
        } else {
            ++it;
        }
    }

    // Ongoing relief holds.
    for (const auto& a : ctx.units) {
        auto& rm = st.relief_mem[a.u->slot];
        if (rm.commander_id != a.u->commander_id || rm.force_id != a.u->force_id) {
            rm = ReliefMem{a.u->commander_id, a.u->force_id, 0, 0, 0, 0, 0, 0};
        }
        if (rm.city_id == 0) continue;
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_WALLS)) { rm.city_id = 0; continue; }
        OrderMem* m = track_unit(ctx, a);
        if (!m || !homebound(a) || !is_mine(m, OWNER_RELIEF)) { rm.city_id = 0; continue; }

        auto cit = ctx.city_by_id.find(rm.city_id);
        bool release = (cit == ctx.city_by_id.end() || cit->second->force_id != a.u->force_id ||
                        !city_has_threat(*cit->second) || (ctx.today - rm.since) > p.walls_relief_days ||
                        a.u->troops < p.walls_relief_keep_troops || a.u->morale < 40);
        if (release) {
            order_unit(ctx, a, *m, OWNER_RELIEF, rm.act, rm.type, rm.id, 0);
            m->mod = 0;
            m->owner = OWNER_NONE;
            rm.city_id = 0;
            continue;
        }
        if (a.u->task_locked != 0) continue;
        const int32_t ti = pick_target_for(a, *cit->second);
        const bool go = should_attack(a, ti, *cit->second);
        if (go) {
            const uint32_t eslot = ctx.units[static_cast<size_t>(ti)].u->slot;
            if (!(a.u->order_code == static_cast<uint32_t>(ACT_ATTACK_ARMY) &&
                  a.u->target_type == static_cast<uint32_t>(TT_ARMY) && a.u->target_id == eslot)) {
                order_unit(ctx, a, *m, OWNER_RELIEF, ACT_ATTACK_ARMY, TT_ARMY, static_cast<int32_t>(eslot), SAN14_TACTICAL_MOD_WALLS);
            }
            rm.mode = 1;
        } else {
            if (!((a.u->order_code == static_cast<uint32_t>(ACT_GO_CITY) || a.u->order_code == static_cast<uint32_t>(ACT_ENTER_CITY)) &&
                  a.u->target_type == static_cast<uint32_t>(TT_CITY) && a.u->target_id == rm.city_id)) {
                order_unit(ctx, a, *m, OWNER_RELIEF, ACT_GO_CITY, TT_CITY, static_cast<int32_t>(rm.city_id), SAN14_TACTICAL_MOD_WALLS);
            }
            rm.mode = 2;
        }
    }

    // New relief holds.
    for (const auto& a : ctx.units) {
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_WALLS)) continue;
        auto& rm = st.relief_mem[a.u->slot];
        if (rm.city_id != 0 || a.is_ship || a.is_engine || a.u->task_locked != 0 || !homebound(a)) continue;
        if (a.troops_id == 0 || (ctx.today - st.relief_troop_mem[a.troops_id].last_intercept_day) > p.walls_relief_recent_days) continue;

        const int32_t act = static_cast<int32_t>(a.u->order_code);
        const int32_t tt = static_cast<int32_t>(a.u->target_type);
        const uint32_t home_id = a.u->target_id;
        if ((act != ACT_GO_CITY && act != ACT_ENTER_CITY) || tt != TT_CITY) continue;
        auto hit = ctx.city_by_id.find(home_id);
        if (hit == ctx.city_by_id.end() || hit->second->force_id != a.u->force_id) continue;
        if (hit->second->food < 5000 || a.u->troops < p.walls_relief_min_troops || a.u->morale < 50) continue;

        OrderMem* m = track_unit(ctx, a);
        if (!is_mine(m, OWNER_RELIEF) || m->mod) continue;

        const int32_t dh = hex_dist(a.u->tile_id, hit->second->tile_id);
        const San14TacticalCity* best_city = nullptr;
        int32_t bd = 999;
        for (uint32_t i = 0; i < ctx.w->city_count; ++i) {
            const auto& c = ctx.w->cities[i];
            if (c.city_id == home_id || c.force_id != a.u->force_id) continue;
            const int32_t d = hex_dist(a.u->tile_id, c.tile_id);
            if (d > p.walls_relief_near || d >= dh || d >= bd || !city_has_threat(c)) continue;
            bd = d;
            best_city = &c;
        }
        if (!best_city) continue;
        rm = ReliefMem{a.u->commander_id, a.u->force_id, best_city->city_id, ctx.today, act, tt, static_cast<int32_t>(home_id), 0};
        const int32_t ti = pick_target_for(a, *best_city);
        if (should_attack(a, ti, *best_city)) {
            order_unit(ctx, a, *m, OWNER_RELIEF, ACT_ATTACK_ARMY, TT_ARMY,
                       static_cast<int32_t>(ctx.units[static_cast<size_t>(ti)].u->slot), SAN14_TACTICAL_MOD_WALLS);
            rm.mode = 1;
        } else {
            order_unit(ctx, a, *m, OWNER_RELIEF, ACT_GO_CITY, TT_CITY, static_cast<int32_t>(best_city->city_id), SAN14_TACTICAL_MOD_WALLS);
            rm.mode = 2;
        }
    }
}

// ---- Module 3: engage (target distribution + waitfight) -----------------------------------------

inline int32_t free_sides_around(DailyContext& ctx, uint32_t tile_id) {
    std::array<uint32_t, 6> nbs{};
    const int n = collect_neighbors(tile_id, nbs);
    int32_t free_cnt = 0;
    for (int i = 0; i < n; ++i) {
        if (ctx.is_walkable_free(nbs[i])) ++free_cnt;
    }
    return free_cnt;
}

inline int32_t current_attack_target_idx(const DailyContext& ctx, const DailyUnit& a) {
    if (a.u->order_code != static_cast<uint32_t>(ACT_ATTACK_ARMY) ||
        a.u->target_type != static_cast<uint32_t>(TT_ARMY)) {
        return -1;
    }
    auto it = ctx.slot_to_idx.find(a.u->target_id);
    return (it != ctx.slot_to_idx.end()) ? static_cast<int32_t>(it->second) : -1;
}

inline void run_day_engage(DailyContext& ctx) {
    const auto& p = ctx.st->params;
    std::vector<uint32_t> forces;
    for (const auto& a : ctx.units) {
        if (ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_ENGAGE) &&
            std::find(forces.begin(), forces.end(), a.u->force_id) == forces.end()) {
            forces.push_back(a.u->force_id);
        }
    }

    std::vector<int32_t> load(ctx.units.size(), 0);
    std::vector<int32_t> free_s(ctx.units.size(), -1);
    std::vector<uint8_t> touch(ctx.units.size(), 0);
    std::vector<int32_t> near_d(ctx.units.size(), 999);

    for (uint32_t f : forces) {
        std::vector<size_t> order;
        for (size_t k = 0; k < ctx.units.size(); ++k) {
            const auto& a = ctx.units[k];
            if (a.u->force_id != f) continue;
            OrderMem* m = track_unit(ctx, a);
            if (!is_mine(m, OWNER_TARGET) || a.u->task_locked != 0) continue;

            const bool multi = (a.troops_id > 0 && ctx.troops_count[a.troops_id] >= 2);
            const bool waiting = (m->p_act == ACT_WAIT);
            bool ok = a.is_melee && (multi || waiting) && is_fighting_task(a.troops_purpose);
            int32_t dn = 999;
            if (ok) {
                for (const auto& e : ctx.units) {
                    if (e.is_ship || !ctx.is_enemy(f, e.u->force_id)) continue;
                    const int32_t d = hex_dist(a.u->tile_id, e.u->tile_id);
                    if (d < dn) dn = d;
                }
            }
            const uint8_t t = (dn <= 1) ? 1 : 0;
            ok = ok && dn <= p.engage_radius &&
                 ((multi && (m->p_act == ACT_ATTACK_ARMY || (t && (m->p_act == ACT_MOVE || m->p_act == ACT_MARCH_ALT)))) ||
                  (t && waiting));
            if (!ok) {
                if (m->mod) restore_unit(ctx, a, *m);
                continue;
            }
            touch[k] = t;
            near_d[k] = dn;
            order.push_back(k);
        }
        if (order.empty()) continue;

        std::fill(load.begin(), load.end(), 0);
        std::fill(free_s.begin(), free_s.end(), -1);
        std::unordered_set<size_t> in_order(order.begin(), order.end());
        for (const auto& a : ctx.units) {
            if (a.u->force_id != f) continue;
            const size_t idx = ctx.slot_to_idx[a.u->slot];
            if (in_order.count(idx)) continue;
            const int32_t ti = current_attack_target_idx(ctx, a);
            if (ti >= 0) load[static_cast<size_t>(ti)] += a.u->troops;
        }

        std::sort(order.begin(), order.end(), [&](size_t x, size_t y) {
            if (touch[x] != touch[y]) return touch[x] > touch[y];
            if (near_d[x] != near_d[y]) return near_d[x] < near_d[y];
            return x < y;
        });

        for (size_t k : order) {
            const auto& a = ctx.units[k];
            OrderMem* m = track_unit(ctx, a);
            const int32_t cur = current_attack_target_idx(ctx, a);
            int32_t best = -1, best_score = -1000000;

            if (touch[k] && cur >= 0 &&
                hex_dist(a.u->tile_id, ctx.units[static_cast<size_t>(cur)].u->tile_id) == 1 &&
                ctx.is_enemy(f, ctx.units[static_cast<size_t>(cur)].u->force_id)) {
                best = cur;
            } else {
                for (size_t j = 0; j < ctx.units.size(); ++j) {
                    const auto& e = ctx.units[j];
                    if (e.is_ship || !ctx.is_enemy(f, e.u->force_id)) continue;
                    const int32_t d = hex_dist(a.u->tile_id, e.u->tile_id);
                    if (touch[k] ? (d != 1) : (d > p.engage_radius)) continue;
                    if (d > 1) {
                        if (free_s[j] < 0) free_s[j] = free_sides_around(ctx, e.u->tile_id);
                        if (free_s[j] <= 0) continue;
                    }
                    const int32_t need = (e.u->troops * p.engage_load_ratio_pct) / 100;
                    int32_t s = -10 * d - e.u->troops / 1000;
                    if (load[j] >= need) s -= 100;
                    if (static_cast<int32_t>(j) == cur) s += 10;
                    if (e.is_engine) s += 15;
                    if (a.u->land_formation == FORM_FENGSHI && (e.is_yanxing || e.is_engine)) s += 40;
                    if (s > best_score) { best_score = s; best = static_cast<int32_t>(j); }
                }
            }

            if (best < 0) {
                if (m->mod) restore_unit(ctx, a, *m);
                continue;
            }
            const size_t bidx = static_cast<size_t>(best);
            load[bidx] += a.u->troops;
            if (hex_dist(a.u->tile_id, ctx.units[bidx].u->tile_id) > 1 && free_s[bidx] > 0) --free_s[bidx];
            if (cur == best) continue;

            const uint32_t target_slot = ctx.units[bidx].u->slot;
            if (m->p_act == ACT_ATTACK_ARMY && m->p_type == TT_ARMY && static_cast<uint32_t>(m->p_id) == target_slot) {
                restore_unit(ctx, a, *m);
            } else {
                order_unit(ctx, a, *m, OWNER_TARGET, ACT_ATTACK_ARMY, TT_ARMY, static_cast<int32_t>(target_slot), SAN14_TACTICAL_MOD_ENGAGE);
            }
        }
    }
}

// ---- Module 7: siegeplan daily layer (fu + siegeprep + supplyguard) ----------------------------

inline bool are_neighbor_areas(const DailyContext& ctx, uint32_t a, uint32_t b) {
    auto it = ctx.area_by_id.find(a);
    if (it == ctx.area_by_id.end()) return false;
    for (uint32_t nb : it->second->neighbor_area_ids) {
        if (nb == b) return true;
    }
    return false;
}

inline void run_day_fu(
    DailyContext& ctx,
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>>& claimed_by_force,
    std::unordered_map<uint32_t, int32_t>& prep_cand_day
) {
    auto& st = *ctx.st;
    constexpr int32_t CITY_FIRE = 3;
    constexpr int32_t FU_R = 3, FU_GUARD = 3, FU_SAFE = 4, FU_ABORT = 2, FU_DAYS = 12, FU_BLOCK = 30;
    constexpr int32_t FU_Z_GOAL = 16, FU_Z_STAND = 3, FU_Z_R = 8, FU_Z_GUARD = 4, FU_Z_OUT = 2, FU_Z_DAYS = 20;

    // Ongoing fu units.
    for (const auto& a : ctx.units) {
        auto& fm = st.fu_mem[a.u->slot];
        if (fm.commander_id != a.u->commander_id || fm.force_id != a.u->force_id) {
            fm = FuMem{a.u->commander_id, a.u->force_id};
        }
        if (fm.tile > MAX_TILE_ID || a.u->task_locked != 0) continue;
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_SIEGEPLAN)) {
            fm.tile = SAN14_TACTICAL_INVALID_TILE_ID; continue;
        }
        OrderMem* m = track_unit(ctx, a);
        if (!m) { fm.tile = SAN14_TACTICAL_INVALID_TILE_ID; continue; }

        const uint32_t h = fm.tile;
        const uint32_t ar = ctx.area_at(h);
        bool ours = (a.u->target_type == static_cast<uint32_t>(TT_HEX) && a.u->target_id == h);

        const uint32_t zone_city_tile = (fm.zone && fm.goal > 0) ? ctx.foothold_tile(fm.goal) : SAN14_TACTICAL_INVALID_TILE_ID;
        const bool drawn_in = (zone_city_tile <= MAX_TILE_ID && hex_dist(a.u->tile_id, zone_city_tile) <= CITY_FIRE + 1);
        if (a.u->tile_id == fm.last_tile) {
            if (fm.still < 255) ++fm.still;
        } else {
            fm.last_tile = a.u->tile_id;
            fm.still = 0;
        }
        if (!ours && fm.zone && drawn_in) {
            fm.tile = SAN14_TACTICAL_INVALID_TILE_ID;
            continue;
        }
        if (!ours && fm.zone) {
            const int32_t act = static_cast<int32_t>(a.u->order_code);
            const bool keep = is_siege_task(a.troops_purpose) && a.troops_goal_id == fm.goal &&
                              (act == ACT_MOVE || act == ACT_WAIT || act == ACT_MARCH_ALT || (act >= 2 && act <= 6)) &&
                              !has_war_foe_near(ctx, a.u->force_id, a.u->tile_id, FU_ABORT) &&
                              (ctx.today - fm.since) <= FU_Z_DAYS;
            if (keep) {
                fm.act = act;
                fm.type = static_cast<int32_t>(a.u->target_type);
                fm.id = static_cast<int32_t>(a.u->target_id);
                order_unit(ctx, a, *m, OWNER_FU, ACT_MOVE, TT_HEX, static_cast<int32_t>(h), SAN14_TACTICAL_MOD_SIEGEPLAN);
                ours = true;
            }
        }
        if (!ours) { fm.tile = SAN14_TACTICAL_INVALID_TILE_ID; continue; }

        auto ait = ctx.area_by_id.find(ar);
        const uint32_t area_own = (ait != ctx.area_by_id.end()) ? ait->second->owner_force_id : 0u;
        const bool done = (a.u->tile_id == h) || (ctx.tile_owner(h) == a.u->force_id) || (area_own == a.u->force_id);
        const bool stop = (ar == 0) || !ctx.is_enemy(a.u->force_id, area_own) ||
                          has_war_foe_near(ctx, a.u->force_id, a.u->tile_id, FU_ABORT) ||
                          has_war_foe_near(ctx, a.u->force_id, h, (fm.zone ? FU_Z_GUARD : FU_GUARD) - 1) ||
                          (ctx.today - fm.since) > (fm.zone ? FU_Z_DAYS : FU_DAYS) ||
                          (fm.zone && fm.still >= 3);
        if (done || stop) {
            order_unit(ctx, a, *m, OWNER_FU, fm.act, fm.type, fm.id, 0);
            m->mod = 0;
            m->owner = OWNER_NONE;
            fm.tile = SAN14_TACTICAL_INVALID_TILE_ID;
            if (!done) {
                fm.block_tile = h;
                fm.block_until = ctx.today + FU_BLOCK;
            } else if (fm.zone && fm.goal > 0) {
                st.prep_step_fc[(static_cast<uint64_t>(a.u->force_id) << 16) | fm.goal] = ctx.today;
            }
        } else {
            claimed_by_force[a.u->force_id].insert(ar);
        }
    }

    // Rule 1: opportunistic 府 within 3 hexes for siege units.
    for (const auto& a : ctx.units) {
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_SIEGEPLAN)) continue;
        if (!is_melee_formation(a.u->land_formation) || a.troops_id == 0 || !is_siege_task(a.troops_purpose)) continue;
        OrderMem* m = track_unit(ctx, a);
        if (!m || m->mod || st.fu_mem[a.u->slot].tile <= MAX_TILE_ID || a.u->task_locked != 0) continue;
        if (a.u->order_code != static_cast<uint32_t>(ACT_MOVE) &&
            a.u->order_code != static_cast<uint32_t>(ACT_WAIT) &&
            a.u->order_code != static_cast<uint32_t>(ACT_MARCH_ALT)) continue;
        if (a.is_lead && ctx.troops_count[a.troops_id] >= 2) continue;
        if (has_war_foe_near(ctx, a.u->force_id, a.u->tile_id, FU_SAFE)) continue;

        const uint32_t goal_tile = ctx.foothold_tile(a.troops_goal_id);
        const uint32_t goal_area = (goal_tile <= MAX_TILE_ID) ? ctx.area_at(goal_tile) : 0u;
        auto& fm = st.fu_mem[a.u->slot];
        uint32_t best_ar = 0;
        int32_t best_score = 1 << 30;
        for (const auto& kv : ctx.area_by_id) {
            const auto* ar = kv.second;
            if (ar->fu_tile_id > MAX_TILE_ID || ctx.area_has_foothold.count(ar->area_id) ||
                claimed_by_force[a.u->force_id].count(ar->area_id)) continue;
            if (!ctx.is_enemy(a.u->force_id, ar->owner_force_id)) continue;
            if (fm.block_tile == ar->fu_tile_id && ctx.today < fm.block_until) continue;
            const int32_t d = hex_dist(a.u->tile_id, ar->fu_tile_id);
            if (d > FU_R || has_war_foe_near(ctx, a.u->force_id, ar->fu_tile_id, FU_GUARD)) continue;
            bool hot = (goal_area != 0 && (ar->area_id == goal_area || are_neighbor_areas(ctx, goal_area, ar->area_id)));
            for (const auto& e : ctx.units) {
                if (hot) break;
                if (e.area_id != 0 && are_neighbor_areas(ctx, ar->area_id, e.area_id) &&
                    ctx.is_enemy(a.u->force_id, e.u->force_id)) {
                    hot = true;
                }
            }
            const int32_t score = d - (hot ? 3 : 0);
            if (score < best_score) { best_score = score; best_ar = ar->area_id; }
        }
        if (best_ar == 0) continue;
        const uint32_t fh = ctx.area_by_id[best_ar]->fu_tile_id;
        fm.act = static_cast<int32_t>(a.u->order_code);
        fm.type = static_cast<int32_t>(a.u->target_type);
        fm.id = static_cast<int32_t>(a.u->target_id);
        fm.tile = fh;
        fm.zone = 0;
        fm.since = ctx.today;
        order_unit(ctx, a, *m, OWNER_FU, ACT_MOVE, TT_HEX, static_cast<int32_t>(fh), SAN14_TACTICAL_MOD_SIEGEPLAN);
        claimed_by_force[a.u->force_id].insert(best_ar);
    }

    // Rule 2: siege-zone flanking 府 around target city.
    std::unordered_set<uint32_t> seen_troops;
    for (const auto& k0 : ctx.units) {
        const uint32_t t = k0.troops_id;
        const uint32_t force_id = k0.u->force_id;
        if (t == 0 || !seen_troops.insert(t).second) continue;
        if (!ctx.has_mod(force_id, SAN14_TACTICAL_MOD_SIEGEPLAN) || !is_siege_task(k0.troops_purpose)) continue;
        const uint32_t goal = k0.troops_goal_id;
        if (!ctx.city_by_id.count(goal)) continue;
        const uint32_t c_tile = ctx.city_by_id[goal]->tile_id;
        if (c_tile > MAX_TILE_ID) continue;

        int32_t lead_idx = -1, melee = 0, out = 0;
        for (size_t k = 0; k < ctx.units.size(); ++k) {
            const auto& a = ctx.units[k];
            if (a.troops_id != t) continue;
            if (a.is_lead) lead_idx = static_cast<int32_t>(k);
            const auto& fm = st.fu_mem[a.u->slot];
            const bool away = (fm.tile <= MAX_TILE_ID && fm.commander_id == a.u->commander_id);
            if (away && fm.zone) ++out;
            if (a.is_melee && !away) ++melee;
        }
        if (lead_idx < 0 || hex_dist(ctx.units[static_cast<size_t>(lead_idx)].u->tile_id, c_tile) > FU_Z_GOAL ||
            out >= FU_Z_OUT) continue;

        std::unordered_map<uint32_t, int32_t> weights;
        const int32_t cx = static_cast<int32_t>(c_tile % GRID_WIDTH);
        const int32_t cy = static_cast<int32_t>(c_tile / GRID_WIDTH);
        for (int32_t dy = -FU_Z_STAND - 1; dy <= FU_Z_STAND + 1; ++dy) {
            for (int32_t dx = -FU_Z_STAND - 1; dx <= FU_Z_STAND + 1; ++dx) {
                const int32_t nx = cx + dx, ny = cy + dy;
                if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
                const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
                if (tid == c_tile || hex_dist(tid, c_tile) > FU_Z_STAND) continue;
                const uint32_t sa = ctx.area_at(tid);
                if (sa == 0) continue;
                ++weights[sa];
                auto ait = ctx.area_by_id.find(sa);
                if (ait != ctx.area_by_id.end()) {
                    for (uint32_t nb : ait->second->neighbor_area_ids) {
                        if (nb > 0) ++weights[nb];
                    }
                }
            }
        }

        struct Cand { uint32_t ar_id; float val; };
        std::vector<Cand> cands;
        for (const auto& wkv : weights) {
            auto ait = ctx.area_by_id.find(wkv.first);
            if (ait == ctx.area_by_id.end()) continue;
            const auto* ar = ait->second;
            if (ar->fu_tile_id > MAX_TILE_ID || ctx.area_has_foothold.count(ar->area_id) ||
                claimed_by_force[force_id].count(ar->area_id)) continue;
            if (hex_dist(ar->fu_tile_id, c_tile) <= CITY_FIRE) continue;
            if (!ctx.is_enemy(force_id, ar->owner_force_id)) continue;
            if (has_war_foe_near(ctx, force_id, ar->fu_tile_id, FU_Z_GUARD)) continue;
            const float share = (ar->total_hex_count > 0)
                ? static_cast<float>(ar->painted_hex_count) / static_cast<float>(ar->total_hex_count)
                : 1.0f;
            cands.push_back({ar->area_id, static_cast<float>(wkv.second) * share});
        }
        if (!cands.empty()) prep_cand_day[t] = ctx.today;
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.val > b.val; });

        for (size_t i = 0; i < cands.size() && out < FU_Z_OUT && melee >= 2; ++i) {
            const uint32_t fh = ctx.area_by_id[cands[i].ar_id]->fu_tile_id;
            int32_t best_k = -1, best_d = FU_Z_R + 1;
            for (size_t k = 0; k < ctx.units.size(); ++k) {
                const auto& a = ctx.units[k];
                if (a.troops_id != t || static_cast<int32_t>(k) == lead_idx || !a.is_melee || a.u->task_locked != 0) continue;
                if (hex_dist(a.u->tile_id, c_tile) <= CITY_FIRE + 1) continue;
                const auto& fm = st.fu_mem[a.u->slot];
                if (fm.tile <= MAX_TILE_ID || (fm.block_tile == fh && ctx.today < fm.block_until)) continue;
                const int32_t act = static_cast<int32_t>(a.u->order_code);
                const bool marching = (act == ACT_MOVE || act == ACT_WAIT || act == ACT_MARCH_ALT);
                const bool far_atk = (act >= 2 && act <= 6 && hex_dist(a.u->tile_id, c_tile) > FU_Z_STAND);
                if (!marching && !far_atk) continue;
                const int32_t d = hex_dist(a.u->tile_id, fh);
                if (d >= best_d || has_war_foe_near(ctx, force_id, a.u->tile_id, FU_SAFE)) continue;
                OrderMem* m = track_unit(ctx, a);
                if (!m || (m->mod && m->owner != OWNER_PREP)) continue;
                best_d = d;
                best_k = static_cast<int32_t>(k);
            }
            if (best_k < 0) continue;
            const auto& a = ctx.units[static_cast<size_t>(best_k)];
            OrderMem* m = track_unit(ctx, a);
            auto& fm = st.fu_mem[a.u->slot];
            fm.act = static_cast<int32_t>(a.u->order_code);
            fm.type = static_cast<int32_t>(a.u->target_type);
            fm.id = static_cast<int32_t>(a.u->target_id);
            fm.tile = fh;
            fm.zone = 1;
            fm.goal = goal;
            fm.since = ctx.today;
            order_unit(ctx, a, *m, OWNER_FU, ACT_MOVE, TT_HEX, static_cast<int32_t>(fh), SAN14_TACTICAL_MOD_SIEGEPLAN);
            claimed_by_force[force_id].insert(cands[i].ar_id);
            ++out;
            --melee;
        }
    }
}

inline void run_day_siegeprep(
    DailyContext& ctx,
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>>& claimed_by_force
) {
    auto& st = *ctx.st;
    const auto& p = st.params;
    constexpr int32_t CITY_FIRE = 3;
    constexpr int32_t PREP_FAR = 24, PREP_DET = 2, PREP_GRACE = 2, FU_Z_GUARD = 4, FU_SAFE = 4;

    struct TroopInfo { uint32_t force = 0; uint32_t goal = 0; int32_t nearest_d = 999; int32_t foe = 999; };
    std::unordered_map<uint32_t, TroopInfo> tinfo;

    for (const auto& a : ctx.units) {
        if (a.troops_id == 0 || a.is_ship || !ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_SIEGEPLAN)) continue;
        if (!is_siege_task(a.troops_purpose) || !ctx.city_by_id.count(a.troops_goal_id)) continue;
        auto& ti = tinfo[a.troops_id];
        ti.force = a.u->force_id;
        ti.goal = a.troops_goal_id;
        auto& pm = st.prep_mem[a.troops_id];
        if (pm.goal != a.troops_goal_id || pm.force_id != a.u->force_id) {
            pm = SiegePrepMem{a.u->force_id, a.troops_goal_id, 0, 0};
        }
        const int32_t d = hex_dist(a.u->tile_id, ctx.city_by_id[a.troops_goal_id]->tile_id);
        if (d < ti.nearest_d) ti.nearest_d = d;
        const int32_t de = nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id);
        if (de < ti.foe) ti.foe = de;
    }

    for (const auto& kv : tinfo) {
        auto& pm = st.prep_mem[kv.first];
        if (pm.done || pm.from != 0) continue;
        if (kv.second.nearest_d < p.siege_prep_in) { pm.done = 1; continue; }
        if (kv.second.nearest_d > p.siege_prep_out || kv.second.foe <= 3) continue;
        pm.from = ctx.today;
    }

    std::unordered_set<uint64_t> done_groups, cand_groups;
    for (const auto& kv : tinfo) {
        const uint32_t f = kv.second.force;
        const uint32_t goal = kv.second.goal;
        const uint64_t gkey = (static_cast<uint64_t>(f) << 16) | goal;
        if (done_groups.count(gkey)) continue;

        bool stopped = false;
        for (const auto& kv2 : tinfo) {
            if (kv2.second.force == f && kv2.second.goal == goal &&
                st.prep_mem[kv2.first].from != 0 && !st.prep_mem[kv2.first].done) {
                stopped = true; break;
            }
        }
        if (!stopped) continue;
        done_groups.insert(gkey);

        const uint32_t c_tile = ctx.city_by_id[goal]->tile_id;
        if (c_tile > MAX_TILE_ID) continue;
        int32_t total = 0, away = 0, out = 0;
        for (const auto& a : ctx.units) {
            if (a.u->force_id != f || !tinfo.count(a.troops_id) || tinfo[a.troops_id].goal != goal) continue;
            if (hex_dist(a.u->tile_id, c_tile) > p.siege_prep_out + 2) continue;
            total += a.u->troops;
            const auto& fm = st.fu_mem[a.u->slot];
            if (fm.tile <= MAX_TILE_ID && fm.commander_id == a.u->commander_id) {
                away += a.u->troops;
                if (fm.zone) ++out;
            }
        }
        if (out >= PREP_DET) continue;

        std::unordered_map<uint32_t, int32_t> weights;
        const int32_t cx = static_cast<int32_t>(c_tile % GRID_WIDTH);
        const int32_t cy = static_cast<int32_t>(c_tile / GRID_WIDTH);
        for (int32_t dy = -4; dy <= 4; ++dy) {
            for (int32_t dx = -4; dx <= 4; ++dx) {
                const int32_t nx = cx + dx, ny = cy + dy;
                if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
                const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
                if (tid == c_tile || hex_dist(tid, c_tile) > 3) continue;
                const uint32_t sa = ctx.area_at(tid);
                if (sa == 0) continue;
                ++weights[sa];
                auto ait = ctx.area_by_id.find(sa);
                if (ait != ctx.area_by_id.end()) {
                    for (uint32_t nb : ait->second->neighbor_area_ids) {
                        if (nb > 0) ++weights[nb];
                    }
                }
            }
        }
        struct Cand { uint32_t ar_id; float val; };
        std::vector<Cand> cands;
        for (const auto& wkv : weights) {
            auto ait = ctx.area_by_id.find(wkv.first);
            if (ait == ctx.area_by_id.end()) continue;
            const auto* ar = ait->second;
            if (ar->fu_tile_id > MAX_TILE_ID || ctx.area_has_foothold.count(ar->area_id) ||
                claimed_by_force[f].count(ar->area_id)) continue;
            if (hex_dist(ar->fu_tile_id, c_tile) <= CITY_FIRE) continue;
            if (!ctx.is_enemy(f, ar->owner_force_id)) continue;
            if (ctx.tile_owner(ar->fu_tile_id) == f) continue;
            if (has_war_foe_near(ctx, f, ar->fu_tile_id, FU_Z_GUARD)) continue;
            const float share = (ar->total_hex_count > 0)
                ? static_cast<float>(ar->painted_hex_count) / static_cast<float>(ar->total_hex_count)
                : 1.0f;
            cands.push_back({ar->area_id, static_cast<float>(wkv.second) * share});
        }
        if (!cands.empty()) cand_groups.insert(gkey);
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.val > b.val; });

        for (size_t ci = 0; ci < cands.size() && out < PREP_DET; ++ci) {
            const uint32_t fh = ctx.area_by_id[cands[ci].ar_id]->fu_tile_id;
            int32_t best_k = -1, best_score = 1 << 30;
            for (size_t k = 0; k < ctx.units.size(); ++k) {
                const auto& a = ctx.units[k];
                if (a.u->force_id != f || !tinfo.count(a.troops_id) || tinfo[a.troops_id].goal != goal) continue;
                if (!a.is_melee || a.u->task_locked != 0) continue;
                const int32_t dc = hex_dist(a.u->tile_id, c_tile);
                if (dc > p.siege_prep_out + 2 || dc <= CITY_FIRE + 1) continue;
                const auto& fm = st.fu_mem[a.u->slot];
                if (fm.tile <= MAX_TILE_ID || (fm.block_tile == fh && ctx.today < fm.block_until)) continue;
                if ((away + a.u->troops) * 2 > total) continue;
                const int32_t d = hex_dist(a.u->tile_id, fh);
                const int32_t score = d * 3000 + a.u->troops;
                if (d > p.siege_prep_reach || score >= best_score || has_war_foe_near(ctx, f, a.u->tile_id, FU_SAFE)) continue;
                OrderMem* m = track_unit(ctx, a);
                if (!m || (m->mod && m->owner != OWNER_PREP)) continue;
                best_score = score;
                best_k = static_cast<int32_t>(k);
            }
            if (best_k < 0) continue;
            const auto& a = ctx.units[static_cast<size_t>(best_k)];
            OrderMem* m = track_unit(ctx, a);
            auto& fm = st.fu_mem[a.u->slot];
            fm.act = static_cast<int32_t>(a.u->order_code);
            fm.type = static_cast<int32_t>(a.u->target_type);
            fm.id = static_cast<int32_t>(a.u->target_id);
            fm.tile = fh;
            fm.zone = 1;
            fm.goal = goal;
            fm.since = ctx.today;
            order_unit(ctx, a, *m, OWNER_FU, ACT_MOVE, TT_HEX, static_cast<int32_t>(fh), SAN14_TACTICAL_MOD_SIEGEPLAN);
            claimed_by_force[f].insert(cands[ci].ar_id);
            away += a.u->troops;
            ++out;
        }
    }

    std::unordered_map<uint32_t, uint8_t> hold_t;
    for (const auto& kv : tinfo) {
        const uint32_t t = kv.first;
        auto& pm = st.prep_mem[t];
        if (pm.done || pm.from == 0) continue;
        const uint32_t f = kv.second.force, goal = kv.second.goal;
        const uint64_t gkey = (static_cast<uint64_t>(f) << 16) | goal;

        bool out_flag = false;
        for (const auto& a : ctx.units) {
            const auto& fm = st.fu_mem[a.u->slot];
            if (a.u->force_id == f && fm.tile <= MAX_TILE_ID && fm.zone && fm.goal == goal && fm.commander_id == a.u->commander_id) {
                out_flag = true; break;
            }
        }
        const int32_t step = st.prep_step_fc.count(gkey) ? st.prep_step_fc[gkey] : 0;
        const bool flip = (step != 0 && (ctx.today - step) <= p.siege_prep_flip_days);
        const bool grace = ((ctx.today - pm.from) < PREP_GRACE && cand_groups.count(gkey));
        bool coming = false;
        for (const auto& kv2 : tinfo) {
            if (kv2.first != t && kv2.second.force == f && kv2.second.goal == goal &&
                kv2.second.nearest_d > p.siege_prep_out && kv2.second.nearest_d <= PREP_FAR &&
                st.rally_mem.count(kv2.first) &&
                (ctx.today - st.rally_mem[kv2.first].progress_day) <= p.rally_meet_stall_days) {
                coming = true; break;
            }
        }
        if ((out_flag || flip || coming || grace) && (ctx.today - pm.from) <= p.siege_prep_max_days) {
            hold_t[t] = 1;
        } else {
            pm.done = 1;
        }
    }

    std::unordered_map<uint64_t, uint32_t> rally_tile;
    std::unordered_map<uint64_t, int32_t> rally_sol;
    for (const auto& a : ctx.units) {
        if (a.troops_id == 0 || !hold_t[a.troops_id] || a.is_ship) continue;
        const auto& ti = tinfo[a.troops_id];
        const uint64_t gkey = (static_cast<uint64_t>(ti.force) << 16) | ti.goal;
        const int32_t d = hex_dist(a.u->tile_id, ctx.city_by_id[ti.goal]->tile_id);
        if (d < p.siege_prep_in - 1 || d > p.siege_prep_out + 1) continue;
        if (a.u->troops > rally_sol[gkey]) {
            rally_sol[gkey] = a.u->troops;
            rally_tile[gkey] = a.u->tile_id;
        }
    }

    for (const auto& a : ctx.units) {
        if (a.troops_id == 0 || !tinfo.count(a.troops_id) || a.is_ship || a.u->task_locked != 0) continue;
        OrderMem* m = track_unit(ctx, a);
        if (!is_mine(m, OWNER_PREP)) continue;
        const auto& ti = tinfo[a.troops_id];
        const uint64_t gkey = (static_cast<uint64_t>(ti.force) << 16) | ti.goal;
        const int32_t d = hex_dist(a.u->tile_id, ctx.city_by_id[ti.goal]->tile_id);
        const bool stay = hold_t[a.troops_id] && d >= p.siege_prep_in - 1 && d <= p.siege_prep_out + 1 &&
                          nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id) > 1;
        const auto& fm = st.fu_mem[a.u->slot];
        const bool for_fu = (fm.tile <= MAX_TILE_ID && fm.commander_id == a.u->commander_id);
        const uint32_t rh = rally_sol[gkey] > 0 ? rally_tile[gkey] : SAN14_TACTICAL_INVALID_TILE_ID;
        const bool rejoin = !stay && hold_t[a.troops_id] && rh <= MAX_TILE_ID && !for_fu &&
                            d > CITY_FIRE + 1 && d < p.siege_prep_in - 1 &&
                            nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id) > 1;
        if (stay) {
            if (!(m->mod && m->w_act == ACT_MOVE && m->w_type == TT_HEX)) {
                order_unit(ctx, a, *m, OWNER_PREP, ACT_MOVE, TT_HEX, static_cast<int32_t>(a.u->tile_id), SAN14_TACTICAL_MOD_SIEGEPLAN);
            }
        } else if (rejoin) {
            if (!(m->mod && m->w_act == ACT_MOVE && m->w_type == TT_HEX && static_cast<uint32_t>(m->w_id) == rh)) {
                order_unit(ctx, a, *m, OWNER_PREP, ACT_MOVE, TT_HEX, static_cast<int32_t>(rh), SAN14_TACTICAL_MOD_SIEGEPLAN);
            }
        } else if (m->mod) {
            restore_unit(ctx, a, *m);
        }
    }
}

inline int32_t supply_road_bfs(
    DailyContext& ctx,
    uint32_t start_tile,
    uint32_t force_id,
    std::unordered_map<uint32_t, int32_t>& out_dist,
    std::unordered_set<uint32_t>& out_on_road,
    std::vector<uint32_t>& out_visited
) {
    out_dist.clear();
    out_on_road.clear();
    out_visited.clear();
    if (start_tile > MAX_TILE_ID) return -1;

    const int32_t depth_cap = ctx.st->params.siege_guard_depth;
    int32_t L = -1;
    bool deep = false;
    out_dist[start_tile] = 0;
    out_visited.push_back(start_tile);
    size_t head = 0;

    while (head < out_visited.size()) {
        const uint32_t h = out_visited[head++];
        const int32_t d = out_dist[h];
        if (L >= 0 && d > L) break;
        const uint32_t ar = ctx.area_at(h);
        auto ait = ctx.area_by_id.find(ar);
        if (ait != ctx.area_by_id.end() && ait->second->owner_force_id == force_id) {
            if (L < 0) L = d;
            out_on_road.insert(h);
            continue;
        }
        if (L >= 0) continue;
        if (d >= depth_cap) { deep = true; continue; }
        std::array<uint32_t, 6> nbs{};
        const int n = collect_neighbors(h, nbs);
        for (int i = 0; i < n; ++i) {
            const uint32_t nxt = nbs[i];
            if (out_dist.count(nxt)) continue;
            if (ctx.tile_owner(nxt) != force_id) continue;
            out_dist[nxt] = d + 1;
            out_visited.push_back(nxt);
        }
    }
    if (L < 0) return deep ? -2 : -1;
    if (L == 0) return 0;

    for (size_t i = out_visited.size(); i > 0; --i) {
        const uint32_t h = out_visited[i - 1];
        const int32_t d = out_dist[h];
        if (d >= L) continue;
        std::array<uint32_t, 6> nbs{};
        const int n = collect_neighbors(h, nbs);
        for (int j = 0; j < n; ++j) {
            const uint32_t nxt = nbs[j];
            auto it = out_dist.find(nxt);
            if (it != out_dist.end() && it->second == d + 1 && out_on_road.count(nxt)) {
                out_on_road.insert(h);
                break;
            }
        }
    }
    return L;
}

inline void run_day_supplyguard(
    DailyContext& ctx,
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>>& claimed_by_force
) {
    auto& st = *ctx.st;
    const auto& p = st.params;
    if (ctx.area_by_id.empty()) return;

    constexpr int32_t CITY_FIRE = 3;
    constexpr int32_t SG_BEHIND = 3, SG_R = 4, SG_CUT_R = 8, SG_REACH = 12, SG_BIG_R = 16;
    constexpr int32_t SG_LEAVE = 6, SG_CLUSTER = 2, SG_GROUP_R = 28, SG_FU_REACH = 8, SG_FU_OUT = 2, SG_FAST = 3;
    constexpr float SG_ODDS = 1.2f;

    struct Group {
        uint32_t force = 0;
        uint32_t goal = 0;
        uint32_t city_tile = 0;
        int32_t group_sol = 0;
        int32_t guard_sol = 0;
        bool big_sent = false;
        std::vector<int32_t> threat_dist;
        std::vector<int32_t> cov_sol;
        std::unordered_map<uint32_t, uint32_t> road_area_weight;
    };

    std::vector<Group> groups;
    auto find_group = [&](uint32_t f, uint32_t goal) -> int32_t {
        for (size_t i = 0; i < groups.size(); ++i) {
            if (groups[i].force == f && groups[i].goal == goal) return static_cast<int32_t>(i);
        }
        return -1;
    };

    std::unordered_map<uint32_t, int32_t> s_dist;
    std::unordered_set<uint32_t> s_on_road;
    std::vector<uint32_t> s_visited;

    for (const auto& lead : ctx.units) {
        if (!lead.is_lead || lead.is_ship || !ctx.has_mod(lead.u->force_id, SAN14_TACTICAL_MOD_SIEGEPLAN)) continue;
        if (!is_siege_task(lead.troops_purpose) || !ctx.city_by_id.count(lead.troops_goal_id)) continue;
        const uint32_t c_tile = ctx.city_by_id[lead.troops_goal_id]->tile_id;
        if (hex_dist(lead.u->tile_id, c_tile) > p.siege_guard_near) continue;

        int32_t gi = find_group(lead.u->force_id, lead.troops_goal_id);
        if (gi < 0) {
            Group g{};
            g.force = lead.u->force_id;
            g.goal = lead.troops_goal_id;
            g.city_tile = c_tile;
            g.threat_dist.assign(ctx.units.size(), 99);
            g.cov_sol.assign(ctx.units.size(), 0);
            gi = static_cast<int32_t>(groups.size());
            groups.push_back(std::move(g));
        }
        auto& g = groups[static_cast<size_t>(gi)];
        const int32_t L = supply_road_bfs(ctx, lead.u->tile_id, lead.u->force_id, s_dist, s_on_road, s_visited);
        if (L > 0 || L == -1) {
            for (uint32_t h : s_visited) {
                if (L > 0 && !s_on_road.count(h)) continue;
                const uint32_t ar = ctx.area_at(h);
                if (ar > 0) ++g.road_area_weight[ar];
            }
        }
        for (size_t j = 0; j < ctx.units.size(); ++j) {
            const auto& e = ctx.units[j];
            if (e.is_ship || !ctx.is_enemy(g.force, e.u->force_id)) continue;
            const int32_t dl = hex_dist(e.u->tile_id, lead.u->tile_id);
            int32_t best = g.threat_dist[j];
            if (L > 0) {
                if (dl - L > 30) continue;
                for (uint32_t h : s_on_road) {
                    if (s_dist[h] < SG_BEHIND) continue;
                    const int32_t d = hex_dist(e.u->tile_id, h);
                    if (d < best) best = d;
                }
            } else if (L == -1 && dl <= SG_CUT_R + SG_LEAVE && hex_dist(e.u->tile_id, c_tile) > hex_dist(lead.u->tile_id, c_tile)) {
                const int32_t d = (std::max)(0, dl - (SG_CUT_R - SG_R));
                if (d < best) best = d;
            }
            g.threat_dist[j] = best;
        }
    }
    if (groups.empty()) return;

    for (const auto& a : ctx.units) {
        if (a.is_ship || a.troops_id == 0 || !is_siege_task(a.troops_purpose)) continue;
        const int32_t gi = find_group(a.u->force_id, a.troops_goal_id);
        if (gi >= 0 && hex_dist(a.u->tile_id, groups[static_cast<size_t>(gi)].city_tile) <= SG_GROUP_R) {
            groups[static_cast<size_t>(gi)].group_sol += a.u->troops;
        }
    }

    auto sg_may_take = [](const OrderMem* m) {
        return m && (!m->mod || m->owner == OWNER_PREP || m->owner == OWNER_RALLY || m->owner == OWNER_SGUARD);
    };
    auto fu_away = [&](const DailyUnit& a) {
        const auto& fm = st.fu_mem[a.u->slot];
        return fm.commander_id == a.u->commander_id && fm.tile <= MAX_TILE_ID;
    };
    auto foe_cluster = [&](uint32_t f, uint32_t tile_id) {
        int32_t n = 0;
        for (const auto& e : ctx.units) {
            if (!e.is_ship && ctx.is_enemy(f, e.u->force_id) && hex_dist(tile_id, e.u->tile_id) <= SG_CLUSTER) {
                n += e.u->troops;
            }
        }
        return n;
    };

    // Ongoing guards.
    for (const auto& a : ctx.units) {
        auto& sg = st.sg_mem[a.u->slot];
        if (sg.commander_id != a.u->commander_id || sg.force_id != a.u->force_id) {
            sg = SupplyGuardMem{a.u->commander_id, a.u->force_id, 0, 0, 0, 0, 0};
        }
        if (sg.enemy_slot == 0) continue;
        OrderMem* m = track_unit(ctx, a);
        if (!m || !m->mod || m->owner != OWNER_SGUARD) { sg.enemy_slot = 0; continue; }

        const int32_t gi = find_group(sg.force_id, sg.goal);
        auto eit = ctx.slot_to_idx.find(sg.enemy_slot);
        const bool gone = (eit == ctx.slot_to_idx.end() || ctx.units[eit->second].u->commander_id != sg.enemy_commander_id);
        const bool touching = !gone && (hex_dist(a.u->tile_id, ctx.units[eit->second].u->tile_id) <= 1);
        const int32_t dr = (gone || gi < 0) ? 99 : groups[static_cast<size_t>(gi)].threat_dist[eit->second];
        if (gi < 0 || gone || (dr > SG_LEAVE && !touching) || (ctx.today - sg.since) > p.siege_guard_days) {
            restore_unit(ctx, a, *m);
            sg.enemy_slot = 0;
            continue;
        }
        groups[static_cast<size_t>(gi)].cov_sol[eit->second] += a.u->troops;
        groups[static_cast<size_t>(gi)].guard_sol += a.u->troops;
    }

    // New threats & road 府.
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        auto& g = groups[gi];
        std::vector<size_t> tlist;
        for (size_t j = 0; j < ctx.units.size(); ++j) {
            if (g.threat_dist[j] <= SG_R) tlist.push_back(j);
        }
        std::sort(tlist.begin(), tlist.end(), [&](size_t a, size_t b) { return g.threat_dist[a] < g.threat_dist[b]; });

        for (size_t ek : tlist) {
            const auto& e = ctx.units[ek];
            const int32_t clus = foe_cluster(g.force, e.u->tile_id);
            if (g.cov_sol[ek] >= static_cast<int32_t>(SG_ODDS * clus)) continue;

            const bool big = (2 * clus >= g.group_sol);
            std::vector<size_t> picks;
            if (big) {
                if (g.big_sent) continue;
                g.big_sent = true;
                for (size_t k = 0; k < ctx.units.size(); ++k) {
                    const auto& a = ctx.units[k];
                    if (a.u->force_id != g.force || a.troops_goal_id != g.goal || !is_siege_task(a.troops_purpose)) continue;
                    if (a.is_ship || a.u->task_locked != 0 || fu_away(a) || st.sg_mem[a.u->slot].enemy_slot != 0) continue;
                    if (hex_dist(a.u->tile_id, e.u->tile_id) > SG_BIG_R || nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id) <= 1) continue;
                    if (hex_dist(a.u->tile_id, g.city_tile) <= CITY_FIRE + 1) continue;
                    OrderMem* m = track_unit(ctx, a);
                    if (!sg_may_take(m)) continue;
                    picks.push_back(k);
                }
            } else {
                const int32_t need = static_cast<int32_t>(SG_ODDS * clus) - g.cov_sol[ek];
                const int32_t room = g.group_sol / 2 - g.guard_sol;
                std::vector<size_t> cands;
                for (size_t k = 0; k < ctx.units.size(); ++k) {
                    const auto& a = ctx.units[k];
                    if (a.u->force_id != g.force || a.troops_goal_id != g.goal || !is_siege_task(a.troops_purpose)) continue;
                    if (!a.is_melee || a.u->task_locked != 0 || fu_away(a) || st.sg_mem[a.u->slot].enemy_slot != 0) continue;
                    if (hex_dist(a.u->tile_id, e.u->tile_id) > SG_REACH || nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id) <= 1) continue;
                    if (hex_dist(a.u->tile_id, g.city_tile) <= CITY_FIRE + 1 || a.u->troops > room) continue;
                    OrderMem* m = track_unit(ctx, a);
                    if (!sg_may_take(m)) continue;
                    cands.push_back(k);
                }
                std::sort(cands.begin(), cands.end(), [&](size_t a, size_t b) {
                    return hex_dist(ctx.units[a].u->tile_id, e.u->tile_id) < hex_dist(ctx.units[b].u->tile_id, e.u->tile_id);
                });
                for (size_t x : cands) {
                    if (ctx.units[x].u->troops >= need) { picks.push_back(x); break; }
                }
                if (picks.empty()) {
                    for (size_t x = 0; x < cands.size() && picks.empty(); ++x) {
                        for (size_t y = x + 1; y < cands.size(); ++y) {
                            const int32_t s = ctx.units[cands[x]].u->troops + ctx.units[cands[y]].u->troops;
                            if (s >= need && s <= room) { picks.push_back(cands[x]); picks.push_back(cands[y]); break; }
                        }
                    }
                }
            }
            for (size_t pk : picks) {
                const auto& a = ctx.units[pk];
                OrderMem* m = track_unit(ctx, a);
                st.sg_mem[a.u->slot] = SupplyGuardMem{
                    a.u->commander_id, g.force, e.u->slot, e.u->commander_id, ctx.today, g.goal, big ? uint8_t(1) : uint8_t(0)
                };
                order_unit(ctx, a, *m, OWNER_SGUARD, ACT_ATTACK_ARMY, TT_ARMY, static_cast<int32_t>(e.u->slot), SAN14_TACTICAL_MOD_SIEGEPLAN);
                g.cov_sol[ek] += a.u->troops;
                g.guard_sol += a.u->troops;
            }
        }

        // Road 府 capture.
        int32_t out = 0, away = 0;
        for (const auto& a : ctx.units) {
            if (a.u->force_id == g.force && is_siege_task(a.troops_purpose) && a.troops_goal_id == g.goal && fu_away(a)) {
                away += a.u->troops;
                if (st.fu_mem[a.u->slot].zone == 2) ++out;
            }
        }
        while (out < SG_FU_OUT) {
            uint32_t best_ar = 0, best_w = 0;
            for (const auto& wkv : g.road_area_weight) {
                if (wkv.second <= best_w || ctx.area_has_foothold.count(wkv.first) ||
                    claimed_by_force[g.force].count(wkv.first)) continue;
                auto ait = ctx.area_by_id.find(wkv.first);
                if (ait == ctx.area_by_id.end() || ait->second->fu_tile_id > MAX_TILE_ID) continue;
                if (!ctx.is_enemy(g.force, ait->second->owner_force_id)) continue;
                if (ctx.tile_owner(ait->second->fu_tile_id) == g.force ||
                    has_war_foe_near(ctx, g.force, ait->second->fu_tile_id, 4)) continue;
                best_ar = wkv.first;
                best_w = wkv.second;
            }
            if (best_ar == 0) break;
            claimed_by_force[g.force].insert(best_ar);
            const uint32_t fh = ctx.area_by_id[best_ar]->fu_tile_id;
            const int32_t room = g.group_sol / 2 - g.guard_sol - away;
            int32_t best_k = -1, best_score = 1 << 30;
            for (size_t k = 0; k < ctx.units.size(); ++k) {
                const auto& a = ctx.units[k];
                if (a.u->force_id != g.force || !is_siege_task(a.troops_purpose) || a.troops_goal_id != g.goal) continue;
                if (!a.is_melee || a.u->task_locked != 0 || st.sg_mem[a.u->slot].enemy_slot != 0) continue;
                const auto& fm = st.fu_mem[a.u->slot];
                if (fm.tile <= MAX_TILE_ID || (fm.block_tile == fh && ctx.today < fm.block_until)) continue;
                const int32_t d = hex_dist(a.u->tile_id, fh);
                if (d > SG_FU_REACH || a.u->troops > room || nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id) <= 1) continue;
                if (hex_dist(a.u->tile_id, g.city_tile) <= CITY_FIRE + 1) continue;
                const int32_t sc = (d - (is_fast_formation(a.u->land_formation) ? SG_FAST : 0)) * 100000 + a.u->troops;
                if (sc >= best_score) continue;
                OrderMem* m = track_unit(ctx, a);
                if (!sg_may_take(m)) continue;
                best_score = sc;
                best_k = static_cast<int32_t>(k);
            }
            if (best_k < 0) continue;
            const auto& a = ctx.units[static_cast<size_t>(best_k)];
            OrderMem* m = track_unit(ctx, a);
            auto& fm = st.fu_mem[a.u->slot];
            fm.act = static_cast<int32_t>(a.u->order_code);
            fm.type = static_cast<int32_t>(a.u->target_type);
            fm.id = static_cast<int32_t>(a.u->target_id);
            fm.tile = fh;
            fm.zone = 2;
            fm.goal = g.goal;
            fm.since = ctx.today;
            order_unit(ctx, a, *m, OWNER_FU, ACT_MOVE, TT_HEX, static_cast<int32_t>(fh), SAN14_TACTICAL_MOD_SIEGEPLAN);
            away += a.u->troops;
            ++out;
        }
    }
}

// ---- Module 2: rally (siege meet + staggered start + siegelone + intercept rally) ---------------

inline void run_day_rally(DailyContext& ctx) {
    auto& st = *ctx.st;
    const auto& p = st.params;

    // 1. Siege meet & staggered start.
    struct SiegeTroop {
        uint32_t force = 0;
        uint32_t goal = 0;
        uint32_t goal_tile = SAN14_TACTICAL_INVALID_TILE_ID;
        int32_t nearest_d = 999;
        int32_t foe = 999;
    };
    std::unordered_map<uint32_t, SiegeTroop> s_troops;
    for (const auto& a : ctx.units) {
        if (a.troops_id == 0 || a.is_ship || !ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_RALLY)) continue;
        if (!is_siege_task(a.troops_purpose)) continue;
        const uint32_t gt = ctx.foothold_tile(a.troops_goal_id);
        if (gt > MAX_TILE_ID) continue;
        auto& stp = s_troops[a.troops_id];
        stp.force = a.u->force_id;
        stp.goal = a.troops_goal_id;
        stp.goal_tile = gt;
        const int32_t d = hex_dist(a.u->tile_id, gt);
        if (d < stp.nearest_d) stp.nearest_d = d;
        const int32_t de = nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id);
        if (de < stp.foe) stp.foe = de;

        auto& tm = st.rally_mem[a.troops_id];
        if (tm.force_id != a.u->force_id || tm.target != a.troops_goal_id) {
            tm = TroopRallyMem{a.u->force_id, a.troops_goal_id, 255, ctx.today, 0, 255, 0};
        }
    }
    for (const auto& kv : s_troops) {
        auto& tm = st.rally_mem[kv.first];
        if (kv.second.nearest_d < tm.dist) {
            tm.dist = (std::min)(kv.second.nearest_d, 254);
            tm.progress_day = ctx.today;
        }
        if (tm.start_dist == 255) tm.start_dist = (std::min)(kv.second.nearest_d, 254);
    }

    std::unordered_map<uint32_t, uint8_t> hold_mode;
    for (const auto& kv : s_troops) {
        const uint32_t t = kv.first;
        const auto& info = kv.second;
        auto& tm = st.rally_mem[t];
        const bool in_band = (info.nearest_d >= p.rally_meet_in && info.nearest_d <= p.rally_meet_out && info.foe > 3);
        bool coming = false;
        for (const auto& kv2 : s_troops) {
            if (kv2.first != t && kv2.second.force == info.force && kv2.second.goal == info.goal &&
                kv2.second.nearest_d > p.rally_meet_out && kv2.second.nearest_d <= p.rally_meet_far &&
                (ctx.today - st.rally_mem[kv2.first].progress_day) <= p.rally_meet_stall_days) {
                coming = true; break;
            }
        }
        if (in_band && coming) {
            if (tm.wait_from == 0) tm.wait_from = ctx.today;
            if ((ctx.today - tm.wait_from) <= p.rally_meet_max_days) hold_mode[t] = 1;
        } else if (!in_band && info.nearest_d > p.rally_meet_out) {
            tm.wait_from = 0;
        }

        const bool at_start = (info.nearest_d > p.rally_meet_out && info.nearest_d + p.rally_start_moved >= tm.start_dist && info.foe > 3);
        bool behind = false;
        for (const auto& kv2 : s_troops) {
            if (kv2.first != t && kv2.second.force == info.force && kv2.second.goal == info.goal &&
                kv2.second.nearest_d <= p.rally_start_far &&
                (kv2.second.nearest_d - info.nearest_d) > p.rally_start_slack &&
                (ctx.today - st.rally_mem[kv2.first].progress_day) <= p.rally_meet_stall_days) {
                behind = true; break;
            }
        }
        if (at_start && behind) {
            if (tm.start_from == 0) tm.start_from = ctx.today;
            if ((ctx.today - tm.start_from) <= p.rally_start_max_days) hold_mode[t] = 2;
        }
    }

    for (const auto& a : ctx.units) {
        if (a.troops_id == 0 || !s_troops.count(a.troops_id) || a.is_ship || a.u->task_locked != 0) continue;
        OrderMem* m = track_unit(ctx, a);
        if (!is_mine(m, OWNER_RALLY)) continue;
        const auto& info = s_troops[a.troops_id];
        const int32_t d = hex_dist(a.u->tile_id, info.goal_tile);
        const uint8_t hm = hold_mode[a.troops_id];
        const bool stay = nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id) > 1 &&
                          ((hm == 1 && d >= p.rally_meet_in && d <= p.rally_meet_out) || hm == 2);
        if (stay) {
            if (!(m->mod && m->w_act == ACT_MOVE && m->w_type == TT_HEX)) {
                order_unit(ctx, a, *m, OWNER_RALLY, ACT_MOVE, TT_HEX, static_cast<int32_t>(a.u->tile_id), SAN14_TACTICAL_MOD_RALLY);
            }
        } else if (m->mod) {
            restore_unit(ctx, a, *m);
        }
    }

    // 2. Intercept & area-attack rally (wait at lead, never pull backwards).
    std::unordered_map<uint32_t, size_t> lead_idx_by_troops;
    for (size_t k = 0; k < ctx.units.size(); ++k) {
        const auto& a = ctx.units[k];
        if (a.troops_id > 0 && a.is_lead) lead_idx_by_troops[a.troops_id] = k;
    }
    std::unordered_map<uint32_t, bool> intercept_behind;
    for (const auto& kv : lead_idx_by_troops) {
        const uint32_t tid = kv.first;
        const auto& lead = ctx.units[kv.second];
        if (!ctx.has_mod(lead.u->force_id, SAN14_TACTICAL_MOD_RALLY)) continue;
        if (ctx.troops_count[tid] < 2) continue;
        if (is_siege_task(lead.troops_purpose) || !is_fighting_task(lead.troops_purpose)) continue;
        if (nearest_enemy_dist(ctx, lead.u->tile_id, lead.u->force_id) <= 4) continue;
        const uint32_t home_city = ctx.unit_home_city_id(lead);
        const uint32_t home_tile = ctx.foothold_tile(home_city);
        if (home_tile > MAX_TILE_ID) continue;
        const int32_t l_home = hex_dist(lead.u->tile_id, home_tile);
        bool has_behind = false;
        for (const auto& a : ctx.units) {
            if (a.troops_id != tid || a.u->slot == lead.u->slot) continue;
            const int32_t dist = hex_dist(lead.u->tile_id, a.u->tile_id);
            const int32_t a_home = hex_dist(a.u->tile_id, home_tile);
            if (dist > 4 && dist <= 12 && l_home > a_home) { has_behind = true; break; }
        }
        if (has_behind) intercept_behind[tid] = true;
    }
    for (const auto& a : ctx.units) {
        if (a.troops_id == 0 || !ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_RALLY)) continue;
        if (is_siege_task(a.troops_purpose) || a.u->task_locked != 0) continue;
        OrderMem* m = track_unit(ctx, a);
        if (!is_mine(m, OWNER_RALLY)) continue;
        if (!intercept_behind[a.troops_id] || nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id) <= 1) {
            if (m->mod) restore_unit(ctx, a, *m);
            continue;
        }
        const auto& lead = ctx.units[lead_idx_by_troops[a.troops_id]];
        const uint32_t home_tile = ctx.foothold_tile(ctx.unit_home_city_id(lead));
        const int32_t l_home = hex_dist(lead.u->tile_id, home_tile);
        const int32_t my_home = hex_dist(a.u->tile_id, home_tile);
        const int32_t my_dist = hex_dist(lead.u->tile_id, a.u->tile_id);
        if (a.is_lead || (my_dist > 4 && my_dist <= 12 && my_home >= l_home)) {
            if (!(m->mod && m->w_act == ACT_WAIT && static_cast<uint32_t>(m->w_id) == a.u->tile_id)) {
                order_unit(ctx, a, *m, OWNER_RALLY, ACT_WAIT, TT_HEX, static_cast<int32_t>(a.u->tile_id), SAN14_TACTICAL_MOD_RALLY);
            }
        } else if (my_dist > 4 && my_dist <= 12 && l_home > my_home) {
            if (!(m->mod && m->w_act == ACT_MOVE && static_cast<uint32_t>(m->w_id) == lead.u->tile_id)) {
                order_unit(ctx, a, *m, OWNER_RALLY, ACT_MOVE, TT_HEX, static_cast<int32_t>(lead.u->tile_id), SAN14_TACTICAL_MOD_RALLY);
            }
        } else if (m->mod) {
            restore_unit(ctx, a, *m);
        }
    }

    // 3. Siege lone guard (prevent single spearhead walking into city garrison alone).
    for (const auto& a : ctx.units) {
        auto& sm = st.siege_lone_mem[a.u->slot];
        if (sm.commander_id != a.u->commander_id || sm.force_id != a.u->force_id) {
            sm = SiegeLoneMem{a.u->commander_id, a.u->force_id};
        }
        if (a.is_ship || !ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_RALLY)) { sm.on = 0; continue; }
        OrderMem* m = track_unit(ctx, a);
        if (!m) { sm.on = 0; continue; }

        const bool ours = (m->mod && m->owner == OWNER_LONE);
        if (sm.on && !ours && m->p_act != ACT_WAIT) {
            sm.o_act = m->p_act; sm.o_type = m->p_type; sm.o_id = m->p_id;
        }
        auto give_back = [&]() {
            if (!sm.on || !is_mine(m, OWNER_LONE)) return;
            order_unit(ctx, a, *m, OWNER_LONE, sm.o_act, sm.o_type, sm.o_id, 0);
            m->mod = 0;
            m->owner = OWNER_NONE;
        };

        const uint32_t goal = is_siege_task(a.troops_purpose) ? a.troops_goal_id : 0u;
        const uint32_t gh = ctx.foothold_tile(goal);
        if (gh > MAX_TILE_ID || !is_mine(m, OWNER_LONE) || a.u->task_locked != 0) {
            if (gh <= MAX_TILE_ID) give_back();
            sm.on = 0;
            continue;
        }
        if (sm.goal != goal) { sm.goal = goal; sm.since = 0; sm.block_until = 0; }
        const int32_t d = hex_dist(a.u->tile_id, gh);
        const int32_t foe = nearest_enemy_dist(ctx, a.u->tile_id, a.u->force_id);
        bool company = false;
        int32_t bi = -1, bd = 999;
        for (size_t j = 0; j < ctx.units.size(); ++j) {
            const auto& x = ctx.units[j];
            if (x.u->force_id != a.u->force_id || x.u->slot == a.u->slot || x.is_ship) continue;
            const int32_t dx = hex_dist(x.u->tile_id, a.u->tile_id);
            if (dx <= p.rally_lone_near) { company = true; break; }
            if (dx > p.rally_lone_join || dx >= bd || hex_dist(x.u->tile_id, gh) < d + p.rally_lone_ahead) continue;
            if (!is_siege_task(x.troops_purpose) || x.troops_goal_id != goal) continue;
            bd = dx; bi = static_cast<int32_t>(j);
        }
        const bool want = (d >= p.rally_lone_min && d <= p.rally_lone_max && !company && bi >= 0 &&
                           foe > 1 && ctx.today >= sm.block_until && is_field_order(m->p_act));
        if (want && sm.since != 0 && (ctx.today - sm.since) > p.rally_lone_wait_days) {
            sm.block_until = ctx.today + p.rally_lone_block_days;
            sm.since = 0;
            give_back();
            sm.on = 0;
            continue;
        }
        if (!want) {
            give_back();
            sm.on = 0;
            continue;
        }
        if (!sm.on) {
            sm.on = 1; sm.o_act = m->p_act; sm.o_type = m->p_type; sm.o_id = m->p_id;
            if (sm.since == 0) sm.since = ctx.today;
        }
        uint32_t to = a.u->tile_id;
        if (foe <= p.rally_lone_foe) {
            const uint32_t by = ctx.units[static_cast<size_t>(bi)].u->tile_id;
            const int32_t bx = static_cast<int32_t>(by % GRID_WIDTH), by_row = static_cast<int32_t>(by / GRID_WIDTH);
            int32_t best_h_d = 999;
            for (int32_t dy = -2; dy <= 2; ++dy) {
                for (int32_t dx = -2; dx <= 2; ++dx) {
                    const int32_t nx = bx + dx, ny = by_row + dy;
                    if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_WIDTH) continue;
                    const uint32_t tid = static_cast<uint32_t>(ny * GRID_WIDTH + nx);
                    if (tid == by || hex_dist(tid, by) > 2 || !ctx.is_walkable_free(tid)) continue;
                    const int32_t dh = hex_dist(tid, a.u->tile_id);
                    if (dh < best_h_d) { best_h_d = dh; to = tid; }
                }
            }
        }
        if (!(ours && m->w_act == ACT_MOVE && m->w_type == TT_HEX &&
              (to == a.u->tile_id || static_cast<uint32_t>(m->w_id) == to))) {
            order_unit(ctx, a, *m, OWNER_LONE, ACT_MOVE, TT_HEX, static_cast<int32_t>(to), SAN14_TACTICAL_MOD_RALLY);
        }
    }
}

// ---- Module 4: retreat (field intercept retreat + gate reach filter + unify dest) ---------------

inline bool enemy_blocked_by_gate(
    DailyContext& ctx,
    uint32_t lead_tile,
    uint32_t lead_force,
    const DailyUnit& enemy,
    int32_t steps
) {
    if (hex_dist(enemy.u->tile_id, lead_tile) > 12) return false;
    const uint32_t ef = enemy.u->force_id;
    const uint32_t form = enemy.u->land_formation;
    if ((form == FORM_YANXING || form == FORM_JINGLAN) && hex_dist(enemy.u->tile_id, lead_tile) <= 2) return false;
    if (form == FORM_TOUSHI && hex_dist(enemy.u->tile_id, lead_tile) <= 3) return false;

    auto bfs = [&](bool gates_open) {
        std::unordered_map<uint32_t, int32_t> dist;
        std::vector<uint32_t> q;
        dist[lead_tile] = 0;
        q.push_back(lead_tile);
        size_t head = 0;
        while (head < q.size()) {
            const uint32_t cur = q[head++];
            if (cur == enemy.u->tile_id) return true;
            const int32_t d = dist[cur];
            if (d >= steps) continue;
            std::array<uint32_t, 6> nbs{};
            const int n = collect_neighbors(cur, nbs);
            for (int i = 0; i < n; ++i) {
                const uint32_t nxt = nbs[i];
                if (dist.count(nxt)) continue;
                const uint32_t ter = ctx.terrain_at(nxt);
                if (ter == TER_GATE) {
                    if (!gates_open && ctx.tile_owner(nxt) != ef) continue;
                } else if (!is_walkable_land_kind(*ctx.st, ter) && !is_pure_water_terrain(ter)) {
                    continue;
                }
                dist[nxt] = d + 1;
                q.push_back(nxt);
            }
        }
        return false;
    };
    (void)lead_force;
    return bfs(true) && !bfs(false);
}

inline void run_day_retreat(DailyContext& ctx) {
    auto& st = *ctx.st;
    const auto& p = st.params;
    const bool is_turn_start = (ctx.w->day == 1 || ctx.w->day == 11 || ctx.w->day == 21);
    const bool check_flip = (p.retreat_check_daily || is_turn_start) &&
                            (st.last_retreat_turn_day != ctx.today) &&
                            ctx.ops &&
                            ctx.ops->struct_size >= offsetof(San14TacticalHostOps, set_troops_purpose) + sizeof(ctx.ops->set_troops_purpose) &&
                            ctx.ops->set_troops_purpose;

    if (check_flip) {
        st.last_retreat_turn_day = ctx.today;
        std::vector<size_t> intercept_leads;
        for (size_t k = 0; k < ctx.units.size(); ++k) {
            const auto& a = ctx.units[k];
            if (a.is_lead && a.troops_id > 0 && a.troops_purpose == 1u &&
                ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_RETREAT)) {
                intercept_leads.push_back(k);
            }
        }
        std::unordered_set<uint32_t> flipped_troops;
        for (size_t k : intercept_leads) {
            const auto& lead = ctx.units[k];
            if (flipped_troops.count(lead.troops_id)) continue;
            const uint32_t home_city = ctx.unit_home_city_id(lead);
            const uint32_t home_tile = ctx.foothold_tile(home_city);
            if (home_tile > MAX_TILE_ID || hex_dist(lead.u->tile_id, home_tile) <= p.retreat_home_safe_dist) continue;

            float ours = lead.troops_fighting_strength;
            float our_around = 0.0f;
            float their_around = 0.0f;
            const bool use_gate_filter = ctx.has_mod(lead.u->force_id, SAN14_TACTICAL_MOD_GATE);
            for (const auto& u : ctx.units) {
                if (hex_dist(u.u->tile_id, lead.u->tile_id) > p.retreat_scan_radius) continue;
                if (u.u->force_id == lead.u->force_id) {
                    our_around += u.u->strength;
                } else if (ctx.is_enemy(lead.u->force_id, u.u->force_id)) {
                    if (use_gate_filter && enemy_blocked_by_gate(ctx, lead.u->tile_id, lead.u->force_id, u, p.gate_reach_steps)) {
                        continue;
                    }
                    their_around += u.u->strength;
                }
            }
            if (our_around > ours) ours = our_around;
            if (ours <= 0.0f) continue;

            if (their_around * 100.0f > ours * static_cast<float>(p.retreat_ratio_pct)) {
                if (ctx.ops->set_troops_purpose(ctx.ops->ops_context, lead.troops_id, 0, home_city) == SAN14_OK) {
                    flipped_troops.insert(lead.troops_id);
                    record_telemetry(st, lead.u->force_id, SAN14_TACTICAL_MOD_RETREAT);
                }
                for (size_t k2 : intercept_leads) {
                    const auto& l2 = ctx.units[k2];
                    if (l2.troops_id == lead.troops_id || l2.u->force_id != lead.u->force_id ||
                        flipped_troops.count(l2.troops_id)) continue;
                    if (hex_dist(lead.u->tile_id, l2.u->tile_id) > p.retreat_scan_radius) continue;
                    const uint32_t h2 = ctx.unit_home_city_id(l2);
                    const uint32_t ht2 = ctx.foothold_tile(h2);
                    if (ht2 <= MAX_TILE_ID && hex_dist(l2.u->tile_id, ht2) > p.retreat_home_safe_dist) {
                        if (ctx.ops->set_troops_purpose(ctx.ops->ops_context, l2.troops_id, 0, h2) == SAN14_OK) {
                            flipped_troops.insert(l2.troops_id);
                            record_telemetry(st, l2.u->force_id, SAN14_TACTICAL_MOD_RETREAT);
                        }
                    }
                }
            }
        }
    }

    // Daily: unify multi-unit retreating troops destination when lead is not pinned.
    std::unordered_map<uint32_t, const DailyUnit*> lead_map;
    for (const auto& a : ctx.units) {
        if (a.is_lead && a.troops_id > 0) lead_map[a.troops_id] = &a;
    }
    for (const auto& a : ctx.units) {
        if (a.troops_id == 0 || a.troops_purpose != 0u || a.u->task_locked != 0) continue;
        if (!ctx.has_mod(a.u->force_id, SAN14_TACTICAL_MOD_RETREAT)) continue;
        if (ctx.troops_count[a.troops_id] < 2 || a.troops_goal_id == 0) continue;
        auto lit = lead_map.find(a.troops_id);
        if (lit == lead_map.end()) continue;
        if (nearest_enemy_dist(ctx, lit->second->u->tile_id, a.u->force_id) <= 2) continue;

        if (!(a.u->order_code == static_cast<uint32_t>(ACT_ENTER_CITY) &&
              a.u->target_type == static_cast<uint32_t>(TT_CITY) &&
              a.u->target_id == a.troops_goal_id)) {
            OrderMem* m = track_unit(ctx, a);
            if (m && is_mine(m, OWNER_RETREAT)) {
                order_unit(ctx, a, *m, OWNER_RETREAT, ACT_ENTER_CITY, TT_CITY, static_cast<int32_t>(a.troops_goal_id), SAN14_TACTICAL_MOD_RETREAT);
            }
        }
    }
}

// ---- Policy callbacks for native hooks & daily tick ----------------------------------------------

inline void SAN14_MOD_CALL on_daily_tactical_tick(
    void* plugin_context,
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops
) {
    if (!plugin_context || !w || !ops) return;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!st->config.any_new8() && !st->ab.active) return;

    const uint32_t date_key = w->year * 10000u + w->month * 100u + w->day;
    const int32_t today = day_number(w->year, w->month, w->day);
    if (date_key == st->last_daily_date_key) return;

    if (st->last_daily_date_key != 0) {
        const uint32_t prev_y = st->last_daily_date_key / 10000u;
        const uint32_t prev_m = (st->last_daily_date_key / 100u) % 100u;
        const uint32_t prev_d = st->last_daily_date_key % 100u;
        const int32_t prev_day = day_number(prev_y, prev_m, prev_d);
        if (date_key < st->last_daily_date_key || (today - prev_day) > 31) {
            st->clear_locked();
        }
    }
    st->last_daily_date_key = date_key;

    DailyContext ctx = build_daily_context(*st, w, ops);
    std::unordered_set<uint32_t> shore_reserved;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> claimed_by_force;
    std::unordered_map<uint32_t, int32_t> prep_cand_day;

    run_day_stance(ctx);
    run_day_defend(ctx);
    run_day_gate(ctx);
    run_day_shore(ctx, shore_reserved);
    run_day_navaldef(ctx, shore_reserved);
    run_day_relief(ctx);
    run_day_engage(ctx);
    run_day_fu(ctx, claimed_by_force, prep_cand_day);
    run_day_siegeprep(ctx, claimed_by_force);
    run_day_supplyguard(ctx, claimed_by_force);
    run_day_rally(ctx);
    run_day_retreat(ctx);
}

inline San14Result SAN14_MOD_CALL on_eval_intercept_deploy(
    void* plugin_context,
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    const San14TacticalInterceptDeployContext* context,
    San14TacticalInterceptDeployDecision* out_decision
) {
    if (!plugin_context || !w || !ops || !context || !out_decision) return SAN14_E_INVALID_ARGUMENT;
    if (out_decision->struct_size < sizeof(San14TacticalInterceptDeployDecision)) return SAN14_E_INVALID_ARGUMENT;

    out_decision->skip_deploy = 0;
    out_decision->override_troops = 0;
    out_decision->override_formation_id = 0;
    out_decision->override_naval_formation_id = 0;

    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    cache_world_metadata(*st, w);

    const bool do_gate = force_has_module(*st, w, ops, context->force_id, SAN14_TACTICAL_MOD_GATE);
    const bool do_deploy = force_has_module(*st, w, ops, context->force_id, SAN14_TACTICAL_MOD_DEPLOY);
    if (!do_gate && !do_deploy) return SAN14_OK;

    San14TacticalOfficer off{sizeof(off)};
    bool have_off = (ops->get_officer && ops->get_officer(ops->ops_context, context->officer_id, &off) == SAN14_OK);

    // 1. Gate intercept filter (Yanxing only, up to gate_max_archers per gate).
    if (do_gate && !is_siege_formation(context->formation_id) && !is_ship_formation(context->formation_id)) {
        auto git = st->city_gate.find(context->source_foothold_id);
        if (git != st->city_gate.end() && git->second > 51u) {
            const uint32_t gate_idx = git->second - 51u;
            const bool can_yanxing = have_off && ((off.land_formation_mask & (1u << FORM_YANXING)) != 0);
            if (can_yanxing && st->gate_arch[gate_idx] < st->params.gate_max_archers) {
                ++st->gate_arch[gate_idx];
                out_decision->override_formation_id = FORM_YANXING;
                record_telemetry(*st, context->force_id, SAN14_TACTICAL_MOD_GATE);
            } else {
                out_decision->skip_deploy = 1;
                record_telemetry(*st, context->force_id, SAN14_TACTICAL_MOD_GATE);
                return SAN14_OK;
            }
        }
    }

    // 2. Deploy intercept composition (split needed troops among strong generals + flank + cheerleaders).
    if (do_deploy && have_off) {
        const auto& p = st->params;
        const uint32_t wave_key = (context->district_id << 16) ^ (context->source_foothold_id << 8) ^ context->target_unit_slot;
        const uint32_t date_key = w->year * 10000u + w->month * 100u + w->day;
        auto& wave = st->deploy_waves[wave_key];

        uint32_t strong_cnt = 0;
        if (context->candidate_officer_ids && context->candidate_count > 0 && ops->get_officer) {
            for (uint32_t i = 0; i < context->candidate_count; ++i) {
                San14TacticalOfficer cand{sizeof(cand)};
                if (ops->get_officer(ops->ops_context, context->candidate_officer_ids[i], &cand) == SAN14_OK &&
                    cand.leadership >= p.deploy_strong_leadership) {
                    ++strong_cnt;
                }
            }
        } else if (off.leadership >= p.deploy_strong_leadership) {
            strong_cnt = 1;
        }
        const uint32_t k_mains = (std::min)(p.deploy_max_mains, (std::max)(1u, strong_cnt));

        if (context->sent_count == 0 || wave.date_key != date_key || wave.target_foothold != context->target_foothold_id) {
            const int32_t base_total = (context->needed_strength > context->troops) ? context->needed_strength : context->troops;
            int32_t share = base_total / static_cast<int32_t>(k_mains);
            if (share < p.deploy_min_share) share = p.deploy_min_share;
            wave = DeployWaveMem{date_key, context->target_foothold_id, share, false};
        }

        if (context->sent_count < k_mains && off.leadership >= p.deploy_strong_leadership) {
            const int32_t cap = (std::min)(context->max_troops > 0 ? context->max_troops : off.max_troops, context->troops);
            if (wave.share_troops > 0 && wave.share_troops < cap) {
                out_decision->override_troops = wave.share_troops;
            }
            if (context->sent_count >= 1 && !wave.flank_assigned && out_decision->override_formation_id == 0 &&
                ((off.land_formation_mask & (1u << FORM_FENGSHI)) != 0)) {
                out_decision->override_formation_id = FORM_FENGSHI;
                wave.flank_assigned = true;
            }
            record_telemetry(*st, context->force_id, SAN14_TACTICAL_MOD_DEPLOY);
        } else if (off.leadership < p.deploy_strong_leadership) {
            if (context->troops > p.deploy_cheer_cap) {
                out_decision->override_troops = p.deploy_cheer_cap;
            }
            if (out_decision->override_formation_id == 0 && ((off.land_formation_mask & (1u << FORM_YANXING)) != 0)) {
                out_decision->override_formation_id = FORM_YANXING;
            }
            record_telemetry(*st, context->force_id, SAN14_TACTICAL_MOD_DEPLOY);
        }
    }

    return SAN14_OK;
}

inline San14Result SAN14_MOD_CALL on_eval_intercept_gate(
    void* plugin_context,
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    uint32_t force_id,
    uint32_t target_foothold_id,
    uint32_t enemy_lead_unit_slot,
    int32_t native_blocked,
    int32_t* out_allow_intercept,
    int32_t* out_halve_water_strength
) {
    if (!plugin_context || !w || !ops || !out_allow_intercept || !out_halve_water_strength) {
        return SAN14_E_INVALID_ARGUMENT;
    }
    *out_allow_intercept = -1;
    *out_halve_water_strength = 0;

    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!force_has_module(*st, w, ops, force_id, SAN14_TACTICAL_MOD_WALLS)) return SAN14_OK;

    *out_halve_water_strength = 1;
    if (native_blocked) {
        const auto* lead = find_unit(w, enemy_lead_unit_slot);
        if (lead && lead->tile_id <= MAX_TILE_ID) {
            const uint32_t fh_tile = foothold_tile_id(w, target_foothold_id);
            bool on_water = false;
            San14TacticalTile t{};
            if (read_tile(ops, lead->tile_id, t)) on_water = is_pure_water_terrain(t.terrain_kind);
            if (on_water || (fh_tile <= MAX_TILE_ID && hex_dist(lead->tile_id, fh_tile) <= st->params.walls_unlock_dist)) {
                *out_allow_intercept = 1;
                record_telemetry(*st, force_id, SAN14_TACTICAL_MOD_WALLS);
            }
        }
    }
    return SAN14_OK;
}

inline San14Result SAN14_MOD_CALL on_eval_command_rollback_ex(
    void* plugin_context,
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    uint32_t district_id,
    uint32_t force_id,
    uint32_t remaining_commands,
    int32_t* out_force_skip
) {
    if (!plugin_context || !out_force_skip) return SAN14_E_INVALID_ARGUMENT;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    cache_world_metadata(*st, w);

    uint32_t eff_force = force_id;
    if (eff_force == 0 && district_id != 0) {
        auto dit = st->district_force_cache.find(district_id);
        if (dit != st->district_force_cache.end()) eff_force = dit->second;
    }

    if (eff_force != 0 && force_has_module(*st, w, ops, eff_force, SAN14_TACTICAL_MOD_SIEGEPLAN)) {
        *out_force_skip = 1;  // siegeenough: never launch under-strength siege when out of command books
        record_telemetry(*st, eff_force, SAN14_TACTICAL_MOD_SIEGEPLAN);
        return SAN14_OK;
    }
    if (eff_force != 0 && force_has_module(*st, w, ops, eff_force, SAN14_TACTICAL_MOD_LEGACY_REINFORCE)) {
        *out_force_skip = (remaining_commands == 0) ? 1 : 0;
        return SAN14_OK;
    }
    if (eff_force == 0 && st->config.emergency_reinforcement && !st->ab.active) {
        *out_force_skip = (remaining_commands == 0) ? 1 : 0;
        return SAN14_OK;
    }
    return SAN14_E_UNSUPPORTED;
}

inline San14Result SAN14_MOD_CALL on_post_order_target(
    void* plugin_context,
    const San14TacticalWorld* w,
    const San14TacticalHostOps* ops,
    uint32_t unit_slot,
    int32_t target_type,
    int32_t target_id,
    San14TacticalOrderProposal* out_proposal
) {
    if (!plugin_context || !w || !ops || !out_proposal) return SAN14_E_INVALID_ARGUMENT;
    out_proposal->decision = SAN14_TACTICAL_ORDER_KEEP;

    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    const auto* unit = find_unit(w, unit_slot);
    if (!unit || unit->kind != 1 || unit->task_locked != 0 || is_player_unit(w, *unit)) return SAN14_OK;
    if (unit->land_formation != FORM_YANXING || !force_has_module(*st, w, ops, unit->force_id, SAN14_TACTICAL_MOD_GATE)) {
        return SAN14_OK;
    }

    auto gmit = st->gate_mem.find(unit_slot);
    if (gmit == st->gate_mem.end()) return SAN14_OK;
    const auto& gm = gmit->second;
    if (gm.commander_id != unit->commander_id || gm.gate_foothold <= 51u) return SAN14_OK;
    const uint32_t gate_id = gm.gate_foothold - 51u;
    if (!st->gate_hot[gate_id]) return SAN14_OK;

    uint32_t target_tile = SAN14_TACTICAL_INVALID_TILE_ID;
    if (target_type == TT_HEX && target_id >= 0 && static_cast<uint32_t>(target_id) <= MAX_TILE_ID) {
        target_tile = static_cast<uint32_t>(target_id);
    } else if (target_type == TT_ARMY && target_id > 0) {
        if (const auto* tu = find_unit(w, static_cast<uint32_t>(target_id))) target_tile = tu->tile_id;
    }
    const uint32_t gh = st->gate_tile.count(gate_id) ? st->gate_tile[gate_id] : SAN14_TACTICAL_INVALID_TILE_ID;
    if (target_tile > MAX_TILE_ID || gh > MAX_TILE_ID || unit->tile_id > MAX_TILE_ID) return SAN14_OK;
    if (hex_dist(target_tile, gh) > st->params.gate_near + 3) return SAN14_OK;

    const auto sit_u = st->gate_side.find(unit->tile_id);
    const auto sit_t = st->gate_side.find(target_tile);
    const uint8_t u_side = (sit_u != st->gate_side.end()) ? sit_u->second : 0;
    const uint8_t t_side = (sit_t != st->gate_side.end()) ? sit_t->second : 0;
    if (u_side == gate_id && t_side != gate_id) {
        std::fill_n(out_proposal->waypoints, 5, SAN14_TACTICAL_INVALID_TILE_ID);
        out_proposal->decision = SAN14_TACTICAL_ORDER_REPLACE_ROUTE;
        out_proposal->order_code = ACT_WAIT;
        out_proposal->target_type = TT_HEX;
        out_proposal->target_id = static_cast<int32_t>(unit->tile_id);
        out_proposal->refresh_route = 0;
        record_telemetry(*st, unit->force_id, SAN14_TACTICAL_MOD_GATE);
    }
    return SAN14_OK;
}

inline San14Result SAN14_MOD_CALL on_query_telemetry(
    void* plugin_context,
    uint32_t force_id,
    San14TacticalForceTelemetry* out_telemetry
) {
    if (!plugin_context || !out_telemetry || out_telemetry->struct_size < sizeof(San14TacticalForceTelemetry)) {
        return SAN14_E_INVALID_ARGUMENT;
    }
    if (force_id == 0 || force_id >= MAX_FORCES) return SAN14_E_NOT_FOUND;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    *out_telemetry = st->telemetry[force_id];
    out_telemetry->struct_size = sizeof(San14TacticalForceTelemetry);
    out_telemetry->force_id = force_id;
    return SAN14_OK;
}

}  // namespace tactical_ai_policy
}  // namespace companion_mod

#endif
