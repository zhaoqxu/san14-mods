#pragma once
#include "plugin_call_gate.h"

// Generic plugin-owned UI resources. Game addresses and SEH live in the injected
// Base backend, never in this registry or the plugin-facing ABI.
#include "san14_ui_ext.h"
#include "san14_ui_style.h"
#include "san14_ui_log.h"
#include "intelligence_log_model.h"
#include "san14_settings_template.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plugin_ui {

struct Backend {
    void* user = nullptr;
    bool (*on_ui)(void*) = nullptr;
    bool (*owner_allowed)(void*, void* owner) = nullptr;
    void* (*create_panel)(void*, int x, int y, int width, int height) = nullptr;
    bool (*frame)(void*, void* panel, int x, int y, int width, int height) = nullptr;
    void* (*text)(void*, void* panel, int x, int y, int width, int height,
                  const wchar_t*, int font_size, int color) = nullptr;
    // Strings remain valid until the next successful update or panel destruction.
    // Failed setters must not retain the candidate string.
    bool (*set_text)(void*, void* control, const wchar_t*) = nullptr;
    bool (*set_visible)(void*, void* panel, bool visible) = nullptr;
    void (*destroy_panel)(void*, void* panel) = nullptr;
    // Base must invoke callbacks through its exception/SEH boundary.
    void (*invoke)(void*, San14ControlCallback, void* plugin_context,
                   San14ControlHandle control) = nullptr;
    void (*invoke_text)(void*, San14TextChangedCallback, void* plugin_context,
                        San14ControlHandle control, San14StringView value) = nullptr;
    // Optional stock button skin; returned object accepts the text setter.
    // Host owns all native state and callback routing, plugins only see handles.
    void* (*button)(void*,void* panel,int x,int y,int width,int height,const wchar_t*) = nullptr;
    bool (*heading)(void*,void* panel,int x,int y,int width,int height) = nullptr;
    bool (*expert_shell)(void*,void* panel,const wchar_t* title) = nullptr;
    void* (*styled_button)(void*,void* panel,int x,int y,int width,int height,const wchar_t*,uint32_t flags) = nullptr;
    bool (*selected)(void*,void* control,bool selected) = nullptr;
    void (*help_visible)(void*,void* panel,bool visible) = nullptr;
    bool (*tint)(void*,void* control,uint32_t argb) = nullptr;
    void* (*slider_create)(void*,void* panel,int x,int y,int width,int height,int32_t minimum,int32_t maximum,int32_t value) = nullptr;
    bool (*slider_update)(void*,void* control,int32_t minimum,int32_t maximum,int32_t value) = nullptr;
    void (*invoke_int)(void*,San14IntChangedCallback,void* plugin_context,San14ControlHandle control,int32_t value) = nullptr;
    void* (*chronicle_row)(void*,void* panel,int x,int y,int width,int height,const wchar_t*) = nullptr;
    // Internal renderer hook, not part of the SDK ABI. May initialize cached
    // resources, but must not present the candidate or block the UI thread.
    bool (*prepare_panel)(void*,void* panel) = nullptr;
    bool (*log_rows)(void*,void*,const std::vector<intelligence_log::Row>&) = nullptr;
    bool (*enabled)(void*,void* control,bool enabled) = nullptr;
    bool (*tint_button_text)(void*,void* control,uint32_t argb) = nullptr;
    plugin_calls::Hooks calls;
};

class Runtime {
public:
    static constexpr int logical_width = 1920;
    static constexpr int logical_height = 1080;
    static constexpr size_t max_owner_panels = 16;
    static constexpr size_t max_controls_per_panel = 512;
    static constexpr uint32_t max_text_bytes = 16384;
    static constexpr size_t max_text_units = 4 * 1024 * 1024;

    explicit Runtime(Backend backend) : backend_(backend) {}
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    ~Runtime() { if (bound_ == this) bound_ = nullptr; }

    // Bind exactly one Base runtime to the SDK C function table. No game object
    // pointer crosses the ABI: host_context remains the opaque owning Mod token.
    San14UiApi api() {
        if (bound_ && bound_ != this) throw std::logic_error("Plugin UI already bound");
        bound_ = this;
        San14UiApi out{};
        out.struct_size = sizeof(out);
        out.create_panel = api_create;
        out.add_label = api_label;
        out.add_button = api_button;
        out.set_text = api_text;
        out.show_panel = api_show;
        out.close_panel = api_close;
        // Native officer-detail attachment has no verified adapter in this layer.
        return out;
    }
    San14UiExtendedApi extended_api() {
        api();
        return {sizeof(San14UiExtendedApi),api_input,api_get_text,api_focus,
                api_set_selection,api_get_selection};
    }
    const San14UiStyleApi* style_api() {
        api();
        const uint32_t capabilities =
            (backend_.expert_shell&&backend_.styled_button&&backend_.selected?SAN14_UI_STYLE_EXPERT:0u) |
            (backend_.tint?SAN14_UI_STYLE_TINT:0u) |
            (backend_.enabled?SAN14_UI_STYLE_ENABLED:0u) |
            (backend_.tint_button_text?SAN14_UI_STYLE_BUTTON_TEXT_TINT:0u);
        style_api_={sizeof(San14UiStyleApi),capabilities,api_selected,api_help,api_tint,api_enabled};
        return &style_api_;
    }
    const San14UiSliderApi* slider_api() {
        api();
        if(!backend_.slider_create||!backend_.slider_update||!backend_.invoke_int)return nullptr;
        slider_api_={sizeof(San14UiSliderApi),api_add_slider,api_set_slider_value,api_get_slider_value};
        return &slider_api_;
    }

    const San14UiLogApi* log_api(){
        api();static const San14UiLogApi service{sizeof(San14UiLogApi),api_log};
        return backend_.log_rows?&service:nullptr;
    }
    San14Result set_log_rows(void* owner,San14PanelHandle handle,const San14LogRow* rows,uint32_t count,uint32_t stride){
        return guard([&]{
            auto check=access(owner);if(check!=SAN14_OK)return check;
            if(!backend_.log_rows)return SAN14_E_UNSUPPORTED;
            if(count>100000||stride<sizeof(San14LogRow)||stride>1024||(count&&!rows))return SAN14_E_INVALID_ARGUMENT;
            auto it=panels_.find(handle);if(it==panels_.end())return SAN14_E_NOT_FOUND;
            auto& p=*it->second;if(p.owner!=owner)return SAN14_E_PERMISSION_DENIED;
            if(p.width!=1920||p.height!=1080)return SAN14_E_INVALID_ARGUMENT;
            if(p.visible)return SAN14_E_BUSY;
            std::vector<intelligence_log::Row> copy;copy.reserve(count);size_t units=0;
            for(uint32_t i=0;i<count;++i){
                const auto& r=*reinterpret_cast<const San14LogRow*>(reinterpret_cast<const unsigned char*>(rows)+size_t(i)*stride);
                if(r.struct_size<sizeof(r))return SAN14_E_INVALID_ARGUMENT;
                intelligence_log::Row item{r.year,r.month,r.day,r.force_color,decode(r.banner,8),decode(r.text,8192)};
                if(!intelligence_log::valid(item))return SAN14_E_INVALID_ARGUMENT;
                units+=item.banner.size()+item.text.size();if(units>max_text_units)return SAN14_E_BUSY;
                copy.push_back(std::move(item));
            }
            return backend_.log_rows(backend_.user,p.native,copy)?SAN14_OK:SAN14_E_PLUGIN_FAILED;
        });
    }
    bool ready() const {
        return backend_.on_ui && backend_.owner_allowed && backend_.create_panel &&
               backend_.frame && backend_.text && backend_.set_text &&
               backend_.set_visible && backend_.destroy_panel && backend_.invoke;
    }
    bool has_visible() const { return !suspended_ && !stack_.empty(); }
    bool has_visible_for_owner(void* owner) const {
        auto p=panels_.find(top_panel());return has_visible()&&p!=panels_.end()&&p->second->owner==owner;
    }
    bool is_suspended() const { return suspended_; }
    bool presentation_pending() const { return presentation_pending_; }
    bool has_top_for_owner(void* owner) const {
        auto p=panels_.find(top_panel());return p!=panels_.end()&&p->second->owner==owner;
    }
    uint64_t creation_checkpoint() const { return next_handle_; }
    San14Result discard_created_since(void* owner,uint64_t checkpoint) {
        return guard([&] {
            if(!ready())return SAN14_E_UNSUPPORTED;
            if(!backend_.on_ui(backend_.user))return SAN14_E_WRONG_THREAD;
            std::vector<San14PanelHandle> remove;
            for(const auto& p:panels_)if(p.first>=checkpoint&&p.second->owner==owner)remove.push_back(p.first);
            for(auto h:remove)destroy(h,false);
            return SAN14_OK;
        });
    }
    San14Result pump() {
        return guard([&] {
            if(!ready())return SAN14_E_UNSUPPORTED;
            if(!backend_.on_ui(backend_.user))return SAN14_E_WRONG_THREAD;
            if(!presentation_pending_)return SAN14_OK;
            return sync_visibility()?SAN14_OK:SAN14_E_PLUGIN_FAILED;
        });
    }
    San14PanelHandle top_panel() const { return stack_.empty() ? 0 : stack_.back(); }
    size_t panel_count() const { return panels_.size(); }
    size_t control_count() const { return controls_.size(); }
    San14ControlHandle focused_input() const {
        auto c=controls_.find(focused_);
        return has_visible() && c!=controls_.end() && c->second->panel==top_panel() ? focused_ : 0;
    }
    San14Result suspend() {
        return guard([&] {
            if(!ready())return SAN14_E_UNSUPPORTED;
            if(!backend_.on_ui(backend_.user))return SAN14_E_WRONG_THREAD;
            if(suspended_)return SAN14_OK;
            clear_hover_help();
            suspended_=true;
            if(!sync_visibility()){suspended_=false;sync_visibility();return SAN14_E_PLUGIN_FAILED;}
            pressed_=0;pending_high_=0;
            if(auto c=focused_control()){c->composition.clear();repaint(*c);}
            return SAN14_OK;
        });
    }
    San14Result resume() {
        return guard([&] {
            if(!ready())return SAN14_E_UNSUPPORTED;
            if(!backend_.on_ui(backend_.user))return SAN14_E_WRONG_THREAD;
            if(!suspended_)return SAN14_OK;
            suspended_=false;
            if(!sync_visibility()){suspended_=true;sync_visibility();return SAN14_E_PLUGIN_FAILED;}
            pressed_=0;
            if(auto c=focused_control())repaint(*c);
            return SAN14_OK;
        });
    }

