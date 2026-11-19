#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "san14_mod.h"
#include "san14_idle.h"
#include "san14_settings_template.h"
#include "plugin_ui_test_fixture.h"
#include "../src/settings_model.h"
#include "../src/officer_traits_dialog.h"
#include "../src/officer_stats_dialog.h"
#include "san14_officer_traits.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <cstring>
namespace {
std::string key(San14StringView s){return {s.data?s.data:"",s.length};}
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
    bool enabled = true;
};
struct MockUiState {
    San14Handle next = 1;
    std::map<San14PanelHandle, std::string> panels;
    std::vector<San14PanelHandle> stack;
    std::map<San14ControlHandle, MockControl> controls;
    San14SettingsPageSpec registered_page{};
    std::map<uint32_t, uint8_t> force_colors{{1u, uint8_t{0}}, {48u, uint8_t{55}}};
    uint64_t world_gen = 1;
    int set_color_calls = 0;
    std::vector<San14RangeSetting> range_settings;
    std::map<std::string, int32_t> range_values;
    std::map<std::string, std::vector<uint8_t>> global_storage;
    uint32_t speed_x100 = 100;
    uint32_t speed_flags = 0;
    int speed_set_calls = 0;
};
MockUiState* g_mock_ui = nullptr;

San14Result SAN14_MOD_CALL ui_create(void*, const San14PanelSpec* spec, San14PanelHandle* out) {
    auto h = g_mock_ui->next++;
    g_mock_ui->panels[h] = key(spec->title);
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
    g_mock_ui->stack.push_back(p);
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL ui_close(void*, San14PanelHandle p) {
    g_mock_ui->panels.erase(p);
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
San14Result SAN14_MOD_CALL style_enabled(void*, San14ControlHandle c, int32_t enabled) {
    g_mock_ui->controls.at(c).enabled = enabled != 0;return SAN14_OK;
}
San14Result SAN14_MOD_CALL style_tint(void*, San14ControlHandle c, uint32_t argb) {
    auto it = g_mock_ui->controls.find(c);
    if (it == g_mock_ui->controls.end()) return SAN14_E_NOT_FOUND;
    if(it->second.is_slider)return SAN14_E_INVALID_ARGUMENT;
    it->second.tint = argb;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL pages_register(void*, const San14SettingsPageSpec* spec) {
    g_mock_ui->registered_page = *spec;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL fc_palette(void*, San14ForceColorSwatch* out, uint32_t cap, uint32_t* req) {
    *req = SAN14_FORCE_COLOR_PALETTE_SIZE;
    if (cap < SAN14_FORCE_COLOR_PALETTE_SIZE || !out) return SAN14_E_BUFFER_TOO_SMALL;
    for (uint32_t i = 0; i < SAN14_FORCE_COLOR_PALETTE_SIZE; ++i) {
        out[i] = {sizeof(San14ForceColorSwatch), static_cast<uint8_t>(i), static_cast<uint8_t>(40 + i), static_cast<uint8_t>(80 + i), static_cast<uint8_t>(160 + i), 0xff, {}};
    }
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL fc_forces(void*, San14ForceColorEntry* out, uint32_t cap, uint32_t* req) {
    *req = static_cast<uint32_t>(g_mock_ui->force_colors.size());
    if (cap < *req) return SAN14_E_BUFFER_TOO_SMALL;
    if (*req && !out) return SAN14_E_INVALID_ARGUMENT;
    uint32_t idx = 0;
    for (const auto& [fid, cid] : g_mock_ui->force_colors) {
        out[idx++] = {sizeof(San14ForceColorEntry), fid, fid * 10u, cid, static_cast<uint8_t>(fid >= 46 && fid <= 50 ? 1 : 0), {}};
    }
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL fc_set(void*, uint32_t fid, uint8_t cid, uint8_t* prev) {
    ++g_mock_ui->set_color_calls;
    for (const auto& [other_id, other_cid] : g_mock_ui->force_colors) {
        if (other_id != fid && other_cid == cid) return SAN14_E_BUSY;
    }
    auto it = g_mock_ui->force_colors.find(fid);
    if (it == g_mock_ui->force_colors.end()) return SAN14_E_NOT_FOUND;
    if (prev) *prev = it->second;
    it->second = cid;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL world_gen_fn(void*, uint64_t* out) {
    *out = g_mock_ui->world_gen;
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
using enhanced_settings_mod::Preferences;
using enhanced_settings_mod::LegacyPreferences;
using enhanced_settings_mod::PreferencesV2;
std::map<std::string,std::vector<uint8_t>> local_storage;
San14SaveStorageInfo storage_info{sizeof(San14SaveStorageInfo),SAN14_STORAGE_READY,1,"pt-a"};
std::map<std::string,int32_t> custom_values;
std::vector<San14ScenarioCustomSpec> custom_specs;
San14BoolSetting edit_setting{},merit_setting{},traits_setting{};
std::map<San14Subscription,std::pair<uint32_t,San14EventCallback>> subs;
std::map<San14ControlHandle,std::pair<San14TextChangedCallback,void*>> inputs;
int edit_rule=0,merit_rule=0,batch_writes=0,single_writes=0;
bool fail_write=false,fail_single=false,fail_configure=false,fail_merit_enable=false,fail_merit_disable=false,query_hire=true;
San14Result SAN14_MOD_CALL reg_setting(void*,const San14BoolSetting* s){
    if(key(s->key)=="enhanced_edit_mode")edit_setting=*s;
    else if(key(s->key)=="raise_officer_merit_cap")merit_setting=*s;
    else{assert(key(s->key)=="expanded_officer_traits");traits_setting=*s;}
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL set_rule(void*,San14StringView k,int32_t v){
    if(key(k)=="edit.enhanced"){edit_rule=v;return SAN14_OK;}
    assert(key(k)=="officers.merit.extended");
    if((v&&fail_merit_enable)||(!v&&fail_merit_disable))return SAN14_E_BUSY;
    merit_rule=v;return SAN14_OK;
}
San14Result SAN14_MOD_CALL query_custom(void*,uint32_t kind,uint32_t group,uint32_t field,San14ScenarioCustomInfo* out){
    assert(field==0&&out&&out->struct_size==sizeof(*out));
    if(kind==SAN14_SCENARIO_CUSTOM_MILITARY_INTEL_CAP&&group>=1&&group<=4){
        const int32_t defaults[]={50000,100000,150000,200000};*out={sizeof(*out),0,1000000,defaults[group-1]};return SAN14_OK;
    }
    if(query_hire&&kind==SAN14_SCENARIO_CUSTOM_OFFICER_HIRE_CAP&&group>=1&&group<=7){
        const int32_t defaults[]={40,30,24,20,12,10,5};*out={sizeof(*out),0,100000,defaults[group-1]};return SAN14_OK;
    }
    return SAN14_E_UNSUPPORTED;
}
San14Result SAN14_MOD_CALL context(void*,San14SaveStorageInfo* out){*out=storage_info;return SAN14_OK;}
San14Result SAN14_MOD_CALL read(void*,San14StringView scope,San14StringView k,void* out,uint32_t cap,uint32_t* req){
    auto& storage=key(scope)=="global"?g_mock_ui->global_storage:local_storage;
    const auto it=storage.find(key(k));if(it==storage.end()){*req=0;return SAN14_E_NOT_FOUND;}
    *req=static_cast<uint32_t>(it->second.size());if(cap<*req)return SAN14_E_BUFFER_TOO_SMALL;
    memcpy(out,it->second.data(),*req);return SAN14_OK;
}
San14Result SAN14_MOD_CALL write(void*,San14StringView scope,San14StringView k,const void* data,uint32_t size){
    if(fail_write)return SAN14_E_STALE_GENERATION;
    auto& storage=key(scope)=="global"?g_mock_ui->global_storage:local_storage;
    const auto bytes=static_cast<const uint8_t*>(data);storage[key(k)]={bytes,bytes+size};return SAN14_OK;
}
bool fail_saved_edit=false;
std::map<std::string,std::vector<uint8_t>> peer_storage;
std::map<std::string,std::vector<uint8_t>>* saved_edit_data=nullptr;
San14Result SAN14_MOD_CALL edit_read(void*,San14StringView,San14StringView k,void* out,uint32_t cap,uint32_t* req){
    auto i=saved_edit_data->find(key(k));if(i==saved_edit_data->end())return SAN14_E_NOT_FOUND;
    *req=static_cast<uint32_t>(i->second.size());if(cap<*req)return SAN14_E_BUFFER_TOO_SMALL;memcpy(out,i->second.data(),*req);return SAN14_OK;
}
San14Result SAN14_MOD_CALL edit_write(void*,San14StringView,San14StringView k,const void* data,uint32_t size){
    const auto* p=static_cast<const uint8_t*>(data);(*saved_edit_data)[key(k)]={p,p+size};return SAN14_OK;
}
San14Result SAN14_MOD_CALL edit_saved(void*,uint32_t target,uint64_t rev,San14SavedDataTransform cb,void* user,San14SavedEditResult* out){
    *out={sizeof(*out),1,0,0};if(fail_saved_edit)return SAN14_E_PLUGIN_FAILED;
    if(rev!=storage_info.revision)return SAN14_E_STALE_GENERATION;
    if(target==SAN14_SAVED_EDIT_CURRENT&&!storage_info.save_basename[0])return SAN14_E_NOT_FOUND;
    auto next=local_storage;saved_edit_data=&next;San14SavedDataAccess access{sizeof(access),nullptr,edit_read,edit_write};
    auto r=cb(user,&access);auto peer=peer_storage;
    if(r==SAN14_OK&&target==SAN14_SAVED_EDIT_ALL){saved_edit_data=&peer;r=cb(user,&access);}
    saved_edit_data=nullptr;if(r==SAN14_OK){local_storage=std::move(next);if(target==SAN14_SAVED_EDIT_ALL)peer_storage=std::move(peer);out->updated_files=1;}return r;
}
San14Result SAN14_MOD_CALL configure(void*,const San14ScenarioCustomSpec* specs,uint32_t count){
    if(fail_configure)return SAN14_E_BUSY;
    ++batch_writes;custom_specs.clear();custom_values.clear();
    for(uint32_t i=0;i<count;++i){custom_specs.push_back(specs[i]);custom_values[key(specs[i].key)]=specs[i].default_value;}
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL read_custom(void*,San14StringView k,int32_t* out){*out=custom_values.at(key(k));return SAN14_OK;}
San14Result SAN14_MOD_CALL write_custom(void*,San14StringView k,int32_t value){if(fail_single)return SAN14_E_BUSY;++single_writes;custom_values.at(key(k))=value;return SAN14_OK;}
San14Result SAN14_MOD_CALL subscribe(void*,uint32_t id,int32_t,San14EventCallback cb,void*,San14Subscription* out){*out=subs.size()+1;subs[*out]={id,cb};return SAN14_OK;}
San14Result SAN14_MOD_CALL unsubscribe(void*,San14Subscription sub){subs.erase(sub);return SAN14_OK;}
San14Result SAN14_MOD_CALL text_input(void* owner,San14PanelHandle panel,const San14ControlSpec* spec,uint32_t,San14TextChangedCallback cb,void* ctx,San14ControlHandle* out){
    const auto r=ui_add_label(owner,panel,spec,out);inputs[*out]={cb,ctx};return r;
}
void fire(uint32_t id){auto copy=subs;San14EventView e{};e.struct_size=sizeof(e);e.event_id=id;for(auto& sub:copy)if(sub.second.first==id)sub.second.second(nullptr,&e);}
San14PanelHandle top(){assert(!g_mock_ui->stack.empty());return g_mock_ui->stack.back();}
void click_text(const std::string& text){
    auto handle=find_button_by_text(top(),text);
    if(!handle)for(const auto& item:g_mock_ui->controls)if(item.second.panel==top()&&item.second.text==text&&item.second.id.rfind("range.value.",0)==0){
        handle=find_control_by_id(top(),("range.edit."+item.second.id.substr(12)).c_str());break;
    }
    assert(handle);const auto c=g_mock_ui->controls.at(handle);assert(c.cb);c.cb(c.ctx,handle);
}
uint32_t range_tint(const char* key){const auto h=find_control_by_id(top(),(std::string("range.edit.")+key).c_str());assert(h);return g_mock_ui->controls.at(h).tint;}

void click_bool_choice(size_t row,const std::string& text){
    for(const auto& item:g_mock_ui->controls)if(item.second.panel==top()&&item.second.is_button&&item.second.text==text){
        if(row--==0){const auto c=item.second;c.cb(c.ctx,item.first);return;}
    }
    assert(false);
}
void slide(size_t index,int32_t value){
    for(auto item:g_mock_ui->controls)if(item.second.panel==top()&&item.second.is_slider){if(index--==0){value=std::clamp(value,item.second.slider_min,item.second.slider_max);g_mock_ui->controls.at(item.first).slider_value=value;item.second.int_cb(item.second.ctx,item.first,value);return;}}
    assert(false);
}
int32_t slider_value(size_t index){for(auto& item:g_mock_ui->controls)if(item.second.panel==top()&&item.second.is_slider){if(index--==0)return item.second.slider_value;}assert(false);return 0;}
void input_value(const std::string& value){const auto h=find_control_by_id(top(),"value");assert(h);const auto cb=inputs.at(h);cb.first(cb.second,h,{value.data(),static_cast<uint32_t>(value.size())});click_text(u8"确定");}
void click_id(const std::string& id){const auto h=find_control_by_id(top(),id);assert(h);const auto c=g_mock_ui->controls.at(h);assert(c.cb);c.cb(c.ctx,h);}
void search_traits(const std::string& text){const auto h=find_control_by_id(top(),"traits.search");assert(h);const auto cb=inputs.at(h);cb.first(cb.second,h,{text.data(),static_cast<uint32_t>(text.size())});click_id("traits.search.go");}
template<class T>std::vector<uint8_t> bytes(const T& p){auto b=reinterpret_cast<const uint8_t*>(&p);return {b,b+sizeof(p)};}
LegacyPreferences legacy_defaults(){LegacyPreferences p;const auto& defs=enhanced_settings_mod::custom_defs();for(size_t i=0;i<p.values.size();++i)p.values[i]=defs[i].def_val;return p;}
using SavedRecords = std::map<std::string,std::vector<uint8_t>>;
using enhanced_settings_mod::PreferencesV3;
void clear_preferences(SavedRecords& storage){
    storage.erase("enhanced.preferences.v4");storage.erase("enhanced.preferences.v3");
}
void seed_v3(SavedRecords& storage,const PreferencesV3& value){
    clear_preferences(storage);storage["enhanced.preferences.v3"]=bytes(value);
}
Preferences saved(const SavedRecords& storage){
    Preferences result;
    if(const auto found=storage.find("enhanced.preferences.v4");found!=storage.end()){
        assert(found->second.size()==sizeof(result));memcpy(&result,found->second.data(),sizeof(result));
    }else{
        auto& legacy=static_cast<PreferencesV3&>(result);
        const auto& value=storage.at("enhanced.preferences.v3");
        assert(value.size()==sizeof(legacy));memcpy(&legacy,value.data(),sizeof(legacy));result.infer_selection();
    }
    assert(result.valid());return result;
}
namespace traits_mock {
bool expanded=false,menu=true,query_busy=false;
uint64_t generation=501;
uint32_t deployed=0;
int sets=0,replaces=0;
std::map<uint32_t,std::array<uint16_t,9>> values;
San14Result SAN14_MOD_CALL query(void*,San14OfficerTraitsStatus* out){if(query_busy)return SAN14_E_BUSY;*out={sizeof(*out),expanded?9u:5u,9,static_cast<uint32_t>(menu?SAN14_TRAITS_CAN_CONFIGURE:SAN14_TRAITS_CAN_EDIT),generation};return SAN14_OK;}
San14Result SAN14_MOD_CALL set(void*,int32_t enabled){if(!menu)return SAN14_E_BUSY;expanded=enabled!=0;++sets;return SAN14_OK;}
San14Result SAN14_MOD_CALL read(void*,uint32_t id,San14OfficerTraits* out){
    if(menu||!expanded)return SAN14_E_BUSY;if(!values.count(id))return SAN14_E_NOT_FOUND;
    *out={sizeof(*out),id,generation,9,31,id==deployed?SAN14_TRAITS_DEPLOYED:0u,{},{}};
    std::copy(values[id].begin(),values[id].end(),out->ids);return SAN14_OK;
}
San14Result SAN14_MOD_CALL options(void*,void* output,uint32_t stride,uint32_t cap,uint32_t* required){
    *required=16;if(cap<16)return SAN14_E_BUFFER_TOO_SMALL;
    for(uint32_t i=0;i<16;++i){San14OfficerTraitOption o{sizeof(o),i+1,{}};const auto name=std::string(u8"个性名")+std::to_string(i+1);strcpy_s(o.name_utf8,name.c_str());memcpy(static_cast<uint8_t*>(output)+i*stride,&o,sizeof(o));}
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL replace(void*,const San14OfficerExtraTraitsEdit* e){
    if(e->generation!=generation)return SAN14_E_STALE_GENERATION;
    if(menu||!expanded||e->officer_id==deployed)return SAN14_E_BUSY;
    auto next=values.at(e->officer_id);std::copy(std::begin(e->ids),std::end(e->ids),next.begin()+5);
    std::set<uint16_t> seen;for(const auto id:next)if(id&&(!seen.insert(id).second||id>16))return SAN14_E_INVALID_ARGUMENT;
    values[e->officer_id]=next;++replaces;return SAN14_OK;
}
San14Result SAN14_MOD_CALL roster(void*,San14RosterInfo* info,void* output,uint32_t stride,uint32_t cap,uint32_t* required){
    *required=12;info->officer_count=12;if(cap<12)return SAN14_E_BUFFER_TOO_SMALL;
    for(uint32_t id=1;id<=12;++id){San14RosterOfficer o{};o.struct_size=sizeof(o);o.officer_id=id;o.available_fields=SAN14_ROSTER_NAMES;
        const auto name=std::string(u8"武将")+std::to_string(id);strcpy_s(o.name_utf8,name.c_str());memcpy(static_cast<uint8_t*>(output)+(id-1)*stride,&o,sizeof(o));}
    return SAN14_OK;
}
}
namespace stats_mock {
std::map<uint32_t, San14OfficerEditState> values;
uint64_t generation = 700;
uint32_t unavailable = 0;
int applications = 0, restorations = 0;
San14Result SAN14_MOD_CALL read(void*, uint32_t id, San14OfficerEditState* out) {
    if (!values.count(id)) return SAN14_E_NOT_FOUND;
    *out = values.at(id); out->generation = generation;
    out->flags = id == unavailable ? 0u : SAN14_OFFICER_EDIT_CAN_APPLY;
    for (const auto value : out->base_stats) if (value < 1 || value > 100) out->flags = 0;
    return SAN14_OK;
}
template<class T> San14Result check(const T& edit) {
    if (edit.generation != generation) return SAN14_E_STALE_GENERATION;
    if (!values.count(edit.officer_id)) return SAN14_E_NOT_FOUND;
    if (edit.officer_id == unavailable) return SAN14_E_BUSY;
    if (!edit.field_mask || (edit.field_mask & ~31u)) return SAN14_E_INVALID_ARGUMENT;
    const auto& value = values.at(edit.officer_id);
    for (size_t i = 0; i < 5; ++i) if (edit.field_mask & (1u << i)) {
        if (value.base_stats[i] != edit.expected_stats[i] || value.experience[i] != edit.expected_experience[i]) return SAN14_E_BUSY;
        if (edit.base_stats[i] < 1 || edit.base_stats[i] > 100) return SAN14_E_INVALID_ARGUMENT;
    }
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL validate(void*, const San14OfficerStatEdit* edit, San14OfficerEditState* out) {
    const auto result = check(*edit); if (result != SAN14_OK) return result;
    return read(nullptr, edit->officer_id, out);
}
San14Result SAN14_MOD_CALL apply(void*, const San14OfficerStatEdit* edit, San14OfficerEditState* out) {
    const auto result = check(*edit); if (result != SAN14_OK) return result;
    auto& value = values.at(edit->officer_id);
    for (size_t i = 0; i < 5; ++i) if (edit->field_mask & (1u << i)) {
        value.base_stats[i] = edit->base_stats[i]; value.experience[i] = edit->base_stats[i] * 100 + static_cast<uint32_t>(i);
    }
    ++applications; return read(nullptr, edit->officer_id, out);
}
San14Result SAN14_MOD_CALL restore(void*, const San14OfficerStatRestore* edit, San14OfficerEditState* out) {
    const auto result = check(*edit); if (result != SAN14_OK) return result;
    auto& value = values.at(edit->officer_id);
    for (size_t i = 0; i < 5; ++i) if (edit->field_mask & (1u << i)) {
        value.base_stats[i] = edit->base_stats[i]; value.experience[i] = edit->experience[i];
    }
    ++restorations; return read(nullptr, edit->officer_id, out);
}
void input(const std::string& id, const std::string& value) {
    const auto h = find_control_by_id(top(), id); assert(h);
    const auto callback = inputs.at(h); callback.first(callback.second, h, {value.data(), static_cast<uint32_t>(value.size())});
}
void dialog_tests(const San14HostApi& source) {
    auto host = source;
    San14OfficerEditApi service{sizeof(service), read, apply, validate, restore}; host.officer_edit = &service;
    host.roster = {sizeof(San14RosterApi), traits_mock::roster};
    values.clear(); applications = restorations = 0; unavailable = 0;
    for (uint32_t id = 1; id <= 12; ++id) {
        auto& state = values[id]; state = {}; state.struct_size = sizeof(state); state.officer_id = id;
        for (size_t i = 0; i < 5; ++i) { state.base_stats[i] = 60 + static_cast<uint32_t>(i); state.experience[i] = 6070 + static_cast<uint32_t>(i); }
    }
    enhanced_settings_mod::OfficerStatsDialog dialog; dialog.bind(&host);
    assert(dialog.open() == SAN14_OK); const auto stable = top(); const auto panel_count = g_mock_ui->panels.size();
    click_id("stats.next"); assert(find_button_by_text(top(), u8"12 武将12"));
    input("stats.search", u8"武将12"); click_id("stats.search.go"); click_id("stats.row.0");
    assert(top() == stable && g_mock_ui->panels.size() == panel_count);
    assert(g_mock_ui->controls.at(find_control_by_id(top(), "stats.value.0")).text == "60");
    click_id("stats.apply"); assert(applications == 0); // Unchanged inputs never reset experience.
    input("stats.value.0", "101"); click_id("stats.apply"); assert(applications == 0 && values.at(12).base_stats[0] == 60);
    input("stats.value.0", "-1"); click_id("stats.apply"); assert(applications == 0);
    input("stats.value.0", "90"); input("stats.value.3", "88");
    const auto before = values.at(12); click_id("stats.apply");
    assert(applications == 1 && values.at(12).base_stats[0] == 90 && values.at(12).base_stats[3] == 88);
    assert(values.at(12).experience[0] == 9000 && values.at(12).experience[3] == 8803);
    assert(values.at(12).experience[1] == before.experience[1]);
    click_id("stats.undo"); assert(restorations == 1);
    assert(!std::memcmp(values.at(12).base_stats, before.base_stats, sizeof(before.base_stats)));
    assert(!std::memcmp(values.at(12).experience, before.experience, sizeof(before.experience)));
    input("stats.value.0", "75"); click_id("stats.apply"); ++values.at(12).experience[0];
    click_id("stats.undo"); assert(restorations == 1 && values.at(12).base_stats[0] == 75);
    assert(g_mock_ui->controls.at(find_control_by_id(top(), "stats.status")).text.find(u8"已变化") != std::string::npos);
    ++generation; input("stats.value.0", "80"); click_id("stats.apply"); assert(applications == 2 && values.at(12).base_stats[0] == 75);
    click_id("stats.refresh"); click_id("stats.row.0"); assert(!g_mock_ui->controls.at(find_control_by_id(top(), "stats.undo")).enabled);
    unavailable = 12; click_id("stats.row.0"); assert(!g_mock_ui->controls.at(find_control_by_id(top(), "stats.apply")).enabled);
    click_id("stats.apply"); assert(applications == 2);
    unavailable = 0; values.at(12).base_stats[1] = 120;
    click_id("stats.row.0"); assert(!g_mock_ui->controls.at(find_control_by_id(top(), "stats.apply")).enabled);
    assert(g_mock_ui->controls.at(find_control_by_id(top(), "stats.status")).text.find(u8"五项基础能力均为1–100") != std::string::npos);
    click_id("stats.close"); assert(!dialog.panel() && g_mock_ui->panels.size() + 1 == panel_count);
    host.struct_size = offsetof(San14HostApi, officer_edit); assert(!enhanced_settings_mod::OfficerStatsDialog::supported(&host));
    host.struct_size = sizeof(host); service.struct_size = offsetof(San14OfficerEditApi, restore); assert(!enhanced_settings_mod::OfficerStatsDialog::supported(&host));
    service.struct_size = sizeof(service);
    {
        // Exercise text focus, commit, stable callbacks, exact undo and deferred
        // modal closure through the production Host UI registry.
        plugin_ui_test::Fixture native; int owner = 0; native.allowed = {&owner};
        auto native_backend=native.styled_backend();native_backend.tint_button_text=plugin_ui_test::Fixture::tint;
        plugin_ui::Runtime runtime(native_backend);
        auto real_host = host; real_host.host_context = &owner; real_host.ui = runtime.api();
        real_host.ui_extended = runtime.extended_api(); real_host.ui_style = runtime.style_api();
        enhanced_settings_mod::OfficerStatsDialog native_editor; native_editor.bind(&real_host);
        assert(native_editor.open() == SAN14_OK && runtime.panel_count() == 1);
        const auto created = native.created;
        const auto click = [&](const wchar_t* value) {
            int x = -1, y = -1;
            for (const auto& p : native.panels) if (p.second->visible) for (const auto& control : p.second->texts)
                if (std::wstring(control->value) == value) { x = p.second->x + control->x + control->w / 2; y = p.second->y + control->y + control->h / 2; }
            assert(x >= 0 && y >= 0 && runtime.mouse_down(x, y) && runtime.mouse_up(x, y));
        };
        click(L"1 武将1"); click(L"60"); assert(runtime.key_down('A', true) && runtime.text_input(L"95"));
        const auto old = values.at(1); click(L"应用"); assert(values.at(1).base_stats[0] == 95 && native.created == created && runtime.panel_count() == 1);
        click(L"撤销上次修改"); assert(values.at(1).base_stats[0] == old.base_stats[0] && values.at(1).experience[0] == old.experience[0]);
        click(L"返回"); assert(runtime.panel_count() == 0 && runtime.control_count() == 0 && native.created == native.destroyed);
    }
}
}
}
int main(){
    MockUiState ui;g_mock_ui=&ui;
    wchar_t filename[MAX_PATH]{};GetModuleFileNameW(nullptr,filename,MAX_PATH);
    const auto dll=LoadLibraryW((std::filesystem::path(filename).parent_path()/"enhanced_settings.dll").c_str());assert(dll);
    const auto init=reinterpret_cast<San14Result(SAN14_MOD_CALL*)(const San14HostApi*,San14ModApi*)>(GetProcAddress(dll,"SAN14Mod_Initialize"));assert(init);
    const auto prepare=reinterpret_cast<San14Result(SAN14_MOD_CALL*)(San14ModContext*)>(GetProcAddress(dll,"SAN14Mod_PrepareReloadV1"));assert(prepare);
    San14UiStyleApi style{sizeof(style),SAN14_UI_STYLE_EXPERT|SAN14_UI_STYLE_TINT|SAN14_UI_STYLE_ENABLED|SAN14_UI_STYLE_BUTTON_TEXT_TINT,style_selected,style_help,style_tint,style_enabled};
    San14UiSliderApi sliders{sizeof(sliders),ui_add_slider_fn,ui_set_slider_fn,ui_get_slider_fn};
    San14PagesApi pages{sizeof(pages),pages_register};
    San14ScenarioApi scenario{sizeof(scenario),nullptr,nullptr,nullptr,nullptr,configure,read_custom,write_custom,query_custom};
    San14ForceColorApi force_colors{SAN14_FORCE_COLOR_API_V1_SIZE,fc_palette,fc_forces,fc_set};
    San14SaveStorageApi save{sizeof(save),context,nullptr,edit_saved};
    strcpy_s(storage_info.save_basename,"svdexceSC00.s14");
    San14HostApi host{};host.struct_size=sizeof(host);host.abi_version=SAN14_HOST_ABI_V1;
    host.ui={sizeof(San14UiApi),ui_create,ui_add_label,ui_add_button,ui_set_text,ui_show,ui_close};
    host.ui_extended.struct_size=sizeof(host.ui_extended);host.ui_extended.add_text_input=text_input;
    host.ui_style=&style;host.ui_slider=&sliders;host.pages=&pages;host.scenario=&scenario;host.force_colors=&force_colors;host.save_storage=&save;
    host.storage={sizeof(San14StorageApi),read,write};host.events={sizeof(San14EventApi),subscribe,unsubscribe};
    host.rules.struct_size=sizeof(host.rules);host.rules.set_bool=set_rule;
    host.settings.struct_size=sizeof(host.settings);host.settings.register_bool=reg_setting;
    San14ModApi api{};api.struct_size=sizeof(api);San14ModContext ctx{};ctx.struct_size=sizeof(ctx);
    // Both old global defaults and an old save retain all 91 values. Newly
    // introduced fields inherit global defaults and are materialized in v2.
    auto old_global=legacy_defaults();old_global.values[16]=650000;
    auto old_local=legacy_defaults();old_local.enhanced_edit=1;
    ui.global_storage["enhanced.preferences.v1"]=bytes(old_global);
    local_storage["enhanced.preferences.v1"]=bytes(old_local);
    force_colors.struct_size=SAN14_FORCE_COLOR_API_V1_SIZE-1;assert(init(&host,&api)==SAN14_E_UNSUPPORTED);force_colors.struct_size=SAN14_FORCE_COLOR_API_V1_SIZE;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
    assert(custom_specs.size()==102&&batch_writes==1&&single_writes==0&&subs.size()==4&&edit_rule==1&&merit_rule==0);
    assert(custom_values.at("city_cap.large.gold")==150000&&custom_values.at("city_cap.mega.gold")==200000&&custom_values.at("city_cap.tribal.durability")==9999);
    // Migrated records are materialized even with no user edits.
    assert(saved(ui.global_storage).values[16]==650000);
    assert(saved(local_storage).values[16]==250000&&saved(local_storage).enhanced_edit==1);
    assert(saved(local_storage).values[91]==50000&&saved(local_storage).values[101]==5);
    assert(local_storage.at("enhanced.preferences.v1")==bytes(old_local));
    const auto original_a=local_storage;
    assert(ui.registered_page.open(nullptr)==SAN14_OK&&ui.stack.size()==1);
    // Dragging/typing changes only the draft; tabs retain it. Current save
    // also updates new-game defaults, but leaves other existing saves alone.
    // Returning discards an unsubmitted draft.
    const auto staged_local=local_storage,staged_global=ui.global_storage;const auto staged_batches=batch_writes;
    Preferences peer;peer.values[0]=77777;peer.values[101]=66;seed_v3(peer_storage,peer);const auto peer_before=peer_storage;
    assert(!find_button_by_text(top(),u8"保存变更到此存档")&&find_button_by_text(top(),u8"保存变更"));
    auto scope_hint_visible=[&](){for(const auto& item:ui.controls)if(item.second.panel==top()&&item.second.text.find(u8"其他已有存档不变")!=std::string::npos&&item.second.text.find('\n')!=std::string::npos)return true;return false;};
    assert(scope_hint_visible());
    assert(range_tint("stat")==0xff262018u);
    slide(0,160);assert(slider_value(0)==160&&custom_values.at("officer_stat_cap")==100&&range_tint("stat")==0xff1d63c7u&&scope_hint_visible());
    slide(0,100);assert(range_tint("stat")==0xff262018u);slide(0,160);
    assert(local_storage==staged_local&&ui.global_storage==staged_global&&batch_writes==staged_batches);
    assert(prepare(&ctx)==SAN14_OK&&custom_values.at("officer_stat_cap")==100&&slider_value(0)==160);
    assert(local_storage==staged_local&&ui.global_storage==staged_global); // Preflight never commits the draft.
    click_text(u8"城市");click_text(u8"常规");assert(slider_value(0)==160&&range_tint("stat")==0xff1d63c7u);
    click_text(u8"城市");slide(0,61000);assert(range_tint("city.0")==0xff1d63c7u);
    click_text(u8"小城 ▼");click_text(u8"中城");assert(range_tint("city.0")==0xff262018u);
    click_text(u8"中城 ▼");click_text(u8"小城");assert(range_tint("city.0")==0xff1d63c7u);
    slide(0,50000);assert(range_tint("city.0")==0xff262018u);click_text(u8"常规");
    click_text(u8"保存变更");assert(custom_values.at("officer_stat_cap")==160&&saved(local_storage).values[20]==160&&saved(ui.global_storage).values[20]==160&&peer_storage==peer_before&&range_tint("stat")==0xff262018u);
    slide(0,170);click_text(u8"保存变更到全部存档");
    assert(saved(peer_storage).values[20]==170&&saved(peer_storage).values[0]==77777&&saved(peer_storage).values[101]==66);
    slide(0,175);click_text(u8"返回");assert(ui.registered_page.open(nullptr)==SAN14_OK&&slider_value(0)==170&&custom_values.at("officer_stat_cap")==170&&range_tint("stat")==0xff262018u);
    local_storage=staged_local;++storage_info.revision;fire(SAN14_EVENT_STORAGE_CHANGED);fire(SAN14_EVENT_UI_IDLE);
    assert(slider_value(0)==100);click_text("100");input_value("155");click_text(u8"保存变更到全部存档");assert(custom_values.at("officer_stat_cap")==155);
    click_text(u8"标准");click_text(u8"保存变更到全部存档");assert(custom_values.at("officer_stat_cap")==100&&slider_value(0)==100);
    click_text(u8"城市");assert(slider_value(0)==50000&&slider_value(3)==2000);
    click_text(u8"小城 ▼");assert(ui.stack.size()==2);click_text(u8"异族城池");assert(ui.stack.size()==1&&slider_value(0)==250000&&slider_value(3)==9999);
    const auto batches=batch_writes,writes=single_writes;slide(0,600000);click_text(u8"保存变更到全部存档");
    assert(custom_values.at("city_cap.tribal.gold")==600000&&custom_values.at("city_cap.small.gold")==50000&&batch_writes==batches+1&&single_writes==writes);
    click_text(u8"恢复当前档位");click_text(u8"保存变更到全部存档");assert(custom_values.at("city_cap.tribal.gold")==250000);
    click_text(u8"阵型");click_text(u8"1 鱼鳞 ▼");click_text(u8"14 四夷");assert(slider_value(0)==26&&slider_value(4)==21);
    slide(4,201);assert(range_tint("formation.4")==0xff1d63c7u);click_text(u8"保存变更到全部存档");assert(custom_values.at("formation.14.defense")==201&&custom_values.at("formation.1.defense")==30);
    click_text(u8"恢复当前阵型");click_text(u8"保存变更到全部存档");assert(slider_value(4)==21);
    click_text(u8"军情");assert(slider_value(0)==50000&&slider_value(3)==200000);
    slide(0,310000);assert(range_tint("military_intel_cap.1")==0xff1d63c7u);click_text(u8"保存变更到全部存档");assert(custom_values.at("military_intel_cap.1")==310000&&custom_values.at("military_intel_cap.4")==200000);
    click_text(u8"恢复全部军情");click_text(u8"保存变更到全部存档");assert(slider_value(0)==50000);
    click_text(u8"雇用");assert(slider_value(0)==40);
    size_t hire_sliders=0;for(const auto& item:ui.controls)if(item.second.panel==top()&&item.second.is_slider){++hire_sliders;assert(item.second.slider_min==0&&item.second.slider_max==100);}
    assert(hire_sliders==7);
    slide(0,100);assert(range_tint("officer_hire_cap.1")==0xff1d63c7u);click_text(u8"保存变更到全部存档");assert(custom_values.at("officer_hire_cap.1")==100&&range_tint("officer_hire_cap.1")==0xff262018u);
    slide(0,100000);click_text(u8"保存变更到全部存档");assert(custom_values.at("officer_hire_cap.1")==100);
    slide(0,99);click_text(u8"保存变更到全部存档");assert(custom_values.at("officer_hire_cap.1")==99&&custom_values.at("officer_hire_cap.7")==5);
    assert(slider_value(6)==5);slide(6,17);click_text(u8"保存变更到全部存档");assert(custom_values.at("officer_hire_cap.7")==17);
    click_text(u8"恢复全部雇用");click_text(u8"保存变更到全部存档");assert(custom_values.at("officer_hire_cap.1")==40&&custom_values.at("officer_hire_cap.7")==5);
    click_text(u8"常规");click_bool_choice(1,u8"有");click_text(u8"保存变更到全部存档");assert(merit_rule==1&&saved(local_storage).extended_merit==1);
    click_bool_choice(1,u8"无");click_text(u8"保存变更到全部存档");assert(merit_rule==0&&saved(local_storage).extended_merit==0);
    const auto before_merit=local_storage;
    assert(merit_setting.changed(nullptr,merit_setting.key,1)==SAN14_OK&&merit_rule==0&&local_storage==before_merit);
    fail_saved_edit=true;click_text(u8"保存变更到全部存档");assert(merit_rule==0&&local_storage==before_merit);fail_saved_edit=false;
    // Restore old A after changing global defaults elsewhere; A retains its own values.
    local_storage=original_a;++storage_info.revision;fire(SAN14_EVENT_STORAGE_CHANGED);fire(SAN14_EVENT_UI_IDLE);
    assert(custom_values.at("city_cap.tribal.gold")==250000&&custom_values.at("formation.14.defense")==21);
    Preferences b;b.enhanced_edit=1;b.extended_merit=1;b.values[20]=180;b.values[19]=8888;b.values[91]=450000;b.values[101]=29;
    const auto batches_before_restore=batch_writes;
    seed_v3(local_storage,b);++storage_info.revision;fire(SAN14_EVENT_SAVE_LOADED);fire(SAN14_EVENT_UI_IDLE);
    assert(batch_writes==batches_before_restore+1&&custom_values.at("military_intel_cap.1")==450000&&custom_values.at("officer_hire_cap.7")==29&&merit_rule==1);
    click_text(u8"常规");assert(slider_value(0)==180&&edit_rule==1);
    bool selected_yes=false;for(auto& item:ui.controls)if(item.second.panel==top()&&item.second.text==u8"有"&&item.second.selected)selected_yes=true;assert(selected_yes);
    // The regular settings page is the sole color editor entry, using only V1.
    size_t color_entries=0;for(const auto& item:ui.controls)if(item.second.panel==top()&&item.second.text==u8"更改势力颜色（载入存档后）")++color_entries;assert(color_entries==1);
    for(uint32_t id=2;id<=12;++id)ui.force_colors[id]=uint8_t(id);
    click_text(u8"打开");assert(ui.stack.size()==2);
    auto palette_control=[&](unsigned color){return find_control_by_id(top(),("fc_pbt_"+std::to_string(color)).c_str());};
    assert(ui.controls.at(palette_control(0)).selected&&ui.controls.at(palette_control(0)).enabled);
    assert(!ui.controls.at(palette_control(55)).enabled&&ui.controls.at(palette_control(55)).text=="55");
    assert(ui.controls.at(find_control_by_id(top(),"fc_psw_55")).tint==0xff808080u);
    const auto before_blocked=ui.set_color_calls;const auto before_status=ui.controls.at(find_control_by_id(top(),"fc_status")).text;
    const auto blocked=ui.controls.at(palette_control(55));blocked.cb(blocked.ctx,palette_control(55));
    assert(ui.set_color_calls==before_blocked&&ui.controls.at(find_control_by_id(top(),"fc_status")).text==before_status);
    click_text(u8"下一页");const auto tribal_row=find_control_by_id(top(),"fc_row_4");assert(tribal_row);const auto tribal_ctrl=ui.controls.at(tribal_row);tribal_ctrl.cb(tribal_ctrl.ctx,tribal_row);
    bool selected_tribal=false;for(auto& item:ui.controls)if(item.second.panel==top()&&item.second.id.rfind("fc_row_",0)==0&&item.second.selected&&item.second.text.find(u8"异族")!=std::string::npos)selected_tribal=true;assert(selected_tribal);
    assert(ui.controls.at(palette_control(55)).enabled&&ui.controls.at(palette_control(55)).selected);
    assert(!ui.controls.at(palette_control(0)).enabled);
    const auto free_color=find_control_by_id(top(),"fc_pbt_20");const auto free_ctrl=ui.controls.at(free_color);free_ctrl.cb(free_ctrl.ctx,free_color);assert(ui.force_colors.at(48)==20);
    assert(ui.controls.at(palette_control(55)).enabled&&!ui.controls.at(palette_control(55)).selected);
    assert(ui.controls.at(palette_control(20)).enabled&&ui.controls.at(palette_control(20)).selected);
    click_text(u8"撤销上次修改");assert(ui.force_colors.at(48)==55&&ui.controls.at(palette_control(55)).selected);
    click_text(u8"返回");assert(ui.stack.size()==1);
    // Failed persistence preserves the draft, actual rules and saved values.
    const auto before_failure=local_storage,defaults_before_failure=ui.global_storage;
    fail_write=true;slide(0,190);click_text(u8"保存变更到全部存档");
    assert(custom_values.at("officer_stat_cap")==180&&slider_value(0)==190&&local_storage==before_failure&&ui.global_storage==defaults_before_failure&&range_tint("stat")==0xff1d63c7u);fail_write=false;
    const auto before_current_failure=ui.global_storage;fail_saved_edit=true;click_text(u8"保存变更");
    assert(custom_values.at("officer_stat_cap")==180&&slider_value(0)==190&&local_storage==before_failure&&ui.global_storage==before_current_failure&&range_tint("stat")==0xff1d63c7u);fail_saved_edit=false;
    // Persistence success and current-world application failure are distinct:
    // keep the durable settings and permit a normal load to apply them.
    fail_configure=true;click_text(u8"保存变更");
    assert(saved(local_storage).values[20]==190&&custom_values.at("officer_stat_cap")==180);fail_configure=false;
    ++storage_info.revision;fire(SAN14_EVENT_STORAGE_CHANGED);fire(SAN14_EVENT_UI_IDLE);assert(custom_values.at("officer_stat_cap")==190);
    fail_merit_disable=true;assert(api.on_disable(&ctx)==SAN14_E_BUSY&&merit_rule==1);fail_merit_disable=false;
    assert(api.on_disable(&ctx)==SAN14_OK&&subs.empty()&&custom_values.empty()&&edit_rule==0&&merit_rule==0);
    api.on_unload(&ctx);assert(ui.panels.empty());
    // Exercise the DLL through the actual Host UI registry. The permissive
    // button mock above previously missed SELECTED without a button style.
    {
        plugin_ui_test::Fixture native;int owner=0;native.allowed={&owner};
        auto native_backend=native.styled_backend();native_backend.tint_button_text=plugin_ui_test::Fixture::tint;
        plugin_ui::Runtime runtime(native_backend);
        auto real_host=host;real_host.host_context=&owner;real_host.ui=runtime.api();
        real_host.ui_extended=runtime.extended_api();real_host.ui_style=runtime.style_api();real_host.ui_slider=runtime.slider_api();
        seed_v3(local_storage,Preferences{});++storage_info.revision;
        assert(init(&real_host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
        assert(ui.registered_page.open(nullptr)==SAN14_OK);
        auto click=[&](const wchar_t* text){
            int x=-1,y=-1;
            for(const auto& panel:native.panels)if(panel.second->visible)for(const auto& control:panel.second->texts)
                if(std::wstring(control->value)==text){x=panel.second->x+control->x+control->w/2;y=panel.second->y+control->y+control->h/2;}
            if(x<0||y<0)std::wcerr<<L"Missing visible control: "<<text<<L"\n";
            assert(x>=0&&y>=0);assert(runtime.mouse_down(x,y)&&runtime.mouse_up(x,y));
        };
        auto visible=[&](){return std::count_if(native.panels.begin(),native.panels.end(),[](const auto& p){return p.second->visible;});};
        // Tint targets the button caption; the native button keeps rendering
        // its own number and remains available for editing.
        click(L"100");assert(runtime.panel_count()==2);assert(runtime.key_down(0x1b)&&runtime.panel_count()==1);
        {auto& panel=native.only_panel();const auto& slider=*panel.sliders.front();
            const int x=panel.x+slider.x+slider.w*3/4,y=panel.y+slider.y+slider.h/2;
            assert(runtime.mouse_down(x,y)&&runtime.mouse_up(x,y));
            size_t blue=0;for(const auto& c:panel.texts)if(c->tint==0xff1d63c7u&&!std::wstring(c->value).empty())++blue;
            assert(blue==1&&custom_values.at("officer_stat_cap")==100);
        }
        click(L"城市");click(L"异族城池 ▼");click(L"小城");
        click(L"小城 ▼");assert(runtime.panel_count()==2&&visible()==2);
        click(L"异族城池");assert(runtime.panel_count()==1&&visible()==1);
        click(L"异族城池 ▼");assert(runtime.panel_count()==2&&visible()==2);
        assert(runtime.key_down(0x1b)&&runtime.panel_count()==1);
        click(L"异族城池 ▼");click(L"小城");assert(runtime.panel_count()==1);
        click(L"阵型");click(L"14 四夷 ▼");click(L"1 鱼鳞");
        click(L"1 鱼鳞 ▼");assert(runtime.panel_count()==2&&visible()==2);
        click(L"14 四夷");assert(runtime.panel_count()==1);
        click(L"14 四夷 ▼");assert(runtime.mouse_down(500,700)&&runtime.mouse_up(500,700)&&runtime.panel_count()==1);
        click(L"常规");click(L"打开");assert(runtime.panel_count()==2&&visible()==1);
        const auto calls_before=ui.set_color_calls;click(L"02");assert(ui.set_color_calls==calls_before);
        click(L"20");assert(ui.force_colors.at(1)==20&&ui.set_color_calls==calls_before+1);
        click(L"撤销上次修改");assert(ui.force_colors.at(1)==0);
        click(L"返回");assert(runtime.panel_count()==1&&visible()==1);
        click(L"预设");click(L"读取");assert(runtime.panel_count()==2&&visible()==1);
        click(L"取消");assert(runtime.panel_count()==1&&visible()==1);
        assert(api.on_disable(&ctx)==SAN14_OK&&runtime.panel_count()==0);api.on_unload(&ctx);
        assert(native.created==native.destroyed);
    }
    // Save Changes works without a filename: update the live new session and
    // future defaults, never the other existing save. A Host failure still
    // retains the draft and restores defaults instead of silently falling back.
    seed_v3(local_storage,Preferences{});++storage_info.revision;
    storage_info.save_basename[0]=0;storage_info.state=SAN14_STORAGE_READY;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK&&ui.registered_page.open(nullptr)==SAN14_OK);
    const auto peer_before_new=peer_storage;
    slide(0,185);const auto global_before_new=ui.global_storage;fail_saved_edit=true;click_text(u8"保存变更");
    assert(ui.global_storage==global_before_new&&custom_values.at("officer_stat_cap")==100&&range_tint("stat")==0xff1d63c7u);fail_saved_edit=false;
    click_text(u8"保存变更");assert(saved(local_storage).values[20]==185&&saved(ui.global_storage).values[20]==185&&peer_storage==peer_before_new&&range_tint("stat")==0xff262018u);
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    storage_info.state=SAN14_STORAGE_UNAVAILABLE;storage_info.playthrough_id[0]=0;++storage_info.revision;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK&&ui.registered_page.open(nullptr)==SAN14_OK);
    const auto local_before_title=local_storage;slide(0,186);click_text(u8"保存变更");
    assert(saved(ui.global_storage).values[20]==186&&local_storage==local_before_title&&peer_storage==peer_before_new&&range_tint("stat")==0xff262018u);
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    storage_info.state=SAN14_STORAGE_READY;strcpy_s(storage_info.playthrough_id,"pt-a");strcpy_s(storage_info.save_basename,"svdexceSC00.s14");++storage_info.revision;
    // An existing save without preferences keeps the frozen old fallback,
    // despite newer global defaults. Newly created sessions inherit the latest.
    local_storage.clear();++storage_info.revision;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
    assert(saved(local_storage).values[20]==100&&saved(ui.global_storage).values[20]==186&&peer_storage==peer_before_new);
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    local_storage.clear();storage_info.save_basename[0]=0;++storage_info.revision;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
    assert(saved(local_storage).values[20]==186);assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    strcpy_s(storage_info.save_basename,"svdexceSC00.s14");
    // New kinds are optional: old Host prefixes keep the original 91 entries;
    // per-kind UNSUPPORTED hides only that category and never configures it.
    local_storage.clear();++storage_info.revision;
    scenario.struct_size=offsetof(San14ScenarioApi,query_custom);
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK&&custom_specs.size()==91);
    assert(saved(local_storage).values[20]==100&&saved(ui.global_storage).values[20]==186);
    assert(ui.registered_page.open(nullptr)==SAN14_OK&&!find_button_by_text(top(),u8"军情")&&!find_button_by_text(top(),u8"雇用"));
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    scenario.struct_size=sizeof(scenario);query_hire=false;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK&&custom_specs.size()==95);
    assert(ui.registered_page.open(nullptr)==SAN14_OK&&find_button_by_text(top(),u8"军情")&&!find_button_by_text(top(),u8"雇用"));
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);query_hire=true;
    auto new_global=Preferences{};new_global.values[20]=190;new_global.values[91]=610000;new_global.values[101]=12;new_global.extended_merit=1;
    auto old_branch=legacy_defaults();old_branch.values[20]=145;
    seed_v3(ui.global_storage,new_global);clear_preferences(local_storage);local_storage["enhanced.preferences.v1"]=bytes(old_branch);
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
    assert(saved(local_storage).values[20]==145&&saved(local_storage).values[91]==50000&&saved(local_storage).values[101]==5&&saved(local_storage).extended_merit==0);
    assert(custom_values.at("officer_stat_cap")==145&&custom_values.at("military_intel_cap.1")==50000&&custom_values.at("officer_hire_cap.7")==5&&merit_rule==0);
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    // Both historical formats retain unrelated settings while capping old hire values.
    for(const bool v2:{false,true}){
        auto previous=Preferences{};previous.values[20]=145;previous.values[91]=610000;
        for(size_t i=enhanced_settings_mod::kOfficerHireFirst;i<enhanced_settings_mod::kCustomCount;++i)previous.values[i]=100000;
        seed_v3(ui.global_storage,previous);
        if(v2){PreferencesV2 previous_v2;previous_v2.values=previous.values;clear_preferences(local_storage);local_storage["enhanced.preferences.v2"]=bytes(previous_v2);}
        else seed_v3(local_storage,previous);
        assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
        assert(saved(ui.global_storage).values[20]==145&&saved(local_storage).values[91]==610000);
        for(size_t i=enhanced_settings_mod::kOfficerHireFirst;i<enhanced_settings_mod::kCustomCount;++i){assert(saved(ui.global_storage).values[i]==100&&saved(local_storage).values[i]==100);}
        assert(custom_values.at("officer_hire_cap.1")==100&&custom_values.at("officer_hire_cap.7")==100);
        assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    }
    local_storage.erase("enhanced.preferences.v2");
    auto enable_failure=Preferences{};enable_failure.enhanced_edit=1;enable_failure.extended_merit=1;
    seed_v3(local_storage,enable_failure);fail_configure=true;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_E_BUSY&&edit_rule==0&&merit_rule==0&&custom_values.empty());
    fail_configure=false;assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    // A corrupt v3 must never silently fall back to the preserved v1 record.
    auto invalid=Preferences{};invalid.version=77;seed_v3(local_storage,invalid);
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_E_INVALID_ARGUMENT&&custom_values.empty());
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    clear_preferences(local_storage);auto invalid_legacy=legacy_defaults();invalid_legacy.values[20]=999;
    local_storage["enhanced.preferences.v1"]=bytes(invalid_legacy);
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_E_INVALID_ARGUMENT&&custom_values.empty());
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    // v2 global and campaign records migrate every numeric value and merit;
    // the new global engine mode is initially disabled.
    ui.global_storage.clear();local_storage.clear();
    PreferencesV2 v2_global;v2_global.values[20]=181;v2_global.extended_merit=1;
    PreferencesV2 v2_local;v2_local.values[20]=149;v2_local.values[101]=18;
    ui.global_storage["enhanced.preferences.v2"]=bytes(v2_global);local_storage["enhanced.preferences.v2"]=bytes(v2_local);
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
    assert(saved(ui.global_storage).values[20]==181&&saved(ui.global_storage).extended_merit==1&&saved(ui.global_storage).extended_traits==0);
    assert(saved(local_storage).values[20]==149&&saved(local_storage).values[101]==18&&saved(local_storage).extended_merit==0);
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    clear_preferences(ui.global_storage);v2_global.version=99;ui.global_storage["enhanced.preferences.v2"]=bytes(v2_global);
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_E_INVALID_ARGUMENT);
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    // Public entries are disabled; keep testing the retained editor directly.
    San14OfficerTraitsApi trait_api{sizeof(trait_api),traits_mock::query,traits_mock::set,traits_mock::read,traits_mock::options,traits_mock::replace};
    host.officer_traits=&trait_api;host.roster={sizeof(host.roster),traits_mock::roster};
    ui.global_storage.clear();local_storage.clear();traits_mock::values.clear();
    for(uint32_t id=1;id<=12;++id)traits_mock::values[id]={1,2,3,4,5,0,0,0,0};
    auto retained_mode=Preferences{};retained_mode.extended_traits=1;
    seed_v3(ui.global_storage,retained_mode);
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
    assert(ui.registered_page.open(nullptr)==SAN14_OK&&!find_button_by_text(top(),u8"编辑个性"));
    San14ControlHandle previous_label=0;size_t placeholders=0;
    for(const auto& item:ui.controls)if(item.second.panel==top()&&item.second.text.find(u8"（实现中）")!=std::string::npos){
        assert(!item.second.is_button&&!item.second.cb&&!item.second.int_cb&&item.second.tint==0xff888888);
        previous_label=item.first;++placeholders;
    }
    assert(placeholders==2&&previous_label&&traits_setting.flags==SAN14_SETTING_DISABLED);
    assert(traits_mock::expanded&&saved(ui.global_storage).extended_traits==1);
    traits_mock::menu=false;
    const auto before_mode_change=ui.global_storage;
    assert(traits_setting.changed(nullptr,traits_setting.key,0)==SAN14_E_UNSUPPORTED&&traits_mock::expanded&&ui.global_storage==before_mode_change);
    auto old_save=Preferences{};old_save.extended_traits=0;old_save.values[20]=142;
    seed_v3(local_storage,old_save);++storage_info.revision;fire(SAN14_EVENT_SAVE_LOADED);fire(SAN14_EVENT_UI_IDLE);
    assert(traits_mock::expanded&&custom_values.at("officer_stat_cap")==142&&saved(ui.global_storage).extended_traits==1);
    enhanced_settings_mod::OfficerTraitsDialog retained_editor;retained_editor.bind(&host);
    assert(retained_editor.open()==SAN14_OK&&ui.stack.size()==2&&find_control_by_id(top(),"traits.search"));
    click_id("traits.next");assert(find_button_by_text(top(),u8"9 武将9"));
    search_traits(u8"武将12");assert(find_button_by_text(top(),u8"12 武将12"));click_id("traits.item.0");
    for(uint32_t i=0;i<5;++i){const auto h=find_control_by_id(top(),"traits.base."+std::to_string(i));assert(h&&!ui.controls.at(h).is_button&&ui.controls.at(h).text.find(u8"只读")!=std::string::npos);}
    click_id("traits.extra.0");search_traits(u8"个性名6");assert(find_button_by_text(top(),u8"0 无")&&find_button_by_text(top(),u8"6 个性名6"));click_text(u8"6 个性名6");
    click_id("traits.apply");assert(traits_mock::values.at(12)[5]==6&&traits_mock::replaces==1);
    for(size_t i=0;i<5;++i)assert(traits_mock::values.at(12)[i]==i+1);
    assert(ui.controls.at(find_control_by_id(top(),"traits.status")).text.find(u8"保存并重新载入")!=std::string::npos);
    click_id("traits.undo");assert(traits_mock::values.at(12)[5]==0&&traits_mock::replaces==2);
    click_id("traits.extra.3");search_traits(u8"个性名9");click_text(u8"9 个性名9");
    ++traits_mock::generation;const auto before_stale=traits_mock::replaces;click_id("traits.apply");
    assert(traits_mock::replaces==before_stale&&traits_mock::values.at(12)[8]==0);
    assert(ui.controls.at(find_control_by_id(top(),"traits.status")).text.find(u8"战局已切换")!=std::string::npos);
    click_id("traits.choose");search_traits(u8"武将12");click_id("traits.item.0");traits_mock::deployed=12;
    // An officer who deployed after opening the editor is rejected by Host.
    click_id("traits.extra.0");search_traits(u8"个性名6");click_text(u8"6 个性名6");click_id("traits.apply");
    assert(traits_mock::replaces==before_stale&&traits_mock::values.at(12)[5]==0);
    click_id("traits.choose");search_traits(u8"武将12");click_id("traits.item.0");
    click_id("traits.extra.0");assert(!find_control_by_id(top(),"traits.search"));
    assert(ui.controls.at(find_control_by_id(top(),"traits.status")).text.find(u8"归城")!=std::string::npos);
    click_id("traits.close");assert(ui.stack.size()==1);
    // A loaded nine-slot world must refuse disable before releasing caps,
    // subscriptions or the still-visible settings page.
    const auto caps_before_disable=custom_values;const auto subs_before_disable=subs.size();
    assert(api.on_disable(&ctx)==SAN14_E_BUSY&&custom_values==caps_before_disable&&subs.size()==subs_before_disable&&ui.stack.size()==1&&traits_mock::expanded);
    traits_mock::menu=true;traits_mock::deployed=0;
    assert(api.on_disable(&ctx)==SAN14_OK&&custom_values.empty()&&subs.empty()&&!traits_mock::expanded);api.on_unload(&ctx);
    // Expanded mode is applied before other rules/caps, and rolled back if
    // another part of enable fails while still at the title menu.
    local_storage.clear();auto traits_default=Preferences{};traits_default.extended_traits=1;traits_default.extended_merit=1;
    seed_v3(ui.global_storage,traits_default);fail_configure=true;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_E_BUSY&&!traits_mock::expanded&&merit_rule==0);
    fail_configure=false;assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    {
        // Run the additional editor through the real Host UI registry, including
        // native focus and committed text, rather than only permissive mocks.
        plugin_ui_test::Fixture native;int owner=0;native.allowed={&owner};auto native_backend=native.styled_backend();native_backend.tint_button_text=plugin_ui_test::Fixture::tint;
        plugin_ui::Runtime runtime(native_backend);
        auto real_host=host;real_host.host_context=&owner;real_host.ui=runtime.api();real_host.ui_extended=runtime.extended_api();real_host.ui_style=runtime.style_api();real_host.ui_slider=runtime.slider_api();
        local_storage.clear();traits_default.extended_merit=0;seed_v3(ui.global_storage,traits_default);
        assert(init(&real_host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
        assert(ui.registered_page.open(nullptr)==SAN14_OK);traits_mock::menu=false;
        const auto click=[&](const wchar_t* text){
            int x=-1,y=-1;for(const auto& p:native.panels)if(p.second->visible)for(const auto& c:p.second->texts)if(std::wstring(c->value)==text){x=p.second->x+c->x+c->w/2;y=p.second->y+c->y+c->h/2;}
            if(x<0||y<0)std::wcerr<<L"Missing traits UI control: "<<text<<L"\n";
            assert(x>=0&&y>=0&&runtime.mouse_down(x,y)&&runtime.mouse_up(x,y));
        };
        const auto visible=[&](){return std::count_if(native.panels.begin(),native.panels.end(),[](const auto& p){return p.second->visible;});};
        int last_regular_y=-1,first_pending_y=-1,last_pending_y=-1;
        for(const auto& p:native.panels)if(p.second->visible)for(const auto& c:p.second->texts){
            const std::wstring text(c->value);
            if(text==L"更改势力颜色（载入存档后）")last_regular_y=c->y;
            if(text==L"扩展武将有效个性至9个（实现中）"){first_pending_y=c->y;assert(c->tint==0xff888888);}
            if(text==L"编辑武将额外个性（实现中）"){last_pending_y=c->y;assert(c->tint==0xff888888);}
        }
        assert(last_regular_y>=0&&first_pending_y>last_regular_y&&last_pending_y>first_pending_y);
        const auto callbacks_before=native.callbacks;
        for(const auto& p:native.panels)if(p.second->visible){
            runtime.mouse_down(p.second->x+500,p.second->y+first_pending_y+10);runtime.mouse_up(p.second->x+500,p.second->y+first_pending_y+10);
            runtime.mouse_down(p.second->x+1200,p.second->y+last_pending_y+10);runtime.mouse_up(p.second->x+1200,p.second->y+last_pending_y+10);
        }
        assert(native.callbacks==callbacks_before&&runtime.panel_count()==1);
        enhanced_settings_mod::OfficerTraitsDialog direct_editor;direct_editor.bind(&real_host);assert(direct_editor.open()==SAN14_OK);
        assert(runtime.panel_count()==2&&visible()==1);
        click(L"下一页");click(L"12 武将12");click(L"个性6：无 ▼");
        int sx=-1,sy=-1;for(const auto& p:native.panels)if(p.second->visible)for(const auto& c:p.second->texts)if(c->x==384&&c->y==52){sx=p.second->x+c->x+100;sy=p.second->y+c->y+16;}
        assert(sx>=0&&sy>=0&&runtime.mouse_down(sx,sy)&&runtime.mouse_up(sx,sy));assert(runtime.text_input(L"个性名6"));
        click(L"搜索");click(L"6 个性名6");click(L"应用");assert(traits_mock::values.at(12)[5]==6);
        click(L"撤销");assert(traits_mock::values.at(12)[5]==0);click(L"返回");assert(runtime.panel_count()==1&&visible()==1);
        traits_mock::menu=true;assert(api.on_disable(&ctx)==SAN14_OK&&runtime.panel_count()==0);api.on_unload(&ctx);assert(native.created==native.destroyed);
    }
    local_storage.clear();seed_v3(ui.global_storage,traits_default);traits_mock::menu=false;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_E_BUSY);
    assert(custom_values.empty()&&subs.empty()&&!traits_mock::expanded);
    traits_mock::menu=true;assert(api.on_enable(&ctx)==SAN14_OK&&traits_mock::expanded&&custom_specs.size()==102);
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    traits_mock::query_busy=true;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_E_BUSY&&custom_values.empty()&&subs.empty());
    traits_mock::query_busy=false;assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    host.officer_traits=nullptr;local_storage.clear();
    seed_v3(ui.global_storage,traits_default);
    const auto protected_global=ui.global_storage;const auto protected_local=local_storage;const auto before_missing_service=batch_writes;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_E_UNSUPPORTED);
    assert(custom_values.empty()&&subs.empty()&&batch_writes==before_missing_service&&ui.global_storage==protected_global&&local_storage==protected_local);
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    auto five_slots=Preferences{};seed_v3(ui.global_storage,five_slots);
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK&&custom_specs.size()==102);
    assert(ui.registered_page.open(nullptr)==SAN14_OK&&!find_button_by_text(top(),u8"编辑个性"));
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    stats_mock::dialog_tests(host);

    // Reading new-game defaults into an older slot is a preview and applies
    // only the chosen fields to that slot after confirmation.
    storage_info.state=SAN14_STORAGE_READY;
    strcpy_s(storage_info.save_basename,"svdexceSC00.s14");
    Preferences new_game;new_game.values[20]=145;seed_v3(ui.global_storage,new_game);
    Preferences old_slot;old_slot.values[0]=77000;old_slot.values[20]=110;seed_v3(local_storage,old_slot);
    ++storage_info.revision;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
    assert(ui.registered_page.open(nullptr)==SAN14_OK);
    const auto old_disk=local_storage,initial_default_records=ui.global_storage,other_slots=peer_storage;
    click_text(u8"预设");click_text(u8"读取");click_text(u8"新局默认");click_text(u8"常规");
    assert(slider_value(0)==145&&range_tint("stat")==0xff1d63c7u&&custom_values.at("officer_stat_cap")==110);
    assert(local_storage==old_disk&&ui.global_storage==initial_default_records&&peer_storage==other_slots);
    fail_saved_edit=true;click_text(u8"保存变更");fail_saved_edit=false;
    assert(local_storage==old_disk&&saved(ui.global_storage).values[20]==145&&slider_value(0)==145);
    click_text(u8"保存变更");
    assert(saved(local_storage).values[20]==145&&saved(local_storage).values[0]==77000);
    assert(saved(ui.global_storage).values[20]==145&&peer_storage==other_slots);

    click_text(u8"标准");click_text(u8"保存变更");
    assert(saved(local_storage).values[20]==100&&saved(local_storage).selected.has(20));
    auto name_preset=[&](const std::string& name){
        const auto h=find_control_by_id(top(),"preset.name");assert(h);
        const auto cb=inputs.at(h);cb.first(cb.second,h,{name.data(),static_cast<uint32_t>(name.size())});
    };
    const auto before_preset=local_storage;
    click_text(u8"预设");click_text(u8"保存为预设");name_preset(u8"自定上限");click_text(u8"保存");
    assert(local_storage==before_preset&&ui.global_storage.count("enhanced.presets.v1"));
    const auto preset_bytes=ui.global_storage.at("enhanced.presets.v1");
    click_text(u8"保存为预设");name_preset(u8"自定上限");click_text(u8"保存");
    assert(ui.stack.size()==2&&ui.global_storage.at("enhanced.presets.v1")==preset_bytes);click_text(u8"取消");
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);

    Preferences another_slot;another_slot.values[20]=190;another_slot.values[25]=203;
    seed_v3(local_storage,another_slot);++storage_info.revision;
    assert(init(&host,&api)==SAN14_OK&&api.on_load(&ctx)==SAN14_OK&&api.on_enable(&ctx)==SAN14_OK);
    assert(ui.registered_page.open(nullptr)==SAN14_OK);
    click_text(u8"预设");click_text(u8"读取");click_text(u8"自定上限");click_text(u8"常规");
    assert(slider_value(0)==100&&custom_values.at("officer_stat_cap")==190);
    click_text(u8"保存变更");
    assert(saved(local_storage).values[20]==100&&saved(local_storage).values[25]==203&&saved(local_storage).values[0]==77000);
    assert(ui.global_storage.at("enhanced.presets.v1")==preset_bytes);
    const auto persisted=local_storage;
    click_text(u8"预设");click_text(u8"读取");assert(ui.stack.size()==2);
    ++storage_info.revision;fire(SAN14_EVENT_SAVE_LOADED);fire(SAN14_EVENT_UI_IDLE);
    assert(ui.stack.size()==1&&local_storage==persisted);
    assert(api.on_disable(&ctx)==SAN14_OK);api.on_unload(&ctx);
    FreeLibrary(dll);
    std::cout<<"Enhanced settings: 102 caps, legacy migration, preset preview and isolation, native UI, rollback and lifecycle passed\n";
}
