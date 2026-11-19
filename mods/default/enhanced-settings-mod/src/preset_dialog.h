#pragma once
#include "san14_host.h"
#include "settings_model.h"
#include <algorithm>
#include <cstring>
#include <string_view>

namespace enhanced_settings_mod {
class PresetDialog {
public:
    using Read = San14Result (*)(const Preferences&);

    San14Result open(const San14HostApi* host, const Preferences& source, Read read) {
        auto result = close();
        if (result != SAN14_OK) return result;
        host_ = host;
        source_ = source;
        read_ = read;
        name_.clear();
        confirm_.clear();
        catalog_ = {};
        const auto stored = load();
        try {
            const bool reading = read_ != nullptr;
            San14PanelSpec panel{sizeof(panel), text(reading ? u8"读取设定" : u8"保存为预设"), 640, reading ? 548 : 258, 0};
            check(host_->ui.create_panel(host_->host_context, &panel, &panel_));
            if (reading) {
                button(0, 28, 58, 584, u8"新局默认");
                if (stored == SAN14_OK) {
                    for (size_t i = 0; i < catalog_.slots.size(); ++i) {
                        if (!catalog_.slots[i].length) continue;
                        button(i + 1, 28, 108 + static_cast<int>(i) * 44, 584, catalog_.slots[i].name_view());
                    }
                }
                status_ = label(468, stored == SAN14_OK ? u8"读取后可预览并选择保存。" : u8"无法读取已保存的预设。");
                button(cancel, 240, 499, 160, u8"取消");
            } else {
                label(56, u8"预设名称");
                San14ControlSpec input{sizeof(input), text("preset.name"), {}, 28, 91, 584, 40, 0};
                check(host_->ui_extended.add_text_input(host_->host_context, panel_, &input, name_limit, name_changed, this, &input_));
                status_ = label(145, stored == SAN14_OK ? u8"同名预设需要确认覆盖。" : u8"无法读取预设，保存已停用。");
                if (stored == SAN14_OK) button(save, 144, 193, 160, u8"保存");
                button(cancel, 336, 193, 160, u8"取消");
            }
            check(host_->ui.show_panel(host_->host_context, panel_));
            if (input_ && host_->ui_extended.struct_size >= offsetof(San14UiExtendedApi, focus_text_input) +
                sizeof(host_->ui_extended.focus_text_input) && host_->ui_extended.focus_text_input)
                host_->ui_extended.focus_text_input(host_->host_context, input_);
            return SAN14_OK;
        } catch (San14Result error) {
            close();
            return error;
        } catch (...) {
            close();
            return SAN14_E_PLUGIN_FAILED;
        }
    }

    San14Result close() {
        if (!panel_) return SAN14_OK;
        const auto result = host_->ui.close_panel(host_->host_context, panel_);
        if (result != SAN14_OK && result != SAN14_E_NOT_FOUND) return result;
        panel_ = 0;
        input_ = status_ = 0;
        return SAN14_OK;
    }

private:
    static constexpr size_t max_presets = 8;
    static constexpr uint32_t name_limit = 72;
    static constexpr size_t save = max_presets + 1, cancel = save + 1;
    struct Slot {
        uint32_t length = 0;
        std::array<char, name_limit> name{};
        Preferences preferences;
        std::string_view name_view() const { return {name.data(), length}; }
        bool valid() const { return length <= name.size() && (!length || (valid_name(name_view()) && preferences.valid())); }
    };
    struct Catalog { uint32_t version = 1; std::array<Slot, max_presets> slots; };
    struct Choice { PresetDialog* dialog; size_t index; };

    const San14HostApi* host_ = nullptr;
    San14PanelHandle panel_ = 0;
    San14ControlHandle input_ = 0, status_ = 0;
    Catalog catalog_;
    Preferences source_;
    Read read_ = nullptr;
    std::string name_, confirm_;
    std::array<Choice, cancel + 1> choices_{};

    static San14StringView text(std::string_view value) { return {value.data(), static_cast<uint32_t>(value.size())}; }
    static void check(San14Result result) { if (result != SAN14_OK) throw result; }
    static bool valid_name(std::string_view name) {
        return !name.empty() && name.size() <= name_limit &&
            std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
    }

