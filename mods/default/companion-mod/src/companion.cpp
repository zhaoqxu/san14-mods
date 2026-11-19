#include <cstddef>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "san14_mod.h"
#include "san14_idle.h"
#include "san14_expert_page.h"
#include "office_policy.h"
#include "persistent_area_policy.h"

namespace {
const San14HostApi* host=nullptr;
constexpr const char* speed_setting_key="game_speed_x100";
constexpr const char* speed_storage_key="companion.pref.game_speed_x100";
constexpr int32_t speed_min_x100=100;
constexpr int32_t speed_max_x100=800;
constexpr int32_t speed_step_x100=25;
int32_t speed_x100_value=speed_min_x100;
bool speed_applied=false;
bool active=false,office_registered=false,areas_registered=false;
San14Handle report_hotkey=0;
San14Subscription spectator_idle=0;
std::array<San14Subscription,4> area_subscriptions{};
San14PanelHandle spectator_panel=0;
struct ForceChoice {uint32_t id=0,controlled=0;uint64_t generation=0;};
std::array<ForceChoice,46> force_choices{};
std::vector<San14ForceSnapshot> menu_forces;
size_t force_page=0;
San14ControlHandle force_status=0;
constexpr size_t forces_per_page=16;
companion_mod::persistent_area_policy::State area_state;
uint64_t area_storage_revision=0;
std::string area_playthrough_id;
San14StringView sv(const char* text){return {text,static_cast<uint32_t>(std::strlen(text))};}
enum class Service {Rule,Office,Report,Scenario,Area};
struct Feature {const char* key;const char* label;const char* help;const char* rule;Service service=Service::Rule;const char* category=u8"常规";};
const Feature features[]={
    {"editable_scenario_settings",u8"剧本设定可修改",u8"允许修改载入剧本后的设定。","scenario.settings.editable"},
    {"fine_expert_mode",u8"精细专家模式",u8"按具体数值调整邻接加成（0–100），并在提案下设置手动战法有／无，使用原版六个分页与预设槽。请关闭剧本设定页面后切换。","scenario.parameters",Service::Scenario},
    {"persistent_area_officers",u8"府官保留与复归",u8"挂起玩家被俘或暂时失守府的原任府官及模式，随战局保存并在满足条件时自动恢复。","area.ai.persistent",Service::Area},
    {"spectator_autosave",u8"看海时自动存档",u8"允许看海时按游戏原有设置自动存档，沿用原版轮换槽。须同时开启游戏的自动存档；关闭此项恢复原版行为。","save.spectator.autosave"},
    {"automatic_historical_events",u8"历史事件自动发生",u8"看海和自己操作时，满足原本触发条件的历史事件都会自动发生，包括不利于自己的事件。看海时自动确认事件对白；自己操作时保留原版交互。关闭后恢复原版触发方式。","events.historical.automatic"},
    {"disable_tribal_officer_gifts",u8"禁止异族赠将",u8"禁止异族外交和对应事件赠送武将。","diplomacy.tribal_gifts.blocked",Service::Rule,u8"外交与异族"},
    {"disable_tribal_troop_replenish",u8"禁止异族增兵",u8"停止异族城市自动补充兵力，保留正常伤兵恢复。","cities.tribal_replenishment.blocked",Service::Rule,u8"外交与异族"},
    {"disable_foreign_trade",u8"禁止外国交易",u8"禁止与外国交易、留学及电脑发起的对应任务。","diplomacy.foreign_trade.blocked",Service::Rule,u8"外交与异族"},
    {"preserve_captive_post_and_return",u8"俘虏保留官职，释放后返回军团长主城",u8"保留被俘武将官职；释放后优先返回当前军团长或君主所在城。","officers.prisoners.preserve_and_return",Service::Rule,u8"武将与军团"},
    {"allow_away_post_assignment",u8"外出武将可授官授名品",u8"允许外出武将授官及授予、没收名品；俘虏不参与名品转移。","officers.away.appoint_and_reward",Service::Rule,u8"武将与军团"},
    {"shared_domestic_duty_slots",u8"募兵、训练、谍报各占一格",u8"三项担当与府内政共用武将原有工作位。容量足够时可兼任，满位仅腾出所需位置，优先撤最早分配的府；不提高武将原有容量。","officers.duties.shared_slots",Service::Rule,u8"武将与军团"},
    {"allow_subordinate_command_officers",u8"可使用军团武将出任务",u8"君主第一军团可在外交、擢用等命令中使用下属军团武将。","officers.subordinate.commands",Service::Rule,u8"武将与军团"},
    {"allow_subordinate_base_operations",u8"可控制军团城池出兵、移动",u8"开放军团据点的出兵、异动和召唤；手动出征后临时转君主直辖，也可选中自家军团部队后点“亲自指挥”。已接管部队可点“交还军团”，灭队或进城后自动归还；原军团解散时归还按钮置灰。","cities.subordinate.operations",Service::Rule,u8"武将与军团"},
    {"allow_person_group_district_officers",u8"移动、编制集团可包括军团武将",u8"集团编制和移动候选可包含下属军团武将。","officers.subordinate.groups",Service::Rule,u8"武将与军团"},
    {"weighted_office_auto_assign",u8"官职自动任命按功劳和能力加权",u8"按功劳与能力排序，区分文武，执行两轮自动任命。功劳上限由“增强设定与编辑”独立控制。","officers.office.weighted",Service::Office,u8"武将与军团"},
    {"block_ai_district_split",u8"禁止电脑新增兵团",u8"禁止电脑势力（含看海／托管势力）自动划分或重组军团；不会解散已有军团，玩家手动划分不受影响。","ai.districts.blocked",Service::Rule,u8"武将与军团"},
    {"disable_elder_portraits",u8"关闭老年立绘",u8"关闭史实武将到年份后的老年立绘切换，保留常规立绘与自定义头像。","officers.elder_portraits.disabled",Service::Rule,u8"报告与演出"},
    {"skip_turn_reports",u8"略过旬末报告",u8"略过自动弹出的旬末报告；启用后按 F8 可打开本旬报告。","presentation.turn_reports.skip",Service::Report,u8"报告与演出"},
    {"defer_disaster_reports",u8"简化重复演出",u8"省略灾害、丰收、吉凶兆等演出并保留报告；同时略过城市增筑和华佗来访等重复演出。","presentation.repeated.simplify",Service::Rule,u8"报告与演出"},
    {"hide_war_book_discovery_message",u8"不显示搜索获得战法书的消息",u8"战法书正常入库，省略玩家自动搜索获得战法书的消息。","notifications.war_book.hidden",Service::Rule,u8"报告与演出"},
    {"global_death_notices",u8"全图战死通知",u8"显示全图武将的原生战死通知。","notifications.death.global",Service::Rule,u8"报告与演出"}
};
constexpr size_t feature_count=sizeof(features)/sizeof(features[0]);
static_assert(feature_count==21,"Companion's supported feature set");
bool applied[feature_count]{};
bool feature_values[feature_count]{};
std::unique_ptr<san14::ExpertPage> settings_page;

companion_office_policy::Officer copied_officer(const San14OfficeCandidate& value){
    return {value.id,value.leadership,value.martial,value.intelligence,value.politics,value.merit};
}
San14Result SAN14_MOD_CALL office_score(void*,const San14OfficeCandidate* value,int32_t maximum,San14OfficeMode mode,int64_t* output){
    if(!value||!output||value->struct_size<sizeof(*value)||mode<SAN14_OFFICE_COMPREHENSIVE||mode>SAN14_OFFICE_CIVIL)return SAN14_E_INVALID_ARGUMENT;
    *output=companion_office_policy::score(copied_officer(*value),maximum,static_cast<companion_office_policy::Mode>(mode));return SAN14_OK;
}
San14Result SAN14_MOD_CALL office_class(void*,const San14OfficeCandidate* value,San14OfficeClass* output){
    if(!value||!output||value->struct_size<sizeof(*value))return SAN14_E_INVALID_ARGUMENT;
    *output=static_cast<San14OfficeClass>(companion_office_policy::military_class(copied_officer(*value)));return SAN14_OK;
}
San14Result SAN14_MOD_CALL office_post(void*,uint32_t id,uint32_t index,const int32_t* values,int32_t* output){
    if(!values||!output||index>3)return SAN14_E_INVALID_ARGUMENT;
    int32_t bonuses[4];std::memcpy(bonuses,values,sizeof(bonuses));
    *output=companion_office_policy::classify_post(id,index,bonuses);return SAN14_OK;
}
bool office_available(){return host->office&&host->office->struct_size>=sizeof(San14OfficeApi)&&host->office->register_policy;}
bool areas_available(){return host->struct_size>=offsetof(San14HostApi,areas)+sizeof(host->areas)&&host->areas&&
    host->areas->struct_size>=sizeof(San14AreaApi)&&host->areas->register_policy&&host->areas->unregister_policy;}
bool area_storage_available(){return host->struct_size>=offsetof(San14HostApi,save_storage)+sizeof(host->save_storage)&&host->save_storage&&
    host->save_storage->struct_size>=sizeof(San14SaveStorageApi)&&host->save_storage->context&&
    host->storage.struct_size>=sizeof(San14StorageApi)&&host->storage.read&&host->storage.write_atomic&&
    host->events.struct_size>=sizeof(San14EventApi)&&host->events.subscribe&&host->events.unsubscribe;}
void flush_area_storage(){
    if(!active||!areas_registered||!area_storage_available())return;
    San14SaveStorageInfo info{};info.struct_size=sizeof(info);
    if(host->save_storage->context(host->host_context,&info)!=SAN14_OK||info.state!=SAN14_STORAGE_READY||!info.playthrough_id[0])return;
    std::vector<uint8_t> blob;
    if(!area_state.consume_dirty(blob))return;
    host->storage.write_atomic(host->host_context,sv("playthrough"),sv("companion.suspended_areas.v1"),
        blob.data(),static_cast<uint32_t>(blob.size()));
}
void sync_area_storage(bool force_reload){
    if(!area_storage_available())return;
    San14SaveStorageInfo info{};info.struct_size=sizeof(info);
    if(host->save_storage->context(host->host_context,&info)!=SAN14_OK||info.state!=SAN14_STORAGE_READY||!info.playthrough_id[0]){
        area_storage_revision=0;area_playthrough_id.clear();area_state.reset_for_new_playthrough();return;
    }
    const std::string next_id(info.playthrough_id);
    if(!force_reload&&area_storage_revision==info.revision&&area_playthrough_id==next_id)return;
    area_storage_revision=info.revision;area_playthrough_id=next_id;
    uint32_t required=0;
    const auto probe=host->storage.read(host->host_context,sv("playthrough"),sv("companion.suspended_areas.v1"),nullptr,0,&required);
    if(probe==SAN14_E_NOT_FOUND||required==0){area_state.reset_for_new_playthrough();return;}
    if(probe!=SAN14_OK&&probe!=SAN14_E_BUFFER_TOO_SMALL)return;
    std::vector<uint8_t> buf(required);uint32_t read_size=0;
    if(host->storage.read(host->host_context,sv("playthrough"),sv("companion.suspended_areas.v1"),buf.data(),required,&read_size)==SAN14_OK){
        area_state.load_blob(buf.data(),read_size);
    }
}
void SAN14_MOD_CALL area_event_cb(void*,const San14EventView* view){
    if(!active||!areas_registered||!view||view->struct_size<sizeof(*view))return;
    if(view->event_id==SAN14_EVENT_NEW_GAME){
        San14SaveStorageInfo info{};info.struct_size=sizeof(info);
        if(area_storage_available()&&host->save_storage->context(host->host_context,&info)==SAN14_OK&&info.state==SAN14_STORAGE_READY){
            area_storage_revision=info.revision;area_playthrough_id=info.playthrough_id;
        }
        area_state.reset_for_new_playthrough();
    }else if(view->event_id==SAN14_EVENT_STORAGE_CHANGED){
        sync_area_storage(true);
    }else if(view->event_id==SAN14_EVENT_SAVE_SAVING||view->event_id==SAN14_EVENT_UI_IDLE){
        flush_area_storage();
    }
}
void unsubscribe_area_events(){
    for(auto& sub:area_subscriptions){
        if(!sub)continue;
        if(host&&host->events.unsubscribe){
            const auto r=host->events.unsubscribe(host->host_context,sub);
            if(r==SAN14_OK||r==SAN14_E_NOT_FOUND)sub=0;
        }else{sub=0;}
    }
}
San14Result subscribe_area_events(){
    if(!area_storage_available())return SAN14_OK;
    const uint32_t ids[4]={SAN14_EVENT_STORAGE_CHANGED,SAN14_EVENT_NEW_GAME,SAN14_EVENT_SAVE_SAVING,SAN14_EVENT_UI_IDLE};
    for(size_t i=0;i<4;++i){
        if(area_subscriptions[i])continue;
        const auto r=host->events.subscribe(host->host_context,ids[i],0,area_event_cb,nullptr,&area_subscriptions[i]);
        if(r!=SAN14_OK){unsubscribe_area_events();return r;}
    }
    sync_area_storage(true);
    return SAN14_OK;
}
bool report_available(){return host->presentation&&host->presentation->struct_size>=sizeof(San14PresentationApi)&&host->presentation->request_turn_report&&
    host->input.struct_size>=sizeof(San14InputApi)&&host->input.register_hotkey&&host->input.release;}
bool spectator_available(){return host->struct_size>=offsetof(San14HostApi,spectator)+sizeof(host->spectator)&&host->spectator&&
    host->spectator->struct_size>=offsetof(San14SpectatorApi,select_force)+sizeof(host->spectator->select_force)&&host->spectator->select_force&&host->spectator->set_menu_enabled&&host->spectator->poll_menu_request&&
    host->spectator->is_spectator&&host->spectator->take_control&&host->world&&host->world->struct_size>=offsetof(San14WorldApi,release)+sizeof(host->world->release)&&
    host->ui.struct_size>=sizeof(San14UiApi)&&host->events.struct_size>=sizeof(San14EventApi)&&host->events.subscribe&&host->events.unsubscribe;}
San14Result close_spectator_panel(){
    if(!spectator_panel){menu_forces.clear();force_status=0;return SAN14_OK;}const auto result=host->ui.close_panel(host->host_context,spectator_panel);
    if(result==SAN14_OK||result==SAN14_E_NOT_FOUND){spectator_panel=0;menu_forces.clear();force_status=0;return SAN14_OK;}return result;
}
void SAN14_MOD_CALL choose_force(void* context,San14ControlHandle){
    if(!active||!context||!spectator_available())return;const auto id=static_cast<ForceChoice*>(context)->id;
    const auto choice=*static_cast<ForceChoice*>(context);
    const auto result=host->spectator->select_force(host->host_context,choice.generation,choice.controlled,id);
    if(result==SAN14_OK||result==SAN14_E_STALE_GENERATION)close_spectator_panel();
    else if(force_status)host->ui.set_text(host->host_context,force_status,sv(u8"暂时无法切换，请返回地图后重试。"));
}
San14Result render_force_menu(size_t page);
void SAN14_MOD_CALL previous_force_page(void*,San14ControlHandle){if(active&&force_page)render_force_menu(force_page-1);}
void SAN14_MOD_CALL next_force_page(void*,San14ControlHandle){if(active)render_force_menu(force_page+1);}
void SAN14_MOD_CALL return_from_forces(void*,San14ControlHandle){if(active)close_spectator_panel();}
San14Result render_force_menu(size_t page){
    if(menu_forces.empty()||page>=(menu_forces.size()+forces_per_page-1)/forces_per_page)return SAN14_E_INVALID_ARGUMENT;
    San14PanelHandle next=0;
    const San14PanelSpec panel{sizeof(panel),sv(u8"操作势力"),SAN14_EXPERT_WIDTH,SAN14_EXPERT_HEIGHT,SAN14_PANEL_EXPERT};
    auto result=host->ui.create_panel(host->host_context,&panel,&next);if(result!=SAN14_OK)return result;
    auto label=[&](const char* id,const std::string& text,int x,int y,int w,int h,uint32_t flags){
        San14ControlHandle out=0;const San14ControlSpec spec{sizeof(spec),sv(id),sv(text.c_str()),x,y,w,h,flags};
        if(result==SAN14_OK)result=host->ui.add_label(host->host_context,next,&spec,&out);return out;};
    auto button=[&](const char* id,const char* text,int x,int y,int w,int h,uint32_t flags,San14ControlCallback callback,void* context){
        San14ControlHandle out=0;const San14ControlSpec spec{sizeof(spec),sv(id),sv(text),x,y,w,h,flags};
        if(result==SAN14_OK)result=host->ui.add_button(host->host_context,next,&spec,callback,context,&out);};
    const auto controlled=force_choices[menu_forces.front().id].controlled;
    std::string current=controlled?u8"当前操作：未知势力":u8"当前：看海";
    for(const auto& force:menu_forces)if(force.id==controlled){current=u8"当前操作：";current+=force.name_utf8;break;}
    label("spectator.current",current,360,112,1200,42,SAN14_CONTROL_EXPERT_ROW);
    const auto first=page*forces_per_page,end=std::min(first+forces_per_page,menu_forces.size());
    for(size_t i=first;i<end;++i){
        const auto& force=menu_forces[i];const int slot=static_cast<int>(i-first),x=360+(slot%2)*612,y=174+(slot/2)*66;
        const std::string key="spectator.row."+std::to_string(force.id);
        label(key.c_str(),"",x,y,588,54,SAN14_CONTROL_EXPERT_ROW);
        const std::string id="spectator.force."+std::to_string(force.id);
        // Keep native tab slice caps intact, inset in the shared stock row plate.
        button(id.c_str(),force.name_utf8,x+8,y+7,420,40,SAN14_CONTROL_TAB|(force.id==controlled?SAN14_CONTROL_SELECTED:0u),choose_force,&force_choices[force.id]);
        const std::string status="spectator.state."+std::to_string(force.id);
        label(status.c_str(),force.id==controlled?u8"操作中":u8"电脑",x+446,y+13,126,28,SAN14_CONTROL_EXPERT_LABEL);
    }
    const auto pages=(menu_forces.size()+forces_per_page-1)/forces_per_page;
    const auto caption=std::to_string(menu_forces.size())+u8" 个势力    "+std::to_string(page+1)+" / "+std::to_string(pages);
    label("spectator.pagination",caption,780,708,400,32,0);
    if(page)button("spectator.previous",u8"上一页",360,706,140,40,0,previous_force_page,nullptr);
    if(page+1<pages)button("spectator.next",u8"下一页",1420,706,140,40,0,next_force_page,nullptr);
    label("spectator.help.plate","",360,758,1200,112,SAN14_CONTROL_EXPERT_ROW);
    label("spectator.help",u8"点击势力开始操作；再次点击当前势力，交给电脑。",384,776,1152,32,SAN14_CONTROL_EXPERT_LABEL);
    const auto next_status=label("spectator.status",u8"Esc 或“返回”关闭，保持当前操作势力。",384,820,1152,32,SAN14_CONTROL_EXPERT_LABEL);
    button("spectator.return",u8"返回",904,SAN14_EXPERT_FOOTER_TOP,112,112,SAN14_CONTROL_CONFIRM,return_from_forces,nullptr);
    if(result==SAN14_OK)result=host->ui.show_panel(host->host_context,next);
    // Request a ready replacement before retiring the current page. The Host
    // keeps its foreground alive while the new stock resources load.
    if(result==SAN14_OK&&spectator_panel){const auto closed=host->ui.close_panel(host->host_context,spectator_panel);
        if(closed!=SAN14_OK&&closed!=SAN14_E_NOT_FOUND)result=closed;}
    if(result!=SAN14_OK){host->ui.close_panel(host->host_context,next);return result;}
    spectator_panel=next;force_page=page;force_status=next_status;return SAN14_OK;
}
San14Result open_force_menu(){
    {const auto closed=close_spectator_panel();if(closed!=SAN14_OK)return closed;}San14Handle snapshot=0;San14WorldInfo info{};info.struct_size=sizeof(info);
    auto result=host->world->capture(host->host_context,SAN14_WORLD_FORCES,&snapshot,&info);if(result!=SAN14_OK)return result;
    std::vector<San14ForceSnapshot> forces(info.force_count);uint32_t count=0;
    result=host->world->forces(host->host_context,snapshot,0,forces.data(),static_cast<uint32_t>(forces.size()),sizeof(San14ForceSnapshot),&count);
    const auto released=host->world->release(host->host_context,snapshot);if(result!=SAN14_OK)return result;if(released!=SAN14_OK)return released;
    if(count>forces.size())return SAN14_E_PLUGIN_FAILED;
    int32_t observing=0;result=host->spectator->is_spectator(host->host_context,&observing);if(result!=SAN14_OK)return result;
    const uint32_t controlled=observing?0u:info.player_force_id;
    forces.resize(count);forces.erase(std::remove_if(forces.begin(),forces.end(),[](const auto& force){return force.id<1||force.id>45||!force.name_utf8[0];}),forces.end());
    if(forces.empty())return SAN14_E_NOT_FOUND;
    menu_forces=std::move(forces);size_t page=0;
    for(size_t i=0;i<menu_forces.size();++i){const auto id=menu_forces[i].id;
        force_choices[id]={id,controlled,info.generation};if(id==controlled)page=i/forces_per_page;}
    result=render_force_menu(page);if(result!=SAN14_OK)close_spectator_panel();return result;
}
void SAN14_MOD_CALL spectator_idle_event(void*,const San14EventView* view){
    if(!active||!view||view->event_id!=SAN14_EVENT_UI_IDLE||!spectator_available())return;
    int32_t requested=0;if(host->spectator->poll_menu_request(host->host_context,&requested)==SAN14_OK&&requested)open_force_menu();
}
San14Result enable_spectator(){
    if(!spectator_available())return SAN14_E_UNSUPPORTED;
    if(spectator_idle)return SAN14_OK;
    auto result=host->events.subscribe(host->host_context,SAN14_EVENT_UI_IDLE,100,spectator_idle_event,nullptr,&spectator_idle);if(result!=SAN14_OK)return result;
    result=host->spectator->set_menu_enabled(host->host_context,1);if(result!=SAN14_OK){host->events.unsubscribe(host->host_context,spectator_idle);spectator_idle=0;}return result;
}
San14Result disable_spectator(){
    bool busy=close_spectator_panel()!=SAN14_OK;if(spectator_idle){const auto result=host->events.unsubscribe(host->host_context,spectator_idle);if(result==SAN14_OK||result==SAN14_E_NOT_FOUND)spectator_idle=0;else busy=true;}
    if(spectator_available()){const auto result=host->spectator->set_menu_enabled(host->host_context,0);if(result!=SAN14_OK&&result!=SAN14_E_NOT_FOUND)busy=true;}return busy?SAN14_E_BUSY:SAN14_OK;
}
San14Result ensure_policy(Service service){
    if(service==Service::Office&&!office_registered){
        if(!office_available())return SAN14_E_UNSUPPORTED;
        const San14OfficePolicy spec{sizeof(spec),nullptr,office_score,office_class,office_post};
        const auto result=host->office->register_policy(host->host_context,&spec);
        if(result!=SAN14_OK)return result;office_registered=true;
    }else if(service==Service::Area&&!areas_registered){
        if(!areas_available())return SAN14_E_UNSUPPORTED;
        const auto spec=companion_mod::persistent_area_policy::make_policy(area_state);
        const auto result=host->areas->register_policy(host->host_context,&spec);
        if(result!=SAN14_OK)return result;
        const auto ev=subscribe_area_events();
        if(ev!=SAN14_OK){host->areas->unregister_policy(host->host_context);return ev;}
        areas_registered=true;
    }
    return SAN14_OK;
}
void SAN14_MOD_CALL open_report(void*){
    if(!active||!host||!report_available())return;
    for(size_t i=0;i<feature_count;++i)if(features[i].service==Service::Report&&applied[i]){host->presentation->request_turn_report(host->host_context);return;}
}
San14Result release_hotkey(){
    if(!report_hotkey)return SAN14_OK;
    const auto result=host->input.release(host->host_context,report_hotkey);
    if(result==SAN14_OK||result==SAN14_E_NOT_FOUND){report_hotkey=0;return SAN14_OK;}return result;
}
San14Result fine_expert(bool enabled){
    if(host->struct_size<offsetof(San14HostApi,scenario)+sizeof(host->scenario)||!host->scenario||
       host->scenario->struct_size<offsetof(San14ScenarioApi,read_bool)+sizeof(host->scenario->read_bool)||
       !host->scenario->query||!host->scenario->configure||!host->scenario->configure_bools)return SAN14_E_UNSUPPORTED;
    if(!enabled){
        const auto result=host->scenario->configure(host->host_context,nullptr,0);if(result!=SAN14_OK)return result;
        const auto bool_res=host->scenario->configure_bools(host->host_context,nullptr,0);if(bool_res!=SAN14_OK)return bool_res;
        return SAN14_OK;
    }
    San14ScenarioParameterSpec specs[2]{};
    const uint32_t ids[]={SAN14_SCENARIO_OWN_ADJACENT_AREA_EFFECT,SAN14_SCENARIO_OTHER_ADJACENT_AREA_EFFECT};
    const char* labels[]={u8"自势力邻接区域加成",u8"他势力邻接区域加成"};
    const char* descriptions[]={
        u8"设置自势力每个完整支援区域提供的基础加成数值，范围 0–100。标准值为 4。实际效果仍按区域占领率及原生支援规则计算，相关政策加成另计。",
        u8"设置他势力每个完整支援区域提供的基础加成数值，范围 0–100。初级至高级标准为 4，超级为 16，顶级为 24。实际效果仍按区域占领率及原生支援规则计算，相关政策加成另计。"};
    for(unsigned i=0;i<2;++i){
        San14ScenarioParameterInfo info{};info.struct_size=sizeof(info);
        auto result=host->scenario->query(host->host_context,ids[i],&info);if(result!=SAN14_OK)return result;
        if(!(info.supported_units&(1u<<SAN14_SCENARIO_BASE_COEFFICIENT))||info.coefficient_maximum<100)return SAN14_E_UNSUPPORTED;
        auto& s=specs[i];s.struct_size=sizeof(s);s.parameter_id=ids[i];s.unit=SAN14_SCENARIO_BASE_COEFFICIENT;s.maximum=100;
        std::copy(std::begin(info.coefficient_bases),std::end(info.coefficient_bases),std::begin(s.defaults));
        s.label=sv(labels[i]);s.description=sv(descriptions[i]);
    }
    auto result=host->scenario->configure(host->host_context,specs,2);if(result!=SAN14_OK)return result;
    San14ScenarioBoolSpec tactics{};tactics.struct_size=sizeof(tactics);tactics.key=sv("manual_tactics");tactics.after_parameter=46;
    tactics.label=sv(u8"战法");tactics.description=sv(u8"设置玩家是否可以手动发动战法。自动发动的战法不受影响。标准为初级至超级有、顶级无；可随读取设定、保存设定一起使用。");
    tactics.false_label=sv(u8"无");tactics.true_label=sv(u8"有");
    tactics.defaults[0]=tactics.defaults[1]=tactics.defaults[2]=tactics.defaults[3]=1;
    tactics.binding=SAN14_SCENARIO_BOOL_MANUAL_TACTICS;
    result=host->scenario->configure_bools(host->host_context,&tactics,1);
    if(result!=SAN14_OK){host->scenario->configure(host->host_context,nullptr,0);return result;}

    return SAN14_OK;
}
San14Result apply(size_t index,bool value){
    const auto& feature=features[index];
    if(applied[index]==value){
        // A failed enable rollback may retain an inert hotkey until cleanup.
        return !value&&feature.service==Service::Report?release_hotkey():SAN14_OK;
    }
    if(value){
        const auto ready=ensure_policy(feature.service);if(ready!=SAN14_OK)return ready;
        if(feature.service==Service::Report){
            if(!report_available())return SAN14_E_UNSUPPORTED;
            if(!report_hotkey){
                const auto result=host->input.register_hotkey(host->host_context,sv("turn-report.open"),0x77,open_report,nullptr,&report_hotkey);
                if(result!=SAN14_OK){report_hotkey=0;return result;}
                if(!report_hotkey)return SAN14_E_PLUGIN_FAILED;
            }
        }
    }
    if(feature.service==Service::Area)area_state.set_enabled(value);
    const auto result=feature.service==Service::Scenario?fine_expert(value):host->rules.set_bool(host->host_context,sv(feature.rule),value?1:0);
    if(result!=SAN14_OK){
        if(feature.service==Service::Area)area_state.set_enabled(applied[index]);
        if(value&&feature.service==Service::Report){const auto rollback=release_hotkey();if(rollback!=SAN14_OK)return rollback;}
        return result;
    }
    applied[index]=value;
    if(!value&&feature.service==Service::Area)flush_area_storage();
    if(!value&&feature.service==Service::Report){
        const auto released=release_hotkey();
        if(released!=SAN14_OK){
            // Preserve the old checkbox behavior if releasing F8 is rejected.
            const auto restored=host->rules.set_bool(host->host_context,sv(feature.rule),1);
            if(restored==SAN14_OK)applied[index]=true;
            return released;
        }
    }
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL apply(size_t index,bool value);
bool global_storage_available(){
    return host&&host->storage.struct_size>=sizeof(San14StorageApi)&&host->storage.read&&host->storage.write_atomic;
}
bool settings_ext_available(){
    return host&&host->struct_size>=offsetof(San14HostApi,settings_ext)+sizeof(host->settings_ext)&&
        host->settings_ext&&host->settings_ext->struct_size>=sizeof(San14SettingsExtendedApi)&&
        host->settings_ext->register_range&&host->settings_ext->get_range;
}
bool game_speed_available(){
    return host&&host->struct_size>=offsetof(San14HostApi,game_speed)+sizeof(host->game_speed)&&
        host->game_speed&&host->game_speed->struct_size>=sizeof(San14GameSpeedApi)&&
        host->game_speed->set_speed_x100;
}
void save_speed_pref(int32_t value){
    if(!global_storage_available())return;
    const int32_t snapped=san14::snap_range_value(value,speed_min_x100,speed_max_x100,speed_step_x100);
    host->storage.write_atomic(host->host_context,sv("global"),sv(speed_storage_key),&snapped,sizeof(snapped));
}
void load_speed_pref(int32_t& out){
    int32_t loaded=speed_min_x100;
    bool found=false;
    if(global_storage_available()){
        int32_t raw=0;
        uint32_t required=0;
        if(host->storage.read(host->host_context,sv("global"),sv(speed_storage_key),&raw,sizeof(raw),&required)==SAN14_OK&&required>=sizeof(raw)){
            loaded=raw;found=true;
        }
    }
    if(!found&&settings_ext_available()){
        int32_t setting_val=0;
        if(host->settings_ext->get_range(host->host_context,sv(speed_setting_key),&setting_val)==SAN14_OK){
            loaded=setting_val;found=true;
        }
    }
    out=found?san14::snap_range_value(loaded,speed_min_x100,speed_max_x100,speed_step_x100):speed_min_x100;
}
San14Result apply_speed(int32_t value){
    if(!game_speed_available())return SAN14_E_UNSUPPORTED;
    const int32_t snapped=san14::snap_range_value(value,speed_min_x100,speed_max_x100,speed_step_x100);
    const auto result=host->game_speed->set_speed_x100(host->host_context,static_cast<uint32_t>(snapped),SAN14_SPEED_FLAG_IN_GAME_ONLY);
    if(result==SAN14_OK)speed_applied=(snapped!=speed_min_x100);
    return result;
}
std::string pref_storage_key(size_t index){
    return std::string("companion.pref.")+features[index].key;
}
void save_pref(size_t index,bool value){
    if(!global_storage_available())return;
    const uint8_t byte=value?1u:0u;
    const auto key=pref_storage_key(index);
    host->storage.write_atomic(host->host_context,sv("global"),sv(key.c_str()),&byte,1);
}
San14Result load_pref(size_t index,bool& out){
    if(global_storage_available()){
        uint8_t byte=0;
        uint32_t required=0;
        const auto key=pref_storage_key(index);
        if(host->storage.read(host->host_context,sv("global"),sv(key.c_str()),&byte,1,&required)==SAN14_OK&&required>=1){
            out=(byte!=0);
            return SAN14_OK;
        }
    }
    int32_t value=0;
    const auto result=host->settings.get_bool(host->host_context,sv(features[index].key),&value);
    if(result!=SAN14_OK)return result;
    out=(value!=0);
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL open_settings_page(void*){
    if(!san14::ExpertPage::supported(host))return SAN14_E_UNSUPPORTED;
    if(settings_page)settings_page->close();
    settings_page=std::make_unique<san14::ExpertPage>(host,u8"优化伴侣");
    if(game_speed_available()){
        int32_t current_speed=speed_x100_value;
        load_speed_pref(current_speed);
        speed_x100_value=current_speed;
        settings_page->add_slider(
            u8"常规",speed_setting_key,u8"游戏加速",
            u8"基于 Cheat Engine 时钟缩放原理调整游戏速度（1.00x–8.00x，步进 0.25x，1.00x 为关闭）。仅在大地图战局内自动生效，启动、标题与读档界面保持原速。",
            current_speed,speed_min_x100,speed_max_x100,speed_step_x100,100,"x",
            [](int32_t next)->San14Result{
                const int32_t snapped=san14::snap_range_value(next,speed_min_x100,speed_max_x100,speed_step_x100);
                if(active){
                    const auto result=apply_speed(snapped);
                    if(result!=SAN14_OK)return result;
                }
                speed_x100_value=snapped;
                save_speed_pref(snapped);
                return SAN14_OK;
            });
    }
    for(size_t i=0;i<feature_count;++i){
        bool current=feature_values[i];
        load_pref(i,current);
        feature_values[i]=current;
        settings_page->add_bool(features[i].category,features[i].key,features[i].label,features[i].help,current,
            [i](int32_t next)->San14Result{
                const bool on=(next!=0);
                if(active){
                    const auto result=apply(i,on);
                    if(result!=SAN14_OK)return result;
                }
                feature_values[i]=on;
                save_pref(i,on);
                return SAN14_OK;
            });
    }
    return settings_page->show();
}
San14Result SAN14_MOD_CALL changed(void*,San14StringView key,int32_t value){
    if(!active)return SAN14_E_BUSY;
    if(!key.data)return SAN14_E_INVALID_ARGUMENT;
    if(key.length==std::strlen(speed_setting_key)&&!std::memcmp(key.data,speed_setting_key,key.length)){
        if(!game_speed_available())return SAN14_E_UNSUPPORTED;
        const int32_t snapped=san14::snap_range_value(value,speed_min_x100,speed_max_x100,speed_step_x100);
        const auto result=apply_speed(snapped);
        if(result==SAN14_OK){speed_x100_value=snapped;save_speed_pref(snapped);}
        return result;
    }
    if(value!=0&&value!=1)return SAN14_E_INVALID_ARGUMENT;
    for(size_t i=0;i<feature_count;++i)if(key.length==std::strlen(features[i].key)&&!std::memcmp(key.data,features[i].key,key.length)){
        const auto result=apply(i,value!=0);
        if(result==SAN14_OK)feature_values[i]=(value!=0);
        return result;
    }
    return SAN14_E_NOT_FOUND;
}
San14Result SAN14_MOD_CALL load(San14ModContext*){
    if(settings_ext_available()){
        const San14RangeSetting speed_spec{
            sizeof(speed_spec),sv(speed_setting_key),sv(u8"游戏加速"),
            sv(u8"基于 Cheat Engine 时钟缩放原理调整游戏速度（1.00x–8.00x，步进 0.25x，1.00x 为关闭）。仅在大地图战局内自动生效，启动、标题与读档界面保持原速。"),
            speed_min_x100,speed_max_x100,speed_step_x100,speed_min_x100,100,sv("x"),changed,nullptr,sv(u8"常规"),
            game_speed_available()?0u:SAN14_SETTING_DISABLED
        };
        const auto speed_result=host->settings_ext->register_range(host->host_context,&speed_spec);
        if(speed_result!=SAN14_OK)return speed_result;
    }
    for(const auto& feature:features){
        const San14BoolSetting spec{sizeof(spec),sv(feature.key),sv(feature.label),sv(feature.help),0,changed,nullptr,sv(feature.category)};
        const auto result=host->settings.register_bool(host->host_context,&spec);if(result!=SAN14_OK)return result;
    }
    if(san14::ExpertPage::supported(host)&&host->struct_size>=offsetof(San14HostApi,pages)+sizeof(host->pages)&&
       host->pages&&host->pages->struct_size>=sizeof(San14PagesApi)&&host->pages->register_page){
        const San14SettingsPageSpec page{sizeof(page),open_settings_page,nullptr};
        const auto page_result=host->pages->register_page(host->host_context,&page);
        if(page_result!=SAN14_OK)return page_result;
    }
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL enable(San14ModContext*){
    active=true;
    {const auto result=enable_spectator();if(result!=SAN14_OK)return result;}
    load_speed_pref(speed_x100_value);
    if(game_speed_available()&&speed_x100_value>speed_min_x100){
        const auto speed_result=apply_speed(speed_x100_value);
        if(speed_result!=SAN14_OK)return speed_result;
    }
    for(size_t i=0;i<feature_count;++i){
        bool value=false;const auto result=load_pref(i,value);
        if(result!=SAN14_OK)return result;
        feature_values[i]=value;
        if(value){const auto applied_result=apply(i,true);if(applied_result!=SAN14_OK)return applied_result;}
    }
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL disable(San14ModContext*){
    bool busy=disable_spectator()!=SAN14_OK;
    if(speed_applied&&apply_speed(speed_min_x100)!=SAN14_OK)busy=true;
    for(size_t i=feature_count;i>0;--i)if((applied[i-1]||features[i-1].service==Service::Report)&&apply(i-1,false)!=SAN14_OK)busy=true;
    bool office_applied=false,area_applied=false;
    for(size_t i=0;i<feature_count;++i){
        if(features[i].service==Service::Office)office_applied=applied[i];
        else if(features[i].service==Service::Area)area_applied=applied[i];
    }
    if(office_registered&&!office_applied){if(host->office->register_policy(host->host_context,nullptr)==SAN14_OK)office_registered=false;else busy=true;}
    if(areas_registered&&!area_applied){
        flush_area_storage();
        unsubscribe_area_events();
        for(auto sub:area_subscriptions)if(sub)busy=true;
        if(host->areas->unregister_policy(host->host_context)==SAN14_OK)areas_registered=false;else busy=true;
    }
    if(busy)return SAN14_E_BUSY;active=false;return SAN14_OK;
}
void SAN14_MOD_CALL unload(San14ModContext*){
    unsubscribe_area_events();
    if(settings_page){settings_page->close();settings_page.reset();}
    active=false;host=nullptr;office_registered=areas_registered=false;
    report_hotkey=0;spectator_idle=0;spectator_panel=0;force_choices={};menu_forces.clear();force_page=0;force_status=0;
    area_storage_revision=0;area_playthrough_id.clear();
    area_state.reset_for_new_playthrough();area_state.set_enabled(false);
    for(auto& value:applied)value=false;
    for(auto& value:feature_values)value=false;
    speed_x100_value=speed_min_x100;speed_applied=false;
}
}
extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(const San14HostApi* h,San14ModApi* out){
    if(!h||!out||h->abi_version!=SAN14_HOST_ABI_V1||out->struct_size<sizeof(*out)||
       h->struct_size<offsetof(San14HostApi,spectator)+sizeof(h->spectator)||
       h->rules.struct_size<sizeof(San14RuleApi)||h->settings.struct_size<sizeof(San14SettingsApi)||
       !h->rules.set_bool||!h->settings.register_bool||!h->settings.get_bool)return SAN14_E_UNSUPPORTED;
    if(active)return SAN14_E_BUSY;
    host=h;*out={sizeof(*out),SAN14_HOST_ABI_V1,load,enable,disable,unload};return SAN14_OK;
}