    San14Result create_panel(void* owner, const San14PanelSpec* spec,
                             San14PanelHandle* output) {
        return guard([&] {
            auto check = access(owner); if (check != SAN14_OK) return check;
            if (!spec || !output || spec->struct_size < SAN14_PANEL_SPEC_V1_SIZE) return SAN14_E_INVALID_ARGUMENT;
            if (spec->flags & ~(SAN14_PANEL_CUSTOM_POSITION|SAN14_PANEL_EXPERT|SAN14_PANEL_POPUP)) return SAN14_E_UNSUPPORTED;
            const bool expert=(spec->flags&SAN14_PANEL_EXPERT)!=0;
            const bool themed=backend_.expert_shell!=nullptr;
            if(expert&&(!backend_.expert_shell||!backend_.styled_button||!backend_.selected))return SAN14_E_UNSUPPORTED;
            if(expert&&(spec->width!=SAN14_EXPERT_WIDTH||spec->height!=SAN14_EXPERT_HEIGHT))return SAN14_E_INVALID_ARGUMENT;
            if ((spec->flags & SAN14_PANEL_CUSTOM_POSITION) && spec->struct_size < sizeof(*spec)) return SAN14_E_INVALID_ARGUMENT;
            if (spec->width < 120 || spec->width > logical_width ||
                spec->height < 80 || spec->height > logical_height) return SAN14_E_INVALID_ARGUMENT;
            if (std::count_if(panels_.begin(), panels_.end(), [=](const auto& pair) {
                    return pair.second->owner == owner;
                }) >= static_cast<std::ptrdiff_t>(max_owner_panels)) return SAN14_E_BUSY;
            auto p = std::make_unique<Panel>();
            p->owner = owner; p->width = spec->width; p->height = spec->height;
            p->expert=expert;
            p->popup=(spec->flags&SAN14_PANEL_POPUP)!=0;
            p->x = (logical_width - p->width) / 2;
            p->y = (logical_height - p->height) / 2;
            if(spec->flags & SAN14_PANEL_CUSTOM_POSITION){
                if(spec->x<0||spec->y<0||spec->x>logical_width-p->width||spec->y>logical_height-p->height)return SAN14_E_INVALID_ARGUMENT;
                p->x=spec->x;p->y=spec->y;
            }
            p->title = decode(spec->title, max_text_bytes);
            if (text_units_ + p->title.size() > max_text_units) return SAN14_E_BUSY;
            p->handle = fresh_handle();
            auto handle = p->handle;
            panels_.emplace(handle, std::move(p));
            auto panel = panels_.at(handle).get();
            text_units_ += panel->title.size();
            try {
                panel->native = backend_.create_panel(backend_.user, panel->x, panel->y,
                                                      panel->width, panel->height);
                if (!panel->native || (themed ? !backend_.expert_shell(backend_.user,panel->native,panel->title.c_str()) : (!backend_.frame(backend_.user, panel->native, 0, 0,
                                                       panel->width, panel->height) ||
                    !backend_.text(backend_.user, panel->native, 24, 16,
                                   panel->width - 48, 36, panel->title.c_str(), 24, 0)))) {
                    destroy(handle, false); return SAN14_E_PLUGIN_FAILED;
                }
            } catch (...) { destroy(handle, false); throw; }
            *output = handle; return SAN14_OK;
        });
    }

    San14Result add_label(void* owner, San14PanelHandle panel, const San14ControlSpec* spec,
                          San14ControlHandle* output) {
        return add(owner, panel, spec, nullptr, nullptr, output, false);
    }
    San14Result add_button(void* owner, San14PanelHandle panel, const San14ControlSpec* spec,
                           San14ControlCallback callback, void* context,
                           San14ControlHandle* output) {
        return add(owner, panel, spec, callback, context, output, true);
    }
    San14Result add_text_input(void* owner,San14PanelHandle panel,const San14ControlSpec* spec,
                               uint32_t maximum,San14TextChangedCallback changed,void* context,
                               San14ControlHandle* output) {
        if(!backend_.invoke_text)return SAN14_E_UNSUPPORTED;
        return add(owner,panel,spec,nullptr,context,output,false,true,maximum,changed);
    }

