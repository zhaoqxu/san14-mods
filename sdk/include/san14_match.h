#ifndef SAN14_MOD_SDK_MATCH_H
#define SAN14_MOD_SDK_MATCH_H
#include "san14_types.h"
#include <stddef.h>

/* Optional automated two-sided campaign service. Uses the original game's
 * spectator mode and rules. It does not promise headless rendering or seeded
 * determinism. UI-thread calls queue work onto the native manager thread. */
typedef struct San14MatchOptions {
    uint32_t struct_size;
    San14StringView request_id; /* unique, retries with the same options deduplicate */
    San14StringView save_basename; /* current-edition basename without .s14; empty = current map */
    San14StringView required_content_id; /* must match the loaded campaign and save attachment */
    uint32_t max_turns; /* 1..3600; date cutoff, no winner inferred */
    uint32_t wall_timeout_seconds; /* 10..86400, real unscaled wall time */
    uint32_t stall_timeout_seconds; /* 10..3600 without a game-date change */
    uint32_t reserved_v1_tail; /* preserves the original 72-byte prefix */
    uint32_t speed_x100; /* optional tail: 0 = inherit (no lease), 100..800 = control; requires game.rules */
    uint32_t full_snapshot_interval_ms; /* 0 = every observation; 1..5000 = periodic */
} San14MatchOptions;
#define SAN14_MATCH_OPTIONS_V1_SIZE offsetof(San14MatchOptions,speed_x100)
#define SAN14_MATCH_CAP_SPEED 1u
#define SAN14_MATCH_CAP_PERIODIC_SNAPSHOTS 2u

typedef struct San14MatchApi {
    uint32_t struct_size;
    /* Requires game.commands, game.read.saves and cities/forces/units reads.
     * Only one run is active. Current-map starts require an explicit empty
     * basename and a matching two-sided campaign; unrelated maps are refused. */
    San14Result (SAN14_MOD_CALL *start)(void*,const San14MatchOptions*,San14Handle*);
    /* Cached UTF-8 JSON; required includes NUL. Short buffers do not mutate or
     * consume the run. Reports termination/truncation/error and pause status
     * separately. Result snapshots are observations, not damage attribution. */
    San14Result (SAN14_MOD_CALL *status)(void*,San14Handle,char*,uint32_t,uint32_t*);
    /* Requests a safe stop; completion is observable through status. */
    San14Result (SAN14_MOD_CALL *cancel)(void*,San14Handle);
    /* Optional tail. Check struct_size before reading and require these bits
     * before submitting options beyond SAN14_MATCH_OPTIONS_V1_SIZE. */
    uint32_t capabilities;
} San14MatchApi;
#endif
