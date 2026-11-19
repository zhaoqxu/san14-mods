#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "san14_mod.h"
#include "san14_reload.h"
#include <cassert>
#include <iostream>

namespace {
struct State {unsigned enabled=0,disabled=0;San14Result result=SAN14_OK;};
San14Result SAN14_MOD_CALL apply(void* owner,const San14OfficerTemperamentOverride* value){
    auto& state=*static_cast<State*>(owner);
    if(state.result!=SAN14_OK)return state.result;
    if(value){
        assert(value->struct_size==sizeof(*value)&&value->field_mask==SAN14_TEMPERAMENT_ALL);
        assert(value->values.honor==-10&&value->values.ambition==20&&value->values.han_loyalty==0);
        assert(value->values.aggression==10&&value->values.diplomacy==0&&value->values.construction==10);
        assert(value->values.gold_weight==0&&value->values.food_weight==20&&value->values.troops_weight==0);
        ++state.enabled;
    }else ++state.disabled;
    return SAN14_OK;
}
}
int main(){
    auto module=LoadLibraryW(L"officer_temperament.dll");assert(module);
    using Initialize=San14Result(SAN14_MOD_CALL*)(const San14HostApi*,San14ModApi*);
    auto initialize=reinterpret_cast<Initialize>(GetProcAddress(module,"SAN14Mod_Initialize"));assert(initialize);
    auto prepare=reinterpret_cast<San14ModPrepareReloadV1>(GetProcAddress(module,"SAN14Mod_PrepareReloadV1"));assert(prepare);
    State state;San14HostApi host{};host.struct_size=sizeof(host);host.abi_version=SAN14_HOST_ABI_V1;host.host_context=&state;
    San14WorldApi world{};world.struct_size=offsetof(San14WorldApi,officer_temperament);host.world=&world;
    San14OfficerTemperamentApi service{};service.struct_size=sizeof(service);service.set_uniform_override=apply;
    world.officer_temperament=&service;
    San14ModApi plugin{};plugin.struct_size=sizeof(plugin);
    assert(initialize(&host,&plugin)==SAN14_E_UNSUPPORTED&&state.enabled==0);
    world.struct_size=sizeof(world);service.set_uniform_override=nullptr;
    assert(initialize(&host,&plugin)==SAN14_E_UNSUPPORTED);
    service.set_uniform_override=apply;assert(initialize(&host,&plugin)==SAN14_OK);
    San14ModContext context{};context.struct_size=sizeof(context);
    assert(plugin.on_load(&context)==SAN14_OK);
    state.result=SAN14_E_BUSY;assert(plugin.on_enable(&context)==SAN14_E_BUSY&&state.enabled==0);
    state.result=SAN14_OK;assert(plugin.on_enable(&context)==SAN14_OK&&state.enabled==1);
    assert(prepare(&context)==SAN14_OK&&state.enabled==1&&state.disabled==0);
    state.result=SAN14_E_BUSY;assert(plugin.on_disable(&context)==SAN14_E_BUSY&&state.disabled==0);
    state.result=SAN14_OK;
    assert(plugin.on_disable(&context)==SAN14_OK&&state.disabled==1);
    plugin.on_unload(&context);
    assert(prepare(&context)==SAN14_OK);
    assert(initialize(&host,&plugin)==SAN14_OK);plugin.on_unload(&context);
    FreeLibrary(module);
    std::cout<<"Optional temperament: legacy API rejection, template ownership, failure and release passed\n";
}
