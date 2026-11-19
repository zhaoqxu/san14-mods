#include "san14_mod.h"
#include "san14_idle.h"
#include "san14_expert_page.h"
#include "san14_reload.h"
#include "settings_model.h"
#include "pending_settings.h"
#include "preset_dialog.h"
#include "force_color_dialog.h"
#include "officer_traits_dialog.h"
#include "officer_stats_dialog.h"
#include <cstring>
#include <memory>
#include <atomic>

namespace {
using namespace enhanced_settings_mod;
const San14HostApi* host=nullptr;
std::atomic<bool> active{false},sync_needed{true};
bool configured=false,edit_applied=false,merit_applied=false;
bool traits_supported=false,traits_applied=false;
std::array<bool,kCustomCount> supported{};
Preferences defaults,current,draft,archive_defaults;
bool draft_ready=false,preset_current_only=false;
void update_pending(const std::string& message="");
void update_value_marks();
uint64_t revision=0;
std::string playthrough;
std::array<San14Subscription,4> subscriptions{};
std::unique_ptr<san14::ExpertPage> page;
ForceColorDialog colors;
OfficerTraitsDialog traits_editor;
OfficerStatsDialog stats_editor;
PresetDialog preset_dialog;
uint64_t preset_revision=0;
std::string preset_playthrough;
San14PanelHandle dropdown=0;
struct Choice {bool formation;size_t index;};
std::vector<Choice> choices;
size_t city=0,formation=0;
San14StringView sv(const std::string& s){return {s.data(),static_cast<uint32_t>(s.size())};}
San14StringView sv(const char* s){return {s,static_cast<uint32_t>(std::strlen(s))};}
void check(San14Result r){if(r!=SAN14_OK)throw r;}
constexpr const char* preferences_key="enhanced.preferences.v4";
San14Result detect_capabilities(){
    traits_supported=false;
    if(OfficerTraitsDialog::supported(host)){
        San14OfficerTraitsStatus status{sizeof(status)};
        const auto r=host->officer_traits->query(host->host_context,&status);
        if(r!=SAN14_OK&&r!=SAN14_E_UNSUPPORTED)return r;
        traits_supported=r==SAN14_OK&&status.physical_slots>=9;
    }
    supported.fill(false);
    for(size_t i=0;i<kLegacyCustomCount;++i)supported[i]=true;
    if(host->scenario->struct_size<offsetof(San14ScenarioApi,query_custom)+sizeof(host->scenario->query_custom)||!host->scenario->query_custom)return SAN14_OK;
    const auto& defs=custom_defs();
    for(size_t i=kLegacyCustomCount;i<defs.size();++i){
        const auto& d=defs[i];San14ScenarioCustomInfo info{};info.struct_size=sizeof(info);
        supported[i]=host->scenario->query_custom(host->host_context,d.kind,d.group,d.field,&info)==SAN14_OK&&
            info.minimum<=d.min_val&&info.maximum>=d.max_val&&info.standard_value==d.def_val;
    }
    return SAN14_OK;
}
San14Result read_preferences(const char* scope,Preferences& out,bool* migrated=nullptr,const San14SavedDataAccess* access=nullptr){
    if(migrated)*migrated=false;
    const auto read_record=[&](San14StringView key,void* output,uint32_t capacity,uint32_t* required){
        return access?access->read(access->context,sv(scope),key,output,capacity,required):host->storage.read(host->host_context,sv(scope),key,output,capacity,required);
    };
    // Retain other settings when reducing the previously offered hire range.
    const auto limit_hire=[](auto& values){
        bool changed=false;
        for(size_t i=kOfficerHireFirst;i<kCustomCount;++i)
            if(values[i]>100&&values[i]<=100000){values[i]=100;changed=true;}
        return changed;
    };
    Preferences next=out;uint32_t size=0;
    auto result=read_record(sv(preferences_key),&next,sizeof(next),&size);
    if(result==SAN14_OK){
        if(size!=sizeof(next)||!next.valid())return SAN14_E_INVALID_ARGUMENT;
        out=next;return SAN14_OK;
    }
    if(result!=SAN14_E_NOT_FOUND)return result;
    PreferencesV3 v3;
    result=read_record(sv("enhanced.preferences.v3"),&v3,sizeof(v3),&size);
    if(result==SAN14_OK){
        const bool limited=limit_hire(v3.values);
        if(size!=sizeof(v3)||!v3.valid())return SAN14_E_INVALID_ARGUMENT;
        static_cast<PreferencesV3&>(next)=v3;next.infer_selection();
        out=next;if(migrated)*migrated=limited;return SAN14_OK;
    }
    if(result!=SAN14_E_NOT_FOUND)return result;
    PreferencesV2 v2;
    result=read_record(sv("enhanced.preferences.v2"),&v2,sizeof(v2),&size);
    if(result==SAN14_OK){
        limit_hire(v2.values);
        if(size!=sizeof(v2)||!migrate_v2(v2,next))return SAN14_E_INVALID_ARGUMENT;
        next.infer_selection();out=next;if(migrated)*migrated=true;return SAN14_OK;
    }
    if(result!=SAN14_E_NOT_FOUND)return result;
    LegacyPreferences legacy;
    result=read_record(sv("enhanced.preferences.v1"),&legacy,sizeof(legacy),&size);
    if(result!=SAN14_OK)return result;
    if(size!=sizeof(legacy)||!migrate_legacy(legacy,next))return SAN14_E_INVALID_ARGUMENT;
    next.infer_selection();out=next;if(migrated)*migrated=true;return SAN14_OK;
}
San14Result write_preferences(const char* scope,const Preferences& value){
    return host->storage.write_atomic(host->host_context,sv(scope),sv(preferences_key),&value,sizeof(value));
}
San14Result load_archive_defaults(){
    // An existing native file without this Mod's record must not inherit
    // defaults edited later for new games. Keep its original fallback stable.
    archive_defaults=defaults;uint32_t size=0;
    const auto key=sv("enhanced.archive-defaults.v2");
    const auto r=host->storage.read(host->host_context,sv("global"),key,&archive_defaults,sizeof(archive_defaults),&size);
    if(r==SAN14_OK)return size==sizeof(archive_defaults)&&archive_defaults.valid()?SAN14_OK:SAN14_E_INVALID_ARGUMENT;
    if(r!=SAN14_E_NOT_FOUND)return r;
    PreferencesV3 legacy;
    const auto old=host->storage.read(host->host_context,sv("global"),sv("enhanced.archive-defaults.v1"),&legacy,sizeof(legacy),&size);
    if(old==SAN14_OK){
        if(size!=sizeof(legacy)||!legacy.valid())return SAN14_E_INVALID_ARGUMENT;
        static_cast<PreferencesV3&>(archive_defaults)=legacy;archive_defaults.infer_selection();
    }else if(old!=SAN14_E_NOT_FOUND)return old;
    return host->storage.write_atomic(host->host_context,sv("global"),key,&archive_defaults,sizeof(archive_defaults));
}
San14Result apply_traits(bool expanded){
    if(!traits_supported)return expanded?SAN14_E_UNSUPPORTED:SAN14_OK;
    if(traits_applied==expanded)return SAN14_OK;
    const auto r=host->officer_traits->set_expanded(host->host_context,expanded?1:0);
    if(r==SAN14_OK)traits_applied=expanded;
    return r;
}
San14Result apply_rule(const char* key,bool value,bool& applied){
    if(applied==value)return SAN14_OK;
    const auto r=host->rules.set_bool(host->host_context,sv(key),value?1:0);
    if(r==SAN14_OK)applied=value;
    return r;
}
San14Result apply(const Preferences& value,bool whole_batch=false){
    const bool old_edit=edit_applied,old_merit=merit_applied,old_traits=traits_applied;
    auto r=apply_traits(value.extended_traits!=0);if(r!=SAN14_OK)return r;
    r=apply_rule("edit.enhanced",value.enhanced_edit!=0,edit_applied);
    if(r!=SAN14_OK){apply_traits(old_traits);return r;}
    r=apply_rule("officers.merit.extended",value.extended_merit!=0,merit_applied);
    if(r!=SAN14_OK){apply_rule("edit.enhanced",old_edit,edit_applied);apply_traits(old_traits);return r;}
    const auto rollback=[&](San14Result result){
        if(result!=SAN14_OK){apply_rule("officers.merit.extended",old_merit,merit_applied);apply_rule("edit.enhanced",old_edit,edit_applied);apply_traits(old_traits);}
        return result;
    };
    const auto& defs=custom_defs();size_t changed=0,index=0;
    for(size_t i=0;i<defs.size();++i)if(supported[i]&&value.values[i]!=current.values[i]){++changed;index=i;}
    if(configured&&!whole_batch&&changed<=1){
        return rollback(changed?host->scenario->write_custom(host->host_context,sv(defs[index].key),value.values[index]):SAN14_OK);
    }
    // configure_custom is an atomic owner-batch replacement. Publish restored
    // values as its initial values, never clamp a new world against defaults
    // or a partially restored mixture of caps from two saves.
    std::vector<San14ScenarioCustomSpec> specs;
    for(size_t i=0;i<defs.size();++i){if(!supported[i])continue;const auto& d=defs[i];specs.push_back({sizeof(San14ScenarioCustomSpec),sv(d.key),sv(d.label),sv(d.help),d.kind,d.group,d.field,d.min_val,d.max_val,d.step,value.values[i]});}
    const auto result=host->scenario->configure_custom(host->host_context,specs.data(),static_cast<uint32_t>(specs.size()));
    if(result==SAN14_OK)configured=true;
    return rollback(result);
}
void update_rows();
San14Result sync(){
    San14SaveStorageInfo info{};info.struct_size=sizeof(info);
    const auto r=host->save_storage->context(host->host_context,&info);if(r!=SAN14_OK)return r;
    if(!sync_needed&&revision==info.revision&&playthrough==info.playthrough_id)return SAN14_OK;
    const bool switched=!draft_ready||revision!=info.revision||playthrough!=info.playthrough_id;
    Preferences next=info.state==SAN14_STORAGE_READY&&info.save_basename[0]?archive_defaults:defaults;
    if(info.state==SAN14_STORAGE_ERROR)return SAN14_E_BUSY;
    if(info.state==SAN14_STORAGE_LOADING)return SAN14_E_BUSY;
    if(info.state==SAN14_STORAGE_READY){
        bool migrated=false;const auto read=read_preferences("playthrough",next,&migrated);
        if(read!=SAN14_OK&&read!=SAN14_E_NOT_FOUND)return read;
        // Engine support must be active before the native load worker. A save's
        // preference cannot safely switch this after its extra IDs were read.
        next.extended_traits=defaults.extended_traits;
        if(read==SAN14_E_NOT_FOUND||migrated){const auto written=write_preferences("playthrough",next);if(written!=SAN14_OK)return written;}
    }
    const bool values_changed=std::memcmp(&next,&current,sizeof(next))!=0;
    const auto applied=switched||values_changed?apply(next,true):SAN14_OK;if(applied!=SAN14_OK)return applied;
    current=next;if(switched){preset_dialog.close();draft=next;draft_ready=true;preset_current_only=false;}revision=info.revision;playthrough=info.state==SAN14_STORAGE_READY?info.playthrough_id:"";sync_needed=false;
    if(page){if(switched||values_changed){traits_editor.reset();stats_editor.reset();}update_rows();page->refresh();update_pending();}
    return SAN14_OK;
}
void close_dropdown(){if(dropdown)host->ui.close_panel(host->host_context,dropdown);dropdown=0;choices.clear();}
void update_rows(){
    if(!page)return;
    const auto& defs=custom_defs();
    page->update_bool("edit",draft.enhanced_edit!=0);
    page->update_bool("merit",draft.extended_merit!=0);
    if(traits_supported)page->update_bool("traits",defaults.extended_traits!=0);
    page->update_range("stat",draft.values[20],100,defs[20].help);
    page->action_caption("city.select",std::string(city_names[city])+u8" ▼");
    page->action_caption("formation.select",std::to_string(formation+1)+" "+formation_names[formation]+u8" ▼");
    for(size_t f=0;f<4;++f){const auto i=city*4+f;page->update_range("city."+std::to_string(f),draft.values[i],defs[i].def_val,defs[i].help);}
    for(size_t f=0;f<5;++f){const auto i=21+formation*5+f;page->update_range("formation."+std::to_string(f),draft.values[i],defs[i].def_val,defs[i].help);}
    for(size_t i=kLegacyCustomCount;i<defs.size();++i)if(supported[i])page->update_range(defs[i].key,draft.values[i],defs[i].def_val,defs[i].help);
    update_value_marks();
}
void update_value_marks(){
    if(!page)return;
    page->set_range_changed("stat",draft.values[20]!=current.values[20]);
    for(size_t f=0;f<4;++f)page->set_range_changed("city."+std::to_string(f),draft.values[city*4+f]!=current.values[city*4+f]);
    for(size_t f=0;f<5;++f)page->set_range_changed("formation."+std::to_string(f),draft.values[21+formation*5+f]!=current.values[21+formation*5+f]);
    const auto& defs=custom_defs();
    for(size_t i=kLegacyCustomCount;i<defs.size();++i)if(supported[i])page->set_range_changed(defs[i].key,draft.values[i]!=current.values[i]);
}
bool saved_edit_supported(){
    return host&&host->save_storage&&host->save_storage->struct_size>=offsetof(San14SaveStorageApi,edit_saved)+sizeof(host->save_storage->edit_saved)&&host->save_storage->edit_saved;
}
void update_pending(const std::string& message){
    if(!page)return;const auto count=PendingSettings(current,draft).count();
    if(!count)preset_current_only=false;
    update_value_marks();
    const auto pending=preset_current_only?u8" 项未保存变更。保存变更仅应用到当前档。":u8" 项未保存变更，尚未生效；返回将放弃。";
    page->footer_status(message.empty()?(count?std::string(u8"有 ")+std::to_string(count)+pending:u8"调整后点击保存变更；需同步其他已有存档时，选择保存变更到全部存档。") : message,count&&saved_edit_supported());
}
San14Result commit(const Preferences& next){
    if(!active)return SAN14_E_BUSY;if(!next.valid())return SAN14_E_INVALID_ARGUMENT;
    draft=next;draft_ready=true;update_pending();return SAN14_OK;
}
San14Result preview_preset(const Preferences& source){
    auto result=sync();
    if(result!=SAN14_OK||revision!=preset_revision||playthrough!=preset_playthrough){
        update_pending(u8"存档忙碌或已切换，请重新读取设定。");
        return result==SAN14_OK?SAN14_E_STALE_GENERATION:result;
    }
    San14SaveStorageInfo info{sizeof(info)};
    result=host->save_storage->context(host->host_context,&info);if(result!=SAN14_OK)return result;
    if(info.revision!=preset_revision){update_pending(u8"存档已切换，请重新读取设定。");return SAN14_E_STALE_GENERATION;}
    auto next=draft;
    for(size_t i=0;i<kCustomCount;++i)if(supported[i]&&source.selected.has(i))next.values[i]=source.values[i];
    if(source.selected.has(SettingFields::edit))next.enhanced_edit=source.enhanced_edit;
    if(source.selected.has(SettingFields::merit))next.extended_merit=source.extended_merit;
    preset_current_only=info.state==SAN14_STORAGE_READY;
    result=commit(next);if(result!=SAN14_OK)return result;
    update_rows();page->refresh();
    if(!PendingSettings(current,draft).count())update_pending(u8"这套设定与当前配置一致。");
    return SAN14_OK;
}
San14Result open_presets(bool reading){
    const auto result=sync();if(result!=SAN14_OK)return result;
    preset_revision=revision;preset_playthrough=playthrough;
    if(reading)return preset_dialog.open(host,defaults,preview_preset);
    const auto snapshot=PendingSettings(current,draft).merge(draft);
    if(std::none_of(snapshot.selected.words.begin(),snapshot.selected.words.end(),[](uint32_t word){return word!=0;})){
        update_pending(u8"请先调整至少一项设定，再保存为预设。");return SAN14_OK;
    }
    return preset_dialog.open(host,snapshot,nullptr);
}
struct EditTransform {PendingSettings patch;Preferences fallback;};
San14Result SAN14_MOD_CALL transform_saved(void* context,const San14SavedDataAccess* access){
    const auto& edit=*static_cast<EditTransform*>(context);Preferences next=edit.fallback;
    auto r=read_preferences("playthrough",next,nullptr,access);if(r!=SAN14_OK&&r!=SAN14_E_NOT_FOUND)return r;
    next=edit.patch.merge(next);if(!next.valid())return SAN14_E_INVALID_ARGUMENT;
    return access->write(access->context,sv("playthrough"),sv(preferences_key),&next,sizeof(next));
}
San14Result save_pending(uint32_t target){
    auto r=sync();if(r!=SAN14_OK){update_pending(u8"存档忙碌或已切换，请稍后重新编辑。");return r;}
    if(!saved_edit_supported()){update_pending(u8"当前管理器不支持直接保存设置，请更新管理器。");return SAN14_E_UNSUPPORTED;}
    const PendingSettings patch(current,draft);if(!patch.count())return SAN14_OK;
    San14SaveStorageInfo info{sizeof(info)};r=host->save_storage->context(host->host_context,&info);if(r!=SAN14_OK)return r;
    if(info.revision!=revision){update_pending(u8"存档已切换，请重新编辑。");return SAN14_E_STALE_GENERATION;}
    const bool current_file=info.state==SAN14_STORAGE_READY&&info.save_basename[0];
    const bool current_only=false;
    const auto old_defaults=defaults;
    const auto next_defaults=current_only?defaults:patch.merge(defaults);
    if(!current_only){
        r=write_preferences("global",next_defaults);if(r!=SAN14_OK){update_pending(u8"默认设置写入失败，变更仍未提交。");return r;}
    }
    const auto next=patch.merge(current);
    EditTransform edit{patch,archive_defaults};San14SavedEditResult report{sizeof(report)};
    r=host->save_storage->edit_saved(host->host_context,target,info.revision,transform_saved,&edit,&report);
    // A current-target NOT_FOUND without a filename is the accepted no-save
    // case. Still query the Host first so worker, pending-save and stale
    // revision guards run before applying new-game defaults/live parameters.
    if(r==SAN14_E_NOT_FOUND&&target==SAN14_SAVED_EDIT_CURRENT&&!current_file)
        r=info.state==SAN14_STORAGE_READY?write_preferences("playthrough",next):SAN14_OK;
    if(r!=SAN14_OK){
        const bool default_failed=!current_only&&write_preferences("global",old_defaults)!=SAN14_OK;
        update_pending(report.rollback_failed||default_failed?u8"保存失败，部分数据未能回退，请保留附加文件备份并核对设置。":u8"保存失败，原有设置已保留，待提交变更仍在。请检查存档与附加文件。");return r;
    }
    defaults=next_defaults;
    r=apply(next,true);current=next;draft=next;preset_current_only=false;
    sync_needed=true;update_rows();page->refresh();
    if(r!=SAN14_OK){update_pending(current_only||current_file||target==SAN14_SAVED_EDIT_ALL?u8"设置已保存，当前局应用失败，请重新读取存档。":u8"新局默认已保存，当前应用失败，请重新进入或读取存档。");return r;}
    update_pending(current_only?u8"已应用到当前战局和存档（如有）。":target==SAN14_SAVED_EDIT_CURRENT?u8"已保存变更：当前存档（如有）及新局默认已更新，其他已有存档不变。":std::string(u8"已保存变更到 ")+std::to_string(report.matched_files)+u8" 个存档并应用，已更新新局默认。");return SAN14_OK;
}
San14Result change_value(size_t index,int32_t value){
    if(index>=kCustomCount||!supported[index])return SAN14_E_UNSUPPORTED;
    if(value<custom_defs()[index].min_val||value>custom_defs()[index].max_val)return SAN14_E_INVALID_ARGUMENT;
    auto r=sync();if(r!=SAN14_OK)return r;
    auto next=draft;next.values[index]=value;return commit(next);
}
San14Result SAN14_MOD_CALL change_merit(void*,San14StringView,int32_t value){
    if(value!=0&&value!=1)return SAN14_E_INVALID_ARGUMENT;
    auto r=sync();if(r!=SAN14_OK)return r;
    auto next=draft;next.extended_merit=value;return commit(next);
}
San14Result SAN14_MOD_CALL change_traits(void*,San14StringView,int32_t value){
    return value==0||value==1?SAN14_E_UNSUPPORTED:SAN14_E_INVALID_ARGUMENT;
}
San14Result SAN14_MOD_CALL change_edit(void*,San14StringView,int32_t value){
    if(value!=0&&value!=1)return SAN14_E_INVALID_ARGUMENT;
    auto r=sync();if(r!=SAN14_OK)return r;
    auto next=draft;next.enhanced_edit=value;return commit(next);
}
void SAN14_MOD_CALL select_choice(void* context,San14ControlHandle){
    const auto choice=*static_cast<Choice*>(context);
    if(choice.index!=SIZE_MAX){if(choice.formation)formation=choice.index;else city=choice.index;}
    close_dropdown();update_rows();if(page)page->refresh();
}
San14Result open_dropdown(bool is_formation){
    close_dropdown();
    const size_t count=is_formation?14:5;const int columns=is_formation?2:1,rows=int((count+columns-1)/columns);
    const int width=columns*270+40,height=92+rows*42;
    San14PanelSpec spec{sizeof(spec),sv(is_formation?u8"选择阵型":u8"选择城市档位"),width,height,SAN14_PANEL_CUSTOM_POSITION|SAN14_PANEL_POPUP,0,1160,222};
    auto fail=[&](const char* stage,San14Result result){
        if(host->log.write){const auto message=std::string(is_formation?"formation":"city")+" dropdown "+stage+" failed: "+std::to_string(result);
            host->log.write(host->host_context,SAN14_LOG_ERROR,sv("settings.ui"),sv(message));}
        close_dropdown();return result;
    };
    auto result=host->ui.create_panel(host->host_context,&spec,&dropdown);
    if(result==SAN14_E_UNSUPPORTED){spec.flags&=~SAN14_PANEL_POPUP;result=host->ui.create_panel(host->host_context,&spec,&dropdown);}
    if(result!=SAN14_OK)return fail("create",result);
    choices.reserve(count+1);
    for(size_t i=0;i<=count;++i){
        choices.push_back({is_formation,i==count?SIZE_MAX:i});
        const auto text=i==count?std::string(u8"取消"):(is_formation?std::to_string(i+1)+" "+formation_names[i]:city_names[i]);
        const auto id="select."+std::to_string(i);
        const int x=i==count?20:20+int(i/rows)*270,y=i==count?height-42:42+int(i%rows)*42;
        const uint32_t flags=i<count?SAN14_CONTROL_TAB|(i==(is_formation?formation:city)?SAN14_CONTROL_SELECTED:0u):0u;
        San14ControlSpec cs{sizeof(cs),sv(id),sv(text),x,y,250,36,flags};
        San14ControlHandle control=0;
        result=host->ui.add_button(host->host_context,dropdown,&cs,select_choice,&choices.back(),&control);
        if(result!=SAN14_OK)return fail("add choice",result);
    }
    result=host->ui.show_panel(host->host_context,dropdown);return result==SAN14_OK?result:fail("show",result);
}
San14Result restore(bool is_formation,bool all){
    auto r=sync();if(r!=SAN14_OK)return r;
    auto next=draft;const auto& defs=custom_defs();
    const size_t first=is_formation?21+(all?0:formation*5):(all?0:city*4);
    const size_t count=is_formation?(all?70:5):(all?20:4);
    for(size_t i=first;i<first+count;++i)next.values[i]=defs[i].def_val;
    r=commit(next);if(r==SAN14_OK){update_rows();page->refresh();}return r;
}
San14Result restore_tiers(size_t first,size_t count){
    auto r=sync();if(r!=SAN14_OK)return r;
    auto next=draft;const auto& defs=custom_defs();
    for(size_t i=first;i<first+count;++i)if(supported[i])next.values[i]=defs[i].def_val;
    r=commit(next);if(r==SAN14_OK){update_rows();page->refresh();}return r;
}
San14Result SAN14_MOD_CALL open_page(void*){
    if(!active)return SAN14_E_BUSY;
    auto r=sync();if(r!=SAN14_OK)return r;
    close_dropdown();preset_dialog.close();traits_editor.reset();stats_editor.reset();if(page)page->close();draft=current;preset_current_only=false;
    try{
        page=std::make_unique<san14::ExpertPage>(host,u8"增强设定与编辑");
        page->set_footer_actions({{u8"保存变更",[]{return save_pending(SAN14_SAVED_EDIT_CURRENT);}},
            {u8"保存变更到全部存档",[]{return save_pending(SAN14_SAVED_EDIT_ALL);}}});
        page->footer_hint(u8"保存变更更新当前档和新局默认，其他已有存档不变。\n读取预设进入草稿，保存后同时更新当前档及新局默认。");
        page->add_bool(u8"常规","edit",u8"城市编辑支持异族势力",u8"在原生编辑中解锁异族城市 47–51 与异族势力 46–50。",current.enhanced_edit!=0,[](int32_t v){return change_edit(nullptr,{},v);});
        page->add_bool(u8"常规","merit",u8"大幅提高武将功劳上限",u8"功劳上限由 12000 提高至 32767，默认关闭。修改后选择保存范围才生效。",current.extended_merit!=0,[](int32_t v){return change_merit(nullptr,{},v);});
        const auto& defs=custom_defs();
        page->add_slider(u8"常规","stat",defs[20].label,defs[20].help,current.values[20],1,255,1,1,"",[](int32_t v){return change_value(20,v);});
        page->add_action(u8"常规","colors",u8"更改势力颜色（载入存档后）",u8"载入存档后，打开当前战局的势力颜色编辑器，选择常规或异族势力；支持占色检查和撤销。",[]{return colors.open();});
        if(OfficerStatsDialog::supported(host))page->add_action(u8"常规","officer.stats",u8"编辑武将基础能力（载入存档后）",u8"选择在城且未出阵武将，编辑统率、武力、智力、政治、魅力，范围1–100；同步该项经验，支持撤销。修改后请正常保存游戏。",[]{return stats_editor.open();});
        else page->add_unavailable(u8"常规","officer.stats",u8"编辑武将基础能力（当前版本不支持）",u8"需要提供武将基础能力编辑服务的新版本管理器。");
        page->add_unavailable(u8"常规","traits",u8"扩展武将有效个性至9个（实现中）",u8"功能实现中，暂不可操作。");
        page->add_unavailable(u8"常规","traits.edit",u8"编辑武将额外个性（实现中）",u8"功能实现中，暂不可操作。");
        page->add_action(u8"城市","city.select",u8"城市档位",u8"选择小城、中城、大城、巨城或异族城池，下方四组数值随选择联动。",[]{return open_dropdown(false);});
        const char* cap_labels[]={u8"资金上限",u8"兵粮上限",u8"士兵上限",u8"耐久上限"};
        for(size_t f=0;f<4;++f){const auto& d=defs[city*4+f];page->add_slider(u8"城市","city."+std::to_string(f),cap_labels[f],d.help,current.values[city*4+f],d.min_val,d.max_val,1,1,"",[f](int32_t v){return change_value(city*4+f,v);});}
        page->add_action(u8"城市","city.reset",u8"恢复当前档位标准",u8"仅恢复当前选中的城市档位，其他档位保持现值。",[]{return restore(false,false);},u8"恢复当前档位");
        page->add_action(u8"城市","city.reset_all",u8"恢复全部城市标准",u8"恢复五个城市档位的全部 20 项标准值。",[]{return restore(false,true);},u8"恢复全部城市");
        page->add_action(u8"阵型","formation.select",u8"阵型",u8"选择 14 种阵型之一，下方五组数值随选择联动。",[]{return open_dropdown(true);});
        const char* form_labels[]={u8"机动",u8"对部队",u8"对城",u8"破城",u8"防御"};
        for(size_t f=0;f<5;++f){const auto& d=defs[21+formation*5+f];page->add_slider(u8"阵型","formation."+std::to_string(f),form_labels[f],d.help,current.values[21+formation*5+f],0,255,1,1,"",[f](int32_t v){return change_value(21+formation*5+f,v);});}
        page->add_action(u8"阵型","formation.reset",u8"恢复当前阵型标准",u8"仅恢复当前选中的阵型。",[]{return restore(true,false);},u8"恢复当前阵型");
        for(size_t i=kMilitaryIntelFirst;i<kOfficerHireFirst;++i)if(supported[i]){
            const auto& d=defs[i];page->add_slider(u8"军情",d.key,military_intel_names[i-kMilitaryIntelFirst],d.help,current.values[i],d.min_val,d.max_val,1,1,"",[i](int32_t v){return change_value(i,v);});
        }
        if(std::any_of(supported.begin()+kMilitaryIntelFirst,supported.begin()+kOfficerHireFirst,[](bool v){return v;}))
            page->add_action(u8"军情","military.reset",u8"恢复军情上限标准",u8"恢复四个官爵档位的军情上限。",[]{return restore_tiers(kMilitaryIntelFirst,4);},u8"恢复全部军情");
        for(size_t i=kOfficerHireFirst;i<kCustomCount;++i)if(supported[i]){
            const auto& d=defs[i];page->add_slider(u8"雇用",d.key,officer_hire_names[i-kOfficerHireFirst],d.help,current.values[i],d.min_val,d.max_val,1,1,"",[i](int32_t v){return change_value(i,v);});
        }
        if(std::any_of(supported.begin()+kOfficerHireFirst,supported.end(),[](bool v){return v;}))
            page->add_action(u8"雇用","hire.reset",u8"恢复雇用上限标准",u8"恢复七个官爵档位的雇用上限。",[]{return restore_tiers(kOfficerHireFirst,7);},u8"恢复全部雇用");
        page->add_action(u8"预设","presets.read",u8"读取设定",u8"载入新局默认或命名预设，预览差异后再保存。",[]{return open_presets(true);},u8"读取");
        page->add_action(u8"预设","presets.save",u8"保存为预设",u8"给当前明确配置的参数和草稿命名，可在其他存档读取。",[]{return open_presets(false);},u8"保存为预设");
        update_rows();update_pending();return page->show();
    }catch(San14Result e){return e;}catch(...){return SAN14_E_PLUGIN_FAILED;}
}
void SAN14_MOD_CALL event(void*,const San14EventView* view){
    if(!active||!view)return;
    if(view->event_id!=SAN14_EVENT_UI_IDLE){sync_needed=true;return;}
    // Storage events may also be delivered in a Host data scope on a worker.
    // Only UI_IDLE touches scenario/UI services.
    sync();
}
San14Result SAN14_MOD_CALL load(San14ModContext*){
    const San14BoolSetting setting{sizeof(setting),sv("enhanced_edit_mode"),sv(u8"城市编辑支持异族势力"),sv(u8"在原生编辑中解锁异族城市与势力。"),0,change_edit,nullptr,sv(u8"常规")};
    const auto result=host->settings.register_bool(host->host_context,&setting);if(result!=SAN14_OK)return result;
    const San14BoolSetting merit{sizeof(merit),sv("raise_officer_merit_cap"),sv(u8"大幅提高武将功劳上限"),sv(u8"功劳上限由 12000 提高至 32767。"),0,change_merit,nullptr,sv(u8"常规")};
    const auto registered=host->settings.register_bool(host->host_context,&merit);if(registered!=SAN14_OK)return registered;
    if(OfficerTraitsDialog::supported(host)){
        const San14BoolSetting traits{sizeof(traits),sv("expanded_officer_traits"),sv(u8"扩展武将有效个性至9个（实现中）"),sv(u8"功能实现中，暂不可操作。"),0,change_traits,nullptr,sv(u8"常规"),SAN14_SETTING_DISABLED};
        const auto r=host->settings.register_bool(host->host_context,&traits);if(r!=SAN14_OK)return r;
    }
    const San14SettingsPageSpec spec{sizeof(spec),open_page,nullptr};
    return host->pages->register_page(host->host_context,&spec);
}
San14Result SAN14_MOD_CALL disable(San14ModContext*);
San14Result SAN14_MOD_CALL enable(San14ModContext* context){
    try{
        draft_ready=false;defaults=Preferences{};bool migrated=false;const auto read=read_preferences("global",defaults,&migrated);
        if(read!=SAN14_OK&&read!=SAN14_E_NOT_FOUND)return read;
        if(migrated)check(write_preferences("global",defaults));
        check(load_archive_defaults());
        check(detect_capabilities());
        // A requested nine-slot mode must never silently fall back to five:
        // native loading can erase the saved extra IDs before UI restoration.
        if(defaults.extended_traits&&!traits_supported)return SAN14_E_UNSUPPORTED;
        active=true;sync_needed=true;
        colors.bind(host);traits_editor.bind(host);stats_editor.bind(host);check(sync());
        const uint32_t events[]={SAN14_EVENT_STORAGE_CHANGED,SAN14_EVENT_NEW_GAME,SAN14_EVENT_SAVE_LOADED,SAN14_EVENT_UI_IDLE};
        for(size_t i=0;i<subscriptions.size();++i)check(host->events.subscribe(host->host_context,events[i],0,event,nullptr,&subscriptions[i]));
        return SAN14_OK;
    }catch(San14Result e){const auto cleaned=disable(context);return cleaned==SAN14_OK?e:cleaned;}
    catch(...){const auto cleaned=disable(context);return cleaned==SAN14_OK?SAN14_E_PLUGIN_FAILED:cleaned;}
}
San14Result SAN14_MOD_CALL disable(San14ModContext*){
    // Refuse a partial disable while nine-slot mode protects a loaded save.
    // Native five-slot recalculation can erase the extra IDs.
    const auto released=apply_traits(false);if(released!=SAN14_OK)return released;
    if(preset_dialog.close()!=SAN14_OK)return SAN14_E_BUSY;
    close_dropdown();colors.reset();traits_editor.reset();stats_editor.reset();if(page)page->close();
    bool busy=false;
    for(auto& sub:subscriptions)if(sub){const auto r=host->events.unsubscribe(host->host_context,sub);if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)sub=0;else busy=true;}
    if(edit_applied){if(host->rules.set_bool(host->host_context,sv("edit.enhanced"),0)==SAN14_OK)edit_applied=false;else busy=true;}
    if(merit_applied){if(host->rules.set_bool(host->host_context,sv("officers.merit.extended"),0)==SAN14_OK)merit_applied=false;else busy=true;}
    if(configured){if(host->scenario->configure_custom(host->host_context,nullptr,0)==SAN14_OK)configured=false;else busy=true;}
    if(busy)return SAN14_E_BUSY;
    active=false;revision=0;playthrough.clear();return SAN14_OK;
}
void SAN14_MOD_CALL unload(San14ModContext*){preset_dialog.close();page.reset();colors.reset();traits_editor.reset();stats_editor.reset();host=nullptr;active=false;traits_supported=false;traits_applied=false;}
}
extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(const San14HostApi* h,San14ModApi* out){
    if(!h||!out||out->struct_size<sizeof(*out)||h->abi_version!=SAN14_HOST_ABI_V1||
       h->struct_size<offsetof(San14HostApi,ui_slider)+sizeof(h->ui_slider)||
       !san14::ExpertPage::supported(h)||!ForceColorDialog::supported(h)||
       !h->scenario||h->scenario->struct_size<offsetof(San14ScenarioApi,write_custom)+sizeof(h->scenario->write_custom)||!h->scenario->configure_custom||!h->scenario->read_custom||!h->scenario->write_custom||
       !h->pages||h->pages->struct_size<sizeof(San14PagesApi)||!h->pages->register_page||
       !h->save_storage||h->save_storage->struct_size<offsetof(San14SaveStorageApi,erase)||!h->save_storage->context||
       h->storage.struct_size<sizeof(San14StorageApi)||!h->storage.read||!h->storage.write_atomic||
       h->events.struct_size<sizeof(San14EventApi)||!h->events.subscribe||!h->events.unsubscribe||
       h->rules.struct_size<sizeof(San14RuleApi)||!h->rules.set_bool||h->settings.struct_size<sizeof(San14SettingsApi)||!h->settings.register_bool||
       !h->ui_slider||h->ui_slider->struct_size<sizeof(San14UiSliderApi)||!h->ui_slider->add_slider||!h->ui_slider->set_slider_value||
       h->ui_extended.struct_size<offsetof(San14UiExtendedApi,add_text_input)+sizeof(h->ui_extended.add_text_input)||!h->ui_extended.add_text_input)return SAN14_E_UNSUPPORTED;
    if(active)return SAN14_E_BUSY;
    host=h;*out={sizeof(*out),SAN14_HOST_ABI_V1,load,enable,disable,unload};return SAN14_OK;
}

// All callbacks are Host-owned; no threads, hooks or helper DLLs. Reasserting
// the committed batch preflights native scenario editors without accepting a
// pending UI draft. Host additionally requires the true title menu.
extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_PrepareReloadV1(San14ModContext*){
    try{return !host||!active?SAN14_OK:apply(current,true);}
    catch(...){return SAN14_E_PLUGIN_FAILED;}
}
