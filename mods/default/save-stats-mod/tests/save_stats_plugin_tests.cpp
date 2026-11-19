#include "../src/save_stats_plugin.cpp"
#include <cassert>
#include <iostream>

namespace test {
std::string str(San14StringView s){return {s.data?s.data:"",s.length};}
struct Fixture {
    San14HostApi api{};San14ModApi mod{};San14ModContext mod_context{};San14SaveStorageApi storage_api{};San14RosterTabsApi tabs_api{};
    San14SaveStorageInfo info{};uint64_t admitted=0,next_sub=0;
    struct Sub {uint32_t id;San14EventCallback callback;void* context;};
    std::map<uint64_t,Sub> subs;
    std::map<std::string,San14BoolSetting> settings;
    std::map<std::string,int32_t> prefs;
    std::vector<San14RosterOfficer> rows;
    std::vector<unsigned char> saved;
    bool has_saved=false,write_fails=false,change_during_capture=false,dead_officers_visible=false,dead_toggle_fails=false;
    int writes=0,captures=0,dead_officer_calls=0;uint32_t visible_mask=0;
    std::vector<std::string> registered_order;
    San14RosterTabRefresh refresh_callback=nullptr;San14RosterTabValue value_callback=nullptr;void* tab_context=nullptr;
    std::vector<std::string> columns;
    San14TerritoryTabsApi territory_api{};
    struct Territory {San14RosterTabRefresh refresh;San14TerritoryTabValue value;void* context;};std::map<uint32_t,Territory> territory;
    static San14Result SAN14_MOD_CALL territory_register(void* p,uint32_t kind,San14StringView key,const San14TerritoryTabSpec* spec){auto& f=self(p);assert((kind==1||kind==2)&&str(key)=="battle"&&str(spec->title)==u8"战斗"&&spec->column_count==1&&str(spec->columns[0].label)==u8"累计阵亡士兵");f.territory[kind]={spec->refresh,spec->value,spec->plugin_context};return SAN14_OK;}
    static San14Result SAN14_MOD_CALL territory_unregister(void* p,uint32_t kind,San14StringView){return self(p).territory.erase(kind)?SAN14_OK:SAN14_E_NOT_FOUND;}
    static San14Result SAN14_MOD_CALL query_deaths(void*,uint32_t kind,uint32_t id,San14BattlefieldDeaths* out){if(id==2)return SAN14_E_NOT_FOUND;*out={sizeof(*out),kind,id,1,(uint64_t(1)<<35)+kind*100+id};return SAN14_OK;}
    San14BattlefieldApi battlefield_api{};uint32_t battlefield_mask=0;bool battlefield_fails=false;
    San14BattlefieldOfficer contribution{sizeof(contribution),1,SAN14_BATTLEFIELD_ALL};
    San14RosterTabRefresh contribution_refresh=nullptr;San14RosterTabValue contribution_value=nullptr;
    static San14Result SAN14_MOD_CALL battlefield_record(void* p,uint32_t mask){auto& f=self(p);if(f.battlefield_fails)return SAN14_E_PLUGIN_FAILED;f.battlefield_mask=mask;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL battlefield_caps(void*,uint32_t* out){*out=SAN14_BATTLEFIELD_ALL;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL battlefield_query(void* p,uint32_t id,San14BattlefieldOfficer* out){*out=self(p).contribution;out->officer_id=id;return SAN14_OK;}
    San14DomesticApi domestic_api{};bool recording=false;
    San14OfficerRecordsApi records_api{};uint32_t records_mask=0;
    San14OfficerHistoryApi history_api{};bool history_recording=false,history_visible=false,history_action_fails=false,history_mask_fails=false;uint32_t history_mask=0;
    static San14Result SAN14_MOD_CALL set_history_mask(void* p,uint32_t value){auto& f=self(p);if(f.history_mask_fails)return SAN14_E_PLUGIN_FAILED;f.history_mask=value;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL history_record(void* p,int32_t value){self(p).history_recording=value!=0;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL history_action(void* p,int32_t value){auto& f=self(p);if(f.history_action_fails&&value)return SAN14_E_PLUGIN_FAILED;f.history_visible=value!=0;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL history_query(void*,uint32_t,uint32_t,void*,uint32_t,uint32_t,uint32_t*,San14OfficerHistoryInfo*){return SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL history_show(void*,uint32_t){return SAN14_E_UNSUPPORTED;}
    San14OfficerRecords outcomes{sizeof(outcomes),1,SAN14_RECORD_ALL};
    San14RosterTabRefresh battle_refresh=nullptr;San14RosterTabValue battle_value=nullptr;
    static San14Result SAN14_MOD_CALL record_results(void* p,uint32_t mask){self(p).records_mask=mask;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL caps(void*,uint32_t* out){*out=SAN14_RECORD_ALL;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL query_results(void* p,uint32_t id,San14OfficerRecords* out){*out=self(p).outcomes;out->officer_id=id;return SAN14_OK;}
    San14RosterTabRefresh domestic_refresh=nullptr;San14RosterTabValue domestic_value=nullptr;
    San14DomesticTotals totals{sizeof(totals),1,10,20,30,100,5};
    static San14Result SAN14_MOD_CALL set_recording(void* p,int32_t on){self(p).recording=on!=0;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL query_domestic(void* p,uint32_t id,San14DomesticTotals* out){auto& f=self(p);*out=f.totals;out->officer_id=id;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL register_named(void* p,San14StringView key,const San14RosterTabSpec* spec){auto& f=self(p);
        if(str(key)=="contribution"){assert(str(spec->title)==u8"战功"&&spec->column_count==8&&spec->visible_mask==255);f.contribution_refresh=spec->refresh;f.contribution_value=spec->value;return SAN14_OK;}
        if(str(key)=="battle"){assert(str(spec->title)==u8"战斗"&&spec->column_count==8&&spec->visible_mask==255);
            for(unsigned i=0;i<8;++i)assert(spec->columns[i].flags==(i==2?SAN14_ROSTER_COLUMN_PERCENT|SAN14_ROSTER_COLUMN_PERCENT_ONE_DECIMAL|SAN14_ROSTER_COLUMN_DASH_IF_MISSING:0u));
            f.battle_refresh=spec->refresh;f.battle_value=spec->value;return SAN14_OK;}
        const unsigned count=f.api.officer_records?8:6;assert(str(key)=="domestic"&&str(spec->title)==u8"内政"&&spec->column_count==count&&spec->visible_mask==((1u<<count)-1));
        for(unsigned i=0;i<count;++i)assert(spec->columns[i].flags==0);
        f.domestic_refresh=spec->refresh;f.domestic_value=spec->value;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL unregister_named(void* p,San14StringView key){auto& f=self(p);if(str(key)=="contribution"){f.contribution_refresh=nullptr;f.contribution_value=nullptr;return SAN14_OK;}if(str(key)=="battle"){f.battle_refresh=nullptr;f.battle_value=nullptr;return SAN14_OK;}assert(str(key)=="domestic");f.domestic_refresh=nullptr;f.domestic_value=nullptr;return SAN14_OK;}
    static Fixture& self(void* p){return *static_cast<Fixture*>(p);}
    static San14Result SAN14_MOD_CALL context(void* p,San14SaveStorageInfo* out){auto& f=self(p);f.admitted=f.info.revision;*out=f.info;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL read(void* p,San14StringView scope,San14StringView key,void* out,uint32_t capacity,uint32_t* required){auto& f=self(p);
        assert(str(scope)=="playthrough"&&str(key)=="officer-growth-v1");if(f.admitted!=f.info.revision)return SAN14_E_STALE_GENERATION;
        if(!f.has_saved)return SAN14_E_NOT_FOUND;*required=static_cast<uint32_t>(f.saved.size());if(!out||capacity<f.saved.size())return SAN14_E_BUFFER_TOO_SMALL;
        std::memcpy(out,f.saved.data(),f.saved.size());return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL write(void* p,San14StringView scope,San14StringView key,const void* data,uint32_t count){auto& f=self(p);
        assert(str(scope)=="playthrough"&&str(key)=="officer-growth-v1");if(f.admitted!=f.info.revision)return SAN14_E_STALE_GENERATION;if(f.write_fails)return SAN14_E_PLUGIN_FAILED;
        auto bytes=static_cast<const unsigned char*>(data);f.saved.assign(bytes,bytes+count);f.has_saved=true;++f.writes;return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL capture(void* p,San14RosterInfo* info,void* out,uint32_t stride,uint32_t capacity,uint32_t* required){auto& f=self(p);++f.captures;
        assert(stride==sizeof(San14RosterOfficer));*info={};info->struct_size=sizeof(*info);info->officer_count=static_cast<uint32_t>(f.rows.size());*required=info->officer_count;
        if(capacity<f.rows.size())return SAN14_E_BUFFER_TOO_SMALL;std::memcpy(out,f.rows.data(),f.rows.size()*sizeof(San14RosterOfficer));
        if(f.change_during_capture){++f.info.revision;f.change_during_capture=false;}return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL subscribe(void* p,uint32_t id,int32_t,San14EventCallback callback,void* c,San14Subscription* out){auto& f=self(p);*out=++f.next_sub;f.subs[*out]={id,callback,c};return SAN14_OK;}
    static San14Result SAN14_MOD_CALL unsubscribe(void* p,San14Subscription sub){return self(p).subs.erase(sub)?SAN14_OK:SAN14_E_NOT_FOUND;}
    static San14Result SAN14_MOD_CALL register_bool(void* p,const San14BoolSetting* setting){auto& f=self(p);auto key=str(setting->key);auto cat=str(setting->category);
        if(key=="general.show_dead_officers")assert(cat==u8"常规");else if(key=="general.officer_deeds"||key.rfind("history.",0)==0)assert(cat==u8"事迹");else if(key=="domestic.recording")assert(cat==u8"内政统计");else if(key=="battle.recording"||key=="battlefield.recording")assert(cat==u8"战斗统计");else assert(cat==u8"武将成长统计");
        f.registered_order.push_back(key);f.settings[key]=*setting;f.prefs[key]=setting->default_value;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL get_bool(void* p,San14StringView key,int32_t* out){auto& f=self(p);auto it=f.prefs.find(str(key));if(it==f.prefs.end())return SAN14_E_NOT_FOUND;*out=it->second;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL register_tab(void* p,const San14RosterTabSpec* spec){auto& f=self(p);assert(!f.refresh_callback&&str(spec->title)==u8"成长"&&spec->column_count==6);
        f.columns.clear();for(uint32_t i=0;i<6;++i){f.columns.push_back(str(spec->columns[i].label));assert(spec->columns[i].flags==SAN14_ROSTER_COLUMN_SIGNED);}f.visible_mask=spec->visible_mask;
        f.refresh_callback=spec->refresh;f.value_callback=spec->value;f.tab_context=spec->plugin_context;return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL visibility(void* p,uint32_t mask){self(p).visible_mask=mask;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL unregister_tab(void* p){auto& f=self(p);if(!f.refresh_callback)return SAN14_E_NOT_FOUND;f.refresh_callback=nullptr;f.value_callback=nullptr;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL set_dead_officers(void* p,int32_t enabled){auto& f=self(p);if(enabled<0||enabled>1)return SAN14_E_INVALID_ARGUMENT;if(f.dead_toggle_fails)return SAN14_E_PLUGIN_FAILED;f.dead_officers_visible=enabled!=0;++f.dead_officer_calls;return SAN14_OK;}
    Fixture(bool history=false,bool filters=true){
        info.struct_size=sizeof(info);info.state=SAN14_STORAGE_READY;info.revision=1;strcpy_s(info.playthrough_id,"0123456789abcdef0123456789abcdef");
        api.struct_size=sizeof(api);api.abi_version=SAN14_HOST_ABI_V1;api.host_context=this;
        if(history){history_api={sizeof(history_api),history_record,history_query,history_show,history_action,filters?set_history_mask:nullptr};if(!filters)history_api.struct_size=SAN14_OFFICER_HISTORY_API_V1_SIZE;api.officer_history=&history_api;}
        storage_api={sizeof(storage_api),context,nullptr};tabs_api={sizeof(tabs_api),register_tab,visibility,unregister_tab,set_dead_officers};api.save_storage=&storage_api;api.roster_tabs=&tabs_api;
        api.storage={sizeof(api.storage),read,write};api.roster={sizeof(api.roster),capture};api.events={sizeof(api.events),subscribe,unsubscribe};api.settings={sizeof(api.settings),register_bool,get_bool};
        mod.struct_size=sizeof(mod);mod_context.struct_size=sizeof(mod_context);assert(SAN14Mod_Initialize(&api,&mod)==SAN14_OK);assert(mod.on_load(&mod_context)==SAN14_OK);
        assert(settings.size()==27&&registered_order[0]=="general.show_dead_officers"&&registered_order[1]=="general.officer_deeds");
        assert(settings.at("general.show_dead_officers").default_value==0&&settings.at("general.show_dead_officers").flags==0&&settings.at("general.show_dead_officers").changed);
        assert(settings.at("general.officer_deeds").default_value==(history?1:0)&&settings.at("general.officer_deeds").flags==(history?0u:SAN14_BOOL_SETTING_DISABLED)&&settings.at("general.officer_deeds").changed);
        assert(str(settings.at("general.officer_deeds").description).find(u8"全势力（含电脑）")!=std::string::npos);
        for(uint32_t i=1;i<=2;++i){San14RosterOfficer row{};row.struct_size=sizeof(row);row.officer_id=i;row.available_fields=SAN14_ROSTER_NAMES|SAN14_ROSTER_BASE_STATS;
            strcpy_s(row.name_utf8,i==1?u8"赵云":u8"张飞");strcpy_s(row.courtesy_utf8,i==1?u8"子龙":u8"益德");for(auto& stat:row.base_stats)stat=70;rows.push_back(row);}
        assert(mod.on_enable(&mod_context)==SAN14_OK);assert(visible_mask==63&&columns[0]==u8"总成长"&&columns[1]==u8"统率"&&!dead_officers_visible);
    }
    ~Fixture(){assert(mod.on_disable(&mod_context)==SAN14_OK);mod.on_unload(&mod_context);assert(subs.empty()&&!refresh_callback);}
    void send(uint32_t id,uint64_t notice_revision=0){San14StorageEventInfo notice{sizeof(notice),info.state,notice_revision?notice_revision:info.revision,SAN14_OK,0};San14EventView event{sizeof(event),id,notice.revision,&notice,sizeof(notice),0};auto copy=subs;for(const auto& s:copy)if(s.second.id==id)s.second.callback(s.second.context,&event);}
    void start(){++info.revision;has_saved=false;saved.clear();send(SAN14_EVENT_STORAGE_CHANGED);send(SAN14_EVENT_NEW_GAME);}
    void refresh(){assert(refresh_callback(tab_context)==SAN14_OK);}
    San14Result get(uint32_t id,uint32_t col,int32_t& out){return value_callback(tab_context,id,col,&out);}
};
}
int main(){
    using test::Fixture;
    {Fixture f;auto& toggle=f.settings.at("general.show_dead_officers");
        assert(toggle.changed(nullptr,toggle.key,1)==SAN14_OK&&f.dead_officers_visible);
        f.prefs["general.show_dead_officers"]=1;
        assert(f.mod.on_disable(&f.mod_context)==SAN14_OK&&!f.dead_officers_visible);
        assert(f.mod.on_enable(&f.mod_context)==SAN14_OK&&f.dead_officers_visible);
        f.dead_toggle_fails=true;
        assert(toggle.changed(nullptr,toggle.key,0)==SAN14_E_PLUGIN_FAILED&&f.dead_officers_visible);
        f.dead_toggle_fails=false;
        assert(toggle.changed(nullptr,toggle.key,0)==SAN14_OK&&!f.dead_officers_visible);
        assert(toggle.changed(nullptr,toggle.key,2)==SAN14_E_INVALID_ARGUMENT);
    }
    {Fixture f;int32_t n=123;
        f.refresh();assert(f.get(1,0,n)==SAN14_E_NOT_FOUND&&f.writes==0&&f.captures==0);
        // An already queued event from before activation cannot invent a baseline.
        f.send(SAN14_EVENT_NEW_GAME);assert(f.writes==0);
        f.start();assert(f.writes==1&&f.captures==2);const auto opening=f.saved;
        f.rows[0].base_stats[0]+=6;f.rows[0].base_stats[1]-=2;f.rows[0].base_stats[3]+=3;f.rows[0].base_stats[4]-=1;
        f.refresh();assert(f.get(1,0,n)==SAN14_OK&&n==6);assert(f.get(1,1,n)==SAN14_OK&&n==6);assert(f.get(1,2,n)==SAN14_OK&&n==-2);assert(f.get(1,3,n)==SAN14_OK&&n==0);
        const auto capture_count=f.captures;for(unsigned i=0;i<500;++i)assert(f.get(1,0,n)==SAN14_OK);assert(f.captures==capture_count);
        auto& s=f.settings.at("growth.martial");assert(s.changed(s.plugin_context,s.key,0)==SAN14_OK&&f.visible_mask==59);assert(f.get(1,0,n)==SAN14_OK&&n==6);
        assert(!f.dead_officers_visible);
        strcpy_s(f.rows[1].courtesy_utf8,u8"换人");f.refresh();assert(f.get(2,0,n)==SAN14_E_NOT_FOUND);
        // Replay/switch loads the saved baseline, and never recaptures it.
        ++f.info.revision;f.saved=opening;f.send(SAN14_EVENT_STORAGE_CHANGED);f.rows[0].base_stats[0]=72;f.refresh();assert(f.get(1,1,n)==SAN14_OK&&n==2&&f.writes==1);
        assert(f.mod.on_disable(&f.mod_context)==SAN14_OK&&!f.dead_officers_visible);assert(f.mod.on_enable(&f.mod_context)==SAN14_OK&&!f.dead_officers_visible);f.refresh();assert(f.get(1,1,n)==SAN14_OK&&n==2);
        ++f.info.revision;f.has_saved=false;f.send(SAN14_EVENT_STORAGE_CHANGED);f.refresh();assert(f.get(1,0,n)==SAN14_E_NOT_FOUND&&f.writes==1);
    }
    {Fixture f;f.write_fails=true;f.start();int32_t n;f.refresh();assert(f.writes==0&&f.get(1,0,n)==SAN14_E_NOT_FOUND);}
    {Fixture f;f.change_during_capture=true;f.start();int32_t n;f.refresh();assert(f.writes==0&&f.get(1,0,n)==SAN14_E_NOT_FOUND);}
    {Fixture f;++f.info.revision;f.has_saved=true;f.saved={'n','e','w','e','r','!','!','!'};const auto before=f.saved;f.send(SAN14_EVENT_NEW_GAME);f.refresh();int32_t n;assert(f.writes==0&&f.saved==before&&f.get(1,0,n)==SAN14_E_NOT_FOUND);}
    {Fixture f;f.rows[1].available_fields=SAN14_ROSTER_NAMES;f.start();assert(f.writes==0);}
    {Fixture f;assert(f.mod.on_disable(&f.mod_context)==SAN14_OK);
        f.domestic_api={sizeof(San14DomesticApi),Fixture::set_recording,Fixture::query_domestic};f.api.domestic=&f.domestic_api;
        f.tabs_api.register_named_tab=Fixture::register_named;f.tabs_api.unregister_named_tab=Fixture::unregister_named;
        assert(f.mod.on_enable(&f.mod_context)==SAN14_OK&&f.recording&&f.domestic_refresh);
        // Existing saves need no growth baseline to start actual contributions.
        assert(f.domestic_refresh(nullptr)==SAN14_OK);int32_t n=0;
        assert(f.domestic_value(nullptr,1,0,&n)==SAN14_OK&&n==60);
        assert(f.domestic_value(nullptr,1,4,&n)==SAN14_OK&&n==100);
        assert(f.domestic_value(nullptr,1,5,&n)==SAN14_OK&&n==5);
        auto& toggle=f.settings.at("domestic.recording");assert(toggle.changed(nullptr,toggle.key,0)==SAN14_OK&&!f.recording);
        f.totals.commerce=UINT64_MAX;assert(f.domestic_refresh(nullptr)==SAN14_OK);assert(f.domestic_value(nullptr,1,0,&n)==SAN14_OK&&n==INT32_MAX);
        assert(f.mod.on_disable(&f.mod_context)==SAN14_OK&&!f.recording&&!f.domestic_refresh);
    }
    {Fixture f;assert(f.mod.on_disable(&f.mod_context)==SAN14_OK);
        f.domestic_api={sizeof(San14DomesticApi),Fixture::set_recording,Fixture::query_domestic};f.api.domestic=&f.domestic_api;
        f.records_api={sizeof(San14OfficerRecordsApi),Fixture::record_results,Fixture::query_results,Fixture::caps};f.api.officer_records=&f.records_api;
        f.tabs_api.register_named_tab=Fixture::register_named;f.tabs_api.unregister_named_tab=Fixture::unregister_named;
        assert(f.mod.on_enable(&f.mod_context)==SAN14_OK&&f.records_mask==63&&f.battle_refresh&&f.domestic_refresh);
        int32_t n=99;assert(f.battle_refresh(nullptr)==SAN14_OK);
        assert(f.battle_value(nullptr,1,0,&n)==SAN14_OK&&n==0);
        assert(f.battle_value(nullptr,1,2,&n)==SAN14_E_NOT_FOUND);
        f.outcomes.duel_losses=3;assert(f.battle_refresh(nullptr)==SAN14_OK);
        assert(f.battle_value(nullptr,1,2,&n)==SAN14_OK&&n==0);
        f.outcomes.duel_wins=1;f.outcomes.duel_losses=2;assert(f.battle_refresh(nullptr)==SAN14_OK);
        assert(f.battle_value(nullptr,1,2,&n)==SAN14_OK&&n==3330);
        f.outcomes.duel_wins=33449;f.outcomes.duel_losses=66551;assert(f.battle_refresh(nullptr)==SAN14_OK);
        assert(f.battle_value(nullptr,1,2,&n)==SAN14_OK&&n==3340); // 33.449% -> 33.4%, without double rounding
        f.outcomes.duel_wins=UINT64_MAX;f.outcomes.duel_losses=UINT64_MAX;f.outcomes.units_destroyed=12;f.outcomes.times_captured=7;
        assert(f.battle_refresh(nullptr)==SAN14_OK&&f.battle_value(nullptr,1,2,&n)==SAN14_OK&&n==5000);
        assert(f.battle_value(nullptr,1,0,&n)==SAN14_OK&&n==INT32_MAX);
        assert(f.battle_value(nullptr,1,3,&n)==SAN14_OK&&n==12&&f.battle_value(nullptr,1,7,&n)==SAN14_OK&&n==7);
        f.outcomes.employ_success=4;f.outcomes.diplomacy_success=5;assert(f.domestic_refresh(nullptr)==SAN14_OK);
        assert(f.domestic_value(nullptr,1,6,&n)==SAN14_OK&&n==4&&f.domestic_value(nullptr,1,7,&n)==SAN14_OK&&n==5);
        auto& domestic=f.settings.at("domestic.recording");auto& battle=f.settings.at("battle.recording");
        assert(domestic.changed(nullptr,domestic.key,0)==SAN14_OK&&f.records_mask==15);
        assert(battle.changed(nullptr,battle.key,0)==SAN14_OK&&f.records_mask==0);
        assert(domestic.changed(nullptr,domestic.key,1)==SAN14_OK&&f.records_mask==48);
        ++f.info.revision;f.send(SAN14_EVENT_STORAGE_CHANGED);assert(f.battle_value(nullptr,1,0,&n)==SAN14_E_NOT_FOUND);
        assert(f.mod.on_disable(&f.mod_context)==SAN14_OK&&!f.records_mask&&!f.battle_refresh&&!f.domestic_refresh);
    }
    {Fixture f(true);assert(f.history_recording&&f.history_visible);
        auto& history=f.settings.at("general.officer_deeds");auto& battle=f.settings.at("battle.recording");auto& domestic=f.settings.at("domestic.recording");
        assert(battle.changed(nullptr,battle.key,0)==SAN14_OK&&domestic.changed(nullptr,domestic.key,0)==SAN14_OK&&f.history_recording);
        assert(history.changed(nullptr,history.key,0)==SAN14_OK&&!f.history_recording&&!f.history_visible);
        f.history_action_fails=true;assert(history.changed(nullptr,history.key,1)==SAN14_E_PLUGIN_FAILED&&!f.history_recording&&!f.history_visible);
        f.history_action_fails=false;assert(history.changed(nullptr,history.key,1)==SAN14_OK&&f.history_recording&&f.history_visible);
        assert(f.mod.on_disable(&f.mod_context)==SAN14_OK&&!f.history_recording&&!f.history_visible);
    }
    {Fixture f(true);assert(f.history_mask==SAN14_HISTORY_RECORD_ALL);
        assert(f.registered_order[1]=="general.officer_deeds"&&f.registered_order[2]=="history.duel_win");
        for(const auto& option:history_options){const auto& spec=f.settings.at(option.key);assert(spec.default_value==1&&spec.flags==0&&test::str(spec.category)==u8"事迹");}
        auto& option=f.settings.at("history.employ");assert(option.changed(nullptr,option.key,0)==SAN14_OK&&!(f.history_mask&SAN14_HISTORY_RECORD_EMPLOY));
        const auto before=f.history_mask;f.history_mask_fails=true;assert(option.changed(nullptr,option.key,1)==SAN14_E_PLUGIN_FAILED&&f.history_mask==before&&history_choices==before);f.history_mask_fails=false;
        auto& master=f.settings.at("general.officer_deeds");assert(master.changed(nullptr,master.key,0)==SAN14_OK&&!f.history_recording);
        auto& death=f.settings.at("history.death");assert(death.changed(nullptr,death.key,0)==SAN14_OK);assert(master.changed(nullptr,master.key,1)==SAN14_OK&&!(f.history_mask&SAN14_HISTORY_RECORD_DEATH)&&!(f.history_mask&SAN14_HISTORY_RECORD_EMPLOY));
    }
    {Fixture f(true,false);assert(f.history_recording&&f.history_visible);for(const auto& option:history_options)assert(f.settings.at(option.key).flags==SAN14_BOOL_SETTING_DISABLED);}
    {Fixture f;assert(f.mod.on_disable(&f.mod_context)==SAN14_OK);
        f.battlefield_api={sizeof(f.battlefield_api),Fixture::battlefield_record,Fixture::battlefield_caps,nullptr,Fixture::battlefield_query,nullptr};f.battlefield_api.query_deaths=Fixture::query_deaths;f.api.battlefield=&f.battlefield_api;
        f.territory_api={sizeof(f.territory_api),Fixture::territory_register,Fixture::territory_unregister};f.api.territory_tabs=&f.territory_api;
        f.tabs_api.register_named_tab=Fixture::register_named;f.tabs_api.unregister_named_tab=Fixture::unregister_named;
        f.prefs["battlefield.recording"]=1;assert(f.mod.on_enable(&f.mod_context)==SAN14_OK&&f.battlefield_mask==SAN14_BATTLEFIELD_ALL&&f.contribution_refresh);
        assert(f.territory.size()==2);for(unsigned kind=1;kind<=2;++kind){auto t=f.territory.at(kind);assert(t.refresh(t.context)==SAN14_OK);int64_t n64=0;assert(t.value(t.context,1,0,&n64)==SAN14_OK&&n64==(int64_t(1)<<35)+kind*100+1);assert(t.value(t.context,2,0,&n64)==SAN14_E_NOT_FOUND);}
        f.contribution.field_damage=123;f.contribution.tactics_used=4;f.contribution.links_joined=2;f.contribution.links_started=1;f.contribution.links_followed=1;
        assert(f.contribution_refresh(nullptr)==SAN14_OK);int32_t n=0;assert(f.contribution_value(nullptr,1,0,&n)==SAN14_OK&&n==123);assert(f.contribution_value(nullptr,1,4,&n)==SAN14_OK&&n==4);assert(f.contribution_value(nullptr,1,7,&n)==SAN14_OK&&n==1);
        f.contribution.available_categories=0;assert(f.contribution_refresh(nullptr)==SAN14_OK&&f.contribution_value(nullptr,1,0,&n)==SAN14_E_NOT_FOUND);
        f.contribution.available_categories=15;f.contribution.field_damage=UINT64_MAX;assert(f.contribution_refresh(nullptr)==SAN14_OK&&f.contribution_value(nullptr,1,0,&n)==SAN14_OK&&n==INT32_MAX);
        auto& toggle=f.settings.at("battlefield.recording");f.battlefield_fails=true;assert(toggle.changed(nullptr,toggle.key,0)==SAN14_E_PLUGIN_FAILED&&f.battlefield_mask==SAN14_BATTLEFIELD_ALL);f.battlefield_fails=false;assert(toggle.changed(nullptr,toggle.key,0)==SAN14_OK&&!f.battlefield_mask);
        ++f.info.revision;f.send(SAN14_EVENT_STORAGE_CHANGED);assert(f.contribution_value(nullptr,1,0,&n)==SAN14_E_NOT_FOUND);
        assert(f.mod.on_disable(&f.mod_context)==SAN14_OK&&!f.battlefield_mask&&!f.contribution_refresh&&f.territory.empty());
    }
    std::cout<<"Save stats plugin: baseline, rollback, columns, independent global history lease/action, failure rollback and lifecycle passed\n";
}
