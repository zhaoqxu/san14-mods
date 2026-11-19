#include "san14_mod.h"
#include "san14_domestic.h"
#include "save_stats_core.h"
#include <array>
#include <cstddef>
#include <limits>
#include <algorithm>

namespace {
const San14HostApi* host=nullptr;
bool active=false,tab_registered=false,have_context=false,baseline_valid=false;
bool domestic_registered=false,domestic_recording_enabled=true,domestic_lease=false;
using OutcomeRow=std::array<int32_t,8>;
std::map<uint32_t,OutcomeRow> domestic_values,battle_values;
bool battle_registered=false,battle_enabled=true,records_lease=false;
bool history_lease=false,history_action=false;
uint32_t records_caps=0;
constexpr const char* domestic_key="domestic.recording";
constexpr const char* battle_key="battle.recording";
uint64_t revision=0,activation_revision=0;
uint32_t storage_state=SAN14_STORAGE_UNAVAILABLE;
std::string playthrough;
std::array<bool,5> visible{{true,true,true,true,true}};
std::array<San14Subscription,2> subscriptions{};
save_stats::Roster baseline;
std::map<uint32_t,save_stats::Growth> values;
constexpr const char* dead_officers_key="general.show_dead_officers";
constexpr const char* deeds_key="general.officer_deeds";
uint32_t history_choices=SAN14_HISTORY_RECORD_ALL;
struct HistoryOption {const char* key;const char* label;const char* description;uint32_t bit;};
constexpr HistoryOption history_options[]={
    {"history.duel_win",u8"单挑获胜",u8"记录武将在单挑中获胜；确认斩杀对手时附记结果。",SAN14_HISTORY_RECORD_DUEL_WIN},
    {"history.duel_loss",u8"单挑战败",u8"记录武将在单挑中战败及已确认的阵亡结果。",SAN14_HISTORY_RECORD_DUEL_LOSS},
    {"history.duel_draw",u8"单挑平局",u8"记录单挑未分胜负。",SAN14_HISTORY_RECORD_DUEL_DRAW},
    {"history.unit_win",u8"消灭敌军",u8"记录主将部队消灭敌队，并附记同次结算中确认的俘虏或斩杀。",SAN14_HISTORY_RECORD_UNIT_WIN},
    {"history.unit_loss",u8"本军覆灭",u8"记录所率部队被消灭，以及同次结算中确认的被俘或阵亡。",SAN14_HISTORY_RECORD_UNIT_LOSS},
    {"history.city",u8"攻克城池",u8"记录主将部队先登城墙、攻下敌方城池。",SAN14_HISTORY_RECORD_CITY},
    {"history.capture",u8"俘虏敌将",u8"记录俘虏其他武将。",SAN14_HISTORY_RECORD_CAPTURE},
    {"history.captured",u8"被俘",u8"记录武将被敌方俘虏。",SAN14_HISTORY_RECORD_CAPTURED},
    {"history.employ",u8"擢用他人",u8"记录执行者实际成功擢用其他武将。",SAN14_HISTORY_RECORD_EMPLOY},
    {"history.employed",u8"被擢用",u8"记录武将经他人擢用而加入势力。",SAN14_HISTORY_RECORD_EMPLOYED},
    {"history.diplomacy",u8"外交成功",u8"记录成功的外交交涉。",SAN14_HISTORY_RECORD_DIPLOMACY},
    {"history.post",u8"官职变动",u8"记录授官、免官及按官职品级判定的升任、降任、平调。",SAN14_HISTORY_RECORD_POST},
    {"history.force",u8"所属势力变动",u8"记录武将实际加入、离开或转投势力；同势力内调军团不记录。",SAN14_HISTORY_RECORD_FORCE},
    {"history.status",u8"其他身份变动",u8"记录君主、都督、在野等身份变化；不记录是否担任太守，死亡由单独开关控制。",SAN14_HISTORY_RECORD_STATUS},
    {"history.death",u8"死亡",u8"记录实际死亡；普通逝世记录病逝与享年，战斗或处决使用已确认的原因。",SAN14_HISTORY_RECORD_DEATH},
    {"history.item_gain",u8"获得名品",u8"记录获得名品；原生赏赐命令记录赏赐者，君主保留“获得名品”。",SAN14_HISTORY_RECORD_ITEM_GAIN},
    {"history.item_loss",u8"失去名品",u8"记录名品易主；原生没收命令记录没收方势力与武将。",SAN14_HISTORY_RECORD_ITEM_LOSS}
};
constexpr const char* keys[]={"growth.leadership","growth.martial","growth.intelligence","growth.politics","growth.charisma"};
constexpr const char* labels[]={u8"统率",u8"武力",u8"智力",u8"政治",u8"魅力"};
San14StringView text(const char* s){return {s,static_cast<uint32_t>(std::strlen(s))};}
void log(const char* s){if(host&&host->log.write)host->log.write(host->host_context,SAN14_LOG_WARNING,text("save-stats"),text(s));}
bool history_supported(){return host&&host->struct_size>=offsetof(San14HostApi,officer_history)+sizeof(host->officer_history)&&host->officer_history&&host->officer_history->struct_size>=SAN14_OFFICER_HISTORY_API_V1_SIZE&&host->officer_history->set_recording&&host->officer_history->query&&host->officer_history->show&&host->officer_history->set_detail_action;}
bool history_filters_supported(){return history_supported()&&host->officer_history->struct_size>=SAN14_OFFICER_HISTORY_API_V2_SIZE&&host->officer_history->set_event_mask;}
San14Result apply_history(bool enabled){
    if(!history_supported())return enabled?SAN14_E_UNSUPPORTED:SAN14_OK;
    const bool before=history_lease;
    if(enabled&&history_filters_supported()){const auto r=host->officer_history->set_event_mask(host->host_context,history_choices);if(r!=SAN14_OK)return r;}
    auto r=host->officer_history->set_recording(host->host_context,enabled?1:0);if(r!=SAN14_OK)return r;history_lease=enabled;
    r=host->officer_history->set_detail_action(host->host_context,enabled?1:0);
    if(r!=SAN14_OK){if(before&&history_filters_supported())host->officer_history->set_event_mask(host->host_context,history_choices);if(host->officer_history->set_recording(host->host_context,before?1:0)==SAN14_OK)history_lease=before;return r;}
    history_action=enabled;return SAN14_OK;
}
void clear_values(){values.clear();}
void discard(){baseline.clear();baseline_valid=false;clear_values();domestic_values.clear();battle_values.clear();}
void reset_context(){discard();have_context=false;revision=0;storage_state=SAN14_STORAGE_UNAVAILABLE;playthrough.clear();}
uint32_t mask(){uint32_t result=1;for(unsigned i=0;i<5;++i)if(visible[i])result|=1u<<(i+1);return result;}
San14Result context(San14SaveStorageInfo& out){out={};out.struct_size=sizeof(out);return host->save_storage->context(host->host_context,&out);}
bool same_context(const San14SaveStorageInfo& info){
    return have_context&&revision==info.revision&&storage_state==info.state&&playthrough==save_stats::fixed(info.playthrough_id);
}
void load_baseline(){
    discard();if(storage_state!=SAN14_STORAGE_READY)return;
    uint32_t required=0;auto result=host->storage.read(host->host_context,text("playthrough"),text("officer-growth-v1"),nullptr,0,&required);
    if(result==SAN14_E_NOT_FOUND)return;
    if(result!=SAN14_E_BUFFER_TOO_SMALL||required<8||required>save_stats::max_payload){log("Baseline unavailable or unsupported; no data was overwritten.");return;}
    std::vector<unsigned char> bytes(required);uint32_t actual=0;
    result=host->storage.read(host->host_context,text("playthrough"),text("officer-growth-v1"),bytes.data(),required,&actual);
    if(result!=SAN14_OK||actual!=required){log("Baseline read failed; no data was overwritten.");return;}
    try{auto loaded=save_stats::decode(bytes);baseline=std::move(loaded);baseline_valid=true;}
    catch(...){log("Baseline format validation failed; no data was overwritten.");}
}
bool sync_context(bool reload=false){
    San14SaveStorageInfo info{};if(context(info)!=SAN14_OK){reset_context();return false;}
    if(!same_context(info)){discard();have_context=true;revision=info.revision;storage_state=info.state;playthrough=save_stats::fixed(info.playthrough_id);reload=true;}
    if(reload)load_baseline();return storage_state==SAN14_STORAGE_READY;
}
save_stats::Roster capture(){
    San14RosterInfo info{};info.struct_size=sizeof(info);uint32_t needed=0;
    auto result=host->roster.capture(host->host_context,&info,nullptr,sizeof(San14RosterOfficer),0,&needed);
    if((result!=SAN14_E_BUFFER_TOO_SMALL&&result!=SAN14_OK)||!needed||needed>save_stats::max_officers)throw std::runtime_error("Cannot size complete officer roster");
    std::vector<San14RosterOfficer> rows(needed);info={};info.struct_size=sizeof(info);uint32_t actual=0;
    result=host->roster.capture(host->host_context,&info,rows.data(),sizeof(San14RosterOfficer),needed,&actual);
    if(result!=SAN14_OK||!actual||actual>needed||info.officer_count!=actual)throw std::runtime_error("Cannot capture complete officer roster");
    rows.resize(actual);return save_stats::roster(rows);
}
San14Result SAN14_MOD_CALL refresh(void*){
    clear_values();if(!active)return SAN14_E_PERMISSION_DENIED;
    try{
        if(!sync_context()||!baseline_valid)return SAN14_OK;
        auto current=capture();San14SaveStorageInfo after{};
        if(context(after)!=SAN14_OK||!same_context(after)){reset_context();return SAN14_OK;}
        values=save_stats::growth(baseline,current);return SAN14_OK;
    }catch(...){clear_values();log("Growth refresh failed; cached values discarded.");return SAN14_OK;}
}
San14Result SAN14_MOD_CALL value(void*,uint32_t officer,uint32_t column,int32_t* output){
    if(!output||column>=6)return SAN14_E_INVALID_ARGUMENT;
    if(!active)return SAN14_E_PERMISSION_DENIED;
    const auto found=values.find(officer);if(found==values.end())return SAN14_E_NOT_FOUND;
    *output=found->second[column];return SAN14_OK;
}
bool domestic_supported(){
    return host->struct_size>=offsetof(San14HostApi,domestic)+sizeof(host->domestic)&&host->domestic&&
        host->domestic->struct_size>=sizeof(San14DomesticApi)&&host->domestic->set_recording&&host->domestic->query&&
        host->roster_tabs->struct_size>=sizeof(San14RosterTabsApi)&&host->roster_tabs->register_named_tab&&host->roster_tabs->unregister_named_tab;
}
bool records_supported(){
    return host->struct_size>=offsetof(San14HostApi,officer_records)+sizeof(host->officer_records)&&host->officer_records&&
        host->officer_records->struct_size>=sizeof(San14OfficerRecordsApi)&&host->officer_records->set_recording&&host->officer_records->query&&host->officer_records->capabilities&&
        host->roster_tabs->struct_size>=sizeof(San14RosterTabsApi)&&host->roster_tabs->register_named_tab&&host->roster_tabs->unregister_named_tab;
}
San14Result apply_records(bool battle,bool domestic){
    if(!records_supported())return SAN14_OK;
    const auto mask=records_caps&((battle?SAN14_RECORD_BATTLE:0u)|(domestic?SAN14_RECORD_DOMESTIC:0u));
    const auto r=host->officer_records->set_recording(host->host_context,mask);if(r==SAN14_OK)records_lease=mask!=0;return r;
}
int32_t count(uint64_t n){return static_cast<int32_t>(std::min<uint64_t>(n,INT32_MAX));}
San14Result SAN14_MOD_CALL domestic_refresh(void*){
    domestic_values.clear();if(!active)return SAN14_E_PERMISSION_DENIED;
    try{
        if(!sync_context())return SAN14_OK;const auto rows=capture();
        for(const auto& row:rows){San14DomesticTotals t{};t.struct_size=sizeof(t);
            if(host->domestic->query(host->host_context,row.first,&t)!=SAN14_OK)continue;
            OutcomeRow v{};v[6]=v[7]=-1;const uint64_t counts[]={t.commerce,t.agriculture,t.barracks,t.recruited,t.morale};
            for(size_t i=0;i<5;++i){v[i+1]=static_cast<int32_t>(std::min<uint64_t>(counts[i],INT32_MAX));if(i<3)v[0]=static_cast<int32_t>(std::min<int64_t>(int64_t(v[0])+v[i+1],INT32_MAX));}
            if(records_supported()){San14OfficerRecords r{};r.struct_size=sizeof(r);
                if(host->officer_records->query(host->host_context,row.first,&r)==SAN14_OK){
                    if(r.available_categories&SAN14_RECORD_EMPLOY)v[6]=count(r.employ_success);
                    if(r.available_categories&SAN14_RECORD_DIPLOMACY)v[7]=count(r.diplomacy_success);
                }}
            domestic_values.emplace(row.first,v);
        }
        San14SaveStorageInfo after{};if(context(after)!=SAN14_OK||!same_context(after))domestic_values.clear();
    }catch(...){domestic_values.clear();log("Domestic totals refresh failed.");}return SAN14_OK;
}
San14Result SAN14_MOD_CALL domestic_value(void*,uint32_t id,uint32_t col,int32_t* out){
    if(!out||col>=8)return SAN14_E_INVALID_ARGUMENT;if(!active)return SAN14_E_PERMISSION_DENIED;
    const auto found=domestic_values.find(id);if(found==domestic_values.end()||found->second[col]<0)return SAN14_E_NOT_FOUND;*out=found->second[col];return SAN14_OK;
}
San14Result SAN14_MOD_CALL battle_refresh(void*){
    battle_values.clear();if(!active)return SAN14_E_PERMISSION_DENIED;
    try{if(!sync_context())return SAN14_OK;const auto rows=capture();
        for(const auto& row:rows){San14OfficerRecords t{};t.struct_size=sizeof(t);
            if(host->officer_records->query(host->host_context,row.first,&t)!=SAN14_OK)continue;
            OutcomeRow v{};v.fill(-1);
            if(t.available_categories&SAN14_RECORD_DUEL){v[0]=count(t.duel_wins);v[1]=count(t.duel_losses);
                const long double total=static_cast<long double>(t.duel_wins)+static_cast<long double>(t.duel_losses);
                // Round the ratio once to tenths of a percent, then publish
                // SDK basis points. Rounding hundredths first can turn an
                // actual 33.449% into a wrongly displayed 33.5%.
                if(total>0)v[2]=static_cast<int32_t>(static_cast<long double>(t.duel_wins)*1000.0L/total+0.5L)*10;}
            if(t.available_categories&SAN14_RECORD_UNITS){v[3]=count(t.units_destroyed);v[4]=count(t.units_lost);}
            if(t.available_categories&SAN14_RECORD_CITIES)v[5]=count(t.cities_taken);
            if(t.available_categories&SAN14_RECORD_CAPTURES){v[6]=count(t.officers_captured);v[7]=count(t.times_captured);}
            battle_values.emplace(row.first,v);
        }San14SaveStorageInfo after{};if(context(after)!=SAN14_OK||!same_context(after))battle_values.clear();
    }catch(...){battle_values.clear();log("Officer battle records refresh failed.");}return SAN14_OK;
}
San14Result SAN14_MOD_CALL battle_value(void*,uint32_t id,uint32_t col,int32_t* out){
    if(!out||col>=8)return SAN14_E_INVALID_ARGUMENT;if(!active)return SAN14_E_PERMISSION_DENIED;
    const auto found=battle_values.find(id);if(found==battle_values.end()||found->second[col]<0)return SAN14_E_NOT_FOUND;*out=found->second[col];return SAN14_OK;
}
void new_game(const San14EventView& event){
    if(!event.payload||event.payload_size<sizeof(San14StorageEventInfo))return;
    const auto& notice=*static_cast<const San14StorageEventInfo*>(event.payload);
    if(notice.struct_size<sizeof(notice)||notice.state!=SAN14_STORAGE_READY||notice.result!=SAN14_OK||notice.revision<=activation_revision)return;
    San14SaveStorageInfo info{};
    if(context(info)!=SAN14_OK||info.state!=SAN14_STORAGE_READY||info.revision!=notice.revision||event.generation!=notice.revision)return;
    // Do not overwrite even unknown/corrupt data, and do not mistake old saves
    // with a missing sidecar for new campaigns. Only NEW_GAME enters here.
    uint32_t required=0;
    const auto existing=host->storage.read(host->host_context,text("playthrough"),text("officer-growth-v1"),nullptr,0,&required);
    if(existing!=SAN14_E_NOT_FOUND){sync_context(true);return;}
    auto captured=capture();auto bytes=save_stats::encode(captured);
    San14SaveStorageInfo after{};
    if(context(after)!=SAN14_OK||after.state!=SAN14_STORAGE_READY||after.revision!=info.revision||save_stats::fixed(after.playthrough_id)!=save_stats::fixed(info.playthrough_id)){reset_context();return;}
    const auto written=host->storage.write_atomic(host->host_context,text("playthrough"),text("officer-growth-v1"),bytes.data(),static_cast<uint32_t>(bytes.size()));
    if(written!=SAN14_OK){discard();log("New-game baseline could not be stored; growth remains unavailable.");return;}
    reset_context();have_context=true;revision=after.revision;storage_state=after.state;playthrough=save_stats::fixed(after.playthrough_id);baseline=std::move(captured);baseline_valid=true;
}
void SAN14_MOD_CALL event(void*,const San14EventView* e){
    if(!active||!e||e->struct_size<sizeof(*e))return;
    try{if(e->event_id==SAN14_EVENT_NEW_GAME)new_game(*e);else if(e->event_id==SAN14_EVENT_STORAGE_CHANGED)sync_context(true);}
    catch(...){reset_context();log("Growth lifecycle failed; no fallback baseline was created.");}
}
San14Result SAN14_MOD_CALL setting(void*,San14StringView key,int32_t next){
    if(!key.data||next<0||next>1)return SAN14_E_INVALID_ARGUMENT;
    if(key.length==std::strlen(deeds_key)&&!std::memcmp(key.data,deeds_key,key.length))return active?apply_history(next!=0):SAN14_OK;
    for(const auto& option:history_options)if(key.length==std::strlen(option.key)&&!std::memcmp(key.data,option.key,key.length)){
        const auto value=next?(history_choices|option.bit):(history_choices&~option.bit);
        if(active&&history_lease){if(!history_filters_supported())return SAN14_E_UNSUPPORTED;const auto r=host->officer_history->set_event_mask(host->host_context,value);if(r!=SAN14_OK)return r;}
        history_choices=value;return SAN14_OK;
    }
    if(key.length==std::strlen(dead_officers_key)&&!std::memcmp(key.data,dead_officers_key,key.length))
        return active?host->roster_tabs->set_show_dead_officers(host->host_context,next):SAN14_OK;
    if(key.length==std::strlen(domestic_key)&&!std::memcmp(key.data,domestic_key,key.length)){
        const auto before=domestic_recording_enabled;
        if(active&&domestic_supported()){
            const auto r=host->domestic->set_recording(host->host_context,next);if(r!=SAN14_OK)return r;domestic_lease=next!=0;
        }
        if(active){const auto r=apply_records(battle_enabled,next!=0);if(r!=SAN14_OK){if(domestic_supported()){host->domestic->set_recording(host->host_context,before?1:0);domestic_lease=before;}return r;}}
        domestic_recording_enabled=next!=0;return SAN14_OK;
    }
    if(key.length==std::strlen(battle_key)&&!std::memcmp(key.data,battle_key,key.length)){
        if(active){const auto r=apply_records(next!=0,domestic_recording_enabled);if(r!=SAN14_OK)return r;}battle_enabled=next!=0;return SAN14_OK;
    }
    for(size_t i=0;i<5;++i)if(key.length==std::strlen(keys[i])&&!std::memcmp(key.data,keys[i],key.length)){
        const auto before=visible[i];visible[i]=next!=0;
        if(active&&tab_registered){const auto result=host->roster_tabs->set_visible_columns(host->host_context,mask());if(result!=SAN14_OK){visible[i]=before;return result;}}
        return SAN14_OK;
    }
    return SAN14_E_NOT_FOUND;
}
San14Result SAN14_MOD_CALL on_load(San14ModContext*){
    const San14BoolSetting dead_spec{sizeof(dead_spec),text(dead_officers_key),text(u8"在武将栏显示死亡武将"),text(u8"重新打开“情报→武将”的全势力列表，显示本局开局后死亡的武将。旧档无基准时从首次开启起记录，已有死亡不能追溯。基准随存档保存和回退；自势力与出征选人列表不受影响。"),0,setting,nullptr,text(u8"常规"),0};
    if(const auto result=host->settings.register_bool(host->host_context,&dead_spec);result!=SAN14_OK)return result;
    const San14BoolSetting deeds_spec{sizeof(deeds_spec),text(deeds_key),text(u8"武将事迹"),text(u8"记录全势力（含电脑）的经历。在人物详情底部点“事迹”查看。下方各项独立控制以后的记录；关闭不删除历史，重新开启不补录，读档同步回退。不记录太守变化。"),history_supported()?1:0,setting,nullptr,text(u8"事迹"),history_supported()?0u:SAN14_BOOL_SETTING_DISABLED};
    if(const auto result=host->settings.register_bool(host->host_context,&deeds_spec);result!=SAN14_OK)return result;
    for(const auto& option:history_options){const San14BoolSetting spec{sizeof(spec),text(option.key),text(option.label),text(option.description),1,setting,nullptr,text(u8"事迹"),history_filters_supported()?0u:SAN14_BOOL_SETTING_DISABLED};
        if(const auto r=host->settings.register_bool(host->host_context,&spec);r!=SAN14_OK)return r;}
    for(size_t i=0;i<5;++i){const San14BoolSetting spec{sizeof(spec),text(keys[i]),text(labels[i]),text(u8"重新打开武将表后显示或隐藏该项成长列；关闭后仍保留基准，总成长包含五项。"),1,setting,nullptr,text(u8"武将成长统计"),0};
        const auto result=host->settings.register_bool(host->host_context,&spec);if(result!=SAN14_OK)return result;}
    const San14BoolSetting domestic_spec{sizeof(domestic_spec),text(domestic_key),text(u8"记录武将内政贡献"),text(u8"累计实际开发点、征兵、训练士气、擢用成功和外交成功。资金、粮食开发是商业、农业开发点，不是收入；只记最终成功，关闭保留数据，读档回退，旧贡献不能追溯。"),1,setting,nullptr,text(u8"内政统计"),0};
    if(const auto result=host->settings.register_bool(host->host_context,&domestic_spec);result!=SAN14_OK)return result;
    const San14BoolSetting battle_spec{sizeof(battle_spec),text(battle_key),text(u8"记录武将战斗结果"),text(u8"累计单挑胜败、歼灭敌队、被灭队、攻克城池、俘虏敌将和被俘次数。俘虏敌将不含破城俘虏。平局不记胜败；胜率百分数保留一位小数，无胜败记录显示“-”。不记录歼灭敌兵或损失兵力。关闭保留数据，读档同步回退。"),1,setting,nullptr,text(u8"战斗统计"),0};
    if(const auto result=host->settings.register_bool(host->host_context,&battle_spec);result!=SAN14_OK)return result;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL on_disable(San14ModContext*){
    active=false;bool busy=false;
    if(history_action&&history_supported()){const auto r=host->officer_history->set_detail_action(host->host_context,0);if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)history_action=false;else busy=true;}
    if(history_lease&&history_supported()){const auto r=host->officer_history->set_recording(host->host_context,0);if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)history_lease=false;else busy=true;}
    if(records_lease){const auto r=host->officer_records->set_recording(host->host_context,0);if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)records_lease=false;else busy=true;}
    if(battle_registered){const auto r=host->roster_tabs->unregister_named_tab(host->host_context,text("battle"));if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)battle_registered=false;else busy=true;}
    battle_values.clear();records_caps=0;
    if(domestic_lease){const auto r=host->domestic->set_recording(host->host_context,0);if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)domestic_lease=false;else busy=true;}
    if(domestic_registered){const auto r=host->roster_tabs->unregister_named_tab(host->host_context,text("domestic"));if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)domestic_registered=false;else busy=true;}
    domestic_values.clear();
    if(host&&host->roster_tabs&&host->roster_tabs->set_show_dead_officers){const auto r=host->roster_tabs->set_show_dead_officers(host->host_context,0);if(r!=SAN14_OK&&r!=SAN14_E_NOT_FOUND)busy=true;}
    if(tab_registered){const auto r=host->roster_tabs->unregister_tab(host->host_context);if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)tab_registered=false;else busy=true;}
    for(auto& sub:subscriptions)if(sub){const auto r=host->events.unsubscribe(host->host_context,sub);if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)sub=0;else busy=true;}
    reset_context();return busy?SAN14_E_BUSY:SAN14_OK;
}
San14Result SAN14_MOD_CALL on_enable(San14ModContext*){
    if(active||tab_registered||domestic_registered||battle_registered||domestic_lease||records_lease||history_lease||history_action||subscriptions[0]||subscriptions[1])return SAN14_E_BUSY;
    try{
        reset_context();San14SaveStorageInfo info{};const auto queried=context(info);if(queried!=SAN14_OK)return queried;activation_revision=info.revision;
        for(size_t i=0;i<5;++i){int32_t v=1;const auto r=host->settings.get_bool(host->host_context,text(keys[i]),&v);if(r!=SAN14_OK)return r;visible[i]=v!=0;}
        int32_t domestic_pref=1,battle_pref=1;
        if(const auto r=host->settings.get_bool(host->host_context,text(domestic_key),&domestic_pref);r!=SAN14_OK)return r;
        if(const auto r=host->settings.get_bool(host->host_context,text(battle_key),&battle_pref);r!=SAN14_OK)return r;
        domestic_recording_enabled=domestic_pref!=0;battle_enabled=battle_pref!=0;
        active=true;
        history_choices=0;for(const auto& option:history_options){int32_t preference=1;const auto r=host->settings.get_bool(host->host_context,text(option.key),&preference);if(r!=SAN14_OK){on_disable(nullptr);return r;}if(preference)history_choices|=option.bit;}
        if(history_supported()){int32_t preference=1;auto r=host->settings.get_bool(host->host_context,text(deeds_key),&preference);if(r==SAN14_OK)r=apply_history(preference!=0);if(r!=SAN14_OK){on_disable(nullptr);return r;}}
        {int32_t show=0;const auto read=host->settings.get_bool(host->host_context,text(dead_officers_key),&show);
         if(read!=SAN14_OK){on_disable(nullptr);return read;}
         const auto dead_res=host->roster_tabs->set_show_dead_officers(host->host_context,show);
         if(dead_res!=SAN14_OK&&(show||dead_res!=SAN14_E_NOT_FOUND)){on_disable(nullptr);return dead_res;}}
        const uint32_t events[]={SAN14_EVENT_NEW_GAME,SAN14_EVENT_STORAGE_CHANGED};
        for(size_t i=0;i<2;++i){const auto r=host->events.subscribe(host->host_context,events[i],0,event,nullptr,&subscriptions[i]);if(r!=SAN14_OK){on_disable(nullptr);return r;}}
        std::array<San14RosterColumn,6> columns{};
        for(size_t i=0;i<6;++i)columns[i]={sizeof(San14RosterColumn),text(i?labels[i-1]:u8"总成长"),SAN14_ROSTER_COLUMN_SIGNED,0};
        const San14RosterTabSpec spec{sizeof(spec),text(u8"成长"),columns.data(),static_cast<uint32_t>(columns.size()),mask(),refresh,value,nullptr};
        const auto registered=host->roster_tabs->register_tab(host->host_context,&spec);
        if(registered!=SAN14_OK){on_disable(nullptr);return registered;}tab_registered=true;
        if(records_supported()){
            const auto queried_caps=host->officer_records->capabilities(host->host_context,&records_caps);
            if(queried_caps!=SAN14_OK){on_disable(nullptr);return queried_caps;}
            const auto recording=apply_records(battle_enabled,domestic_recording_enabled);if(recording!=SAN14_OK){on_disable(nullptr);return recording;}
            if(records_caps!=SAN14_RECORD_ALL)log("Some officer result categories are unavailable on this game profile; unavailable cells stay invalid.");
            constexpr const char* names[]={u8"单挑胜场",u8"单挑败场",u8"单挑胜率",u8"歼灭敌队",u8"被灭队次数",u8"攻克城池",u8"俘虏敌将",u8"被俘次数"};
            std::array<San14RosterColumn,8> cols{};for(size_t i=0;i<8;++i)cols[i]={sizeof(San14RosterColumn),text(names[i]),i==2?SAN14_ROSTER_COLUMN_PERCENT|SAN14_ROSTER_COLUMN_PERCENT_ONE_DECIMAL|SAN14_ROSTER_COLUMN_DASH_IF_MISSING:0u,0};
            const San14RosterTabSpec battle_spec{sizeof(battle_spec),text(u8"战斗"),cols.data(),8,255,battle_refresh,battle_value,nullptr};
            const auto added=host->roster_tabs->register_named_tab(host->host_context,text("battle"),&battle_spec);if(added!=SAN14_OK){on_disable(nullptr);return added;}battle_registered=true;
        }
        if(domestic_supported()){
            int32_t enabled=1;const auto r=host->settings.get_bool(host->host_context,text(domestic_key),&enabled);if(r!=SAN14_OK){on_disable(nullptr);return r;}domestic_recording_enabled=enabled!=0;
            const auto started=host->domestic->set_recording(host->host_context,enabled);
            if(started==SAN14_OK){domestic_lease=enabled!=0;
                constexpr const char* names[]={u8"总开发量",u8"资金开发",u8"粮食开发",u8"兵营开发",u8"总征兵数",u8"士气上升",u8"擢用成功",u8"外交成功"};
                std::array<San14RosterColumn,8> cols{};for(size_t i=0;i<8;++i)cols[i]={sizeof(San14RosterColumn),text(names[i]),0,0};
                const uint32_t columns_count=records_supported()?8u:6u;
                const San14RosterTabSpec domestic_spec{sizeof(domestic_spec),text(u8"内政"),cols.data(),columns_count,(1u<<columns_count)-1,domestic_refresh,domestic_value,nullptr};
                const auto added=host->roster_tabs->register_named_tab(host->host_context,text("domestic"),&domestic_spec);
                if(added!=SAN14_OK){on_disable(nullptr);return added;}domestic_registered=true;
            }else if(started!=SAN14_E_UNSUPPORTED){on_disable(nullptr);return started;}
            else log("Actual domestic settlement observation is unavailable on this game profile.");
        }
        sync_context();return SAN14_OK;
    }catch(...){on_disable(nullptr);return SAN14_E_PLUGIN_FAILED;}
}
void SAN14_MOD_CALL on_unload(San14ModContext*){on_disable(nullptr);host=nullptr;}
bool supported(const San14HostApi* h){
    if(!h||h->abi_version!=SAN14_HOST_ABI_V1||h->struct_size<offsetof(San14HostApi,roster_tabs)+sizeof(h->roster_tabs))return false;
    return h->roster_tabs&&h->roster_tabs->struct_size>=offsetof(San14RosterTabsApi,register_named_tab)&&h->roster_tabs->register_tab&&h->roster_tabs->unregister_tab&&h->roster_tabs->set_visible_columns&&h->roster_tabs->set_show_dead_officers&&
        h->save_storage&&h->save_storage->struct_size>=sizeof(San14SaveStorageApi)&&h->save_storage->context&&
        h->events.struct_size>=sizeof(San14EventApi)&&h->events.subscribe&&h->events.unsubscribe&&
        h->roster.struct_size>=sizeof(San14RosterApi)&&h->roster.capture&&
        h->settings.struct_size>=sizeof(San14SettingsApi)&&h->settings.register_bool&&h->settings.get_bool&&
        h->storage.struct_size>=sizeof(San14StorageApi)&&h->storage.read&&h->storage.write_atomic;
}
}
extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(const San14HostApi* h,San14ModApi* out){
    if(!out||out->struct_size<sizeof(*out)||!supported(h))return SAN14_E_UNSUPPORTED;
    host=h;San14ModApi api{};api.struct_size=sizeof(api);api.abi_version=SAN14_HOST_ABI_V1;api.on_load=on_load;api.on_enable=on_enable;api.on_disable=on_disable;api.on_unload=on_unload;*out=api;return SAN14_OK;
}
