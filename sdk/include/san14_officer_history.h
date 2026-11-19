#ifndef SAN14_MOD_SDK_OFFICER_HISTORY_H
#define SAN14_MOD_SDK_OFFICER_HISTORY_H
#include "san14_types.h"
#include <stddef.h>
/* Global confirmed facts. IDs refer to the current campaign's data tables.
 * 0 means unknown/no actor; location UINT32_MAX means unknown tile. */
enum San14OfficerHistoryKind {
    SAN14_HISTORY_DUEL=1, SAN14_HISTORY_UNIT_DEFEATED=2,
    SAN14_HISTORY_CITY_TAKEN=3, SAN14_HISTORY_CAPTURE=4,
    SAN14_HISTORY_EMPLOY=5, SAN14_HISTORY_DIPLOMACY=6,
    SAN14_HISTORY_POST=7, SAN14_HISTORY_FORCE=8,
    SAN14_HISTORY_ITEM=9, SAN14_HISTORY_STATUS=10,
    SAN14_HISTORY_RECORDING_STARTED=11, SAN14_HISTORY_RECORDING_STOPPED=12,
    SAN14_HISTORY_DEATH=13
};
enum San14OfficerHistoryRecording {
    SAN14_HISTORY_RECORD_DUEL_WIN=1u<<0, SAN14_HISTORY_RECORD_DUEL_LOSS=1u<<1,
    SAN14_HISTORY_RECORD_DUEL_DRAW=1u<<2, SAN14_HISTORY_RECORD_UNIT_WIN=1u<<3,
    SAN14_HISTORY_RECORD_UNIT_LOSS=1u<<4, SAN14_HISTORY_RECORD_CITY=1u<<5,
    SAN14_HISTORY_RECORD_CAPTURE=1u<<6, SAN14_HISTORY_RECORD_CAPTURED=1u<<7,
    SAN14_HISTORY_RECORD_EMPLOY=1u<<8, SAN14_HISTORY_RECORD_EMPLOYED=1u<<9,
    SAN14_HISTORY_RECORD_DIPLOMACY=1u<<10, SAN14_HISTORY_RECORD_POST=1u<<11,
    SAN14_HISTORY_RECORD_FORCE=1u<<12, SAN14_HISTORY_RECORD_STATUS=1u<<13,
    SAN14_HISTORY_RECORD_DEATH=1u<<14, SAN14_HISTORY_RECORD_ITEM_GAIN=1u<<15,
    SAN14_HISTORY_RECORD_ITEM_LOSS=1u<<16, SAN14_HISTORY_RECORD_ALL=(1u<<17)-1
};
enum San14OfficerHistoryDeath { SAN14_HISTORY_DEATH_UNKNOWN=0, SAN14_HISTORY_DEATH_NATURAL=1,
    SAN14_HISTORY_DEATH_BATTLE=2, SAN14_HISTORY_DEATH_EXECUTION=3 };
enum San14OfficerHistoryItem { SAN14_HISTORY_ITEM_TRANSFER=0, SAN14_HISTORY_ITEM_GRANT=1,
    SAN14_HISTORY_ITEM_CONFISCATE=2 };
enum San14OfficerHistoryPost { SAN14_HISTORY_POST_UNKNOWN=0, SAN14_HISTORY_POST_PROMOTED=1,
    SAN14_HISTORY_POST_DEMOTED=2, SAN14_HISTORY_POST_LATERAL=3,
    SAN14_HISTORY_POST_APPOINTED=4, SAN14_HISTORY_POST_REMOVED=5 };
/* A confirmed whole-person settlement ended in a new force ruled by actor.
 * This records independence, without guessing whether a scheme caused it. */
enum San14OfficerHistoryForce { SAN14_HISTORY_FORCE_TRANSFER=0, SAN14_HISTORY_FORCE_INDEPENDENT=1 };
/* STATUS 6 -> serving/free only; UNKNOWN includes legacy records. Captured at
 * the release settlement, never inferred from a same-day report or new rank. */
enum San14OfficerHistoryRelease { SAN14_HISTORY_RELEASE_UNKNOWN=0,
    SAN14_HISTORY_RELEASE_ESCAPE=1, SAN14_HISTORY_RELEASE_DIPLOMATIC=2,
    SAN14_HISTORY_RELEASE_FREED=3, SAN14_HISTORY_RELEASE_RESCUED=4,
    SAN14_HISTORY_RELEASE_UNIT_DEFEAT=5 };
/* High flag bits select the participating officer views recorded for this event.
 * Zero high bits are legacy records, visible to both participants. */
#define SAN14_HISTORY_VIEW_ACTOR 0x40u
#define SAN14_HISTORY_VIEW_SUBJECT 0x80u
/* DUEL/UNIT/CAPTURE/DEATH old/new_value hold the participants' force rulers
 * captured at settlement. CITY related_officer_id holds the previous ruler.
 * EMPLOY related_officer_id holds the joining force ruler. FORCE related/detail
 * hold previous/new ruler IDs. POST/STATUS/DIPLOMACY related holds actor ruler;
 * CITY detail holds actor ruler; ITEM old/new hold recipient/previous-owner rulers.
 * Missing/legacy values retain the force-ID fallback. */