    San14Result set_text(void* owner, San14ControlHandle handle, San14StringView value) {
        return guard([&] {
            auto check = access(owner); if (check != SAN14_OK) return check;
            auto found = controls_.find(handle); if (found == controls_.end()) return SAN14_E_NOT_FOUND;
            auto c = found->second.get(); if (c->owner != owner) return SAN14_E_PERMISSION_DENIED;
            if (c->slider) return SAN14_E_INVALID_ARGUMENT;
            auto next = std::make_unique<std::wstring>(decode(value, max_text_bytes));
            if(!c->input&&*c->value==*next)return SAN14_OK;
            auto end=next->size();
            return assign_value(*c,std::move(next),end,end);
        });
    }
    San14Result add_slider(void* owner, San14PanelHandle handle, const San14SliderSpec* spec,
                           San14IntChangedCallback changed, void* context, San14ControlHandle* output) {
        return guard([&] {
            auto check = access(owner); if (check != SAN14_OK) return check;
            if (!backend_.slider_create || !backend_.slider_update || !backend_.invoke_int) return SAN14_E_UNSUPPORTED;
            if (!spec || !output || spec->struct_size < sizeof(*spec) || spec->flags != 0) return SAN14_E_INVALID_ARGUMENT;
            if (spec->min_value >= spec->max_value || spec->step <= 0 || spec->display_divisor < 0) return SAN14_E_INVALID_ARGUMENT;
            auto found = panels_.find(handle); if (found == panels_.end()) return SAN14_E_NOT_FOUND;
            auto p = found->second.get(); if (p->owner != owner) return SAN14_E_PERMISSION_DENIED;
            if (p->children.size() >= max_controls_per_panel) return SAN14_E_BUSY;
            if (spec->x < 0 || spec->y < 0 || spec->width < 48 || spec->height < 16 ||
                spec->width > p->width || spec->height > p->height ||
                spec->x > p->width - spec->width || spec->y > p->height - spec->height) return SAN14_E_INVALID_ARGUMENT;
            auto c = std::make_unique<Control>();
            c->id = control_id(spec->control_id);
            for (auto child : p->children) if (controls_.at(child)->id == c->id) return SAN14_E_INVALID_ARGUMENT;
            c->value = std::make_unique<std::wstring>();
            c->display = std::make_unique<std::wstring>();
            c->handle = fresh_handle(); c->panel = handle; c->owner = owner;
            c->x = spec->x; c->y = spec->y; c->width = spec->width; c->height = spec->height;
            c->slider = true;
            c->min_value = spec->min_value; c->max_value = spec->max_value; c->step = spec->step;
            c->int_value = san14::snap_range_value(spec->value, spec->min_value, spec->max_value, spec->step);
            c->int_changed = changed; c->context = context;
            p->children.reserve(p->children.size() + 1);
            auto control = c->handle;
            controls_.emplace(control, std::move(c));
            auto stored = controls_.at(control).get();
            try {
                stored->native = backend_.slider_create(backend_.user, p->native, stored->x, stored->y,
                                                        stored->width, stored->height,
                                                        stored->min_value, stored->max_value, stored->int_value);
                if (!stored->native) { controls_.erase(control); return SAN14_E_PLUGIN_FAILED; }
            } catch (...) { controls_.erase(control); throw; }
            p->children.push_back(control);
            *output = control;
            return SAN14_OK;
        });
    }
    San14Result set_slider_value(void* owner, San14ControlHandle handle, int32_t value) {
        return guard([&] {
            auto check = access(owner); if (check != SAN14_OK) return check;
            if (!backend_.slider_update) return SAN14_E_UNSUPPORTED;
            auto found = controls_.find(handle); if (found == controls_.end()) return SAN14_E_NOT_FOUND;
            auto c = found->second.get(); if (c->owner != owner) return SAN14_E_PERMISSION_DENIED;
            if (!c->slider) return SAN14_E_INVALID_ARGUMENT;
            const int32_t snapped = san14::snap_range_value(value, c->min_value, c->max_value, c->step);
            if (!backend_.slider_update(backend_.user, c->native, c->min_value, c->max_value, snapped)) return SAN14_E_PLUGIN_FAILED;
            c->int_value = snapped;
            return SAN14_OK;
        });
    }
    San14Result get_slider_value(void* owner, San14ControlHandle handle, int32_t* out_value) {
        return guard([&] {
            auto check = access(owner); if (check != SAN14_OK) return check;
            if (!out_value) return SAN14_E_INVALID_ARGUMENT;
            auto found = controls_.find(handle); if (found == controls_.end()) return SAN14_E_NOT_FOUND;
            auto c = found->second.get(); if (c->owner != owner) return SAN14_E_PERMISSION_DENIED;
            if (!c->slider) return SAN14_E_INVALID_ARGUMENT;
            *out_value = c->int_value;
            return SAN14_OK;
        });
    }
    San14Result set_selected(void* owner,San14ControlHandle handle,int32_t selected) {
        return guard([&]{
            auto check=access(owner);if(check!=SAN14_OK)return check;
            auto found=controls_.find(handle);if(found==controls_.end())return SAN14_E_NOT_FOUND;
            auto c=found->second.get();if(c->owner!=owner)return SAN14_E_PERMISSION_DENIED;
            if(!c->selectable||!backend_.selected)return SAN14_E_UNSUPPORTED;
            if(selected!=0&&selected!=1)return SAN14_E_INVALID_ARGUMENT;
            return backend_.selected(backend_.user,c->native,selected!=0)?SAN14_OK:SAN14_E_PLUGIN_FAILED;
        });
    }
    San14Result set_enabled(void* owner,San14ControlHandle handle,int32_t enabled) {
        return guard([&]{
            auto check=access(owner);if(check!=SAN14_OK)return check;
            auto found=controls_.find(handle);if(found==controls_.end())return SAN14_E_NOT_FOUND;
            auto c=found->second.get();if(c->owner!=owner)return SAN14_E_PERMISSION_DENIED;
            if(enabled!=0&&enabled!=1)return SAN14_E_INVALID_ARGUMENT;
            if(!c->selectable||!backend_.enabled)return SAN14_E_UNSUPPORTED;
            if(!backend_.enabled(backend_.user,c->native,enabled!=0))return SAN14_E_PLUGIN_FAILED;
            c->enabled=enabled!=0;
            if(!c->enabled){
                if(pressed_==handle)pressed_=0;
                if(navigation_==handle)navigation_=0;
                if(c->help_target&&hover_help_target_==c->help_target)clear_hover_help();
            }
            return SAN14_OK;
        });
    }
    San14Result bind_help(void* owner,San14ControlHandle source,San14ControlHandle target,San14StringView description) {
        return guard([&]{
            auto check=access(owner);if(check!=SAN14_OK)return check;
            auto found=controls_.find(source);if(found==controls_.end())return SAN14_E_NOT_FOUND;
            auto c=found->second.get();if(c->owner!=owner)return SAN14_E_PERMISSION_DENIED;
            if(target){auto label=controls_.find(target);if(label==controls_.end())return SAN14_E_NOT_FOUND;
                if(label->second->owner!=owner||label->second->panel!=c->panel)return SAN14_E_PERMISSION_DENIED;
                if(label->second->button||label->second->input||label->second->slider)return SAN14_E_INVALID_ARGUMENT;}
            auto help=target?decode(description,max_text_bytes):std::wstring{};
            if(text_units_-c->help.size()+help.size()>max_text_units)return SAN14_E_BUSY;
            if(hover_help_target_&&hover_help_target_==c->help_target)clear_hover_help();
            text_units_=text_units_-c->help.size()+help.size();c->help=std::move(help);c->help_target=target;return SAN14_OK;
        });
    }
    San14Result set_tint(void* owner,San14ControlHandle handle,uint32_t argb) {
        return guard([&]{
            auto check=access(owner);if(check!=SAN14_OK)return check;
            auto found=controls_.find(handle);if(found==controls_.end())return SAN14_E_NOT_FOUND;
            auto c=found->second.get();if(c->owner!=owner)return SAN14_E_PERMISSION_DENIED;
            if(c->input||c->slider)return SAN14_E_INVALID_ARGUMENT;
            if(c->button){
                if(!backend_.tint_button_text)return SAN14_E_INVALID_ARGUMENT;
                return backend_.tint_button_text(backend_.user,c->native,argb)?SAN14_OK:SAN14_E_UNSUPPORTED;
            }
            if(!backend_.tint)return SAN14_E_UNSUPPORTED;
            return backend_.tint(backend_.user,c->native,argb)?SAN14_OK:SAN14_E_PLUGIN_FAILED;
        });
    }
    void mouse_move(int x,int y) {
        if(!input_ready())return;
        auto p=panels_.at(top_panel()).get();
        if(pressed_){
            auto found=controls_.find(pressed_);
            if(found!=controls_.end()&&found->second->slider&&found->second->panel==top_panel()){
                scrub_slider(found->second.get(),p,x);
            }
        }
        x-=p->x;y-=p->y;
        San14ControlHandle hit=0;
        for(auto i=p->children.rbegin();i!=p->children.rend();++i){auto c=controls_.at(*i).get();
            if(c->enabled&&c->help_target&&x>=c->x&&x<c->x+c->width&&y>=c->y&&y<c->y+c->height){hit=c->handle;break;}}
        auto target=hit?controls_.at(hit)->help_target:0;
        if(hover_help_target_&&hover_help_target_!=target)clear_hover_help();
        if(hit){hover_help_target_=target;show_help(hit);if(backend_.help_visible)backend_.help_visible(backend_.user,p->native,true);}
    }
    void mouse_leave(){clear_hover_help();}
    San14Result get_text(void* owner,San14ControlHandle handle,char* buffer,uint32_t capacity,uint32_t* required) {
        return guard([&] {
            auto check=access(owner);if(check!=SAN14_OK)return check;
            auto found=controls_.find(handle);if(found==controls_.end())return SAN14_E_NOT_FOUND;
            auto c=found->second.get();if(c->owner!=owner)return SAN14_E_PERMISSION_DENIED;
            if(!required)return SAN14_E_INVALID_ARGUMENT;
            auto value=encode(*c->value);*required=static_cast<uint32_t>(value.size()+1);
            if(!buffer||capacity<*required)return SAN14_E_BUFFER_TOO_SMALL;
            std::memcpy(buffer,value.c_str(),value.size()+1);return SAN14_OK;
        });
    }
    San14Result focus_text_input(void* owner,San14ControlHandle handle) {
        return guard([&] {
            auto check=access(owner);if(check!=SAN14_OK)return check;
            auto found=controls_.find(handle);if(found==controls_.end())return SAN14_E_NOT_FOUND;
            auto c=found->second.get();if(c->owner!=owner)return SAN14_E_PERMISSION_DENIED;
            if(!c->input)return SAN14_E_INVALID_ARGUMENT;
            if(!has_visible()||c->panel!=top_panel())return SAN14_E_BUSY;
            return focus(handle);
        });
    }
    San14Result set_selection(void* owner,San14ControlHandle handle,const San14TextSelection* selection) {
        return guard([&] {
            auto check=access(owner);if(check!=SAN14_OK)return check;
            auto found=controls_.find(handle);if(found==controls_.end())return SAN14_E_NOT_FOUND;
            auto c=found->second.get();if(c->owner!=owner)return SAN14_E_PERMISSION_DENIED;
            if(!c->input||!selection||selection->struct_size<sizeof(*selection))return SAN14_E_INVALID_ARGUMENT;
            auto anchor=unit_index(*c->value,selection->anchor),caret=unit_index(*c->value,selection->caret);
            auto old_anchor=c->anchor,old_caret=c->caret;c->anchor=anchor;c->caret=caret;
            if(!repaint(*c)){c->anchor=old_anchor;c->caret=old_caret;return SAN14_E_PLUGIN_FAILED;}
            return SAN14_OK;
        });
    }
    San14Result get_selection(void* owner,San14ControlHandle handle,San14TextSelection* output) {
        return guard([&] {
            auto check=access(owner);if(check!=SAN14_OK)return check;
            auto found=controls_.find(handle);if(found==controls_.end())return SAN14_E_NOT_FOUND;
            auto c=found->second.get();if(c->owner!=owner)return SAN14_E_PERMISSION_DENIED;
            if(!c->input||!output||output->struct_size<sizeof(*output))return SAN14_E_INVALID_ARGUMENT;
            output->anchor=scalar_index(*c->value,c->anchor);output->caret=scalar_index(*c->value,c->caret);return SAN14_OK;
        });
    }

