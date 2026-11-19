#ifndef SAN14_BATTLEFIELD_DEATHS_H
#define SAN14_BATTLEFIELD_DEATHS_H
#include "san14_types.h"
typedef struct San14BattlefieldDeaths {
 uint32_t struct_size,kind,object_id,available;
 uint64_t soldiers_dead;
} San14BattlefieldDeaths;
#endif
