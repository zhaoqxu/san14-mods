#ifndef SAN14_MOD_SDK_BATTLEFIELD_H
#define SAN14_MOD_SDK_BATTLEFIELD_H
#include "san14_types.h"
#define SAN14_BATTLEFIELD_LOSSES 1u
#define SAN14_BATTLEFIELD_DEFEATS 2u
#define SAN14_BATTLEFIELD_OFFICERS 4u
#define SAN14_BATTLEFIELD_TACTICS 8u
#define SAN14_BATTLEFIELD_ALL 15u
typedef struct San14BattlefieldOfficer {
    uint32_t struct_size,officer_id,available_categories,reserved;
    uint64_t field_damage,city_damage,battle_losses,durability_damage;
    uint64_t tactics_used,links_joined,links_started,links_followed;
} San14BattlefieldOfficer;
typedef struct San14BattlefieldTactic {
    uint32_t struct_size,tactic_id;
    uint64_t uses;
    char name_utf8[80];
} San14BattlefieldTactic;
/* Copied geography and campaign counters. Losses include wounded soldiers;
 * they are not a count of deaths. Missing coverage is not invented as zero.
 * Geography is shared for the fixed map; only counters roll back with saves. */
typedef struct San14BattlefieldArea {
    uint32_t struct_size,area_id,city_id,available_categories;
    uint64_t battle_losses,units_defeated;
    char city_name_utf8[32],area_name_utf8[48];
} San14BattlefieldArea;
typedef struct San14BattlefieldApi {
    uint32_t struct_size;
    /* UI thread; recording leases are owner-scoped. All services require
     * game.read.areas, game.read.officers and storage.playthrough.
     * Releasing a lease preserves existing counters. */
    San14Result (SAN14_MOD_CALL *set_recording)(void*,uint32_t categories);
    San14Result (SAN14_MOD_CALL *capabilities)(void*,uint32_t* categories);
    /* UI thread; area 1..500. Initialize output.struct_size. */
    San14Result (SAN14_MOD_CALL *query_area)(void*,uint32_t,San14BattlefieldArea*);
    /* UI or roster-refresh data context. Counter structs require struct_size. */
    San14Result (SAN14_MOD_CALL *query_officer)(void*,uint32_t,San14BattlefieldOfficer*);
    /* UI thread; offset enumerates only tactics actually recorded for officer.
     * Returns NOT_FOUND at end. No inferred unique-tactic classification. */
    San14Result (SAN14_MOD_CALL *query_tactic)(void*,uint32_t,uint32_t,San14BattlefieldTactic*);
} San14BattlefieldApi;
#endif