#define SAN14_HISTORY_FORCE_RULERS 0x10u
/* DUEL result: 0 actor wins, 1 subject wins, 2 draw.
 * POST/FORCE/STATUS: actor changed old_value -> new_value.
 * FORCE INDEPENDENT object_id: confirmed current city when known (0 unknown).
 * STATUS 6 -> 1..5 result: San14OfficerHistoryRelease; object_id: previous
 * current city, validated before release (0 unknown). Never home-city fallback.
 * RESCUED/UNIT_DEFEAT subject_id: confirmed liberating commander, subject_force
 * and detail_value: commander's force/ruler. Rescue additionally requires the
 * winning force to match the prisoner's pre-release original force.
 * ITEM: actor=recipient, subject=previous owner, object_id=item ID.
 * CITY: object_id=city ID, old/new_value=force IDs.
 * UNIT/CAPTURE: actor=winner/captor (possibly unknown), subject=loser/captive.
 * Flags bit 0 on FORCE: ownership change observed inside Employ.
 * DEATH bit 1: correlated lethal duel settlement; remaining low bits reserved. */
typedef struct San14OfficerHistoryEvent {
    uint32_t struct_size,kind;
    uint64_t event_id;
    uint32_t year,month,day;
    uint32_t actor_id,subject_id,object_id,old_value,new_value;
    uint32_t actor_force_id,subject_force_id,result,flags,location_tile_id;
    uint32_t reserved_v1_padding;
    /* Optional v2 tail. episode_id is a causal settlement ID (never a pointer).
     * related_officer_id: decision maker for a confirmed item grant/confiscation.
     * detail_value: captured age for DEATH, packed old/new grades for POST.
     * DEATH actor=deceased, subject=confirmed killer (or 0), result=death cause.
     * ITEM result=confirmed transfer type. POST result=grade comparison.
     * Old records have zero-valued tails; callers must not invent missing facts. */
    uint32_t episode_id,related_officer_id,detail_value;
    uint32_t reserved_v2_padding; /* Preserve the original 88-byte V2 stride. */
    /* Optional v3 appearance snapshots: low byte=color index+1, bits8..23=UTF-16 banner.
     * Zero means unavailable. Never a current-force lookup. */
    uint32_t actor_appearance,subject_appearance;
} San14OfficerHistoryEvent;
#define SAN14_OFFICER_HISTORY_EVENT_V2_SIZE ((uint32_t)offsetof(San14OfficerHistoryEvent,actor_appearance))
#define SAN14_OFFICER_HISTORY_EVENT_V1_SIZE ((uint32_t)offsetof(San14OfficerHistoryEvent,episode_id))
typedef struct San14OfficerHistoryInfo {
    uint32_t struct_size,recording;
    uint64_t revision,total_events;
    uint32_t officer_events,first_date;
    int32_t recording_error;
    uint32_t reserved;
} San14OfficerHistoryInfo;
typedef struct San14OfficerHistoryApi {
    uint32_t struct_size;
    /* UI thread. An independent owner lease, unrelated to aggregate counters.
     * Requires game.read.officers + storage.playthrough. */
    San14Result (SAN14_MOD_CALL *set_recording)(void*,int32_t enabled);
    /* Latest first, offset in this officer's index; officer_id=0 queries all.
     * UI thread. No game pointers. capacity <= 256.
     * Initialize info.struct_size. A revision change requires requery at offset 0. */
    San14Result (SAN14_MOD_CALL *query)(void*,uint32_t officer_id,uint32_t offset,
        void* events,uint32_t capacity,uint32_t stride,uint32_t* count,San14OfficerHistoryInfo* info);
    /* UI thread; the stock intelligence-log presentation, scoped to one officer.
     * Optional: UNSUPPORTED when the native UI adapter is unavailable. */
    San14Result (SAN14_MOD_CALL *show)(void*,uint32_t officer_id);
    /* Attach/detach the history action to stock officer details. */
    San14Result (SAN14_MOD_CALL *set_detail_action)(void*,int32_t enabled);
    /* Optional v2 tail. UI thread, same recording permissions. Per-owner union;
     * selected participants are stored in the event, so later setting changes do
     * not alter old records. Set before enabling recording; default ALL. */
    San14Result (SAN14_MOD_CALL *set_event_mask)(void*,uint32_t mask);
    /* Optional v3 tail. UI thread; same read/storage permissions. Formats a
     * stored fact for a recorded participant. No raw native message IDs.
     * flags=0 plain UTF-8, flags=1 native rich text. required includes NUL;
     * insufficient capacity returns BUFFER_TOO_SMALL without partial text.
     * revision must match the query that supplied event_id. */
    San14Result (SAN14_MOD_CALL *describe)(void*,uint64_t revision,uint64_t event_id,
        uint32_t officer_id,uint32_t flags,char* buffer,uint32_t capacity,uint32_t* required);
} San14OfficerHistoryApi;
#define SAN14_OFFICER_HISTORY_API_V2_SIZE ((uint32_t)offsetof(San14OfficerHistoryApi,describe))
#define SAN14_OFFICER_HISTORY_API_V1_SIZE ((uint32_t)offsetof(San14OfficerHistoryApi,set_event_mask))
#endif