    San14Result show_panel(void* owner, San14PanelHandle handle) {
        return guard([&] {
            auto check = access(owner); if (check != SAN14_OK) return check;
            auto found = panels_.find(handle); if (found == panels_.end()) return SAN14_E_NOT_FOUND;
            auto p = found->second.get(); if (p->owner != owner) return SAN14_E_PERMISSION_DENIED;
            if (top_panel() == handle) return SAN14_OK;
            // Reserve before touching native visibility; allocation failure leaves UI intact.
            stack_.reserve(stack_.size() + 1);
            auto prior = top_panel();
            if(p->popup&&(!prior||panels_.at(prior)->owner!=owner))return SAN14_E_PERMISSION_DENIED;
            auto previous_stack=stack_;
            clear_hover_help();
            stack_.erase(std::remove(stack_.begin(), stack_.end(), handle), stack_.end());
            stack_.push_back(handle);
            if(!sync_visibility()){
                stack_=std::move(previous_stack);sync_visibility();return SAN14_E_PLUGIN_FAILED;
            }
            pressed_ = 0; focus(0); return SAN14_OK;
        });
    }

    San14Result close_panel(void* owner, San14PanelHandle handle) {
        return guard([&] {
            // Resource release stays available after the Mod stopped accepting work.
            if (!ready()) return SAN14_E_UNSUPPORTED;
            if (!backend_.on_ui(backend_.user)) return SAN14_E_WRONG_THREAD;
            if (!owner) return SAN14_E_INVALID_ARGUMENT;
            auto found = panels_.find(handle); if (found == panels_.end()) return SAN14_E_NOT_FOUND;
            if (found->second->owner != owner) return SAN14_E_PERMISSION_DENIED;
            destroy(handle, true); return SAN14_OK;
        });
    }

    // Host-only lifecycle hooks. Return errors instead of destroying game objects
    // from a worker thread. Cleanup never checks active/enabled permission state.
    San14Result cleanup_owner(void* owner) {
        return guard([&] {
            if (!ready()) return SAN14_E_UNSUPPORTED;
            if (!backend_.on_ui(backend_.user)) return SAN14_E_WRONG_THREAD;
            std::vector<San14PanelHandle> owned;
            for (const auto& kv : panels_) if (kv.second->owner == owner) owned.push_back(kv.first);
            for (auto handle : owned) destroy(handle, false);
            for(auto& old:retired_)if(old.panel->owner==owner)visibility(*old.panel,false);
            drain_retired();
            sync_visibility();
            return SAN14_OK;
        });
    }
    San14Result cleanup_all() {
        return guard([&] {
            if (!ready()) return SAN14_E_UNSUPPORTED;
            if (!backend_.on_ui(backend_.user)) return SAN14_E_WRONG_THREAD;
            while (!panels_.empty()) destroy(panels_.begin()->first, false);
            return SAN14_OK;
        });
    }

    // All mouse input is consumed while a plugin modal is visible, including
    // clicks outside it. The caller must also consume mouse move/wheel and key-up.
    bool mouse_down(int x, int y) {
        if(presentation_pending_&&input_context())return true;
        if (!input_ready()) return false;
        auto top=panels_.at(top_panel()).get();
        if(top->popup&&(x<top->x||y<top->y||x>=top->x+top->width||y>=top->y+top->height)){
            destroy(top_panel(),true);return true;
        }
        pressed_ = hit_control(x, y);
        navigation_=pressed_;
        auto found=controls_.find(pressed_);
        if(found!=controls_.end()&&found->second->input){
            auto c=found->second.get();c->anchor=c->caret=c->value->size();
            last_text_result_=focus(c->handle);pressed_=0;
        }else{
            last_text_result_=focus(0);
            if(found!=controls_.end()&&found->second->slider){
                auto p=panels_.at(top_panel()).get();
                scrub_slider(found->second.get(),p,x);
            }
        }
        return true;
    }
    bool mouse_up(int x, int y) {
        if(presentation_pending_&&input_context())return true;
        if (!input_ready()) return false;
        auto handle = pressed_; pressed_ = 0;
        if (!handle) return true;
        auto found = controls_.find(handle);
        if (found == controls_.end()) return true;
        if (found->second->slider) {
            if (found->second->panel == top_panel()) {
                scrub_slider(found->second.get(), panels_.at(top_panel()).get(), x);
            }
            return true;
        }
        if (hit_control(x, y) != handle) return true;
        auto callback = found->second->callback;
        auto context = found->second->context;
        auto owner = found->second->owner;
        if (!backend_.owner_allowed(backend_.user, owner)) return true;
        // Do not retain references across arbitrary plugin code. A callback may
        // close the panel, disable the Mod, or replace this entire stack.
        if (callback){plugin_calls::Lease lease(backend_.calls,owner);if(lease)backend_.invoke(backend_.user, callback, context, handle);}
        return true;
    }
    bool key_down(uint32_t key,bool control=false,bool shift=false) {
        if(presentation_pending_&&input_context()){
            if(key==0x1b)destroy(top_panel(),true);
            return key!=0x70;
        }
        if (!input_ready() || key == 0x70 /* F1 belongs to manager */) return false;
        if((panels_.at(top_panel())->expert||panels_.at(top_panel())->popup)&&!focused_control()&&(key==0x09||key==0x26||key==0x28||key==0x25||key==0x27)){
            auto p=panels_.at(top_panel()).get();std::vector<San14ControlHandle> buttons;
            for(auto id:p->children)if(controls_.at(id)->button&&controls_.at(id)->enabled)buttons.push_back(id);
            if(!buttons.empty()){auto i=std::find(buttons.begin(),buttons.end(),navigation_);bool back=key==0x26||key==0x25||(key==0x09&&shift);
                size_t n=i==buttons.end()?(back?0:buttons.size()-1):size_t(i-buttons.begin());
                navigation_=buttons[back?(n+buttons.size()-1)%buttons.size():(n+1)%buttons.size()];}
            return true;
        }
        if(key==0x09){cycle_focus(shift);return true;}
        auto c=focused_control();
        if(key==0x1b){
            if(c&&!c->composition.empty()){c->composition.clear();pending_high_=0;repaint(*c);}
            else destroy(top_panel(),true);
            return true;
        }
        if(!c){if((key==0x0d||key==0x20)&&navigation_){auto i=controls_.find(navigation_);
            if(i!=controls_.end()&&i->second->enabled&&i->second->panel==top_panel()&&i->second->callback&&backend_.owner_allowed(backend_.user,i->second->owner)){
                auto callback=i->second->callback;auto context=i->second->context;auto id=i->first;plugin_calls::Lease lease(backend_.calls,i->second->owner);if(lease)backend_.invoke(backend_.user,callback,context,id);}}
            return true;}
        if(control&&key=='A'){c->anchor=0;c->caret=c->value->size();repaint(*c);return true;}
        if(key==0x08||key==0x2e){last_text_result_=erase_focused(key==0x08);return true;}
        if(key==0x25||key==0x27||key==0x24||key==0x23){
            if(!shift&&c->caret!=c->anchor&&(key==0x25||key==0x27))
                c->caret=key==0x25?std::min(c->caret,c->anchor):std::max(c->caret,c->anchor);
            else if(key==0x25)c->caret=previous(*c->value,c->caret);
            else if(key==0x27)c->caret=next(*c->value,c->caret);
            else c->caret=key==0x24?0:c->value->size();
            if(!shift)c->anchor=c->caret;
            c->composition.clear();pending_high_=0;repaint(*c);
        }
        return true;
    }
    void cancel_pointer_capture() { pressed_ = 0; }
    // Internal Base renderer lookup; never exposed through the plugin SDK.
    void* pointer_button(int x,int y) const {
        if(!input_ready())return nullptr;
        auto c=controls_.find(hit_control(x,y));
        return c!=controls_.end()&&c->second->button&&backend_.owner_allowed(backend_.user,c->second->owner)?c->second->native:nullptr;
    }
    San14Result last_text_result() const {return last_text_result_;}
    bool has_composition() const {auto c=focused_control();return focused_input()&&c&&!c->composition.empty();}

