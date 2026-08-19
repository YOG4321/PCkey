#include "pckey/config_store.hpp"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

namespace pckey {

namespace {

constexpr int kConfigVersion = 4;
constexpr int kOldestSupportedConfigVersion = 1;

bool WideToUtf8(
    const std::wstring_view source,
    std::string& destination) {
    if (source.empty()) {
        destination.clear();
        return true;
    }

    const auto required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        source.data(),
        static_cast<int>(source.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return false;
    }

    destination.resize(static_cast<std::size_t>(required));
    return WideCharToMultiByte(
               CP_UTF8,
               WC_ERR_INVALID_CHARS,
               source.data(),
               static_cast<int>(source.size()),
               destination.data(),
               required,
               nullptr,
               nullptr) == required;
}

bool Utf8ToWide(
    const std::string_view source,
    std::wstring& destination) {
    if (source.empty()) {
        destination.clear();
        return true;
    }

    const auto required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        source.data(),
        static_cast<int>(source.size()),
        nullptr,
        0);
    if (required <= 0) {
        return false;
    }

    destination.resize(static_cast<std::size_t>(required));
    return MultiByteToWideChar(
               CP_UTF8,
               MB_ERR_INVALID_CHARS,
               source.data(),
               static_cast<int>(source.size()),
               destination.data(),
               required) == required;
}

bool IsDefaultAction(
    const std::size_t layer,
    const Action& action) noexcept {
    return layer == 0
               ? action.kind == ActionKind::PassThrough
               : action.kind == ActionKind::Transparent;
}

bool IsNestedDecisionAction(
    const Action& action) noexcept {
    return action.kind == ActionKind::LayerTap ||
           action.kind == ActionKind::ModTap ||
           action.kind == ActionKind::TapDance;
}

bool ValidateActionShape(
    const Action& action,
    const std::size_t layer_count) noexcept {
    switch (action.kind) {
    case ActionKind::Transparent:
    case ActionKind::PassThrough:
    case ActionKind::Block:
    case ActionKind::StopMacros:
        return true;

    case ActionKind::Key:
        return action.target_key.IsValid();

    case ActionKind::Shortcut:
        return action.target_key.IsValid() &&
               action.shortcut_modifiers != 0 &&
               (action.shortcut_modifiers & ~0xFFU) == 0;

    case ActionKind::MomentaryLayer:
        return action.target_layer < layer_count;

    case ActionKind::LayerTap:
        return action.target_key.IsValid() &&
               action.target_layer < layer_count &&
               action.tapping_term_ms >= 100 &&
               action.tapping_term_ms <= 1500 &&
               action.tapping_term_ms % 10 == 0 &&
               action.quick_tap_term_ms <= 500 &&
               action.quick_tap_term_ms % 10 == 0;

    case ActionKind::ModTap:
        return action.target_key.IsValid() &&
               action.hold_key.IsValid() &&
               action.tapping_term_ms >= 100 &&
               action.tapping_term_ms <= 1500 &&
               action.tapping_term_ms % 10 == 0 &&
               action.quick_tap_term_ms <= 500 &&
               action.quick_tap_term_ms % 10 == 0;

    case ActionKind::VirtualKey:
        return action.virtual_key > 0 &&
               action.virtual_key <= 0xFF;

    case ActionKind::MouseButton:
        return action.mouse_button <= MouseButton::X2;

    case ActionKind::MouseMove:
        return (action.mouse_x != 0 || action.mouse_y != 0) &&
               action.mouse_x >= -100 &&
               action.mouse_x <= 100 &&
               action.mouse_y >= -100 &&
               action.mouse_y <= 100;

    case ActionKind::MouseWheel:
        return action.mouse_amount != 0;

    case ActionKind::Macro:
    case ActionKind::TapDance:
        return action.reference_id != 0;
    }

    return false;
}

bool ValidateActionReferences(
    const Action& action,
    const Profile& profile) noexcept {
    if (action.kind == ActionKind::Macro) {
        return profile.FindMacro(action.reference_id) != nullptr;
    }
    if (action.kind == ActionKind::TapDance) {
        return profile.FindTapDance(action.reference_id) != nullptr;
    }
    return true;
}

bool ReadActionFields(
    std::istream& stream,
    const int version,
    Action& action) {
    unsigned int kind = 0;
    unsigned int target_scan = 0;
    unsigned int target_prefix = 0;
    unsigned int target_layer = 0;
    unsigned int hold_scan = 0;
    unsigned int hold_prefix = 0;
    unsigned int tapping_term = 500;
    unsigned int quick_tap_term = 200;
    unsigned int virtual_key = 0;
    unsigned int mouse_button = 0;
    int mouse_x = 0;
    int mouse_y = 0;
    int mouse_amount = 0;
    unsigned int reference_id = 0;
    unsigned int shortcut_modifiers = 0;

    if (!(stream >> kind >>
          target_scan >>
          target_prefix >>
          target_layer)) {
        return false;
    }

    if (version >= 2 &&
        !(stream >> hold_scan >>
          hold_prefix >>
          tapping_term >>
          quick_tap_term)) {
        return false;
    }

    if (version >= 3 &&
        !(stream >> virtual_key >>
          mouse_button >>
          mouse_x >>
          mouse_y >>
          mouse_amount >>
          reference_id)) {
        return false;
    }

    if (version >= 4 &&
        !(stream >> shortcut_modifiers)) {
        return false;
    }

    const auto maximum_kind =
        version >= 4
            ? ActionKind::Shortcut
            : version >= 3
                  ? ActionKind::TapDance
            : version >= 2
                  ? ActionKind::ModTap
                  : ActionKind::MomentaryLayer;

    if (kind > static_cast<unsigned int>(maximum_kind) ||
        target_scan > 0xFF ||
        target_prefix >
            static_cast<unsigned int>(KeyPrefix::E1) ||
        target_layer >
            std::numeric_limits<std::uint8_t>::max() ||
        hold_scan > 0xFF ||
        hold_prefix >
            static_cast<unsigned int>(KeyPrefix::E1) ||
        tapping_term >
            std::numeric_limits<std::uint16_t>::max() ||
        quick_tap_term >
            std::numeric_limits<std::uint16_t>::max() ||
        virtual_key >
            std::numeric_limits<std::uint16_t>::max() ||
        mouse_button >
            static_cast<unsigned int>(MouseButton::X2) ||
        mouse_x < std::numeric_limits<std::int16_t>::min() ||
        mouse_x > std::numeric_limits<std::int16_t>::max() ||
        mouse_y < std::numeric_limits<std::int16_t>::min() ||
        mouse_y > std::numeric_limits<std::int16_t>::max() ||
        mouse_amount < std::numeric_limits<std::int16_t>::min() ||
        mouse_amount > std::numeric_limits<std::int16_t>::max() ||
        reference_id >
            std::numeric_limits<std::uint16_t>::max() ||
        shortcut_modifiers >
            std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    action = Action{
        static_cast<ActionKind>(kind),
        PhysicalKey{
            static_cast<std::uint16_t>(target_scan),
            static_cast<KeyPrefix>(target_prefix)},
        static_cast<std::uint8_t>(target_layer),
        PhysicalKey{
            static_cast<std::uint16_t>(hold_scan),
            static_cast<KeyPrefix>(hold_prefix)},
        static_cast<std::uint16_t>(tapping_term),
        static_cast<std::uint16_t>(quick_tap_term),
        static_cast<std::uint16_t>(virtual_key),
        static_cast<MouseButton>(mouse_button),
        static_cast<std::int16_t>(mouse_x),
        static_cast<std::int16_t>(mouse_y),
        static_cast<std::int16_t>(mouse_amount),
        static_cast<std::uint16_t>(reference_id),
        static_cast<std::uint16_t>(shortcut_modifiers)};
    return true;
}

void WriteActionFields(
    std::ostream& stream,
    const Action& action) {
    stream
        << static_cast<unsigned int>(action.kind) << ' '
        << action.target_key.scan_code << ' '
        << static_cast<unsigned int>(action.target_key.prefix) << ' '
        << static_cast<unsigned int>(action.target_layer) << ' '
        << action.hold_key.scan_code << ' '
        << static_cast<unsigned int>(action.hold_key.prefix) << ' '
        << action.tapping_term_ms << ' '
        << action.quick_tap_term_ms << ' '
        << action.virtual_key << ' '
        << static_cast<unsigned int>(action.mouse_button) << ' '
        << action.mouse_x << ' '
        << action.mouse_y << ' '
        << action.mouse_amount << ' '
        << action.reference_id << ' '
        << action.shortcut_modifiers;
}

bool ValidateProfile(
    const Profile& profile,
    std::wstring& error) {
    if (profile.macros().size() > kMaximumMacros ||
        profile.tap_dances().size() > kMaximumTapDances ||
        profile.combos().size() > kMaximumCombos ||
        profile.overrides().size() > kMaximumOverrides) {
        error = L"高级规则数量超过限制。";
        return false;
    }

    std::unordered_set<std::uint16_t> macro_ids;
    for (const auto& macro : profile.macros()) {
        if (macro.id == 0 ||
            !macro_ids.insert(macro.id).second ||
            macro.name.empty() ||
            macro.events.empty() ||
            macro.events.size() > kMaximumMacroEvents) {
            error = L"宏定义无效、为空或ID重复。";
            return false;
        }

        for (const auto& event : macro.events) {
            if (!event.key.IsValid() ||
                event.transition == KeyTransition::Repeat) {
                error = L"宏事件无效。";
                return false;
            }
        }
    }

    std::unordered_set<std::uint16_t> tap_dance_ids;
    for (const auto& tap_dance : profile.tap_dances()) {
        if (tap_dance.id == 0 ||
            !tap_dance_ids.insert(tap_dance.id).second ||
            tap_dance.name.empty() ||
            tap_dance.hold_term_ms < 100 ||
            tap_dance.hold_term_ms > 1000 ||
            tap_dance.multi_tap_term_ms < 50 ||
            tap_dance.multi_tap_term_ms > 500 ||
            tap_dance.quick_tap_term_ms > 500) {
            error = L"Tap Dance定义或时间参数无效。";
            return false;
        }

        for (const auto& action :
             {tap_dance.tap_action,
              tap_dance.hold_action,
              tap_dance.double_tap_action,
              tap_dance.tap_hold_action}) {
            if (!ValidateActionShape(
                    action,
                    profile.layer_count()) ||
                IsNestedDecisionAction(action) ||
                !ValidateActionReferences(action, profile)) {
                error = L"Tap Dance包含不允许的嵌套动作。";
                return false;
            }
        }
        if (tap_dance.tap_action.kind ==
                ActionKind::MomentaryLayer ||
            tap_dance.double_tap_action.kind ==
                ActionKind::MomentaryLayer) {
            error =
                L"Tap Dance的MO(n)只能用于长按或单击后长按动作。";
            return false;
        }
    }

    std::unordered_set<std::uint16_t> combo_ids;
    for (const auto& combo : profile.combos()) {
        if (combo.id == 0 ||
            !combo_ids.insert(combo.id).second ||
            combo.name.empty() ||
            combo.member_count < 2 ||
            combo.member_count > combo.members.size() ||
            combo.term_ms < 20 ||
            combo.term_ms > 300 ||
            combo.layer_mask == 0 ||
            !ValidateActionShape(
                combo.output_action,
                profile.layer_count()) ||
            IsNestedDecisionAction(combo.output_action) ||
            !ValidateActionReferences(
                combo.output_action,
                profile)) {
            error = L"Combo定义无效。";
            return false;
        }

        std::unordered_set<std::size_t> members;
        for (std::size_t index = 0;
             index < combo.member_count;
             ++index) {
            if (!combo.members[index].IsValid() ||
                !members.insert(
                    ToKeyIndex(combo.members[index]))
                     .second) {
                error = L"Combo成员无效或重复。";
                return false;
            }
        }
    }

    std::unordered_set<std::uint16_t> override_ids;
    for (const auto& rule : profile.overrides()) {
        if (rule.id == 0 ||
            !override_ids.insert(rule.id).second ||
            rule.name.empty() ||
            !rule.trigger_key.IsValid() ||
            rule.layer_mask == 0 ||
            (rule.required_modifiers |
             rule.forbidden_modifiers |
             rule.suppressed_modifiers) > 0xFFU ||
            (rule.required_modifiers &
             rule.forbidden_modifiers) != 0 ||
            !ValidateActionShape(
                rule.replacement_action,
                profile.layer_count()) ||
            IsNestedDecisionAction(rule.replacement_action) ||
            !ValidateActionReferences(
                rule.replacement_action,
                profile)) {
            error = L"Key Override定义无效。";
            return false;
        }
    }

    const auto& mouse = profile.mouse_settings();
    if (mouse.initial_speed == 0 ||
        mouse.maximum_speed < mouse.initial_speed ||
        mouse.maximum_speed > 100 ||
        mouse.acceleration_ms > 5000 ||
        mouse.repeat_ms < 5 ||
        mouse.repeat_ms > 100 ||
        mouse.wheel_step == 0) {
        error = L"鼠标键参数无效。";
        return false;
    }

    for (std::size_t layer = 0;
         layer < profile.layer_count();
         ++layer) {
        for (std::size_t index = 0;
             index < kPhysicalKeySlotCount;
             ++index) {
            const auto action =
                profile.GetAction(layer, FromKeyIndex(index));
            if (!ValidateActionShape(
                    action,
                    profile.layer_count()) ||
                !ValidateActionReferences(action, profile)) {
                error = L"配置包含无效或缺少引用的按键动作。";
                return false;
            }
        }
    }

    return true;
}

}  // namespace

Profile* Configuration::FindProfile(
    const std::wstring_view name) noexcept {
    const auto iterator = std::find_if(
        profiles.begin(),
        profiles.end(),
        [name](const Profile& profile) {
            return profile.name() == name;
        });
    return iterator == profiles.end() ? nullptr : &*iterator;
}

const Profile* Configuration::FindProfile(
    const std::wstring_view name) const noexcept {
    return const_cast<Configuration*>(this)->FindProfile(name);
}

std::filesystem::path ConfigStore::DefaultPath() {
    std::wstring override_path(32768, L'\0');
    const auto override_length = GetEnvironmentVariableW(
        L"PCKEY_CONFIG_PATH",
        override_path.data(),
        static_cast<DWORD>(override_path.size()));

    if (override_length > 0 &&
        static_cast<std::size_t>(override_length) <
            override_path.size()) {
        override_path.resize(override_length);
        return std::filesystem::path(override_path);
    }

    std::wstring local_app_data(32768, L'\0');
    const auto length = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        local_app_data.data(),
        static_cast<DWORD>(local_app_data.size()));

