#ifndef SAN14_MOD_SDK_TACTICAL_AI_H
#define SAN14_MOD_SDK_TACTICAL_AI_H

#include "san14_types.h"
#include "san14_officer_traits.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SAN14_TACTICAL_TREATY_STRIDE = 55,
    SAN14_TACTICAL_INVALID_TILE_ID = 0xbd10,
    SAN14_TACTICAL_MAX_TILE_ID = 0xbd0f
};

enum {
    SAN14_TACTICAL_MOD_DEPLOY           = 1u << 0,
    SAN14_TACTICAL_MOD_RALLY            = 1u << 1,
    SAN14_TACTICAL_MOD_ENGAGE           = 1u << 2,
    SAN14_TACTICAL_MOD_RETREAT          = 1u << 3,
    SAN14_TACTICAL_MOD_STANCE           = 1u << 4,
    SAN14_TACTICAL_MOD_WALLS            = 1u << 5,
    SAN14_TACTICAL_MOD_SIEGEPLAN        = 1u << 6,
    SAN14_TACTICAL_MOD_GATE             = 1u << 7,
    SAN14_TACTICAL_MOD_LEGACY_NAVAL     = 1u << 8,
    SAN14_TACTICAL_MOD_LEGACY_REINFORCE = 1u << 9,
    SAN14_TACTICAL_MOD_LEGACY_SIEGE_INT = 1u << 10,
    SAN14_TACTICAL_MOD_ALL_NEW8         = 0x00FFu,
    SAN14_TACTICAL_MOD_ALL_LEGACY3      = 0x0700u
};

typedef struct San14TacticalDistrict {
    uint32_t struct_size;
    uint32_t district_id;
    uint32_t force_id;
    uint32_t remaining_commands;
    uint8_t is_player_direct;
    uint8_t reserved[3];
} San14TacticalDistrict;

typedef struct San14TacticalForce {
    uint32_t struct_size;
    uint32_t force_id;
    uint32_t ruler_id;
} San14TacticalForce;

typedef struct San14TacticalCity {
    uint32_t struct_size;
    uint32_t city_id;
    uint32_t district_id;
    uint32_t force_id;
    int32_t gold;
    int32_t troops;
    int32_t food;
    int32_t morale;
    uint32_t tile_id;
    uint32_t home_global_id;
    uint8_t is_player_direct;
    uint8_t reserved[3];
} San14TacticalCity;

typedef struct San14TacticalUnit {
    uint32_t struct_size;
    uint32_t slot;
    uint32_t force_id;
    uint32_t home_global_id;
    uint32_t kind;
    uint32_t commander_id;
    int32_t troops;
    int32_t morale;
    uint32_t land_formation;
    uint32_t naval_formation;
    uint32_t flags;
    uint32_t tile_id;
    uint32_t next_step_tile_id;
    uint32_t waypoints[5];
    uint32_t order_code;
    uint32_t target_type;
    uint32_t target_id;
    uint32_t target_tile_id;
    uint32_t behavior0;
    uint32_t behavior1;
    uint32_t aux42;
    uint32_t aux44;
    uint32_t aux46;
    uint32_t task_locked;
    uint8_t is_player_direct;
    uint8_t reserved[3];
    float strength;
} San14TacticalUnit;

typedef struct San14TacticalFormation {
    uint32_t struct_size;
    uint32_t formation_id;
    uint32_t speed;
    uint32_t attack;
    uint32_t defense;
    uint32_t cost;
} San14TacticalFormation;

typedef struct San14TacticalTerrain {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t effort;
    uint8_t is_water;
    uint8_t reserved[3];
} San14TacticalTerrain;

typedef struct San14TacticalTile {
    uint32_t struct_size;
    uint32_t tile_id;
    uint32_t terrain_kind;
    uint32_t effort;
    uint32_t area_id;
    uint32_t area_city_id;
    uint32_t owner_force_id;
    uint32_t occupant_unit_slot;
    uint8_t is_water;
    uint8_t reserved[3];
} San14TacticalTile;

