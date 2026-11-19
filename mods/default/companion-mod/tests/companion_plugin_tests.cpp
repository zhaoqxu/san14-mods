#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "san14_mod.h"
#include "san14_idle.h"
#include "san14_settings_template.h"
#include "plugin_ui_test_fixture.h"
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
std::vector<San14BoolSetting> settings;
std::map<std::string, int> values, rules, rule_calls;
std::string fail_rule, fail_get;
int fail_rule_value = -1;
bool fail_hotkey = false, fail_release = false, fail_office = false, fail_government = false, fail_office_cleanup = false, fail_government_cleanup = false;
bool fail_area = false, fail_area_cleanup = false;
int office_registers = 0, office_unregisters = 0, government_registers = 0, government_unregisters = 0, hotkey_registers = 0, hotkey_releases = 0, report_requests = 0;
int area_registers = 0, area_unregisters = 0;
int spectator_enables = 0, spectator_disables = 0;
San14Subscription next_subscription = 0;
San14EventCallback force_menu_event=nullptr;void* force_menu_context=nullptr;
bool force_menu_requested=false;
uint32_t controlled_force=1;
uint32_t force_count=3;
uint64_t force_generation=7;
San14Result force_selection_result=SAN14_OK;
int force_selections=0;
San14OfficePolicy registered_office{};
San14GovernmentScoreCallback registered_government = nullptr;
void* government_context = nullptr;
San14AreaPolicy registered_area{};
struct Hotkey { San14TaskCallback callback; void* context; };
std::map<San14Handle, Hotkey> registered_hotkeys;
San14Handle next_hotkey = 0;