    if (length == 0 ||
        static_cast<std::size_t>(length) >=
            local_app_data.size()) {
        return std::filesystem::current_path() /
               L"PCkey" /
               L"config.pckey";
    }

    local_app_data.resize(length);
    return std::filesystem::path(local_app_data) /
           L"PCkey" /
           L"config.pckey";
}

std::filesystem::path ConfigStore::DraftPath() {
    auto path = DefaultPath();
    path += L".draft";
    return path;
}

bool ConfigStore::Load(
    const std::filesystem::path& path,
    Configuration& configuration,
    std::wstring& error) {
    error.clear();
    configuration = {};

    std::error_code filesystem_error;
    const auto path_exists =
        std::filesystem::exists(path, filesystem_error);
    if (filesystem_error) {
        error = L"无法检查配置文件是否存在。";
        return false;
    }
    if (!path_exists) {
        return true;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = L"无法打开配置文件。";
        return false;
    }

    std::string signature;
    int version = 0;
    if (!(stream >> signature >> version) ||
        signature != "PCKEY_CONFIG" ||
        version < kOldestSupportedConfigVersion ||
        version > kConfigVersion) {
        error = L"配置文件版本无效。";
        return false;
    }

    std::unordered_set<std::wstring> profile_names;
    Profile* current_profile = nullptr;
    MacroDefinition* current_macro = nullptr;
    TapDanceDefinition* current_tap_dance = nullptr;
    ComboDefinition* current_combo = nullptr;
    KeyOverrideDefinition* current_override = nullptr;
    bool active_seen = false;
    std::string token;

    while (stream >> token) {
        if (token == "active") {
            if (active_seen ||
                current_profile != nullptr ||
                current_macro != nullptr ||
                current_tap_dance != nullptr ||
                current_combo != nullptr ||
                current_override != nullptr) {
                error = L"活动配置字段位置无效或重复。";
                return false;
            }
            std::string active_utf8;
            if (!(stream >> std::quoted(active_utf8)) ||
                !Utf8ToWide(
                    active_utf8,
                    configuration.active_profile)) {
                error = L"活动配置名称损坏。";
                return false;
            }
            active_seen = true;
            continue;
        }

        if (token == "profile") {
            if (current_profile != nullptr ||
                current_macro != nullptr ||
                current_tap_dance != nullptr ||
                current_combo != nullptr ||
                current_override != nullptr ||
                configuration.profiles.size() >=
                kMaximumProfiles) {
                error = L"配置数量超过限制。";
                return false;
            }

            std::string name_utf8;
            std::size_t layer_count = 0;
            unsigned int layout_value = static_cast<unsigned int>(
                KeyboardLayoutPreset::FullSize104);
            std::wstring name;

            if (!(stream >> std::quoted(name_utf8) >>
                  layer_count) ||
                (version >= 2 &&
                 !(stream >> layout_value)) ||
                !Utf8ToWide(name_utf8, name) ||
                name.empty() ||
                name == kNormalModeName ||
                layer_count == 0 ||
                layer_count > kMaximumLayerCount ||
                layout_value >
                    static_cast<unsigned int>(
                        KeyboardLayoutPreset::Laptop) ||
                profile_names.contains(name)) {
                error = L"配置定义无效或名称重复。";
                return false;
            }

            profile_names.insert(name);
            configuration.profiles.emplace_back(
                std::move(name),
                layer_count,
                static_cast<KeyboardLayoutPreset>(
                    layout_value));
            current_profile = &configuration.profiles.back();
            current_macro = nullptr;
            current_tap_dance = nullptr;
            current_combo = nullptr;
            current_override = nullptr;
            continue;
        }

        if (token == "mouse") {
            if (version < 3 ||
                current_profile == nullptr ||
                current_macro != nullptr ||
                current_tap_dance != nullptr ||
                current_combo != nullptr ||
                current_override != nullptr) {
                error = L"鼠标参数位置无效。";
                return false;
            }

            unsigned int initial = 0;
            unsigned int maximum = 0;
            unsigned int acceleration = 0;
            unsigned int repeat = 0;
            int wheel = 0;
            if (!(stream >> initial >> maximum >>
                  acceleration >> repeat >> wheel) ||
                initial >
                    std::numeric_limits<std::uint16_t>::max() ||
                maximum >
                    std::numeric_limits<std::uint16_t>::max() ||
                acceleration >
                    std::numeric_limits<std::uint16_t>::max() ||
                repeat >
                    std::numeric_limits<std::uint16_t>::max() ||
                wheel < std::numeric_limits<std::int16_t>::min() ||
                wheel > std::numeric_limits<std::int16_t>::max()) {
                error = L"鼠标参数数据无效。";
                return false;
            }

            current_profile->mouse_settings() = MouseSettings{
                static_cast<std::uint16_t>(initial),
                static_cast<std::uint16_t>(maximum),
                static_cast<std::uint16_t>(acceleration),
                static_cast<std::uint16_t>(repeat),
                static_cast<std::int16_t>(wheel)};
            continue;
        }

        if (token == "macro") {
            if (version < 3 || current_profile == nullptr ||
                current_macro != nullptr ||
                current_tap_dance != nullptr ||
                current_combo != nullptr ||
                current_override != nullptr ||
                current_profile->macros().size() >=
                    kMaximumMacros) {
                error = L"宏定义位置或数量无效。";
                return false;
            }

            unsigned int id = 0;
            std::string name_utf8;
            std::wstring name;
            if (!(stream >> id >> std::quoted(name_utf8)) ||
                id == 0 ||
                id >
                    std::numeric_limits<std::uint16_t>::max() ||
                !Utf8ToWide(name_utf8, name)) {
                error = L"宏定义损坏。";
                return false;
            }
            current_profile->macros().push_back(
                MacroDefinition{
                    static_cast<std::uint16_t>(id),
                    std::move(name),
                    {}});
            current_macro = &current_profile->macros().back();
            continue;
        }

        if (token == "macroevent") {
            if (current_macro == nullptr ||
                current_macro->events.size() >=
                    kMaximumMacroEvents) {
                error = L"宏事件位置或数量无效。";
                return false;
            }

            unsigned int delay = 0;
            unsigned int scan = 0;
            unsigned int prefix = 0;
            unsigned int transition = 0;
            if (!(stream >> delay >> scan >> prefix >> transition) ||
                delay >
                    std::numeric_limits<std::uint16_t>::max() ||
                scan > 0xFF ||
                prefix >
                    static_cast<unsigned int>(KeyPrefix::E1) ||
                transition >
                    static_cast<unsigned int>(
                        KeyTransition::Release)) {
                error = L"宏事件数据无效。";
                return false;
            }
            current_macro->events.push_back(
                MacroEvent{
                    static_cast<std::uint16_t>(delay),
                    PhysicalKey{
                        static_cast<std::uint16_t>(scan),
                        static_cast<KeyPrefix>(prefix)},
                    static_cast<KeyTransition>(transition)});
            continue;
        }

        if (token == "endmacro") {
            if (current_macro == nullptr) {
                error = L"宏结束位置无效。";
                return false;
            }
            current_macro = nullptr;
            continue;
        }

        if (token == "tapdance") {
            if (version < 3 || current_profile == nullptr ||
                current_macro != nullptr ||
                current_tap_dance != nullptr ||
                current_combo != nullptr ||
                current_override != nullptr ||
                current_profile->tap_dances().size() >=
                    kMaximumTapDances) {
                error = L"Tap Dance定义位置或数量无效。";
                return false;
            }

            unsigned int id = 0;
            std::string name_utf8;
            unsigned int hold = 0;
            unsigned int multi = 0;
            unsigned int quick = 0;
            std::wstring name;
            if (!(stream >> id >> std::quoted(name_utf8) >>
                  hold >> multi >> quick) ||
                id == 0 ||
                id >
                    std::numeric_limits<std::uint16_t>::max() ||
                hold >
                    std::numeric_limits<std::uint16_t>::max() ||
                multi >
                    std::numeric_limits<std::uint16_t>::max() ||
                quick >
                    std::numeric_limits<std::uint16_t>::max() ||
                !Utf8ToWide(name_utf8, name)) {
                error = L"Tap Dance定义损坏。";
                return false;
            }
            current_profile->tap_dances().push_back(
                TapDanceDefinition{
                    static_cast<std::uint16_t>(id),
                    std::move(name),
                    Action::Transparent(),
                    Action::Transparent(),
                    Action::Transparent(),
                    Action::Transparent(),
                    static_cast<std::uint16_t>(hold),
                    static_cast<std::uint16_t>(multi),
                    static_cast<std::uint16_t>(quick)});
            current_tap_dance =
                &current_profile->tap_dances().back();
            continue;
        }

        if (token == "tdaction") {
            if (current_tap_dance == nullptr) {
                error = L"Tap Dance动作位置无效。";
                return false;
            }
            unsigned int slot = 0;
            Action action;
            if (!(stream >> slot) ||
                slot > 3 ||
                !ReadActionFields(stream, version, action)) {
                error = L"Tap Dance动作数据无效。";
                return false;
            }
            switch (slot) {
            case 0:
                current_tap_dance->tap_action = action;
                break;
            case 1:
                current_tap_dance->hold_action = action;
                break;
            case 2:
                current_tap_dance->double_tap_action = action;
                break;
            case 3:
                current_tap_dance->tap_hold_action = action;
                break;
            default:
                break;
            }
            continue;
        }

        if (token == "endtapdance") {
            if (current_tap_dance == nullptr) {
                error = L"Tap Dance结束位置无效。";
                return false;
            }
            current_tap_dance = nullptr;
            continue;
        }

        if (token == "combo") {
            if (version < 3 || current_profile == nullptr ||
                current_macro != nullptr ||
                current_tap_dance != nullptr ||
                current_combo != nullptr ||
                current_override != nullptr ||
                current_profile->combos().size() >=
                    kMaximumCombos) {
                error = L"Combo定义位置或数量无效。";
                return false;
            }
            unsigned int id = 0;
            std::string name_utf8;
            unsigned int member_count = 0;
            unsigned int term = 0;
            std::uint32_t layer_mask = 0;
            std::wstring name;
            if (!(stream >> id >> std::quoted(name_utf8) >>
                  member_count >> term >> layer_mask) ||
                id == 0 ||
                id >
                    std::numeric_limits<std::uint16_t>::max() ||
                member_count > 4 ||
                term >
                    std::numeric_limits<std::uint16_t>::max() ||
                !Utf8ToWide(name_utf8, name)) {
                error = L"Combo定义损坏。";
                return false;
            }
            current_profile->combos().push_back(
                ComboDefinition{
                    static_cast<std::uint16_t>(id),
                    std::move(name),
                    {},
                    static_cast<std::uint8_t>(member_count),
                    Action::Transparent(),
                    static_cast<std::uint16_t>(term),
                    layer_mask});
            current_combo = &current_profile->combos().back();
            continue;
        }

        if (token == "combomember") {
            if (current_combo == nullptr) {
                error = L"Combo成员位置无效。";
                return false;
            }
            unsigned int index = 0;
            unsigned int scan = 0;
            unsigned int prefix = 0;
            if (!(stream >> index >> scan >> prefix) ||
                index >= current_combo->members.size() ||
                scan > 0xFF ||
                prefix >
                    static_cast<unsigned int>(KeyPrefix::E1)) {
                error = L"Combo成员数据无效。";
                return false;
            }
            current_combo->members[index] = PhysicalKey{
                static_cast<std::uint16_t>(scan),
                static_cast<KeyPrefix>(prefix)};
            continue;
        }

        if (token == "comboaction") {
            if (current_combo == nullptr ||
                !ReadActionFields(
                    stream,
                    version,
                    current_combo->output_action)) {
                error = L"Combo输出动作无效。";
                return false;
            }
            continue;
        }

        if (token == "endcombo") {
            if (current_combo == nullptr) {
                error = L"Combo结束位置无效。";
                return false;
            }
            current_combo = nullptr;
            continue;
        }

        if (token == "override") {
            if (version < 3 || current_profile == nullptr ||
                current_macro != nullptr ||
                current_tap_dance != nullptr ||
                current_combo != nullptr ||
                current_override != nullptr ||
                current_profile->overrides().size() >=
                    kMaximumOverrides) {
                error = L"Key Override定义位置或数量无效。";
                return false;
            }

            unsigned int id = 0;
            std::string name_utf8;
            unsigned int trigger_scan = 0;
            unsigned int trigger_prefix = 0;
            unsigned int required = 0;
            unsigned int forbidden = 0;
            unsigned int suppressed = 0;
            unsigned int exact = 0;
            std::uint32_t layer_mask = 0;
            std::wstring name;
            if (!(stream >> id >> std::quoted(name_utf8) >>
                  trigger_scan >> trigger_prefix >>
                  required >> forbidden >> suppressed >>
                  exact >> layer_mask) ||
                id == 0 ||
                id >
                    std::numeric_limits<std::uint16_t>::max() ||
                trigger_scan > 0xFF ||
                trigger_prefix >
                    static_cast<unsigned int>(KeyPrefix::E1) ||
                required > 0xFF ||
                forbidden > 0xFF ||
                suppressed > 0xFF ||
                exact > 1 ||
                !Utf8ToWide(name_utf8, name)) {
                error = L"Key Override定义损坏。";
                return false;
            }

            current_profile->overrides().push_back(
                KeyOverrideDefinition{
                    static_cast<std::uint16_t>(id),
                    std::move(name),
                    PhysicalKey{
                        static_cast<std::uint16_t>(trigger_scan),
                        static_cast<KeyPrefix>(trigger_prefix)},
                    static_cast<std::uint16_t>(required),
                    static_cast<std::uint16_t>(forbidden),
                    static_cast<std::uint16_t>(suppressed),
                    exact != 0,
                    Action::Transparent(),
                    layer_mask});
            current_override =
                &current_profile->overrides().back();
            continue;
        }

        if (token == "overrideaction") {
            if (current_override == nullptr ||
                !ReadActionFields(
                    stream,
                    version,
                    current_override->replacement_action)) {
                error = L"Key Override替换动作无效。";
                return false;
            }
            continue;
        }

        if (token == "endoverride") {
            if (current_override == nullptr) {
                error = L"Key Override结束位置无效。";
                return false;
            }
            current_override = nullptr;
            continue;
        }

        if (token == "action") {
            if (current_profile == nullptr ||
                current_macro != nullptr ||
                current_tap_dance != nullptr ||
                current_combo != nullptr ||
                current_override != nullptr) {
                error = L"按键动作不属于任何配置。";
                return false;
            }

            std::size_t layer = 0;
            unsigned int source_scan = 0;
            unsigned int source_prefix = 0;
            Action action;
            if (!(stream >> layer >> source_scan >> source_prefix) ||
                layer >= current_profile->layer_count() ||
                source_scan > 0xFF ||
                source_prefix >
                    static_cast<unsigned int>(KeyPrefix::E1) ||
                !ReadActionFields(stream, version, action) ||
                !ValidateActionShape(
                    action,
                    current_profile->layer_count()) ||
                !current_profile->SetAction(
                    layer,
                    PhysicalKey{
                        static_cast<std::uint16_t>(source_scan),
                        static_cast<KeyPrefix>(source_prefix)},
                    action)) {
                error = L"按键动作数据无效。";
                return false;
            }
            continue;
        }

        if (token == "endprofile") {
            if (current_profile == nullptr ||
                current_macro != nullptr ||
                current_tap_dance != nullptr ||
                current_combo != nullptr ||
                current_override != nullptr ||
                !ValidateProfile(*current_profile, error)) {
                if (error.empty()) {
                    error = L"配置结束位置无效。";
                }
                return false;
            }
            current_profile = nullptr;
            current_macro = nullptr;
            current_tap_dance = nullptr;
            current_combo = nullptr;
            current_override = nullptr;
            continue;
        }

        error = L"配置文件包含未知字段。";
        return false;
    }

    if (current_profile != nullptr) {
        error = L"配置文件缺少endprofile。";
        return false;
    }

    if (!stream.eof()) {
        error = L"配置文件读取失败。";
        return false;
    }

    if (configuration.active_profile != kNormalModeName &&
        configuration.FindProfile(
            configuration.active_profile) == nullptr) {
        configuration.active_profile =
            std::wstring(kNormalModeName);
    }

    return true;
}

