#ifndef SAN14_MOD_SDK_RELOAD_H
#define SAN14_MOD_SDK_RELOAD_H
#include "san14_mod.h"
/* Optional V1 opt-in. Old Hosts ignore this export; Mod API v1 is unchanged.
   Export ONLY after auditing every lifecycle path, including failed init/load.
   V1 supports single-DLL, Host-managed callbacks only: no private worker threads,
   unmanaged callbacks/hooks, module self-references or external DLL consumers.
   on_disable/on_unload must release all resources without relying on a future
   callback. Lifecycle must not change native entities or unmanaged files.
   on_unload must tolerate partial on_load/on_enable failure. Preferences must
   use SDK storage, and package assets must be read-only. All calls are on UI.
   Prepare is a veto/preflight, not disable: return BUSY with old state intact
   if a safe stop is currently impossible. Host also requires title-menu state,
   drained callback leases and no outstanding native work. A missing export
   keeps the existing restart-update path. Returning OK is a contract, not a
   guarantee that Host will unload immediately. */
typedef San14Result (SAN14_MOD_CALL *San14ModPrepareReloadV1)(San14ModContext*);
#ifdef __cplusplus
extern "C" {
#endif
SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_PrepareReloadV1(San14ModContext*);
#ifdef __cplusplus
}
#endif
#endif
