#pragma once
#include "san14_scenario.h"
#include <array>
#include <string>
#include <vector>
namespace enhanced_settings_mod {
struct CustomItemDef {
    std::string key,label,help;
    uint32_t kind,group,field;
    int32_t min_val,max_val,step,def_val;
};
inline constexpr size_t kLegacyCustomCount=91;
inline constexpr size_t kMilitaryIntelFirst=kLegacyCustomCount;
inline constexpr size_t kOfficerHireFirst=kMilitaryIntelFirst+4;
inline constexpr size_t kCustomCount=kOfficerHireFirst+7;
inline constexpr const char* military_intel_names[]={u8"无爵位／州刺史",u8"中郎将／州牧",u8"大司马／大将军",u8"皇帝／王／公等"};
inline constexpr const char* officer_hire_names[]={u8"皇帝／王／公等",u8"大司马",u8"大将军",u8"中郎将",u8"州牧",u8"州刺史",u8"无爵位"};
inline constexpr const char* city_names[]={u8"小城",u8"中城",u8"大城",u8"巨城",u8"异族城池"};
inline constexpr const char* formation_names[]={u8"鱼鳞",u8"锋矢",u8"雁行",u8"方圆",u8"鹤翼",u8"长蛇",u8"锥行",u8"井阑",u8"冲车",u8"投石",u8"走舸",u8"蒙冲",u8"楼船",u8"四夷"};
inline const std::vector<CustomItemDef>& custom_defs(){
    static const auto defs=[](){
        std::vector<CustomItemDef> out;
        const char* keys[]={"small","medium","large","mega","tribal"};
        const char* cap_fields[]={"gold","food","troops","durability"};
        const char* labels[]={u8"资金上限",u8"兵粮上限",u8"士兵上限",u8"耐久上限"};
        const int32_t defaults[5][4]={{50000,100000,50000,2000},{100000,200000,70000,3000},{150000,300000,100000,4000},{200000,400000,150000,5000},{250000,500000,300000,9999}};
        for(uint32_t tier=0;tier<5;++tier)for(uint32_t field=0;field<4;++field){
            auto label=std::string(city_names[tier])+u8"・"+labels[field];
            auto help=label+u8"，标准值 "+std::to_string(defaults[tier][field])+u8"。调整为待保存变更，选择保存范围后生效。异族城池与普通城池独立。";
            out.push_back({std::string("city_cap.")+keys[tier]+"."+cap_fields[field],label,help,SAN14_SCENARIO_CUSTOM_CITY_CAP,tier+1,field,field==3?1:0,field==3?9999:1000000,1,defaults[tier][field]});
        }
        out.push_back({"officer_stat_cap",u8"武将基础属性上限",u8"五维基础属性上限 1–255，标准值 100。调整为待保存变更，选择保存范围后生效。",SAN14_SCENARIO_CUSTOM_OFFICER_STAT_CAP,0,0,1,255,1,100});
        struct FormationInit { uint32_t id; const char* name; int32_t defaults[5]; };
        static const FormationInit formations[14]={
            {1,u8"鱼鳞",{14,30,13,6,30}},
            {2,u8"锋矢",{20,45,13,8,18}},
            {3,u8"雁行",{12,30,25,5,12}},
            {4,u8"方圆",{13,10,5,5,40}},
            {5,u8"鹤翼",{16,18,12,13,30}},
            {6,u8"长蛇",{18,21,10,10,21}},
            {7,u8"锥行",{26,5,5,5,5}},
            {8,u8"井阑",{9,10,70,20,20}},
            {9,u8"冲车",{10,10,15,60,18}},
            {10,u8"投石",{12,14,45,45,16}},
            {11,u8"走舸",{10,16,2,2,14}},
            {12,u8"蒙冲",{20,20,2,2,18}},
            {13,u8"楼船",{30,28,2,2,24}},
            {14,u8"四夷",{26,45,13,5,21}},
        };
        struct FieldInit { const char* key; const char* label; const char* desc; };
        static const FieldInit fields[5]={
            {"movement",u8"机动",u8"机动"},
            {"attack_unit",u8"对部队",u8"攻击（对部队）"},
            {"attack_city",u8"对城",u8"攻城（对建筑／城塞）"},
            {"siege",u8"破城",u8"破城"},
            {"defense",u8"防御",u8"防御"},
        };
        for(const auto& f:formations){
            for(uint32_t fi=0;fi<5;++fi){
                std::string k="formation."+std::to_string(f.id)+"."+fields[fi].key;
                std::string lbl=std::string(f.name)+u8"・"+fields[fi].label;
                std::string hlp=std::string(u8"设置")+f.name+u8"的"+fields[fi].desc+
                    u8"数值，范围 0–255。标准值为 "+std::to_string(f.defaults[fi])+u8"。调整为待保存变更，选择保存范围后生效。";
                out.push_back({std::move(k),std::move(lbl),std::move(hlp),
                    SAN14_SCENARIO_CUSTOM_FORMATION,f.id,fi,0,255,1,f.defaults[fi]});
            }
        }
        const int32_t intel_defaults[]={50000,100000,150000,200000};
        for(uint32_t tier=0;tier<4;++tier){
            const auto label=std::string(military_intel_names[tier])+u8"・军情上限";
            const auto help=label+u8"，标准值 "+std::to_string(intel_defaults[tier])+u8"。按势力官爵档位生效，适用于所有势力。修改后选择保存范围才生效。";
            out.push_back({"military_intel_cap."+std::to_string(tier+1),label,help,SAN14_SCENARIO_CUSTOM_MILITARY_INTEL_CAP,tier+1,0,0,1000000,1,intel_defaults[tier]});
        }
        const int32_t hire_defaults[]={40,30,24,20,12,10,5};
        for(uint32_t tier=0;tier<7;++tier){
            const auto label=std::string(officer_hire_names[tier])+u8"・雇用上限";
            const auto help=label+u8"，范围 0–100，标准值 "+std::to_string(hire_defaults[tier])+u8"。按势力官爵档位生效，适用于所有势力。修改后选择保存范围才生效。";
            out.push_back({"officer_hire_cap."+std::to_string(tier+1),label,help,SAN14_SCENARIO_CUSTOM_OFFICER_HIRE_CAP,tier+1,0,0,100,1,hire_defaults[tier]});
        }
        return out;
    }();
    return defs;
}
struct PreferencesV2 {
    int32_t version=2;
    int32_t enhanced_edit=0;
    std::array<int32_t,kCustomCount> values{};
    int32_t extended_merit=0;
    PreferencesV2(){const auto& defs=custom_defs();for(size_t i=0;i<values.size();++i)values[i]=defs[i].def_val;}
    bool valid()const{
        if(version!=2||(enhanced_edit!=0&&enhanced_edit!=1)||(extended_merit!=0&&extended_merit!=1))return false;
        const auto& defs=custom_defs();
        for(size_t i=0;i<values.size();++i)if(values[i]<defs[i].min_val||values[i]>defs[i].max_val)return false;
        return true;
    }
};
struct PreferencesV3 {
    int32_t version=3;
    int32_t enhanced_edit=0;
    std::array<int32_t,kCustomCount> values{};
    int32_t extended_merit=0;
    int32_t extended_traits=0; // Global engine preference; never restored from an older save.
    PreferencesV3(){const auto& defs=custom_defs();for(size_t i=0;i<values.size();++i)values[i]=defs[i].def_val;}
    bool valid()const{
        if(version!=3||(enhanced_edit!=0&&enhanced_edit!=1)||(extended_merit!=0&&extended_merit!=1)||
           (extended_traits!=0&&extended_traits!=1))return false;
        const auto& defs=custom_defs();
        for(size_t i=0;i<values.size();++i)if(values[i]<defs[i].min_val||values[i]>defs[i].max_val)return false;
        return true;
    }
};
// The v4 record retains the v3 value prefix. The mask records user choices
// even when an explicitly saved value happens to equal the game's standard.
struct SettingFields {
    static constexpr size_t edit=kCustomCount,merit=edit+1,count=merit+1;
    std::array<uint32_t,(count+31)/32> words{};
    bool has(size_t field)const{return (words[field/32]&(uint32_t{1}<<(field%32)))!=0;}
    void set(size_t field){words[field/32]|=uint32_t{1}<<(field%32);}
    bool valid()const{
        if constexpr(count%32==0)return true;
        return (words.back()>>(count%32))==0;
    }
};
struct Preferences : PreferencesV3 {
    SettingFields selected;
    bool valid()const{return PreferencesV3::valid()&&selected.valid();}
    void infer_selection(){
        selected={};const auto& defs=custom_defs();
        for(size_t i=0;i<values.size();++i)if(values[i]!=defs[i].def_val)selected.set(i);
        if(enhanced_edit)selected.set(SettingFields::edit);
        if(extended_merit)selected.set(SettingFields::merit);
    }
};
inline bool migrate_v2(const PreferencesV2& legacy,Preferences& out){
    if(!legacy.valid())return false;
    auto next=out;next.enhanced_edit=legacy.enhanced_edit;next.values=legacy.values;next.extended_merit=legacy.extended_merit;
    if(!next.valid())return false;
    out=next;return true;
}
struct LegacyPreferences {
    int32_t version=1;
    int32_t enhanced_edit=0;
    std::array<int32_t,kLegacyCustomCount> values{};
    bool valid()const{
        if(version!=1||(enhanced_edit!=0&&enhanced_edit!=1))return false;
        const auto& defs=custom_defs();
        for(size_t i=0;i<values.size();++i)if(values[i]<defs[i].min_val||values[i]>defs[i].max_val)return false;
        return true;
    }
};
inline bool migrate_legacy(const LegacyPreferences& legacy,Preferences& out){
    if(!legacy.valid())return false;
    auto next=out;
    next.enhanced_edit=legacy.enhanced_edit;
    for(size_t i=0;i<legacy.values.size();++i)next.values[i]=legacy.values[i];
    if(!next.valid())return false;
    out=next;return true;
}
static_assert(sizeof(LegacyPreferences)==93*sizeof(int32_t),"Stable storage v1 wire layout");
static_assert(sizeof(PreferencesV2)==105*sizeof(int32_t),"Stable storage v2 wire layout");
static_assert(sizeof(PreferencesV3)==106*sizeof(int32_t),"Stable storage v3 wire layout");
static_assert(sizeof(Preferences)==sizeof(PreferencesV3)+sizeof(SettingFields),"Stable storage v4 wire layout");
}