typedef struct San14TacticalOfficer {
    uint32_t struct_size;
    uint32_t officer_id;
    uint32_t rank;
    uint32_t district_id;
    uint32_t home_global_id;
    uint32_t stay_global_id;
    int32_t leadership;
    int32_t strength;
    int32_t intelligence;
    uint32_t land_formation_mask;
    uint32_t naval_formation;
    int32_t max_troops;
    uint16_t traits[5];
    uint8_t is_busy;
    uint8_t is_away;
    uint8_t has_active_unit;
    uint8_t has_active_command;
} San14TacticalOfficer;

typedef struct San14TacticalDeployRequest {
    uint32_t struct_size;
    uint32_t officer_id;
    uint32_t formation_id;
    uint32_t naval_formation_id;
    int32_t troops;
    int32_t order_code;
    uint32_t target_type;
    uint32_t target_id;
    /* Set every unused stop to SAN14_TACTICAL_INVALID_TILE_ID.
       Zero is a real tile ID; zero-initializing this array does not omit stops. */
    uint32_t waypoint_tiles[5];
} San14TacticalDeployRequest;

typedef struct San14TacticalDeployResult {
    uint32_t struct_size;
    uint32_t unit_slot;
    int32_t deployed_troops;
} San14TacticalDeployResult;

typedef enum San14TacticalOrderDecision {
    SAN14_TACTICAL_ORDER_KEEP = 0,
    SAN14_TACTICAL_ORDER_REPLACE_ROUTE = 1
} San14TacticalOrderDecision;

typedef struct San14TacticalOrderProposal {
    uint32_t struct_size;
    San14TacticalOrderDecision decision;
    int32_t order_code;
    int32_t target_type;
    int32_t target_id;
    /* Host initializes all stops to SAN14_TACTICAL_INVALID_TILE_ID before the
       callback. A replacing policy should explicitly fill its intended route
       (including unused stops), for compatibility with older Hosts. */
    uint32_t waypoints[5];
    uint8_t refresh_route;
    uint8_t reserved[3];
} San14TacticalOrderProposal;

typedef struct San14TacticalUnitExt {
    uint32_t struct_size;
    uint32_t unit_slot;
    uint32_t troops_id;
    uint32_t troops_purpose;
    uint32_t troops_goal_id;
    uint32_t troops_leader_officer_id;
    uint32_t troops_leader_slot;
    uint32_t ship_kind;
    float troops_fighting_strength;
} San14TacticalUnitExt;

typedef struct San14TacticalForceExt {
    uint32_t struct_size;
    uint32_t force_id;
    uint32_t naval_ship_kind;
} San14TacticalForceExt;

typedef struct San14TacticalArea {
    uint32_t struct_size;
    uint32_t area_id;
    uint32_t city_id;
    uint32_t fu_tile_id;
    uint32_t owner_force_id;
    uint16_t painted_hex_count;
    uint16_t total_hex_count;
    uint32_t neighbor_area_ids[8];
    uint8_t has_foothold;
    uint8_t reserved[3];
} San14TacticalArea;

typedef struct San14TacticalGate {
    uint32_t struct_size;
    uint32_t gate_id;
    uint32_t foothold_id;
    uint32_t tile_id;
    uint32_t owner_force_id;
    uint32_t district_id;
} San14TacticalGate;

typedef enum San14TacticalAbMode {
    SAN14_TACTICAL_AB_ALL_ACTIVE = 0,
    SAN14_TACTICAL_AB_VANILLA = 1,
    SAN14_TACTICAL_AB_LEGACY = 2,
    SAN14_TACTICAL_AB_NEW8 = 3,
    SAN14_TACTICAL_AB_FUSED = 4,
    SAN14_TACTICAL_AB_CUSTOM_MASK = 5
} San14TacticalAbMode;

typedef struct San14TacticalForceAbOverride {
    uint32_t struct_size;
    uint8_t has_override;
    uint8_t reserved[3];
    San14TacticalAbMode mode;
    uint32_t enabled_module_mask;
} San14TacticalForceAbOverride;

