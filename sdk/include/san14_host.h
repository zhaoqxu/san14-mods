#ifndef SAN14_MOD_SDK_HOST_H
#define SAN14_MOD_SDK_HOST_H

#include "san14_events.h"
#include "san14_game.h"
#include "san14_ui.h"
#include "san14_ui_style.h"
#include "san14_ui_log.h"
#include "san14_clipboard.h"
#include "san14_rules.h"
#include "san14_roster.h"
#include "san14_ui_ext.h"
#include "san14_pages.h"
#include "san14_memory.h"
#include "san14_presentation.h"
#include "san14_office.h"
#include "san14_government.h"
#include "san14_world.h"
#include "san14_automation.h"
#include "san14_spectator.h"
#include "san14_scenario.h"
#include "san14_save_storage.h"
#include "san14_roster_tabs.h"
#include "san14_area.h"
#include "san14_diplomacy.h"
#include "san14_tactical_ai.h"
#include "san14_force_color.h"
#include "san14_speed.h"
#include "san14_domestic.h"
#include "san14_officer_records.h"
#include "san14_officer_history.h"
#include "san14_text_rendering.h"
#include "san14_content.h"
#include "san14_navigation.h"
#include "san14_officer_traits.h"
#include "san14_officer_edit.h"

typedef struct San14LogApi {
    uint32_t struct_size;
    /* Host attributes every row to host_context's Mod ID. Rows written here
       automatically appear as that Mod's source in the manager log exporter. */
    void (SAN14_MOD_CALL *write)(
        void* host_context,
        San14LogLevel level,
        San14StringView category,
        San14StringView message
    );
} San14LogApi;

typedef struct San14SchedulerApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *dispatch)(
        void* host_context,
        San14Thread thread,
        San14TaskCallback callback,
        void* plugin_context,
        San14TaskHandle* out_task
    );
    San14Result (SAN14_MOD_CALL *cancel)(void* host_context, San14TaskHandle task);
} San14SchedulerApi;

typedef struct San14InputApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *register_hotkey)(
        void* host_context,
        San14StringView action_id,
        uint32_t virtual_key,
        San14TaskCallback callback,
        void* plugin_context,
        San14Handle* out_hotkey
    );
    San14Result (SAN14_MOD_CALL *release)(void* host_context, San14Handle input_resource);
} San14InputApi;

typedef struct San14StorageApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *read)(
        void* host_context,
        San14StringView scope,
        San14StringView key,
        void* buffer,
        uint32_t buffer_size,
        uint32_t* out_required_size
    );
    San14Result (SAN14_MOD_CALL *write_atomic)(
        void* host_context,
        San14StringView scope,
        San14StringView key,
        const void* data,
        uint32_t data_size
    );
} San14StorageApi;

typedef struct San14HostApi {
    uint32_t struct_size;
    uint32_t abi_version;
    void* host_context;
    San14LogApi log;
    San14EventApi events;
    San14UiApi ui;
    San14InputApi input;
    San14GameApi game;
    San14StorageApi storage;
    San14SchedulerApi scheduler;
    /* Optional tail: check struct_size before reading these services. */
    San14RuleApi rules;
    San14SettingsApi settings;
    /* Optional append-only service; check struct_size before access. */
    San14RosterApi roster;
    San14UiExtendedApi ui_extended;
    /* Optional pointer table. Check Host size, pointer and service prefix. */
    const San14PagesApi* pages;
    /* Optional tail. Check Host size, pointer and service prefix before use. */
    const San14MemoryApi* memory;
    const San14PresentationApi* presentation;
    const San14OfficeApi* office;
    const San14GovernmentApi* government;
    const San14WorldApi* world;
    const San14AutomationApi* automation;
    const San14McpApi* mcp;
    /* Optional exact-build pause-menu spectator/takeover service. */
    const San14SpectatorApi* spectator;
    const San14ScenarioApi* scenario;
    /* Optional managed sidecar storage, appended without changing ABI v1 prefix. */
    const San14SaveStorageApi* save_storage;
    const struct San14UiStyleApi* ui_style;
    const San14ClipboardApi* clipboard;
    const San14RosterTabsApi* roster_tabs;
    const San14AreaApi* areas;
    const San14DiplomacyApi* diplomacy;
    const San14TacticalAiApi* tactical_ai;
    const San14ForceColorApi* force_colors;
    const San14SettingsExtendedApi* settings_ext;
    const struct San14UiSliderApi* ui_slider;
    const San14GameSpeedApi* game_speed;
    const San14DomesticApi* domestic;
    const San14OfficerRecordsApi* officer_records;
    const San14OfficerHistoryApi* officer_history;
    const San14ContentApi* content;
    /* Optional global text-cache rendering service; append-only ABI v1 tail. */
    const San14TextRenderingApi* text_rendering;
    const San14OfficerTraitsApi* officer_traits;
    const San14NavigationApi* navigation;
    const San14UiLogApi* ui_log;
    const San14OfficerEditApi* officer_edit;
    /* Fixed optional slots for independently built adapters. Unavailable services are null. */
    const struct San14BattlefieldApi* battlefield;
    const struct San14RosterViewApi* roster_view;
    const struct San14TerritoryTabsApi* territory_tabs;
    const struct San14MatchApi* match;
} San14HostApi;

#endif
