#ifndef SAN14_TERRITORY_TABS_H
#define SAN14_TERRITORY_TABS_H
#include "san14_roster_tabs.h"
#define SAN14_TERRITORY_CITY 1u
#define SAN14_TERRITORY_AREA 2u
typedef San14Result (SAN14_MOD_CALL *San14TerritoryTabValue)(void*,uint32_t object_id,uint32_t column,int64_t*);
typedef struct San14TerritoryTabSpec {
 uint32_t struct_size;
 San14StringView title;
 const San14RosterColumn* columns;
 uint32_t column_count,visible_mask;
 San14RosterTabRefresh refresh;
 San14TerritoryTabValue value;
 void* plugin_context;
} San14TerritoryTabSpec;
/* Optional copied tabs in native own/all-force intelligence city/area lists.
 * IDs, row eligibility, sorting and list lifetime remain native-owned.
 * Register/unregister on UI thread, refresh may use host data context.
 * Requires ui.panel plus game.read.cities or game.read.areas respectively.
 * Numeric values are signed 64-bit. NOT_FOUND displays unavailable. */
typedef struct San14TerritoryTabsApi {
 uint32_t struct_size;
 San14Result (SAN14_MOD_CALL *register_tab)(void*,uint32_t kind,San14StringView key,const San14TerritoryTabSpec*);
 San14Result (SAN14_MOD_CALL *unregister_tab)(void*,uint32_t kind,San14StringView key);
} San14TerritoryTabsApi;
#endif
