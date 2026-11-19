#ifndef SAN14_MOD_SDK_NAVIGATION_H
#define SAN14_MOD_SDK_NAVIGATION_H
#include "san14_types.h"
enum San14DetailKind {SAN14_DETAIL_OFFICER=1,SAN14_DETAIL_CITY=2};
enum San14NativeRosterDestination {SAN14_NAVIGATION_OWN_FORCE_OFFICERS=1};
enum San14NativeRosterNavigationState {
    SAN14_ROSTER_NAV_PENDING=1, SAN14_ROSTER_NAV_OPEN=2,
    SAN14_ROSTER_NAV_CLOSED=3, SAN14_ROSTER_NAV_FAILED=4
};
/* Native read-only details, no game pointers. UI thread; close plugin modals
 * before opening. Needs ui.panel plus game.read.officers / game.read.cities.
 * The native dialog owns its lifetime; releasing a ticket does not close it. */
typedef struct San14NavigationApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *open_details)(void*,uint32_t kind,uint32_t id,San14Handle* ticket);
    /* Reports closure once and releases the ticket. A campaign revision change
     * returns STALE_GENERATION and releases it. */
    San14Result (SAN14_MOD_CALL *is_open)(void*,San14Handle ticket,int32_t* open);
    San14Result (SAN14_MOD_CALL *release)(void*,San14Handle ticket);
    /* Optional tail. From the planning map, queue the stock intelligence
     * menu's own-force officer list. A successful request may still fail at
     * the native update. Ticket state is owned by the requesting Mod. */
    San14Result (SAN14_MOD_CALL *open_roster)(void*,uint32_t destination,San14Handle* ticket);
    San14Result (SAN14_MOD_CALL *roster_status)(void*,San14Handle ticket,uint32_t* state);
    /* Return to the map when this ticket's roster is foreground. Cancels a
     * pending request. Never closes another native list or dialog. */
    San14Result (SAN14_MOD_CALL *close_roster)(void*,San14Handle ticket);
} San14NavigationApi;
#endif