    // Host committed-text entry. This is single-line editing, not a complete
    // shaping/clipboard/accessibility implementation. UTF-16 pairs stay atomic.
    bool text_input(std::wstring_view committed) {
        if(presentation_pending_&&input_context())return true;
        if(!input_ready())return false;
        if(!focused_control())return true;
        if(committed.empty())return true;
        if(committed.size()==1&&static_cast<uint32_t>(committed.front())<0x20)return true;
        pending_high_=0;last_text_result_=replace_focused(committed);return true;
    }
    // WM_CHAR may split one supplementary scalar over two messages on Windows.
    bool text_char(wchar_t value) {
        if(presentation_pending_&&input_context())return true;
        if(!input_ready())return false;
        if(!focused_control())return true;
        auto unit=static_cast<uint32_t>(value);
        if constexpr(sizeof(wchar_t)==2){
            if(unit>=0xd800&&unit<=0xdbff){pending_high_=value;return true;}
            if(unit>=0xdc00&&unit<=0xdfff){
                if(!pending_high_){last_text_result_=SAN14_E_INVALID_ARGUMENT;return true;}
                wchar_t pair[2]={pending_high_,value};pending_high_=0;
                return text_input(std::wstring_view(pair,2));
            }
        }
        pending_high_=0;return text_input(std::wstring_view(&value,1));
    }
    bool composition_update(std::wstring_view preview) {
        if(presentation_pending_&&input_context())return true;
        if(!input_ready())return false;
        auto c=focused_control();if(!c)return true;
        last_text_result_=guard([&]{
            auto utf8=encode(preview);if(utf8.size()>max_text_bytes)return SAN14_E_INVALID_ARGUMENT;
            auto old=c->composition;c->composition.assign(preview);
            if(!repaint(*c)){c->composition=std::move(old);return SAN14_E_PLUGIN_FAILED;}
            return SAN14_OK;
        });return true;
    }
    bool composition_commit(std::wstring_view committed) {
        if(presentation_pending_&&input_context())return true;
        if(!input_ready())return false;
        if(auto c=focused_control()){c->composition.clear();repaint(*c);}
        return text_input(committed);
    }
    bool focused_rect(int& x,int& y,int& width,int& height) const {
        auto handle=focused_input();if(!handle)return false;
        auto c=controls_.at(handle).get();auto p=panels_.at(c->panel).get();
        x=p->x+c->x;y=p->y+c->y;width=c->width;height=c->height;return true;
    }

private:
    static San14Result SAN14_MOD_CALL api_log(void* owner,San14PanelHandle p,const San14LogRow* rows,uint32_t count,uint32_t stride){return bound_?bound_->set_log_rows(owner,p,rows,count,stride):SAN14_E_UNSUPPORTED;}

    struct Panel {
        San14PanelHandle handle = 0;
        void* owner = nullptr; void* native = nullptr;
        int x = 0, y = 0, width = 0, height = 0;
        std::wstring title;
        bool expert=false,popup=false,visible=false;
        std::vector<San14ControlHandle> children;
    };
    struct Control {
        San14ControlHandle handle = 0;
        San14PanelHandle panel = 0;
        void* owner = nullptr; void* native = nullptr;
        int x = 0, y = 0, width = 0, height = 0;
        bool button = false;
        bool input = false;
        bool slider = false;
        bool selectable=false;
        bool enabled=true;
        int32_t min_value = 0, max_value = 100, step = 1, int_value = 0;
        San14IntChangedCallback int_changed = nullptr;
        std::wstring help;
        San14ControlHandle help_target=0;
        std::string id;
        std::unique_ptr<std::wstring> value;
        std::unique_ptr<std::wstring> display;
        std::wstring composition;
        uint32_t maximum = max_text_bytes;
        size_t anchor = 0,caret = 0;
        San14ControlCallback callback = nullptr;
        San14TextChangedCallback changed = nullptr;
        void* context = nullptr;
    };
    struct RetiredPanel {
        std::unique_ptr<Panel> panel;
        std::vector<std::unique_ptr<Control>> controls;
    };
    Backend backend_;
    std::map<San14PanelHandle, std::unique_ptr<Panel>> panels_;
    std::map<San14ControlHandle, std::unique_ptr<Control>> controls_;
    std::vector<San14PanelHandle> stack_;
    std::vector<RetiredPanel> retired_;
    San14ControlHandle pressed_ = 0;
    San14ControlHandle focused_ = 0;
    San14ControlHandle navigation_ = 0;
    San14ControlHandle hover_help_target_ = 0;
    bool suspended_ = false;
    bool presentation_pending_ = false;
    wchar_t pending_high_ = 0;
    San14Result last_text_result_ = SAN14_OK;
    uint64_t next_handle_ = 1;
    size_t text_units_ = 0;
    inline static Runtime* bound_ = nullptr;
    San14UiStyleApi style_api_{};
    San14UiSliderApi slider_api_{};

    void scrub_slider(Control* c, const Panel* p, int screen_x) {
        if (!c || !p || !c->slider || !backend_.slider_update) return;
        const int local_x = screen_x - p->x;
        const int clamped_x = std::clamp(local_x - c->x, 0, std::max(1, c->width));
        const int64_t span = int64_t(c->max_value) - int64_t(c->min_value);
        const int32_t raw = static_cast<int32_t>(int64_t(c->min_value) + (int64_t(clamped_x) * span + std::max(1, c->width) / 2) / std::max(1, c->width));
        const int32_t next = san14::snap_range_value(raw, c->min_value, c->max_value, c->step);
        if (next == c->int_value) return;
        if (!backend_.slider_update(backend_.user, c->native, c->min_value, c->max_value, next)) return;
        c->int_value = next;
        auto callback = c->int_changed;
        auto context = c->context;
        auto owner = c->owner;
        auto handle = c->handle;
        if (callback && backend_.invoke_int && backend_.owner_allowed(backend_.user, owner)){
            plugin_calls::Lease lease(backend_.calls,owner);if(lease)backend_.invoke_int(backend_.user, callback, context, handle, next);
        }
    }

    void show_help(San14ControlHandle source) {
        auto i=controls_.find(source);if(i==controls_.end()||!i->second->help_target)return;
        auto c=i->second.get();auto t=controls_.find(c->help_target);if(t==controls_.end()||*t->second->value==c->help)return;
        auto value=std::make_unique<std::wstring>(c->help);auto end=value->size();assign_value(*t->second,std::move(value),end,end);
    }
    void clear_hover_help(){
        auto i=controls_.find(hover_help_target_);hover_help_target_=0;if(i==controls_.end())return;
        auto p=panels_.find(i->second->panel);
        assign_value(*i->second,std::make_unique<std::wstring>(),0,0);
        if(p!=panels_.end()&&backend_.help_visible)backend_.help_visible(backend_.user,p->second->native,false);
    }