typedef struct San14TacticalInterceptDeployContext {
    uint32_t struct_size;
    uint32_t district_id;
    uint32_t force_id;
    uint32_t source_foothold_id;
    uint32_t target_foothold_id;
    uint32_t officer_id;
    uint32_t formation_id;
    uint32_t naval_formation_id;
    int32_t troops;
    int32_t max_troops;
    int32_t needed_strength;
    uint32_t sent_count;
    uint32_t target_unit_slot;
    const uint32_t* candidate_officer_ids;
    uint32_t candidate_count;
} San14TacticalInterceptDeployContext;

typedef struct San14TacticalInterceptDeployDecision {
    uint32_t struct_size;
    uint8_t skip_deploy;
    uint8_t reserved[3];
    int32_t override_troops;
    uint32_t override_formation_id;
    uint32_t override_naval_formation_id;
} San14TacticalInterceptDeployDecision;

typedef struct San14TacticalForceTelemetry {
    uint32_t struct_size;
    uint32_t force_id;
    uint32_t deploy_overrides;
    uint32_t rally_holds;
    uint32_t engage_retargets;
    uint32_t retreat_triggers;
    uint32_t stance_adjusts;
    uint32_t walls_actions;
    uint32_t siegeplan_adjusts;
    uint32_t gate_actions;
    uint32_t legacy_actions;
} San14TacticalForceTelemetry;

typedef struct San14TacticalWorld {
    uint32_t struct_size;
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t now_ms;
    uint32_t player_force_id;
    uint8_t is_spectator_mode;
    uint8_t reserved[3];
    const San14TacticalDistrict* districts;
    uint32_t district_count;
    const San14TacticalForce* forces;
    uint32_t force_count;
    const San14TacticalCity* cities;
    uint32_t city_count;
    const San14TacticalUnit* units;
    uint32_t unit_count;
    const San14TacticalFormation* formations;
    uint32_t formation_count;
    const San14TacticalTerrain* terrains;
    uint32_t terrain_count;
    const uint8_t* treaties;
    uint32_t treaty_stride;
    /* Optional append-only tail (check struct_size > SAN14_TACTICAL_WORLD_V1_SIZE). */
    const San14TacticalUnitExt* unit_exts;
    uint32_t unit_ext_count;
    const San14TacticalForceExt* force_exts;
    uint32_t force_ext_count;
    const San14TacticalArea* areas;
    uint32_t area_count;
    const San14TacticalGate* gates;
    uint32_t gate_count;
} San14TacticalWorld;
#define SAN14_TACTICAL_WORLD_V1_SIZE offsetof(San14TacticalWorld, unit_exts)

/* Synchronous Host query/command operations valid only for the duration of a
 * tactical policy callback. Do not retain ops or ops_context. */