    San14Result load() {
        uint32_t size = 0;
        const auto result = host_->storage.read(host_->host_context, text("global"), text("enhanced.presets.v1"),
                                                &catalog_, sizeof(catalog_), &size);
        if (result == SAN14_E_NOT_FOUND) return SAN14_OK;
        if (result != SAN14_OK) return result;
        if (size != sizeof(catalog_) || catalog_.version != 1 ||
            !std::all_of(catalog_.slots.begin(), catalog_.slots.end(), [](const Slot& slot) { return slot.valid(); }))
            return SAN14_E_INVALID_ARGUMENT;
        return SAN14_OK;
    }

    San14ControlHandle label(int y, std::string_view value) {
        const auto id = "preset.label." + std::to_string(y);
        San14ControlSpec spec{sizeof(spec), text(id), text(value), 28, y, 584, 28, 0};
        San14ControlHandle control = 0;
        check(host_->ui.add_label(host_->host_context, panel_, &spec, &control));
        return control;
    }

    void button(size_t index, int x, int y, int width, std::string_view caption) {
        const auto id = "preset." + std::to_string(index);
        San14ControlSpec spec{sizeof(spec), text(id), text(caption), x, y, width, 38, 0};
        San14ControlHandle control = 0;
        choices_[index] = {this, index};
        check(host_->ui.add_button(host_->host_context, panel_, &spec, clicked, &choices_[index], &control));
    }

    void message(std::string_view value) {
        if (status_) host_->ui.set_text(host_->host_context, status_, text(value));
    }

    void store() {
        const auto first = name_.find_first_not_of(' ');
        const auto name = first == std::string::npos ? std::string{} : name_.substr(first, name_.find_last_not_of(' ') - first + 1);
        if (!valid_name(name) || name == u8"新局默认") { message(u8"请输入其他预设名称。"); return; }
        // Read again before replacing the catalog so a missing or changed
        // record during the modal's lifetime is never overwritten blindly.
        catalog_ = {};
        if (load() != SAN14_OK) { message(u8"无法读取预设，未保存。"); return; }
        auto slot = std::find_if(catalog_.slots.begin(), catalog_.slots.end(),
            [&](const Slot& entry) { return entry.length && entry.name_view() == name; });
        if (slot != catalog_.slots.end() && confirm_ != name) {
            confirm_ = name;
            message(u8"已有同名预设。再次按保存以覆盖。");
            return;
        }
        if (slot == catalog_.slots.end())
            slot = std::find_if(catalog_.slots.begin(), catalog_.slots.end(), [](const Slot& entry) { return !entry.length; });
        if (slot == catalog_.slots.end()) { message(u8"预设已满。可输入已有名称覆盖。"); return; }
        *slot = {};
        slot->length = static_cast<uint32_t>(name.size());
        std::memcpy(slot->name.data(), name.data(), name.size());
        slot->preferences = source_;
        const auto result = host_->storage.write_atomic(host_->host_context, text("global"), text("enhanced.presets.v1"),
                                                       &catalog_, sizeof(catalog_));
        if (result == SAN14_OK) close();
        else message(u8"预设保存失败。请重试。");
    }

    static void SAN14_MOD_CALL name_changed(void* context, San14ControlHandle, San14StringView value) {
        auto& self = *static_cast<PresetDialog*>(context);
        try {
            self.name_.assign(value.data ? value.data : "", value.data ? value.length : 0);
            self.confirm_.clear();
        } catch (...) {}
    }

    static void SAN14_MOD_CALL clicked(void* context, San14ControlHandle) {
        const auto choice = *static_cast<Choice*>(context);
        auto& self = *choice.dialog;
        if (!self.panel_) return;
        try {
            if (choice.index == cancel) self.close();
            else if (choice.index == save) self.store();
            else if (self.read_) {
                const auto values = choice.index ? self.catalog_.slots[choice.index - 1].preferences : self.source_;
                const auto read = self.read_;
                if (self.close() == SAN14_OK) read(values);
            }
        } catch (...) { self.message(u8"预设操作失败。请重试。"); }
    }
};
}