    template<class F> static San14Result guard(F&& f) noexcept {
        try { return f(); }
        catch (const std::invalid_argument&) { return SAN14_E_INVALID_ARGUMENT; }
        catch (...) { return SAN14_E_PLUGIN_FAILED; }
    }
    San14Result access(void* owner) const {
        if (!ready()) return SAN14_E_UNSUPPORTED;
        if (!backend_.on_ui(backend_.user)) return SAN14_E_WRONG_THREAD;
        if (!owner) return SAN14_E_INVALID_ARGUMENT;
        return backend_.owner_allowed(backend_.user, owner) ? SAN14_OK : SAN14_E_PERMISSION_DENIED;
    }
    bool input_context() const {return ready()&&backend_.on_ui(backend_.user)&&has_visible();}
    bool input_ready() const {return input_context()&&!presentation_pending_;}
    uint64_t fresh_handle() {
        if (next_handle_ == std::numeric_limits<uint64_t>::max()) throw std::runtime_error("UI handles exhausted");
        return next_handle_++;
    }
    static std::wstring decode(San14StringView value, uint32_t limit) {
        if (value.length > limit || (!value.data && value.length)) throw std::invalid_argument("Invalid string view");
        std::wstring out;
        for (uint32_t i = 0; i < value.length;) {
            auto first = static_cast<unsigned char>(value.data[i++]);
            uint32_t scalar = first, count = 0, minimum = 0;
            if (first < 0x80) { if (!first) throw std::invalid_argument("Embedded NUL"); }
            else if ((first & 0xe0) == 0xc0) { scalar = first & 0x1f; count = 1; minimum = 0x80; }
            else if ((first & 0xf0) == 0xe0) { scalar = first & 0x0f; count = 2; minimum = 0x800; }
            else if ((first & 0xf8) == 0xf0) { scalar = first & 7; count = 3; minimum = 0x10000; }
            else throw std::invalid_argument("Invalid UTF-8");
            if (value.length - i < count) throw std::invalid_argument("Truncated UTF-8");
            for (uint32_t n = 0; n < count; ++n) {
                auto c = static_cast<unsigned char>(value.data[i++]);
                if ((c & 0xc0) != 0x80) throw std::invalid_argument("Invalid UTF-8 continuation");
                scalar = (scalar << 6) | (c & 0x3f);
            }
            if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) throw std::invalid_argument("Invalid Unicode scalar");
            if (sizeof(wchar_t) == 2 && scalar > 0xffff) {
                scalar -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xd800 + (scalar >> 10)));
                out.push_back(static_cast<wchar_t>(0xdc00 + (scalar & 0x3ff)));
            } else out.push_back(static_cast<wchar_t>(scalar));
        }
        return out;
    }
    static uint32_t scalar_at(std::wstring_view value,size_t& position) {
        if(position>=value.size())throw std::invalid_argument("Missing Unicode scalar");
        auto scalar=static_cast<uint32_t>(value[position++]);
        if(sizeof(wchar_t)==2&&scalar>=0xd800&&scalar<=0xdbff){
            if(position>=value.size())throw std::invalid_argument("Unpaired UTF-16 surrogate");
            auto low=static_cast<uint32_t>(value[position++]);
            if(low<0xdc00||low>0xdfff)throw std::invalid_argument("Unpaired UTF-16 surrogate");
            scalar=0x10000+((scalar-0xd800)<<10)+(low-0xdc00);
        }else if(scalar>=0xd800&&scalar<=0xdfff)throw std::invalid_argument("Unpaired UTF-16 surrogate");
        if(!scalar||scalar>0x10ffff)throw std::invalid_argument("Invalid Unicode scalar");
        return scalar;
    }
    static std::string encode(std::wstring_view value) {
        std::string out;
        for(size_t i=0;i<value.size();){
            auto scalar=scalar_at(value,i);
            if(scalar<0x80)out.push_back(static_cast<char>(scalar));
            else if(scalar<0x800){out.push_back(static_cast<char>(0xc0|(scalar>>6)));out.push_back(static_cast<char>(0x80|(scalar&63)));}
            else if(scalar<0x10000){out.push_back(static_cast<char>(0xe0|(scalar>>12)));out.push_back(static_cast<char>(0x80|((scalar>>6)&63)));out.push_back(static_cast<char>(0x80|(scalar&63)));}
            else{out.push_back(static_cast<char>(0xf0|(scalar>>18)));out.push_back(static_cast<char>(0x80|((scalar>>12)&63)));out.push_back(static_cast<char>(0x80|((scalar>>6)&63)));out.push_back(static_cast<char>(0x80|(scalar&63)));}
        }return out;
    }
    static size_t previous(std::wstring_view value,size_t position) {
        if(!position)return 0;--position;
        auto unit=static_cast<uint32_t>(value[position]);
        if(sizeof(wchar_t)==2&&unit>=0xdc00&&unit<=0xdfff&&position)--position;
        return position;
    }
    static size_t next(std::wstring_view value,size_t position) {
        if(position>=value.size())return value.size();
        scalar_at(value,position);return position;
    }
    static size_t unit_index(std::wstring_view value,uint32_t scalar_position) {
        size_t unit=0;for(uint32_t i=0;i<scalar_position;++i)scalar_at(value,unit);return unit;
    }
    static uint32_t scalar_index(std::wstring_view value,size_t unit_position) {
        if(unit_position>value.size())throw std::invalid_argument("Caret exceeds text");
        uint32_t count=0;size_t unit=0;
        while(unit<unit_position){scalar_at(value,unit);++count;}
        if(unit!=unit_position)throw std::invalid_argument("Caret splits a scalar");return count;
    }
    static bool single_line(std::wstring_view value) {
        for(auto c:value)if(static_cast<uint32_t>(c)<0x20||c==0x7f)return false;return true;
    }
    static std::wstring display_text(std::wstring_view value,size_t anchor,size_t caret,
                                     std::wstring_view composition,bool focused) {
        if(!focused)return std::wstring(value);
        auto begin=std::min(anchor,caret),end=std::max(anchor,caret);
        std::wstring out(value.substr(0,begin));
        if(!composition.empty()){out+=L"[";out+=composition;out+=L"]|";}
        else if(begin!=end){out+=L"[";out+=value.substr(begin,end-begin);out+=L"]|";}
        else out+=L"|";
        out+=value.substr(end);return out;
    }
    Control* focused_control() const {
        auto found=controls_.find(focused_);
        return found!=controls_.end()&&found->second->input ? found->second.get() : nullptr;
    }
    bool repaint(Control& control) {
        auto display=std::make_unique<std::wstring>(control.input ? display_text(*control.value,control.anchor,control.caret,
            control.composition,focused_==control.handle&&!suspended_) : *control.value);
        if(!backend_.set_text(backend_.user,control.native,display->c_str()))return false;
        control.display.swap(display);return true;
    }
    San14Result assign_value(Control& control,std::unique_ptr<std::wstring> value,size_t anchor,size_t caret) {
        auto bytes=encode(*value);
        if(control.input&&(!single_line(*value)||bytes.size()>control.maximum))return SAN14_E_INVALID_ARGUMENT;
        if(text_units_-control.value->size()+value->size()>max_text_units)return SAN14_E_BUSY;
        auto display=std::make_unique<std::wstring>(control.input ? display_text(*value,anchor,caret,L"",
            focused_==control.handle&&!suspended_) : *value);
        if(!backend_.set_text(backend_.user,control.native,display->c_str()))return SAN14_E_PLUGIN_FAILED;
        text_units_=text_units_-control.value->size()+value->size();
        control.value.swap(value);control.display.swap(display);
        control.anchor=anchor;control.caret=caret;control.composition.clear();return SAN14_OK;
    }
    San14Result focus(San14ControlHandle handle) {
        if(focused_==handle){
            pending_high_=0;
            if(auto c=focused_control()){c->composition.clear();return repaint(*c)?SAN14_OK:SAN14_E_PLUGIN_FAILED;}
            return SAN14_OK;
        }
        if(handle){auto found=controls_.find(handle);
            if(found==controls_.end()||!found->second->input)return SAN14_E_INVALID_ARGUMENT;
            if(!backend_.owner_allowed(backend_.user,found->second->owner))return SAN14_E_PERMISSION_DENIED;
        }
        auto prior=focused_;focused_=handle;pending_high_=0;
        if(auto p=controls_.find(prior);p!=controls_.end()){p->second->composition.clear();if(!repaint(*p->second)){focused_=prior;return SAN14_E_PLUGIN_FAILED;}}
        if(auto c=focused_control();c&&!repaint(*c)){focused_=prior;if(auto p=focused_control())repaint(*p);return SAN14_E_PLUGIN_FAILED;}
        return SAN14_OK;
    }
    void cycle_focus(bool backwards) {
        auto p=panels_.at(top_panel()).get();std::vector<San14ControlHandle> inputs;
        for(auto child:p->children)if(controls_.at(child)->input)inputs.push_back(child);
        if(inputs.empty()){focus(0);return;}
        auto current=std::find(inputs.begin(),inputs.end(),focused_);
        size_t index=current==inputs.end()?(backwards?0:inputs.size()-1):static_cast<size_t>(current-inputs.begin());
        index=backwards?(index+inputs.size()-1)%inputs.size():(index+1)%inputs.size();
        last_text_result_=focus(inputs[index]);
    }
    void notify_changed(San14ControlHandle handle,const std::string& text) {
        auto found=controls_.find(handle);if(found==controls_.end())return;
        auto callback=found->second->changed;auto context=found->second->context;auto owner=found->second->owner;
        if(callback&&backend_.invoke_text&&backend_.owner_allowed(backend_.user,owner)){
            plugin_calls::Lease lease(backend_.calls,owner);if(lease)backend_.invoke_text(backend_.user,callback,context,handle,{text.data(),static_cast<uint32_t>(text.size())});
        }
    }
    San14Result replace_focused(std::wstring_view inserted) {
        return guard([&]{
            auto control=focused_control();if(!control)return SAN14_OK;
            if(!backend_.owner_allowed(backend_.user,control->owner))return SAN14_E_PERMISSION_DENIED;
            // Validate committed UTF-16 before touching either value or selection.
            encode(inserted);if(!single_line(inserted))return SAN14_E_INVALID_ARGUMENT;
            auto begin=std::min(control->anchor,control->caret),end=std::max(control->anchor,control->caret);
            auto candidate=std::make_unique<std::wstring>(*control->value);
            candidate->replace(begin,end-begin,inserted);
            auto changed=*candidate!=*control->value;
            auto notification=encode(*candidate);auto handle=control->handle;
            auto result=assign_value(*control,std::move(candidate),begin+inserted.size(),begin+inserted.size());
            if(result==SAN14_OK&&changed)notify_changed(handle,notification);return result;
        });
    }
    San14Result erase_focused(bool backspace) {
        return guard([&]{
            auto c=focused_control();if(!c)return SAN14_OK;
            if(!backend_.owner_allowed(backend_.user,c->owner))return SAN14_E_PERMISSION_DENIED;
            auto begin=std::min(c->anchor,c->caret),end=std::max(c->anchor,c->caret);
            if(begin==end){if(backspace)begin=previous(*c->value,begin);else end=next(*c->value,end);}
            if(begin==end)return SAN14_OK;
            auto candidate=std::make_unique<std::wstring>(*c->value);candidate->erase(begin,end-begin);
            auto notification=encode(*candidate);auto handle=c->handle;
            auto result=assign_value(*c,std::move(candidate),begin,begin);
            if(result==SAN14_OK)notify_changed(handle,notification);return result;
        });
    }
    static std::string control_id(San14StringView value) {
        if (!value.data || !value.length || value.length > 128) throw std::invalid_argument("Invalid control ID");
        std::string out(value.data, value.length);
        for (auto c : out) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) throw std::invalid_argument("Invalid control ID");
        return out;
    }
    San14Result add(void* owner, San14PanelHandle handle, const San14ControlSpec* spec,
                    San14ControlCallback callback, void* context, San14ControlHandle* output, bool button,
                    bool input=false,uint32_t maximum=max_text_bytes,San14TextChangedCallback changed=nullptr) {
        return guard([&] {
            auto check = access(owner); if (check != SAN14_OK) return check;
            if (!spec || !output || spec->struct_size < sizeof(*spec) || (button && !callback)) return SAN14_E_INVALID_ARGUMENT;
            const auto style=spec->flags&~SAN14_CONTROL_SELECTED;
            const bool styled=button&&(style==SAN14_CONTROL_TAB||style==SAN14_CONTROL_CHOICE_LEFT||style==SAN14_CONTROL_CHOICE_RIGHT||style==SAN14_CONTROL_CONFIRM);
            const bool heading=!button&&!input&&(style==SAN14_CONTROL_HEADING||style==SAN14_CONTROL_EXPERT_ROW);
            const bool expert_label=!button&&!input&&spec->flags==SAN14_CONTROL_EXPERT_LABEL;
            const bool chronicle=!button&&!input&&spec->flags==SAN14_CONTROL_CHRONICLE_ROW;
            const bool link=button&&!input&&spec->flags==SAN14_CONTROL_LINK;
            if(spec->flags&&!((heading&&backend_.heading&&!(spec->flags&SAN14_CONTROL_SELECTED))||(styled&&backend_.styled_button)||expert_label||link||(chronicle&&backend_.chronicle_row)))return SAN14_E_UNSUPPORTED;
            if(chronicle&&(spec->width!=1112||spec->height<58||spec->height>240))return SAN14_E_INVALID_ARGUMENT;
            if(input&&(!maximum||maximum>max_text_bytes))return SAN14_E_INVALID_ARGUMENT;
            auto found = panels_.find(handle); if (found == panels_.end()) return SAN14_E_NOT_FOUND;
            auto p = found->second.get(); if (p->owner != owner) return SAN14_E_PERMISSION_DENIED;
            if (p->children.size() >= max_controls_per_panel) return SAN14_E_BUSY;
            if (spec->x < 0 || spec->y < 0 || spec->width <= 0 || spec->height <= 0 ||
                spec->width > p->width || spec->height > p->height ||
                spec->x > p->width - spec->width || spec->y > p->height - spec->height) return SAN14_E_INVALID_ARGUMENT;
            if ((button||input) && (spec->width < 24 || spec->height < 16)) return SAN14_E_INVALID_ARGUMENT;
            auto c = std::make_unique<Control>();
            c->id = control_id(spec->control_id);
            for (auto child : p->children) if (controls_.at(child)->id == c->id) return SAN14_E_INVALID_ARGUMENT;
            c->value = std::make_unique<std::wstring>(decode(spec->text, max_text_bytes));
            if(input&&(!single_line(*c->value)||encode(*c->value).size()>maximum))return SAN14_E_INVALID_ARGUMENT;
            c->display=std::make_unique<std::wstring>(*c->value);
            if (text_units_ + c->value->size() > max_text_units) return SAN14_E_BUSY;
            c->handle = fresh_handle(); c->panel = handle; c->owner = owner;
            c->x = spec->x; c->y = spec->y; c->width = spec->width; c->height = spec->height;
            c->button = button; c->input=input;c->maximum=maximum;c->changed=changed;
            c->selectable=styled&&style!=SAN14_CONTROL_CONFIRM;
            c->anchor=c->caret=c->value->size();c->callback = callback; c->context = context;
            p->children.reserve(p->children.size() + 1);
            auto control = c->handle;
            controls_.emplace(control, std::move(c));
            auto stored = controls_.at(control).get();
            try {
                if(heading&&!backend_.heading(backend_.user,p->native,stored->x,stored->y,stored->width,stored->height)){
                    controls_.erase(control);return SAN14_E_PLUGIN_FAILED;
                }
                if(chronicle){
                    stored->native=backend_.chronicle_row(backend_.user,p->native,stored->x,stored->y,stored->width,stored->height,stored->display->c_str());
                }else if(styled){
                    stored->native=backend_.styled_button(backend_.user,p->native,stored->x,stored->y,stored->width,stored->height,stored->display->c_str(),spec->flags);
                }else if(button&&!link&&backend_.button){
                    stored->native=backend_.button(backend_.user,p->native,stored->x,stored->y,stored->width,stored->height,stored->display->c_str());
                }else{
                if ((button||input)&&!link && !backend_.frame(backend_.user, p->native, stored->x, stored->y, stored->width, stored->height)) {
                    controls_.erase(control); return SAN14_E_PLUGIN_FAILED;
                }
                const bool padded=(button||input)&&!link;
                stored->native = backend_.text(backend_.user, p->native, stored->x + (padded ? 8 : 0),
                    stored->y + (padded ? 4 : 0), std::max(1, stored->width - (padded ? 16 : 0)),
                    std::max(1, stored->height - (padded ? 8 : 0)), stored->display->c_str(), (style==SAN14_CONTROL_EXPERT_ROW||expert_label||link)?24:18, spec->flags==SAN14_CONTROL_HEADING?0:9);
                if(link&&stored->native&&backend_.tint)backend_.tint(backend_.user,stored->native,0x945000ffu);
                }
                if (!stored->native) { controls_.erase(control); return SAN14_E_PLUGIN_FAILED; }
            } catch (...) { controls_.erase(control); throw; }
            text_units_ += stored->value->size();
            p->children.push_back(control); *output = control; return SAN14_OK;
        });
    }
    bool visibility(Panel& p,bool visible) {
        if(p.visible==visible)return true;
        if(!backend_.set_visible(backend_.user,p.native,visible))return false;
        p.visible=visible;return true;
    }
    void drain_retired(){
        for(auto i=retired_.begin();i!=retired_.end();){
            if(i->panel->visible){++i;continue;}
            // Both panel title and control strings outlive native destruction.
            if(i->panel->native)backend_.destroy_panel(backend_.user,i->panel->native);
            i=retired_.erase(i);
        }
    }
    bool sync_visibility(){
        std::vector<Panel*> target;
        if(!suspended_&&!stack_.empty()){
            size_t first=stack_.size()-1;
            while(first&&panels_.at(stack_[first])->popup)--first;
            for(size_t i=first;i<stack_.size();++i)target.push_back(panels_.at(stack_[i]).get());
        }
        for(auto p:target)if(!p->visible&&backend_.prepare_panel&&!backend_.prepare_panel(backend_.user,p->native)){
            presentation_pending_=true;return true;
        }
        std::vector<Panel*> shown,hidden;
        auto rollback=[&]{for(auto p:hidden)visibility(*p,true);for(auto p:shown)visibility(*p,false);};
        // Present the ready replacement before removing any old foreground.
        for(auto p:target)if(!p->visible){
            if(!visibility(*p,true)){rollback();return false;}shown.push_back(p);
        }
        auto hide=[&](Panel* p){
            if(!p->visible||std::find(target.begin(),target.end(),p)!=target.end())return true;
            if(!visibility(*p,false))return false;hidden.push_back(p);return true;
        };
        for(auto& p:panels_)if(!hide(p.second.get())){rollback();return false;}
        for(auto& p:retired_)if(!hide(p.panel.get())){rollback();return false;}
        presentation_pending_=false;drain_retired();return true;
    }
    void destroy(San14PanelHandle handle, bool restore_top) {
        auto found = panels_.find(handle); if (found == panels_.end()) return;
        // A popup cannot outlive the underlay it was shown over. Recursively
        // close its popup descendants before releasing the parent controls.
        auto position=std::find(stack_.begin(),stack_.end(),handle);
        if(position!=stack_.end()){
            const size_t index=static_cast<size_t>(position-stack_.begin());
            while(index+1<stack_.size()&&panels_.at(stack_[index+1])->popup)destroy(stack_[index+1],false);
        }
        stack_.erase(std::remove(stack_.begin(), stack_.end(), handle), stack_.end());
        auto p = std::move(found->second); panels_.erase(found);
        // Invalidate handles before calling native cleanup, but retain backing
        // strings until the game's panel/children have actually been destroyed.
        std::vector<std::unique_ptr<Control>> children;
        children.reserve(p->children.size());
        for (auto child : p->children) {
            auto c = controls_.find(child);
            if (c != controls_.end()) {
                if (pressed_ == child) pressed_ = 0;
                if (navigation_ == child) navigation_ = 0;
                if (hover_help_target_ == child) hover_help_target_ = 0;
                if (focused_ == child){focused_=0;pending_high_=0;}
                text_units_ -= c->second->value->size()+c->second->help.size();
                children.push_back(std::move(c->second)); controls_.erase(c);
            }
        }
        text_units_ -= p->title.size();
        if(!restore_top)visibility(*p,false);
        retired_.push_back({std::move(p),std::move(children)});
        if(restore_top)sync_visibility();
        else {drain_retired();if(stack_.empty())sync_visibility();}
    }
    San14ControlHandle hit_control(int x, int y) const {
        auto p = panels_.at(top_panel()).get(); x -= p->x; y -= p->y;
        for (auto i = p->children.rbegin(); i != p->children.rend(); ++i) {
            auto c = controls_.at(*i).get();
            if (c->enabled && (c->button||c->input||c->slider) && x >= c->x && y >= c->y && x < c->x + c->width && y < c->y + c->height) return c->handle;
        }
        return 0;
    }
    static San14Result SAN14_MOD_CALL api_add_slider(void* o, San14PanelHandle p, const San14SliderSpec* s, San14IntChangedCallback c, void* x, San14ControlHandle* h) { return bound_ ? bound_->add_slider(o,p,s,c,x,h) : SAN14_E_UNSUPPORTED; }
    static San14Result SAN14_MOD_CALL api_set_slider_value(void* o, San14ControlHandle c, int32_t v) { return bound_ ? bound_->set_slider_value(o,c,v) : SAN14_E_UNSUPPORTED; }
    static San14Result SAN14_MOD_CALL api_get_slider_value(void* o, San14ControlHandle c, int32_t* v) { return bound_ ? bound_->get_slider_value(o,c,v) : SAN14_E_UNSUPPORTED; }
    static San14Result SAN14_MOD_CALL api_create(void* o, const San14PanelSpec* s, San14PanelHandle* h) { return bound_ ? bound_->create_panel(o,s,h) : SAN14_E_UNSUPPORTED; }
    static San14Result SAN14_MOD_CALL api_selected(void* o,San14ControlHandle c,int32_t s){return bound_?bound_->set_selected(o,c,s):SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL api_enabled(void* o,San14ControlHandle c,int32_t e){return bound_?bound_->set_enabled(o,c,e):SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL api_help(void* o,San14ControlHandle c,San14ControlHandle t,San14StringView s){return bound_?bound_->bind_help(o,c,t,s):SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL api_tint(void* o,San14ControlHandle c,uint32_t a){return bound_?bound_->set_tint(o,c,a):SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL api_label(void* o, San14PanelHandle p, const San14ControlSpec* s, San14ControlHandle* h) { return bound_ ? bound_->add_label(o,p,s,h) : SAN14_E_UNSUPPORTED; }
    static San14Result SAN14_MOD_CALL api_button(void* o, San14PanelHandle p, const San14ControlSpec* s, San14ControlCallback c, void* x, San14ControlHandle* h) { return bound_ ? bound_->add_button(o,p,s,c,x,h) : SAN14_E_UNSUPPORTED; }
    static San14Result SAN14_MOD_CALL api_text(void* o, San14ControlHandle c, San14StringView s) { return bound_ ? bound_->set_text(o,c,s) : SAN14_E_UNSUPPORTED; }
    static San14Result SAN14_MOD_CALL api_show(void* o, San14PanelHandle p) { return bound_ ? bound_->show_panel(o,p) : SAN14_E_UNSUPPORTED; }
    static San14Result SAN14_MOD_CALL api_close(void* o, San14PanelHandle p) { return bound_ ? bound_->close_panel(o,p) : SAN14_E_UNSUPPORTED; }
    static San14Result SAN14_MOD_CALL api_input(void* o,San14PanelHandle p,const San14ControlSpec* s,uint32_t n,San14TextChangedCallback c,void* x,San14ControlHandle* h) {return bound_?bound_->add_text_input(o,p,s,n,c,x,h):SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL api_get_text(void* o,San14ControlHandle c,char* b,uint32_t n,uint32_t* r) {return bound_?bound_->get_text(o,c,b,n,r):SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL api_focus(void* o,San14ControlHandle c) {return bound_?bound_->focus_text_input(o,c):SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL api_set_selection(void* o,San14ControlHandle c,const San14TextSelection* s) {return bound_?bound_->set_selection(o,c,s):SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL api_get_selection(void* o,San14ControlHandle c,San14TextSelection* s) {return bound_?bound_->get_selection(o,c,s):SAN14_E_UNSUPPORTED;}
};

} // namespace plugin_ui