std::string key(San14StringView s) { return {s.data, s.length}; }
San14Result SAN14_MOD_CALL reg(void*, const San14BoolSetting* s) { settings.push_back(*s); return SAN14_OK; }
San14Result SAN14_MOD_CALL get(void*, San14StringView k, int32_t* out) { if (key(k) == fail_get) return SAN14_E_BUSY; *out = values[key(k)]; return SAN14_OK; }
San14Result SAN14_MOD_CALL rule(void*, San14StringView k, int32_t v) {
    const auto name = key(k);
    ++rule_calls[name];
    if (name == fail_rule && (fail_rule_value < 0 || v == fail_rule_value)) return SAN14_E_BUSY;
    if (v && name == "officers.office.weighted") assert(registered_office.score);
    if (v && name == "government.ai.optimized") assert(registered_government);
    if (v && name == "presentation.turn_reports.skip") assert(registered_hotkeys.size() == 1);
    rules[name] = v;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL office_policy(void*, const San14OfficePolicy* spec) {
    if (!spec) { ++office_unregisters; if (fail_office_cleanup) return SAN14_E_BUSY; registered_office = {}; return SAN14_OK; }
    ++office_registers;
    if (fail_office) return SAN14_E_BUSY;
    assert(!registered_office.score && spec->struct_size == sizeof(*spec) && spec->score && spec->classify_officer && spec->classify_post);
    registered_office = *spec;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL government_policy(void*, San14GovernmentScoreCallback callback, void* context) {
    ++government_registers;
    if (fail_government) return SAN14_E_BUSY;
    assert(!registered_government && callback);
    registered_government = callback;
    government_context = context;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL government_cleanup(void*) {
    ++government_unregisters;
    if (fail_government_cleanup) return SAN14_E_BUSY;
    registered_government = nullptr;
    government_context = nullptr;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL area_register(void*, const San14AreaPolicy* policy) {
    ++area_registers;
    if (fail_area) return SAN14_E_BUSY;
    assert(policy && policy->struct_size >= sizeof(San14AreaPolicy) && policy->on_sync);
    registered_area = *policy;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL area_unregister(void*) {
    ++area_unregisters;
    if (fail_area_cleanup) return SAN14_E_BUSY;
    registered_area = {};
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL hotkey_register(void*, San14StringView action, uint32_t vk, San14TaskCallback callback, void* context, San14Handle* out) {
    ++hotkey_registers;
    assert(key(action) == "turn-report.open" && vk == 0x77 && callback && out);
    if (fail_hotkey) return SAN14_E_BUSY;
    assert(registered_hotkeys.empty());
    *out = ++next_hotkey;
    registered_hotkeys.emplace(*out, Hotkey{callback, context});
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL hotkey_release(void*, San14Handle handle) {
    ++hotkey_releases;
    if (fail_release) return SAN14_E_BUSY;
    return registered_hotkeys.erase(handle) ? SAN14_OK : SAN14_E_NOT_FOUND;
}
San14Result SAN14_MOD_CALL report_request(void*) { ++report_requests; return SAN14_OK; }
San14Result SAN14_MOD_CALL event_subscribe(void*, uint32_t event, int32_t, San14EventCallback callback, void* ctx, San14Subscription* out) {
    assert(callback && out);
    assert(event == SAN14_EVENT_UI_IDLE);
    force_menu_event=callback;force_menu_context=ctx;
    *out = ++next_subscription;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL event_unsubscribe(void*, San14Subscription) { return SAN14_OK; }
San14Result SAN14_MOD_CALL spectator_menu(void*, int32_t value) { value ? ++spectator_enables : ++spectator_disables; return SAN14_OK; }
San14Result SAN14_MOD_CALL spectator_poll(void*, int32_t* out) { *out = force_menu_requested?1:0;force_menu_requested=false; return SAN14_OK; }
San14Result SAN14_MOD_CALL spectator_active(void*, int32_t* out) { *out = controlled_force==0?1:0; return SAN14_OK; }
San14Result SAN14_MOD_CALL spectator_takeover(void*, uint32_t) { return SAN14_OK; }
San14Result SAN14_MOD_CALL spectator_select(void*,uint64_t generation,uint32_t expected,uint32_t selected){
    if(generation!=force_generation||expected!=controlled_force)return SAN14_E_STALE_GENERATION;
    if(force_selection_result!=SAN14_OK)return force_selection_result;
    ++force_selections;controlled_force=selected==controlled_force?0u:selected;return SAN14_OK;
}
San14Result SAN14_MOD_CALL force_capture(void*,uint32_t sections,San14Handle* handle,San14WorldInfo* info){
    assert(sections==SAN14_WORLD_FORCES);*handle=55;info->force_count=force_count;info->player_force_id=controlled_force;info->generation=force_generation;return SAN14_OK;
}
San14Result SAN14_MOD_CALL force_rows(void*,San14Handle handle,uint32_t offset,void* rows,uint32_t capacity,uint32_t stride,uint32_t* count){
    assert(handle==55&&offset==0&&capacity==force_count&&stride==sizeof(San14ForceSnapshot));
    for(unsigned i=0;i<force_count;++i){auto* row=reinterpret_cast<San14ForceSnapshot*>(static_cast<unsigned char*>(rows)+stride*i);*row={};row->struct_size=sizeof(*row);row->id=i+1;strcpy_s(row->name_utf8,i==0?u8"曹操":i==1?u8"刘备":u8"孙权");}*count=force_count;return SAN14_OK;
}
San14Result SAN14_MOD_CALL force_release(void*,San14Handle handle){assert(handle==55);return SAN14_OK;}

const San14OfficeApi office_service{sizeof(San14OfficeApi), office_policy};
const San14GovernmentApi government_service{sizeof(San14GovernmentApi), government_policy, government_cleanup};
const San14PresentationApi presentation_service{sizeof(San14PresentationApi), report_request};
const San14WorldApi world_service{sizeof(San14WorldApi),force_capture,nullptr,force_rows,nullptr,force_release};
const San14SpectatorApi spectator_service{sizeof(San14SpectatorApi), spectator_menu, spectator_poll, spectator_active, spectator_takeover,spectator_select};
const San14AreaApi area_service{sizeof(San14AreaApi), nullptr, nullptr, nullptr, nullptr, area_register, area_unregister};

San14Result SAN14_MOD_CALL scenario_query(void*, uint32_t id, San14ScenarioParameterInfo* out) {
    assert(id == 4 || id == 5);
    out->supported_units = 3;
    out->coefficient_maximum = 100;
    for (int d = 0; d < 5; ++d) out->coefficient_bases[d] = id == 4 ? 4 : d == 4 ? 24 : d == 3 ? 16 : 4;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL scenario_configure(void*, const San14ScenarioParameterSpec* specs, uint32_t count) {
    assert(count == 0 || count == 2);
    if (count) {
        for (unsigned i = 0; i < count; ++i) {
            assert(specs[i].parameter_id == 4 + i && specs[i].minimum == 0 && specs[i].maximum == 100 && specs[i].unit == SAN14_SCENARIO_BASE_COEFFICIENT && specs[i].description.length);
        }
        assert(specs[0].defaults[4] == 4 && specs[1].defaults[4] == 24);
    }
    return rule(nullptr, {"scenario.parameters", 19}, count ? 1 : 0);
}
bool scenario_choice = false;
San14Result SAN14_MOD_CALL scenario_bools(void*, const San14ScenarioBoolSpec* specs, uint32_t count) {
    assert(count <= 1);
    if (count) {
        assert(key(specs[0].key) == "manual_tactics" && specs[0].after_parameter == 46 && specs[0].binding == SAN14_SCENARIO_BOOL_MANUAL_TACTICS);
        assert(specs[0].defaults[0] == 1 && specs[0].defaults[4] == 0);
    }
    scenario_choice = count != 0;
    return SAN14_OK;
}
const San14ScenarioApi scenario_service{sizeof(San14ScenarioApi), scenario_query, scenario_configure, scenario_bools, nullptr};
const std::map<std::string, std::string> expected_features = {
    {"spectator_autosave", "save.spectator.autosave"},
    {"automatic_historical_events", "events.historical.automatic"},
    {"disable_tribal_officer_gifts", "diplomacy.tribal_gifts.blocked"},
    {"disable_foreign_trade", "diplomacy.foreign_trade.blocked"},
    {"disable_tribal_troop_replenish", "cities.tribal_replenishment.blocked"},
    {"editable_scenario_settings", "scenario.settings.editable"},
    {"fine_expert_mode", "scenario.parameters"},
    {"preserve_captive_post_and_return", "officers.prisoners.preserve_and_return"},
    {"allow_away_post_assignment", "officers.away.appoint_and_reward"},
    {"shared_domestic_duty_slots", "officers.duties.shared_slots"},
    {"allow_subordinate_command_officers", "officers.subordinate.commands"},
    {"allow_subordinate_base_operations", "cities.subordinate.operations"},
    {"allow_person_group_district_officers", "officers.subordinate.groups"},
    {"weighted_office_auto_assign", "officers.office.weighted"},
    {"block_ai_district_split", "ai.districts.blocked"},
    {"disable_elder_portraits", "officers.elder_portraits.disabled"},
    {"skip_turn_reports", "presentation.turn_reports.skip"},
    {"defer_disaster_reports", "presentation.repeated.simplify"},
    {"hide_war_book_discovery_message", "notifications.war_book.hidden"},
    {"global_death_notices", "notifications.death.global"},
    {"persistent_area_officers", "area.ai.persistent"}
};

void reset_companion_mocks() {
    settings.clear(); values.clear(); rules.clear(); rule_calls.clear(); fail_rule.clear(); fail_get.clear(); fail_rule_value = -1;
    fail_hotkey = fail_release = fail_office = fail_government = fail_office_cleanup = fail_government_cleanup = false;
    fail_area = fail_area_cleanup = false;
    office_registers = office_unregisters = government_registers = government_unregisters = hotkey_registers = hotkey_releases = report_requests = 0;
    area_registers = area_unregisters = 0;
    spectator_enables = spectator_disables = 0; next_subscription = 0;
    force_menu_event=nullptr;force_menu_context=nullptr;force_menu_requested=false;controlled_force=1;force_count=3;force_generation=7;force_selection_result=SAN14_OK;force_selections=0;
    registered_office = {}; registered_government = nullptr; government_context = nullptr;
    registered_area = {}; registered_hotkeys.clear();
}
San14HostApi fake_host() {
    San14HostApi host{}; host.struct_size = sizeof(host); host.abi_version = SAN14_HOST_ABI_V1;
    host.rules = {sizeof(host.rules), rule}; host.settings = {sizeof(host.settings), reg, get};
    host.input = {sizeof(host.input), hotkey_register, hotkey_release}; host.office = &office_service;
    host.government = &government_service; host.presentation = &presentation_service; host.world = &world_service;
    host.spectator = &spectator_service; host.scenario = &scenario_service;
    host.areas = &area_service;
    host.ui.struct_size = sizeof(San14UiApi); host.events = {sizeof(San14EventApi), event_subscribe, event_unsubscribe};
    return host;
}

struct MockControl {
    San14PanelHandle panel = 0;
    std::string id;
    std::string text;
    uint32_t tint = 0;
    int32_t selected = 0;
    int32_t slider_value = 0;
    int32_t slider_min = 0;
    int32_t slider_max = 0;
    int32_t slider_step = 0;
    San14ControlCallback cb = nullptr;
    San14IntChangedCallback int_cb = nullptr;
    void* ctx = nullptr;
    bool is_button = false;
    bool is_slider = false;
};
struct MockUiState {
    San14Handle next = 1;
    std::map<San14PanelHandle, std::string> panels;
    std::map<San14PanelHandle, San14PanelSpec> panel_geometry;
    std::vector<San14PanelHandle> stack;
    std::map<San14ControlHandle, MockControl> controls;
    San14SettingsPageSpec registered_page{};
    std::vector<San14RangeSetting> range_settings;
    std::map<std::string, int32_t> range_values;
    std::map<std::string, std::vector<uint8_t>> global_storage;
    uint32_t speed_x100 = 100;
    uint32_t speed_flags = 0;
    int speed_set_calls = 0;
    bool fail_show=false;
};
MockUiState* g_mock_ui = nullptr;

San14Result SAN14_MOD_CALL ui_create(void*, const San14PanelSpec* spec, San14PanelHandle* out) {
    auto h = g_mock_ui->next++;
    g_mock_ui->panels[h] = key(spec->title);
    g_mock_ui->panel_geometry[h]=*spec;
    assert(spec->height<=1080&&spec->y>=0&&spec->y+spec->height<=1080);
    *out = h;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL ui_add_label(void*, San14PanelHandle p, const San14ControlSpec* spec, San14ControlHandle* out) {
    auto h = g_mock_ui->next++;
    g_mock_ui->controls[h] = {p, key(spec->control_id), key(spec->text), 0, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, false, false};
    *out = h;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL ui_add_button(void*, San14PanelHandle p, const San14ControlSpec* spec, San14ControlCallback cb, void* ctx, San14ControlHandle* out) {
    if(key(spec->control_id).rfind("spectator.force.",0)==0){
        assert((spec->flags&~SAN14_CONTROL_SELECTED)==SAN14_CONTROL_TAB);
        // Native tab root adds 24px, with fixed 28px top/bottom slices.
        // The old 30px button produced a 54px root and collapsed its plate.
        assert(spec->height==40&&spec->height+24>28+28);
        assert(spec->y>=181&&(spec->y-181)%66==0);
        const auto& panel=g_mock_ui->panel_geometry.at(p);
        assert(spec->x>=0&&spec->x+spec->width<=panel.width);
        assert(spec->y+spec->height<=panel.height);
    }
    auto h = g_mock_ui->next++;
    g_mock_ui->controls[h] = {p, key(spec->control_id), key(spec->text), 0, (spec->flags & SAN14_CONTROL_SELECTED) ? 1 : 0, 0, 0, 0, 0, cb, nullptr, ctx, true, false};
    *out = h;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL ui_set_text(void*, San14ControlHandle c, San14StringView v) {
    auto it = g_mock_ui->controls.find(c);
    if (it == g_mock_ui->controls.end()) return SAN14_E_NOT_FOUND;
    it->second.text = key(v);
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL ui_show(void*, San14PanelHandle p) {
    if(g_mock_ui->fail_show){g_mock_ui->fail_show=false;return SAN14_E_PLUGIN_FAILED;}
    g_mock_ui->stack.push_back(p);
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL ui_close(void*, San14PanelHandle p) {
    g_mock_ui->panels.erase(p);
    g_mock_ui->panel_geometry.erase(p);
    for (auto it = g_mock_ui->controls.begin(); it != g_mock_ui->controls.end();) {
        if (it->second.panel == p) it = g_mock_ui->controls.erase(it);
        else ++it;
    }
    for (auto it = g_mock_ui->stack.begin(); it != g_mock_ui->stack.end();) {
        if (*it == p) it = g_mock_ui->stack.erase(it);
        else ++it;
    }
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL style_selected(void*, San14ControlHandle c, int32_t s) {
    auto it = g_mock_ui->controls.find(c);
    if (it == g_mock_ui->controls.end()) return SAN14_E_NOT_FOUND;
    it->second.selected = s;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL style_help(void*, San14ControlHandle, San14ControlHandle, San14StringView) {
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL style_tint(void*, San14ControlHandle c, uint32_t argb) {
    auto it = g_mock_ui->controls.find(c);
    if (it == g_mock_ui->controls.end()) return SAN14_E_NOT_FOUND;
    it->second.tint = argb;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL pages_register(void*, const San14SettingsPageSpec* spec) {
    g_mock_ui->registered_page = *spec;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL storage_read_fn(void*, San14StringView scope, San14StringView k, void* out, uint32_t cap, uint32_t* req) {
    if (key(scope) != "global") return SAN14_E_UNSUPPORTED;
    auto it = g_mock_ui->global_storage.find(key(k));
    if (it == g_mock_ui->global_storage.end()) { *req = 0; return SAN14_E_NOT_FOUND; }
    *req = static_cast<uint32_t>(it->second.size());
    if (cap < *req) return SAN14_E_BUFFER_TOO_SMALL;
    if (*req && out) std::memcpy(out, it->second.data(), *req);
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL storage_write_fn(void*, San14StringView scope, San14StringView k, const void* data, uint32_t size) {
    if (key(scope) != "global") return SAN14_E_UNSUPPORTED;
    const auto* bytes = static_cast<const uint8_t*>(data);
    g_mock_ui->global_storage[key(k)] = size ? std::vector<uint8_t>(bytes, bytes + size) : std::vector<uint8_t>{};
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL reg_range_fn(void*, const San14RangeSetting* spec) {
    g_mock_ui->range_settings.push_back(*spec);
    auto k = key(spec->key);
    if (!g_mock_ui->range_values.count(k)) g_mock_ui->range_values[k] = spec->default_value;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL get_range_fn(void*, San14StringView k, int32_t* out) {
    auto it = g_mock_ui->range_values.find(key(k));
    if (it == g_mock_ui->range_values.end()) return SAN14_E_NOT_FOUND;
    *out = it->second;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL ui_add_slider_fn(void*, San14PanelHandle p, const San14SliderSpec* spec, San14IntChangedCallback cb, void* ctx, San14ControlHandle* out) {
    auto h = g_mock_ui->next++;
    MockControl mc{};
    mc.panel = p; mc.id = key(spec->control_id);
    mc.slider_min = spec->min_value; mc.slider_max = spec->max_value;
    mc.slider_step = spec->step; mc.slider_value = san14::snap_range_value(spec->value, spec->min_value, spec->max_value, spec->step);
    mc.int_cb = cb; mc.ctx = ctx; mc.is_slider = true;
    g_mock_ui->controls[h] = mc;
    *out = h;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL ui_set_slider_fn(void*, San14ControlHandle c, int32_t value) {
    auto it = g_mock_ui->controls.find(c);
    if (it == g_mock_ui->controls.end() || !it->second.is_slider) return SAN14_E_NOT_FOUND;
    it->second.slider_value = san14::snap_range_value(value, it->second.slider_min, it->second.slider_max, it->second.slider_step);
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL ui_get_slider_fn(void*, San14ControlHandle c, int32_t* out) {
    auto it = g_mock_ui->controls.find(c);
    if (it == g_mock_ui->controls.end() || !it->second.is_slider) return SAN14_E_NOT_FOUND;
    *out = it->second.slider_value;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL speed_caps_fn(void*, San14SpeedCaps* out) {
    *out = {sizeof(*out), 100, 800, 25, SAN14_SPEED_FLAG_IN_GAME_ONLY};
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL speed_set_fn(void*, uint32_t speed_x100, uint32_t flags) {
    if (speed_x100 < 100 || speed_x100 > 800) return SAN14_E_INVALID_ARGUMENT;
    ++g_mock_ui->speed_set_calls;
    g_mock_ui->speed_x100 = speed_x100;
    g_mock_ui->speed_flags = flags;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL speed_get_fn(void*, uint32_t* cfg, uint32_t* eff) {
    if (cfg) *cfg = g_mock_ui->speed_x100;
    if (eff) *eff = g_mock_ui->speed_x100;
    return SAN14_OK;
}

San14ControlHandle find_button_by_text(San14PanelHandle p, const std::string& label) {
    for (const auto& [h, c] : g_mock_ui->controls) {
        if (c.panel == p && c.is_button && c.text == label) return h;
    }
    return 0;
}
San14ControlHandle find_control_by_id(San14PanelHandle p, const std::string& id) {
    for (const auto& [h, c] : g_mock_ui->controls) {
        if (c.panel == p && c.id == id) return h;
    }
    return 0;
}
San14BoolSetting& setting(const std::string& name) { for (auto& s : settings) if (key(s.key) == name) return s; assert(false); return settings[0]; }
San14Result change(const std::string& name, int32_t value) { auto& s = setting(name); return s.changed(s.plugin_context, s.key, value); }
void all_preferences(bool value) { for (const auto& s : settings) values[key(s.key)] = value ? 1 : 0; }
void all_rules_off() { for (const auto& r : rules) assert(r.second == 0); assert(registered_hotkeys.empty() && !registered_office.score && !registered_government && !registered_area.on_sync); }

using Initialize = San14Result (SAN14_MOD_CALL*)(const San14HostApi*, San14ModApi*);
void start(Initialize init, San14HostApi& host, San14ModApi& api, San14ModContext& context) {
    api = {}; api.struct_size = sizeof(api); assert(init(&host, &api) == SAN14_OK);
    context = {}; context.struct_size = sizeof(context);
    assert(api.on_load(&context) == SAN14_OK && settings.size() == 21);
    std::map<std::string, int> categories;
    for (const auto& s : settings) ++categories[key(s.category)];
    assert(categories.size() == 4 && categories[u8"常规"] == 5 && categories[u8"外交与异族"] == 3 && categories[u8"武将与军团"] == 8 && categories[u8"报告与演出"] == 5);
    std::map<std::string, int> seen;
    for (const auto& s : settings) {
        assert(expected_features.count(key(s.key)) && ++seen[key(s.key)] == 1 && s.default_value == 0 && s.changed && s.label.length && s.description.length);
    }
}
void stop(San14ModApi& api, San14ModContext& context) { assert(api.on_disable(&context) == SAN14_OK); all_rules_off(); api.on_unload(&context); }
void check_policies() {
    San14OfficeCandidate officer{sizeof(San14OfficeCandidate), 10, 90, 85, 70, 50, 20000};
    int64_t score = 0; San14OfficeClass classification = SAN14_OFFICE_KEEP_CLASS;
    assert(registered_office.score(registered_office.plugin_context, &officer, 20000, SAN14_OFFICE_MILITARY, &score) == SAN14_OK && score == 185000000);
    assert(registered_office.classify_officer(registered_office.plugin_context, &officer, &classification) == SAN14_OK && classification == SAN14_OFFICE_CLASS_MILITARY);
    const int32_t bonuses[4]{}; int32_t bonus = 0;
    assert(registered_office.classify_post(registered_office.plugin_context, 2, 2, bonuses, &bonus) == SAN14_OK && bonus == 1);
    assert(registered_office.score(nullptr, nullptr, 1, SAN14_OFFICE_CIVIL, &score) == SAN14_E_INVALID_ARGUMENT);

}
void test_companion(Initialize init) {
    San14ModApi api{}; api.struct_size = sizeof(api); San14ModContext context{};
    auto host = fake_host(); auto bad = host;
    bad.struct_size = static_cast<uint32_t>(offsetof(San14HostApi, settings) + sizeof(San14SettingsApi));
    assert(init(&bad, &api) == SAN14_E_UNSUPPORTED);
    bad = host; bad.rules.set_bool = nullptr; assert(init(&bad, &api) == SAN14_E_UNSUPPORTED);
    bad = host; bad.rules.struct_size = sizeof(uint32_t); assert(init(&bad, &api) == SAN14_E_UNSUPPORTED);
    bad = host; bad.abi_version = 0; assert(init(&bad, &api) == SAN14_E_UNSUPPORTED);
    reset_companion_mocks(); start(init, host, api, context);
    // Leave the retired preference intact, but never register or apply it.
    values["raise_officer_merit_cap"] = 1;
    values["weighted_government_auto_assign"] = 1;
    assert(api.on_enable(&context) == SAN14_OK && rules.empty() && office_registers == 0 && government_registers == 0 && registered_hotkeys.empty());
    assert(values.at("raise_officer_merit_cap") == 1 && !rule_calls.count("officers.merit.extended"));
    assert(change("global_death_notices", 2) == SAN14_E_INVALID_ARGUMENT);
    const auto& first = settings.front(); assert(first.changed(nullptr, {"unknown", 7}, 1) == SAN14_E_NOT_FOUND);
    for (const auto& entry : expected_features) {
        assert(change(entry.first, 1) == SAN14_OK && rules.at(entry.second) == 1);
        const auto count = rule_calls.at(entry.second);
        assert(change(entry.first, 1) == SAN14_OK && rule_calls.at(entry.second) == count);
        assert(change(entry.first, 0) == SAN14_OK && rules.at(entry.second) == 0);
        assert(change(entry.first, 0) == SAN14_OK && rule_calls.at(entry.second) == count + 1);
    }
    assert(office_registers == 1 && government_registers == 0 && office_unregisters == 0 && government_unregisters == 0);
    assert(!rule_calls.count("government.ai.optimized") && !registered_government);
    check_policies(); const auto previous_calls = rule_calls;
    assert(api.on_disable(&context) == SAN14_OK); all_rules_off(); assert(rule_calls == previous_calls);
    assert(api.on_disable(&context) == SAN14_OK && rule_calls == previous_calls); assert(change("global_death_notices", 1) == SAN14_E_BUSY);
    all_preferences(true);
    assert(api.on_enable(&context) == SAN14_OK && rules.size() == 21 && registered_hotkeys.size() == 1 && scenario_choice);
    assert(!rule_calls.count("officers.merit.extended"));
    assert(registered_area.on_sync);
    for (const auto& r : rules) assert(r.second == 1);
    const auto calls = rule_calls;
    const auto office_count = office_registers, government_count = government_registers, hotkey_count = hotkey_registers;
    assert(api.on_enable(&context) == SAN14_OK && calls == rule_calls && office_count == office_registers && government_count == government_registers && hotkey_count == hotkey_registers);
    const auto retained_hotkey = registered_hotkeys.begin()->second;
    retained_hotkey.callback(retained_hotkey.context); assert(report_requests == 1);
    fail_rule = "notifications.death.global"; fail_rule_value = 0;
    assert(change("global_death_notices", 0) == SAN14_E_BUSY && rules.at(fail_rule) == 1);
    assert(api.on_disable(&context) == SAN14_E_BUSY && rules.at(fail_rule) == 1);
    retained_hotkey.callback(retained_hotkey.context); assert(report_requests == 1);
    fail_rule.clear(); stop(api, context);

    reset_companion_mocks(); host = fake_host(); start(init, host, api, context); all_preferences(true);
    fail_rule = "officers.subordinate.groups"; fail_rule_value = 1;
    assert(api.on_enable(&context) == SAN14_E_BUSY && government_registers == 0 && office_registers == 0);
    assert(api.on_disable(&context) == SAN14_OK); all_rules_off();
    assert(!rule_calls.count("officers.office.weighted") && !rule_calls.count("notifications.death.global"));
    api.on_unload(&context);
    reset_companion_mocks(); host = fake_host(); start(init, host, api, context); all_preferences(true);
    fail_get = "allow_away_post_assignment";
    assert(api.on_enable(&context) == SAN14_E_BUSY); stop(api, context);

    reset_companion_mocks(); host = fake_host(); host.office = nullptr; host.government = nullptr; host.presentation = nullptr;
    start(init, host, api, context);
    for (const auto* name : {"global_death_notices", "hide_war_book_discovery_message", "raise_officer_merit_cap", "block_ai_district_split", "disable_elder_portraits"}) values[name] = 1;
    assert(api.on_enable(&context) == SAN14_OK && rules.size() == 4 && office_registers == 0 && government_registers == 0 && hotkey_registers == 0);
    assert(values.at("raise_officer_merit_cap") == 1 && !rule_calls.count("officers.merit.extended"));
    assert(change("weighted_office_auto_assign", 1) == SAN14_E_UNSUPPORTED && change("skip_turn_reports", 1) == SAN14_E_UNSUPPORTED);
    stop(api, context);
    reset_companion_mocks(); host = fake_host();
    San14OfficeApi short_office = office_service; short_office.struct_size = sizeof(uint32_t); host.office = &short_office;
    San14GovernmentApi short_government = government_service; short_government.struct_size = sizeof(uint32_t); host.government = &short_government;
    San14PresentationApi short_report = presentation_service; short_report.struct_size = sizeof(uint32_t); host.presentation = &short_report;
    start(init, host, api, context); assert(api.on_enable(&context) == SAN14_OK);
    assert(change("weighted_office_auto_assign", 1) == SAN14_E_UNSUPPORTED && change("skip_turn_reports", 1) == SAN14_E_UNSUPPORTED);
    stop(api, context);

    reset_companion_mocks(); host = fake_host(); start(init, host, api, context); assert(api.on_enable(&context) == SAN14_OK);
    fail_office = fail_government = true;
    assert(change("weighted_office_auto_assign", 1) == SAN14_E_BUSY && rules.empty());
    stop(api, context); assert(office_unregisters == 0 && government_unregisters == 0);

    reset_companion_mocks(); host = fake_host(); start(init, host, api, context); assert(api.on_enable(&context) == SAN14_OK);
    fail_hotkey = true; assert(change("skip_turn_reports", 1) == SAN14_E_BUSY && rules.empty() && registered_hotkeys.empty()); fail_hotkey = false;
    fail_rule = "presentation.turn_reports.skip"; fail_rule_value = 1;
    assert(change("skip_turn_reports", 1) == SAN14_E_BUSY && registered_hotkeys.empty() && rules.empty()); fail_rule.clear();
    assert(change("skip_turn_reports", 1) == SAN14_OK && registered_hotkeys.size() == 1);
    auto report_key = registered_hotkeys.begin()->second; report_key.callback(report_key.context); assert(report_requests == 1);
    fail_release = true; assert(change("skip_turn_reports", 0) == SAN14_E_BUSY && rules.at("presentation.turn_reports.skip") == 1 && registered_hotkeys.size() == 1); fail_release = false;
    assert(change("skip_turn_reports", 0) == SAN14_OK && registered_hotkeys.empty()); report_key.callback(report_key.context); assert(report_requests == 1);
    fail_release = true; fail_rule = "presentation.turn_reports.skip";
    assert(change("skip_turn_reports", 1) == SAN14_E_BUSY && registered_hotkeys.size() == 1);
    report_key = registered_hotkeys.begin()->second; report_key.callback(report_key.context); assert(report_requests == 1);
    fail_release = false; fail_rule.clear(); stop(api, context);

    reset_companion_mocks(); host = fake_host(); start(init, host, api, context); assert(api.on_enable(&context) == SAN14_OK);
    assert(change("weighted_office_auto_assign", 1) == SAN14_OK);
    fail_rule = "officers.office.weighted"; fail_rule_value = 0;
    assert(api.on_disable(&context) == SAN14_E_BUSY && registered_office.score && office_unregisters == 0 && !registered_government);
    fail_rule.clear(); fail_office_cleanup = true; assert(api.on_disable(&context) == SAN14_E_BUSY && registered_office.score);
    fail_office_cleanup = false; stop(api, context);
    // Lazy area service negotiation for persistent_area_officers
    reset_companion_mocks(); host = fake_host();
    host.struct_size = static_cast<uint32_t>(offsetof(San14HostApi, areas));
    host.areas = nullptr;
    start(init, host, api, context); assert(api.on_enable(&context) == SAN14_OK);
    assert(change("persistent_area_officers", 1) == SAN14_E_UNSUPPORTED);
    assert(change("global_death_notices", 1) == SAN14_OK && rules.at("notifications.death.global") == 1);
    stop(api, context);
}

void test_game_speed_setting(Initialize init) {
    MockUiState ui_state;
    g_mock_ui = &ui_state;
    reset_companion_mocks();

    San14PagesApi pages_api{sizeof(San14PagesApi), pages_register};
    San14UiStyleApi style_api{sizeof(San14UiStyleApi), SAN14_UI_STYLE_EXPERT | SAN14_UI_STYLE_TINT, style_selected, style_help, style_tint};
    San14SettingsExtendedApi settings_ext{sizeof(San14SettingsExtendedApi), reg_range_fn, get_range_fn};
    San14UiSliderApi slider_api{sizeof(San14UiSliderApi), ui_add_slider_fn, ui_set_slider_fn, ui_get_slider_fn};
    San14GameSpeedApi speed_api{sizeof(San14GameSpeedApi), speed_caps_fn, speed_set_fn, speed_get_fn};

    auto host = fake_host();
    host.storage = {sizeof(San14StorageApi), storage_read_fn, storage_write_fn};
    host.ui = {sizeof(San14UiApi), ui_create, ui_add_label, ui_add_button, ui_set_text, ui_show, ui_close};
    host.ui_style = &style_api;
    host.pages = &pages_api;
    host.settings_ext = &settings_ext;
    host.ui_slider = &slider_api;
    host.game_speed = &speed_api;

    San14ModApi api{}; api.struct_size = sizeof(api); San14ModContext context{};
    start(init, host, api, context);
    assert(ui_state.range_settings.size() == 1);
    const auto& gs = ui_state.range_settings[0];
    assert(key(gs.key) == "game_speed_x100" && key(gs.category) == u8"常规" &&
           gs.min_value == 100 && gs.max_value == 800 && gs.step == 25 &&
           gs.default_value == 100 && gs.display_divisor == 100 && key(gs.unit_suffix) == "x");
    assert(api.on_enable(&context) == SAN14_OK);
    assert(ui_state.speed_x100 == 100 && ui_state.speed_set_calls == 0);

    // Open ExpertPage and verify first row of "常规" contains slider + "1.00x" button + "标准" button
    assert(ui_state.registered_page.open(ui_state.registered_page.plugin_context) == SAN14_OK);
    const auto page = ui_state.stack.back();
    San14ControlHandle slider = 0, readout_btn = 0, reset_btn = 0;
    for (const auto& [h, c] : ui_state.controls) {
        if (c.panel == page && c.is_slider) slider = h;
        if (c.panel == page && c.is_button && c.text == "1.00x") readout_btn = h;
        if (c.panel == page && c.is_button && c.text == u8"标准") reset_btn = h;
    }
    assert(slider != 0 && readout_btn != 0 && reset_btn != 0);

    // Trigger slider change to 350 (3.50x) -> updates readout button, applies speed, persists to global storage
    const auto& sc = ui_state.controls.at(slider);
    assert(sc.slider_min == 100 && sc.slider_max == 800 && sc.slider_step == 25);
    sc.int_cb(sc.ctx, slider, 350);
    assert(ui_state.speed_x100 == 350 && ui_state.speed_flags == SAN14_SPEED_FLAG_IN_GAME_ONLY);
    assert(ui_state.controls.at(readout_btn).text == "3.50x");
    assert(ui_state.global_storage.at("companion.pref.game_speed_x100").size() == sizeof(int32_t));

    // Click "标准" button -> resets speed to 100 (1.00x)
    ui_state.controls.at(reset_btn).cb(ui_state.controls.at(reset_btn).ctx, reset_btn);
    assert(ui_state.speed_x100 == 100 && ui_state.controls.at(readout_btn).text == "1.00x");

    // Set 375 (3.75x) via slider callback -> persists across reload
    sc.int_cb(sc.ctx, slider, 375);
    assert(ui_state.speed_x100 == 375 && ui_state.controls.at(readout_btn).text == "3.75x");

    // Disabling mod releases speed back to 1.00x while keeping persisted 375
    assert(api.on_disable(&context) == SAN14_OK);
    assert(ui_state.speed_x100 == 100);
    api.on_unload(&context);
    reset_companion_mocks();
    ui_state.panels.clear(); ui_state.stack.clear(); ui_state.controls.clear(); ui_state.range_settings.clear();

    // Reloading restores 375 and re-engages 3.75x (in-game gated) on enable
    start(init, host, api, context);
    assert(api.on_enable(&context) == SAN14_OK);
    assert(ui_state.speed_x100 == 375 && ui_state.speed_flags == SAN14_SPEED_FLAG_IN_GAME_ONLY);

    stop(api, context);
    assert(ui_state.speed_x100 == 100 && ui_state.panels.empty());
    g_mock_ui = nullptr;
}

void test_force_control_menu(Initialize init){
    MockUiState ui;g_mock_ui=&ui;reset_companion_mocks();auto host=fake_host();
    San14WorldApi original_world=world_service;original_world.struct_size=offsetof(San14WorldApi,officer_temperament);host.world=&original_world;
    host.ui={sizeof(San14UiApi),ui_create,ui_add_label,ui_add_button,ui_set_text,ui_show,ui_close};
    San14ModApi api{};api.struct_size=sizeof(api);San14ModContext context{};start(init,host,api,context);
    assert(api.on_enable(&context)==SAN14_OK&&spectator_enables==1&&force_menu_event);
    auto open=[&](){force_menu_requested=true;San14EventView event{};event.struct_size=sizeof(event);event.event_id=SAN14_EVENT_UI_IDLE;
        force_menu_event(force_menu_context,&event);assert(ui.stack.size()==1&&ui.panels.at(ui.stack.back())==u8"操作势力");};
    auto button=[&](unsigned id){for(const auto& pair:ui.controls)if(pair.second.id=="spectator.force."+std::to_string(id))return pair.second;assert(false);return MockControl{};};
    auto click=[&](unsigned id){const auto c=button(id);assert(c.cb);c.cb(c.ctx,0);};
    open();assert(button(1).selected==1&&button(1).cb&&button(2).selected==0);click(1);
    assert(controlled_force==0&&force_selections==1&&ui.panels.empty());
    open();assert(button(1).selected==0&&button(2).selected==0);click(2);
    assert(controlled_force==2&&force_selections==2&&ui.panels.empty());
    open();assert(button(2).selected==1&&button(3).selected==0);click(3);
    assert(controlled_force==3&&force_selections==3&&ui.panels.empty());
    open();++force_generation;click(1);assert(controlled_force==3&&force_selections==3&&ui.panels.empty());
    open();force_selection_result=SAN14_E_BUSY;click(3);assert(controlled_force==3&&force_selections==3&&ui.stack.size()==1);
    auto find=[&](const std::string& id){for(const auto& p:ui.controls)if(p.second.id==id)return p.second;assert(false);return MockControl{};};
    auto invoke=[&](const std::string& id){const auto c=find(id);assert(c.cb);c.cb(c.ctx,0);};
    auto visible_forces=[&](){size_t count=0;for(const auto& p:ui.controls)if(p.second.id.rfind("spectator.force.",0)==0)++count;return count;};
    for(auto count:{1u,9u,16u,17u,45u}){
        force_count=count;controlled_force=0;open();assert(visible_forces()==std::min(count,16u));
        const auto& panel=ui.panel_geometry.at(ui.stack.back());assert(panel.width==1920&&panel.height==1080&&panel.flags==SAN14_PANEL_EXPERT);
        assert(find("spectator.current").text==u8"当前：看海");
    }
    const auto unchanged=force_selections;
    const auto old=ui.stack.back();ui.fail_show=true;invoke("spectator.next");
    assert(ui.stack.size()==1&&ui.stack.back()==old&&visible_forces()==16);
    invoke("spectator.next");assert(visible_forces()==16&&button(17).cb&&force_selections==unchanged);
    invoke("spectator.next");assert(visible_forces()==13&&button(45).cb&&find("spectator.pagination").text==u8"45 个势力    3 / 3");
    invoke("spectator.previous");assert(visible_forces()==16&&button(17).cb);
    invoke("spectator.return");assert(ui.panels.empty()&&controlled_force==0&&force_selections==unchanged);
    controlled_force=45;open();assert(button(45).selected==1&&find("spectator.state.45").text==u8"操作中");
    force_selection_result=SAN14_E_BUSY;click(45);assert(find("spectator.status").text==u8"暂时无法切换，请返回地图后重试。");
    force_selection_result=SAN14_OK;click(45);assert(controlled_force==0&&ui.panels.empty());
    stop(api,context);assert(ui.panels.empty());
    San14SpectatorApi legacy=spectator_service;legacy.struct_size=offsetof(San14SpectatorApi,select_force);
    reset_companion_mocks();host.spectator=&legacy;start(init,host,api,context);assert(api.on_enable(&context)==SAN14_E_UNSUPPORTED&&spectator_enables==0);stop(api,context);
    g_mock_ui=nullptr;
}

bool delay_force_page=false;
void* create_force_test_panel(void* p,int x,int y,int w,int h){
    auto native=plugin_ui_test::Fixture::create(p,x,y,w,h);
    plugin_ui_test::Fixture::self(p).panels.at(native)->ready=!delay_force_page;return native;
}
void test_force_control_host_ui(Initialize init){
    reset_companion_mocks();force_count=45;controlled_force=35;
    plugin_ui_test::Fixture native;int owner=0;native.allowed={&owner};
    auto backend=native.styled_backend();backend.prepare_panel=plugin_ui_test::Fixture::prepare;backend.create_panel=create_force_test_panel;
    plugin_ui::Runtime ui(backend);auto host=fake_host();host.host_context=&owner;host.ui=ui.api();
    San14ModApi api{};api.struct_size=sizeof(api);San14ModContext context{};start(init,host,api,context);assert(api.on_enable(&context)==SAN14_OK);
    auto open=[&](){force_menu_requested=true;San14EventView event{};event.struct_size=sizeof(event);event.event_id=SAN14_EVENT_UI_IDLE;
        force_menu_event(force_menu_context,&event);assert(ui.has_visible()&&native.only_panel().themed);};
    auto click=[&](int x,int y){assert(ui.mouse_down(x,y));assert(ui.mouse_up(x,y));};
    open();assert(native.only_panel().width==1920&&native.only_panel().height==1080);
    int selected=0;for(const auto& t:native.only_panel().texts)if(t->flags&SAN14_CONTROL_TAB){assert(t->h==40);if(t->chosen)++selected;}
    assert(selected==1);
    // Current force 35 is on page 3, in the second row, left column.
    click(500,267);assert(controlled_force==0&&!ui.has_visible()&&ui.panel_count()==0);
    open();const auto old=native.panels.begin()->first;delay_force_page=true;click(1490,726);
    assert(ui.presentation_pending()&&native.panels.size()==2&&native.panels.at(old)->visible&&force_selections==1);
    for(auto& p:native.panels)p.second->ready=true;
    delay_force_page=false;assert(ui.pump()==SAN14_OK&&!ui.presentation_pending()&&native.panels.size()==1);
    // Page 2 starts with force 17. The real Host registry routes the click.
    click(500,201);assert(controlled_force==17&&force_selections==2&&!ui.has_visible());
    open();assert(ui.key_down(VK_ESCAPE)&&!ui.has_visible()&&controlled_force==17&&force_selections==2);
    // Esc destroys the Host handle; reopening must tolerate that stale handle.
    open();click(960,1000);assert(!ui.has_visible()&&controlled_force==17&&force_selections==2);
    stop(api,context);assert(native.panels.empty()&&ui.panel_count()==0&&ui.control_count()==0);
}
}  // namespace

int main() {
    wchar_t exe_path[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path beside_exe = (len > 0 && len < MAX_PATH)
        ? (std::filesystem::path(exe_path).parent_path() / L"companion.dll")
        : std::filesystem::path(L"companion.dll");
    auto dll = LoadLibraryExW(beside_exe.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) dll = LoadLibraryExW(std::filesystem::absolute(L"companion.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(dll);
    auto init = reinterpret_cast<Initialize>(GetProcAddress(dll, "SAN14Mod_Initialize"));
    assert(init);
    test_companion(init);
    test_game_speed_setting(init);
    test_force_control_menu(init);
    test_force_control_host_ui(init);
    std::cout << "Companion plugin: ABI, 21 features, speed slider, scenario choice, copied policies, lazy areas, F8 rollback, lifecycle passed\n";
    FreeLibrary(dll);
    return 0;
}