bool ConfigStore::SaveAtomic(
    const std::filesystem::path& path,
    const Configuration& configuration,
    std::wstring& error) {
    error.clear();

    if (configuration.profiles.size() > kMaximumProfiles) {
        error = L"配置数量超过限制。";
        return false;
    }

    if (configuration.active_profile != kNormalModeName &&
        configuration.FindProfile(
            configuration.active_profile) == nullptr) {
        error = L"活动配置不存在。";
        return false;
    }

    std::unordered_set<std::wstring> profile_names;
    for (const auto& profile : configuration.profiles) {
        if (profile.name().empty() ||
            profile.name() == kNormalModeName ||
            !profile_names.insert(profile.name()).second) {
            error = L"配置名称为空、重复或使用了保留名称。";
            return false;
        }
        if (!ValidateProfile(profile, error)) {
            return false;
        }
    }

    std::error_code filesystem_error;
    const auto parent_path = path.parent_path();
    if (!parent_path.empty()) {
        std::filesystem::create_directories(
            parent_path,
            filesystem_error);
        if (filesystem_error) {
            error = L"无法创建配置目录。";
            return false;
        }
    }

    auto temporary_path = path;
    temporary_path += L".tmp";
    const auto remove_temporary = [&temporary_path] {
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
    };
    std::ofstream stream(
        temporary_path,
        std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = L"无法创建临时配置文件。";
        return false;
    }

    std::string active_utf8;
    if (!WideToUtf8(
            configuration.active_profile,
            active_utf8)) {
        stream.close();
        remove_temporary();
        error = L"活动配置名称无法转换为UTF-8。";
        return false;
    }

    stream << "PCKEY_CONFIG " << kConfigVersion << '\n';
    stream << "active " << std::quoted(active_utf8) << '\n';

    for (const auto& profile : configuration.profiles) {
        std::string name_utf8;
        if (!WideToUtf8(profile.name(), name_utf8)) {
            stream.close();
            remove_temporary();
            error = L"配置名称无法转换为UTF-8。";
            return false;
        }

        stream << "profile " << std::quoted(name_utf8)
               << ' ' << profile.layer_count()
               << ' ' << static_cast<unsigned int>(
                              profile.layout())
               << '\n';

        const auto& mouse = profile.mouse_settings();
        stream << "mouse "
               << mouse.initial_speed << ' '
               << mouse.maximum_speed << ' '
               << mouse.acceleration_ms << ' '
               << mouse.repeat_ms << ' '
               << mouse.wheel_step << '\n';

        for (const auto& macro : profile.macros()) {
            std::string macro_name;
            if (!WideToUtf8(macro.name, macro_name)) {
                stream.close();
                remove_temporary();
                error = L"宏名称无法转换为UTF-8。";
                return false;
            }
            stream << "macro " << macro.id << ' '
                   << std::quoted(macro_name) << '\n';
            for (const auto& event : macro.events) {
                stream << "macroevent "
                       << event.delay_ms << ' '
                       << event.key.scan_code << ' '
                       << static_cast<unsigned int>(
                              event.key.prefix)
                       << ' '
                       << static_cast<unsigned int>(
                              event.transition)
                       << '\n';
            }
            stream << "endmacro\n";
        }

        for (const auto& tap_dance : profile.tap_dances()) {
            std::string tap_dance_name;
            if (!WideToUtf8(
                    tap_dance.name,
                    tap_dance_name)) {
                stream.close();
                remove_temporary();
                error = L"Tap Dance名称无法转换为UTF-8。";
                return false;
            }
            stream << "tapdance " << tap_dance.id << ' '
                   << std::quoted(tap_dance_name) << ' '
                   << tap_dance.hold_term_ms << ' '
                   << tap_dance.multi_tap_term_ms << ' '
                   << tap_dance.quick_tap_term_ms << '\n';
            const std::array actions{
                tap_dance.tap_action,
                tap_dance.hold_action,
                tap_dance.double_tap_action,
                tap_dance.tap_hold_action};
            for (std::size_t index = 0;
                 index < actions.size();
                 ++index) {
                stream << "tdaction " << index << ' ';
                WriteActionFields(stream, actions[index]);
                stream << '\n';
            }
            stream << "endtapdance\n";
        }

        for (const auto& combo : profile.combos()) {
            std::string combo_name;
            if (!WideToUtf8(combo.name, combo_name)) {
                stream.close();
                remove_temporary();
                error = L"Combo名称无法转换为UTF-8。";
                return false;
            }
            stream << "combo " << combo.id << ' '
                   << std::quoted(combo_name) << ' '
                   << static_cast<unsigned int>(
                          combo.member_count)
                   << ' ' << combo.term_ms
                   << ' ' << combo.layer_mask << '\n';
            for (std::size_t index = 0;
                 index < combo.member_count;
                 ++index) {
                stream << "combomember " << index << ' '
                       << combo.members[index].scan_code << ' '
                       << static_cast<unsigned int>(
                              combo.members[index].prefix)
                       << '\n';
            }
            stream << "comboaction ";
            WriteActionFields(stream, combo.output_action);
            stream << "\nendcombo\n";
        }

        for (const auto& rule : profile.overrides()) {
            std::string rule_name;
            if (!WideToUtf8(rule.name, rule_name)) {
                stream.close();
                remove_temporary();
                error = L"Key Override名称无法转换为UTF-8。";
                return false;
            }
            stream << "override " << rule.id << ' '
                   << std::quoted(rule_name) << ' '
                   << rule.trigger_key.scan_code << ' '
                   << static_cast<unsigned int>(
                          rule.trigger_key.prefix)
                   << ' ' << rule.required_modifiers
                   << ' ' << rule.forbidden_modifiers
                   << ' ' << rule.suppressed_modifiers
                   << ' ' << (rule.exact_match ? 1 : 0)
                   << ' ' << rule.layer_mask << '\n';
            stream << "overrideaction ";
            WriteActionFields(stream, rule.replacement_action);
            stream << "\nendoverride\n";
        }

        for (std::size_t layer = 0;
             layer < profile.layer_count();
             ++layer) {
            for (std::size_t index = 0;
                 index < kPhysicalKeySlotCount;
                 ++index) {
                const auto source = FromKeyIndex(index);
                if (!source.IsValid()) {
                    continue;
                }
                const auto action =
                    profile.GetAction(layer, source);
                if (IsDefaultAction(layer, action)) {
                    continue;
                }

                stream << "action "
                       << layer << ' '
                       << source.scan_code << ' '
                       << static_cast<unsigned int>(
                              source.prefix)
                       << ' ';
                WriteActionFields(stream, action);
                stream << '\n';
            }
        }

        stream << "endprofile\n";
    }

    stream.flush();
    if (!stream) {
        stream.close();
        remove_temporary();
        error = L"写入配置文件失败。";
        return false;
    }
    stream.close();

    if (MoveFileExW(
            temporary_path.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH) == FALSE) {
        std::filesystem::remove(
            temporary_path,
            filesystem_error);
        error = L"无法原子替换配置文件。";
        return false;
    }

    return true;
}

}  // namespace pckey