typedef struct San14TacticalHostOps {
    uint32_t struct_size;
    void* ops_context;
    San14Result (SAN14_MOD_CALL *get_tile)(
        void* ops_context,
        uint32_t tile_id,
        San14TacticalTile* out_tile
    );
    int32_t (SAN14_MOD_CALL *hex_distance)(
        void* ops_context,
        uint32_t tile_a,
        uint32_t tile_b
    );
    San14Result (SAN14_MOD_CALL *get_officer)(
        void* ops_context,
        uint32_t officer_id,
        San14TacticalOfficer* out_officer
    );
    San14Result (SAN14_MOD_CALL *list_city_officers)(
        void* ops_context,
        uint32_t city_id,
        San14TacticalOfficer* out_officers,
        uint32_t capacity,
        uint32_t* out_count
    );
    int32_t (SAN14_MOD_CALL *city_base_strength)(
        void* ops_context,
        uint32_t city_id,
        uint32_t arg1,
        uint32_t arg2,
        uint32_t arg3
    );
    int32_t (SAN14_MOD_CALL *is_enemy_force)(
        void* ops_context,
        uint32_t force_a,
        uint32_t force_b
    );
    San14Result (SAN14_MOD_CALL *submit_unit_order)(
        void* ops_context,
        uint32_t unit_slot,
        int32_t order_code,
        int32_t target_type,
        int32_t target_id
    );
    San14Result (SAN14_MOD_CALL *set_unit_route)(
        void* ops_context,
        uint32_t unit_slot,
        int32_t order_code,
        int32_t target_type,
        int32_t target_id,
        const uint32_t waypoints[5],
        uint8_t refresh_route
    );
    San14Result (SAN14_MOD_CALL *return_unit)(
        void* ops_context,
        uint32_t unit_slot,
        int32_t rebuild,
        int32_t report,
        int32_t enter
    );
    San14Result (SAN14_MOD_CALL *deploy_unit)(
        void* ops_context,
        const San14TacticalDeployRequest* request,
        San14TacticalDeployResult* out_result
    );
    /* Optional append-only tail. The returned IDs retain physical positions.
       active_mask is the native activation mask, not an ID-validity mask.
       This callback-scoped read has generation=0 and cannot authorize edits.
       Check struct_size before accessing; old get/list records remain frozen. */
    San14Result (SAN14_MOD_CALL *get_officer_traits)(
        void* ops_context, uint32_t officer_id, San14OfficerTraits* out_traits
    );
    /* Optional append-only tail (check struct_size > SAN14_TACTICAL_HOST_OPS_V1_SIZE). */
    San14Result (SAN14_MOD_CALL *eval_area_bonus)(
        void* ops_context,
        uint32_t tile_id,
        uint32_t force_id,
        float* out_multiplier,
        int32_t* out_sum
    );
    San14Result (SAN14_MOD_CALL *eval_unit_exchange_ratio)(
        void* ops_context,
        uint32_t attacker_slot,
        uint32_t atk_formation_id,
        uint32_t defender_slot,
        uint32_t def_formation_id,
        float* out_ratio
    );
    San14Result (SAN14_MOD_CALL *set_troops_purpose)(
        void* ops_context,
        uint32_t troops_id,
        int32_t purpose,
        uint32_t goal_foothold_id
    );
    San14Result (SAN14_MOD_CALL *get_force_ab_override)(
        void* ops_context,
        uint32_t force_id,
        San14TacticalForceAbOverride* out_override
    );
} San14TacticalHostOps;
#define SAN14_TACTICAL_HOST_OPS_V1_SIZE offsetof(San14TacticalHostOps, eval_area_bonus)

typedef enum San14TacticalStrategyPhase {
    SAN14_TACTICAL_PHASE_PLANNING = 1,
    SAN14_TACTICAL_PHASE_POST_PLANNING = 2
} San14TacticalStrategyPhase;

typedef void (SAN14_MOD_CALL *San14TacticalResetCallback)(
    void* plugin_context
);

typedef void (SAN14_MOD_CALL *San14TacticalStrategyCallback)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    San14TacticalStrategyPhase phase
);

typedef San14Result (SAN14_MOD_CALL *San14TacticalOrderTargetFilter)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    uint32_t unit_slot,
    int32_t target_type,
    int32_t target_id,
    San14TacticalOrderProposal* out_proposal
);

/* out_override: -1 = keep native_result, 0 = prevent retreat, 1 = force retreat. */
typedef San14Result (SAN14_MOD_CALL *San14TacticalRetreatEval)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    uint32_t unit_slot,
    int32_t native_result,
    int32_t* out_override
);

/* out_allow: 1 = allow native return, 0 = suppress native return. */
typedef San14Result (SAN14_MOD_CALL *San14TacticalUnitReturnEval)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    uint32_t unit_slot,
    int32_t rebuild,
    int32_t report,
    int32_t enter,
    int32_t* out_allow
);

/* out_include: 1 = keep native transport contribution, 0 = exclude from city defense count. */
typedef San14Result (SAN14_MOD_CALL *San14TacticalTransportIncludeEval)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    uint32_t unit_slot,
    uint32_t dest_city_id,
    int32_t* out_include
);

typedef San14Result (SAN14_MOD_CALL *San14TacticalTargetStrengthEval)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    uint32_t district_id,
    uint32_t target_city_id,
    int32_t base_strength,
    int32_t base_reserve,
    int32_t* out_corrected_strength
);

typedef San14Result (SAN14_MOD_CALL *San14TacticalReserveEval)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    uint32_t source_city_id,
    int32_t current_base_reserve,
    int32_t current_quota,
    int32_t* out_new_reserve,
    int32_t* out_new_quota
);

