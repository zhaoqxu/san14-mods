#pragma once
// UI-only cooperative tasks, idle subscriptions and trusted Host storage
// notifications. This module owns no game hooks and synthesizes no lifecycle.
#include "san14_host.h"
#include "plugin_call_gate.h"
#include "san14_idle.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace plugin_tasks {

struct Backend {
    void* user=nullptr;
    bool (*on_ui)(void*)=nullptr;
    bool (*owner_allowed)(void*,void* owner)=nullptr;
    bool (*event_allowed)(void*,void* owner,uint32_t event_id)=nullptr;
    // The embedding Base must protect these callback invocations with its
    // native exception/SEH boundary. Context belongs to the plugin, not a game object.
    void (*invoke_task)(void*,San14TaskCallback,void* plugin_context)=nullptr;
    void (*invoke_event)(void*,San14EventCallback,void* plugin_context,const San14EventView*)=nullptr;
    // Optional testing/embedding clock; default is steady_clock in microseconds.
    uint64_t (*clock_us)(void*)=nullptr;
    // Optional thread gate for Host storage-event publish(); defaults to on_ui.
    bool (*on_event_publish)(void*)=nullptr;
    plugin_calls::Hooks calls;
};

struct Limits {
    size_t tasks=4096,owner_tasks=512,subscriptions=1024,owner_subscriptions=128;
    uint32_t steps_per_pump=128;
};
struct PumpStats {
    uint32_t work_steps=0,tasks_executed=0,events_delivered=0,inactive_dropped=0,callback_failures=0;
    bool budget_exhausted=false,step_limit_reached=false;
};

class Runtime {
    struct Task {void* owner;San14TaskCallback callback;void* context;};
    struct Subscription {void* owner;uint32_t event_id;int32_t priority;San14EventCallback callback;void* context;};
    struct Work {San14Handle handle;bool event;};
    Backend backend_;
    Limits limits_;
    std::map<San14TaskHandle,Task> tasks_;
    std::map<San14Subscription,Subscription> subscriptions_;
    uint64_t serial_=1,last_ms_=0;
    bool pumping_=false;
    PumpStats last_pump_;
    inline static Runtime* bound_=nullptr;

