#pragma once
#include "settings_model.h"
namespace enhanced_settings_mod {
struct PendingSettings {
    Preferences value;
    std::array<bool,kCustomCount> fields{};
    bool edit=false,merit=false;
    PendingSettings(const Preferences& before,const Preferences& after):value(after){
        for(size_t i=0;i<fields.size();++i)fields[i]=before.values[i]!=after.values[i];
        edit=before.enhanced_edit!=after.enhanced_edit;merit=before.extended_merit!=after.extended_merit;
    }
    size_t count()const{size_t n=edit+merit;for(auto field:fields)n+=field;return n;}
    Preferences merge(Preferences target)const{
        for(size_t i=0;i<fields.size();++i)if(fields[i]){target.values[i]=value.values[i];target.selected.set(i);}
        if(edit){target.enhanced_edit=value.enhanced_edit;target.selected.set(SettingFields::edit);}
        if(merit){target.extended_merit=value.extended_merit;target.selected.set(SettingFields::merit);}
        return target;
    }
};
}