typedef San14Result (SAN14_MOD_CALL *San14TacticalCommandRollbackEval)(
    void* plugin_context,
    uint32_t district_id,
    uint32_t remaining_commands,
    int32_t* out_force_skip
);

typedef San14Result (SAN14_MOD_CALL *San14TacticalDefenseOfficerEval)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    uint32_t source_city_id,
    int32_t native_area_id,
    uint32_t planned_target_tile_id,
    const uint32_t* candidate_officer_ids,
    uint32_t candidate_count,
    uint32_t* out_promoted_officer_id,
    uint32_t* out_override_target_tile_id
);

typedef void (SAN14_MOD_CALL *San14TacticalDailyCallback)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops
);

typedef San14Result (SAN14_MOD_CALL *San14TacticalInterceptDeployEval)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    const San14TacticalInterceptDeployContext* context,
    San14TacticalInterceptDeployDecision* out_decision
);

/* out_allow_intercept: -1 = keep native_blocked, 0 = force block, 1 = force allow.
 * out_halve_water_strength: 0 = normal, 1 = halve enemy strength on water tiles. */
typedef San14Result (SAN14_MOD_CALL *San14TacticalInterceptGateEval)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    uint32_t force_id,
    uint32_t target_foothold_id,
    uint32_t enemy_lead_unit_slot,
    int32_t native_blocked,
    int32_t* out_allow_intercept,
    int32_t* out_halve_water_strength
);

typedef San14Result (SAN14_MOD_CALL *San14TacticalCommandRollbackExEval)(
    void* plugin_context,
    const San14TacticalWorld* world,
    const San14TacticalHostOps* ops,
    uint32_t district_id,
    uint32_t force_id,
    uint32_t remaining_commands,
    int32_t* out_force_skip
);

typedef San14Result (SAN14_MOD_CALL *San14TacticalTelemetryQuery)(
    void* plugin_context,
    uint32_t force_id,
    San14TacticalForceTelemetry* out_telemetry
);

typedef struct San14TacticalAiPolicy {
    uint32_t struct_size;
    void* plugin_context;
    San14TacticalResetCallback on_reset;
    San14TacticalStrategyCallback on_strategy_update;
    San14TacticalOrderTargetFilter on_filter_order_target;
    San14TacticalRetreatEval on_eval_retreat;
    San14TacticalUnitReturnEval on_eval_unit_return;
    San14TacticalTransportIncludeEval on_eval_transport_include;
    San14TacticalTargetStrengthEval on_eval_target_strength;
    San14TacticalReserveEval on_eval_candidate_reserve;
    San14TacticalReserveEval on_eval_draft_reserve;
    San14TacticalCommandRollbackEval on_eval_command_rollback;
    San14TacticalDefenseOfficerEval on_select_defense_officer;
    /* Optional append-only tail (check struct_size > SAN14_TACTICAL_AI_POLICY_V1_SIZE). */
    San14TacticalDailyCallback on_daily_tactical_tick;
    San14TacticalInterceptDeployEval on_eval_intercept_deploy;
    San14TacticalInterceptGateEval on_eval_intercept_gate;
    San14TacticalCommandRollbackExEval on_eval_command_rollback_ex;
    San14TacticalOrderTargetFilter on_post_order_target;
    San14TacticalTelemetryQuery on_query_telemetry;
} San14TacticalAiPolicy;
#define SAN14_TACTICAL_AI_POLICY_V1_SIZE offsetof(San14TacticalAiPolicy, on_daily_tactical_tick)

/* Optional service for district/unit tactical AI policies.
 * Requires permission game.rules for policy registration (single active owner),
 * paired with named rule "ai.tactical.policy". */
typedef struct San14TacticalAiApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *register_policy)(
        void* host_context,
        const San14TacticalAiPolicy* policy
    );
    San14Result (SAN14_MOD_CALL *unregister_policy)(
        void* host_context
    );
} San14TacticalAiApi;

#ifdef __cplusplus
}
#endif

#endif