    template<class F>static San14Result guard(F&& f)noexcept {
        try{return f();}catch(...){return SAN14_E_PLUGIN_FAILED;}
    }
    void bind() {
        if(bound_&&bound_!=this)throw std::logic_error("Plugin task runtime already bound");
        bound_=this;
    }
    San14Result access(void* owner)const {
        if(!backend_.on_ui||!backend_.owner_allowed)return SAN14_E_UNSUPPORTED;
        if(!backend_.on_ui(backend_.user))return SAN14_E_WRONG_THREAD;
        if(!owner||!backend_.owner_allowed(backend_.user,owner))return SAN14_E_PERMISSION_DENIED;
        return SAN14_OK;
    }
    San14Handle fresh() {
        // Separate service namespace, never reused within this runtime's life.
        if(serial_>UINT32_MAX)throw std::overflow_error("Plugin task handles exhausted");
        return UINT64_C(0x53414e5400000000)|serial_++;
    }
    uint64_t clock_us()const {
        if(backend_.clock_us)return backend_.clock_us(backend_.user);
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    static uint64_t elapsed(uint64_t start,uint64_t now){return now>=start?now-start:0;}
    template<class Map>static size_t owned(const Map& map,void* owner) {
        size_t result=0;for(const auto& item:map)if(item.second.owner==owner)++result;return result;
    }
    bool live_owner(void* owner)const{return backend_.owner_allowed&&backend_.owner_allowed(backend_.user,owner);}
public:
    static bool storage_event(uint32_t id)noexcept {
        return id>=SAN14_EVENT_SAVE_LOADING&&id<=SAN14_EVENT_NEW_GAME;
    }
    explicit Runtime(Backend backend,Limits limits={}):backend_(backend),limits_(limits) {
        if(!limits.tasks||!limits.owner_tasks||!limits.subscriptions||!limits.owner_subscriptions||
           !limits.steps_per_pump||limits.tasks>65536||limits.subscriptions>16384||limits.steps_per_pump>4096)
            throw std::invalid_argument("Invalid plugin task limits");
    }
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    ~Runtime(){if(bound_==this)bound_=nullptr;}
    San14SchedulerApi scheduler_api() {
        bind();return {sizeof(San14SchedulerApi),api_dispatch,api_cancel};
    }
    San14EventApi event_api() {
        bind();return {sizeof(San14EventApi),api_subscribe,api_unsubscribe};
    }
    size_t task_count()const{return tasks_.size();}
    size_t subscription_count()const{return subscriptions_.size();}
    bool pumping()const{return pumping_;}
    const PumpStats& last_pump()const{return last_pump_;}

    San14Result dispatch(void* owner,San14Thread thread,San14TaskCallback callback,void* context,San14TaskHandle* output)noexcept {
        if(output)*output=0;
        return guard([&]{
            const auto allowed=access(owner);if(allowed!=SAN14_OK)return allowed;
            if(thread==SAN14_THREAD_GAME||thread==SAN14_THREAD_WORKER)return SAN14_E_UNSUPPORTED;
            if(thread!=SAN14_THREAD_UI||!callback||!output)return SAN14_E_INVALID_ARGUMENT;
            if(!backend_.invoke_task)return SAN14_E_UNSUPPORTED;
            if(tasks_.size()>=limits_.tasks||owned(tasks_,owner)>=limits_.owner_tasks)return SAN14_E_BUSY;
            const auto handle=fresh();tasks_.emplace(handle,Task{owner,callback,context});*output=handle;return SAN14_OK;
        });
    }
    San14Result cancel(void* owner,San14TaskHandle handle)noexcept {
        return guard([&]{
            const auto allowed=access(owner);if(allowed!=SAN14_OK)return allowed;
            const auto found=tasks_.find(handle);if(found==tasks_.end())return SAN14_E_NOT_FOUND;
            if(found->second.owner!=owner)return SAN14_E_PERMISSION_DENIED;
            tasks_.erase(found);return SAN14_OK;
        });
    }
    San14Result subscribe(void* owner,uint32_t event_id,int32_t priority,San14EventCallback callback,void* context,San14Subscription* output)noexcept {
        if(output)*output=0;
        return guard([&]{
            const auto allowed=access(owner);if(allowed!=SAN14_OK)return allowed;
            if(event_id!=SAN14_EVENT_UI_IDLE&&!storage_event(event_id))return SAN14_E_UNSUPPORTED;
            if(!callback||!output)return SAN14_E_INVALID_ARGUMENT;
            if(!backend_.event_allowed||!backend_.invoke_event)return SAN14_E_UNSUPPORTED;
            if(!backend_.event_allowed(backend_.user,owner,event_id))return SAN14_E_PERMISSION_DENIED;
            if(subscriptions_.size()>=limits_.subscriptions||owned(subscriptions_,owner)>=limits_.owner_subscriptions)return SAN14_E_BUSY;
            const auto handle=fresh();subscriptions_.emplace(handle,Subscription{owner,event_id,priority,callback,context});*output=handle;return SAN14_OK;
        });
    }
    San14Result unsubscribe(void* owner,San14Subscription handle)noexcept {
        return guard([&]{
            const auto allowed=access(owner);if(allowed!=SAN14_OK)return allowed;
            const auto found=subscriptions_.find(handle);if(found==subscriptions_.end())return SAN14_E_NOT_FOUND;
            if(found->second.owner!=owner)return SAN14_E_PERMISSION_DENIED;
            subscriptions_.erase(found);return SAN14_OK;
        });
    }
    // Trusted Host cleanup does not require an active owner: disable has usually
    // revoked permission already. Erase before later snapshot lookups can invoke.
    San14Result cleanup_owner(void* owner)noexcept {
        return guard([&]{
            if(!backend_.on_ui)return SAN14_E_UNSUPPORTED;
            if(!backend_.on_ui(backend_.user))return SAN14_E_WRONG_THREAD;
            for(auto i=tasks_.begin();i!=tasks_.end();)if(i->second.owner==owner)i=tasks_.erase(i);else ++i;
            for(auto i=subscriptions_.begin();i!=subscriptions_.end();)if(i->second.owner==owner)i=subscriptions_.erase(i);else ++i;
            return SAN14_OK;
        });
    }

    // Trusted Host entry: the adapter queues copied observations and calls this
    // on the UI thread. Membership is fixed at entry, but owner permission and
    // subscription existence are rechecked before every callback. Neither the
    // view nor its payload outlives this call. Idle remains owned by pump().
    San14Result publish(const San14EventView& view)noexcept {
        return guard([&]{
            const auto thread_ok=backend_.on_event_publish?backend_.on_event_publish:backend_.on_ui;
            if(!thread_ok||!backend_.owner_allowed||!backend_.event_allowed||!backend_.invoke_event)return SAN14_E_UNSUPPORTED;
            if(!thread_ok(backend_.user))return SAN14_E_WRONG_THREAD;
            if(pumping_)return SAN14_E_BUSY;
            if(!storage_event(view.event_id))return SAN14_E_UNSUPPORTED;
            if(view.struct_size<sizeof(view)||view.reserved||!view.payload||view.payload_size<sizeof(San14StorageEventInfo))return SAN14_E_INVALID_ARGUMENT;
            const auto& info=*static_cast<const San14StorageEventInfo*>(view.payload);
            if(info.struct_size<sizeof(info)||info.struct_size>view.payload_size||info.state>SAN14_STORAGE_ERROR||info.reserved)return SAN14_E_INVALID_ARGUMENT;
            pumping_=true;struct Reset{bool& flag;~Reset(){flag=false;}} reset{pumping_};
            std::vector<std::pair<San14Subscription,int32_t>> events;
            for(const auto& item:subscriptions_)if(item.second.event_id==view.event_id)events.emplace_back(item.first,item.second.priority);
            std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.second!=b.second?a.second>b.second:a.first<b.first;});
            bool failed=false;
            for(const auto& item:events){
                const auto found=subscriptions_.find(item.first);if(found==subscriptions_.end())continue;
                const Subscription event=found->second;
                if(!live_owner(event.owner)||!backend_.event_allowed(backend_.user,event.owner,view.event_id)){subscriptions_.erase(found);continue;}
                try{plugin_calls::Lease lease(backend_.calls,event.owner);if(lease)backend_.invoke_event(backend_.user,event.callback,event.context,&view);}catch(...){failed=true;}
            }
            return failed?SAN14_E_PLUGIN_FAILED:SAN14_OK;
        });
    }

    San14Result pump(uint64_t monotonic_ms,uint32_t budget_us=4000)noexcept {
        return guard([&]{
            if(!backend_.on_ui||!backend_.owner_allowed)return SAN14_E_UNSUPPORTED;
            if(!backend_.on_ui(backend_.user))return SAN14_E_WRONG_THREAD;
            if(pumping_)return SAN14_E_BUSY;
            if(!budget_us||budget_us>1000000)return SAN14_E_INVALID_ARGUMENT;
            pumping_=true;struct Reset{bool& flag;~Reset(){flag=false;}} reset{pumping_};
            last_pump_={};last_ms_=std::max(last_ms_,monotonic_ms);
            const auto started=clock_us();
            std::vector<San14TaskHandle> tasks;tasks.reserve(tasks_.size());
            for(const auto& item:tasks_)tasks.push_back(item.first);
            std::vector<std::pair<San14Subscription,int32_t>> events;events.reserve(subscriptions_.size());
            for(const auto& item:subscriptions_)if(item.second.event_id==SAN14_EVENT_UI_IDLE)events.emplace_back(item.first,item.second.priority);
            std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){
                return a.second!=b.second?a.second>b.second:a.first<b.first;
            });
            // Interleave one-shot FIFO and priority-ordered idle subscribers.
            // Membership is frozen at entry; newly queued work waits for next pump.
            std::vector<Work> work;work.reserve(tasks.size()+events.size());
            for(size_t t=0,e=0;t<tasks.size()||e<events.size();){
                if(t<tasks.size())work.push_back({tasks[t++],false});
                if(e<events.size())work.push_back({events[e++].first,true});
            }
            for(const auto& item:work) {
                if(last_pump_.work_steps>=limits_.steps_per_pump){last_pump_.step_limit_reached=true;break;}
                const auto spent=elapsed(started,clock_us());
                if(spent>=budget_us){last_pump_.budget_exhausted=true;break;}
                ++last_pump_.work_steps;
                if(!item.event) {
                    const auto found=tasks_.find(item.handle);if(found==tasks_.end())continue;
                    const Task task=found->second;tasks_.erase(found); // consume before user callback
                    if(!live_owner(task.owner)||!backend_.invoke_task){++last_pump_.inactive_dropped;continue;}
                    ++last_pump_.tasks_executed;
                    try{plugin_calls::Lease lease(backend_.calls,task.owner);if(lease)backend_.invoke_task(backend_.user,task.callback,task.context);}
                    catch(...){++last_pump_.callback_failures;}
                }else {
                    const auto found=subscriptions_.find(item.handle);if(found==subscriptions_.end())continue;
                    const Subscription event=found->second;
                    if(!live_owner(event.owner)||!backend_.event_allowed||!backend_.invoke_event||
                       !backend_.event_allowed(backend_.user,event.owner,SAN14_EVENT_UI_IDLE)) {
                        subscriptions_.erase(found);++last_pump_.inactive_dropped;continue;
                    }
                    const San14IdleInfo info{sizeof(San14IdleInfo),last_ms_,static_cast<uint32_t>(budget_us-spent)};
                    const San14EventView view{sizeof(San14EventView),SAN14_EVENT_UI_IDLE,0,&info,sizeof(info),0};
                    ++last_pump_.events_delivered;
                    try{plugin_calls::Lease lease(backend_.calls,event.owner);if(lease)backend_.invoke_event(backend_.user,event.callback,event.context,&view);}
                    catch(...){++last_pump_.callback_failures;}
                }
            }
            return last_pump_.callback_failures?SAN14_E_PLUGIN_FAILED:SAN14_OK;
        });
    }
private:
    static San14Result SAN14_MOD_CALL api_dispatch(void* owner,San14Thread thread,San14TaskCallback callback,void* context,San14TaskHandle* output) {
        if(!bound_){if(output)*output=0;return SAN14_E_UNSUPPORTED;}return bound_->dispatch(owner,thread,callback,context,output);
    }
    static San14Result SAN14_MOD_CALL api_cancel(void* owner,San14TaskHandle handle) {
        return bound_?bound_->cancel(owner,handle):SAN14_E_UNSUPPORTED;
    }
    static San14Result SAN14_MOD_CALL api_subscribe(void* owner,uint32_t event_id,int32_t priority,San14EventCallback callback,void* context,San14Subscription* output) {
        if(!bound_){if(output)*output=0;return SAN14_E_UNSUPPORTED;}return bound_->subscribe(owner,event_id,priority,callback,context,output);
    }
    static San14Result SAN14_MOD_CALL api_unsubscribe(void* owner,San14Subscription handle) {
        return bound_?bound_->unsubscribe(owner,handle):SAN14_E_UNSUPPORTED;
    }
};
}
