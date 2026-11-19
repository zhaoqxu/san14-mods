#pragma once
#include <atomic>
#include <cstdint>

namespace plugin_calls {
// The gate belongs to an immutable owner generation, never to a reusable DLL
// address. Retired owner generations stay closed even if the loader reuses VA.
class Gate {
    static constexpr uint64_t closed=uint64_t{1}<<63;
    std::atomic<uint64_t> state_{0};
public:
    bool enter(){
        auto state=state_.load(std::memory_order_acquire);
        while(!(state&closed)){
            if(state==closed-1)return false;
            if(state_.compare_exchange_weak(state,state+1,std::memory_order_acq_rel))return true;
        }return false;
    }
    void leave(){state_.fetch_sub(1,std::memory_order_release);}
    bool close(){return (state_.fetch_or(closed,std::memory_order_acq_rel)&~closed)==0;}
    void reopen(){state_.fetch_and(~closed,std::memory_order_release);}
    bool idle()const{return (state_.load(std::memory_order_acquire)&~closed)==0;}
    bool stopped()const{return (state_.load(std::memory_order_acquire)&closed)!=0;}
};
struct Hooks {
    void* user=nullptr;
    bool (*enter)(void*,void*)=nullptr;
    void (*leave)(void*,void*)=nullptr;
};
class Lease {
    Hooks hooks_;void* owner_;bool entered_;
public:
    Lease(Hooks hooks,void* owner):hooks_(hooks),owner_(owner),entered_(!hooks.enter||(hooks.leave&&hooks.enter(hooks.user,owner))){}
    ~Lease(){if(entered_&&hooks_.enter&&hooks_.leave)hooks_.leave(hooks_.user,owner_);}
    Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
    explicit operator bool()const{return entered_;}
};
}
