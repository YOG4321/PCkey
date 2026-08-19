#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pckey/action.hpp"

namespace pckey {

inline constexpr std::size_t kDefaultLayerCount = 4;
inline constexpr std::size_t kMaximumLayerCount = 32;
inline constexpr std::size_t kMaximumMacros = 32;
inline constexpr std::size_t kMaximumMacroEvents = 256;
inline constexpr std::size_t kMaximumTapDances = 64;
inline constexpr std::size_t kMaximumCombos = 64;
inline constexpr std::size_t kMaximumOverrides = 64;

enum class KeyboardLayoutPreset : std::uint8_t {
    FullSize104,
    Tkl87,
    Compact75,
    Compact65,
    Compact60,
    Laptop,
};

enum ModifierMask : std::uint16_t {
    ModifierNone = 0,
    ModifierLeftControl = 1U << 0,
    ModifierLeftShift = 1U << 1,
    ModifierLeftAlt = 1U << 2,
    ModifierLeftWin = 1U << 3,
    ModifierRightControl = 1U << 4,
    ModifierRightShift = 1U << 5,
    ModifierRightAlt = 1U << 6,
    ModifierRightWin = 1U << 7,
    ModifierAnyControl =
        ModifierLeftControl | ModifierRightControl,
    ModifierAnyShift =
        ModifierLeftShift | ModifierRightShift,
    ModifierAnyAlt =
        ModifierLeftAlt | ModifierRightAlt,
    ModifierAnyWin =
        ModifierLeftWin | ModifierRightWin,
};

struct MacroEvent {
    std::uint16_t delay_ms{};
    PhysicalKey key{};
    KeyTransition transition{KeyTransition::Press};
};

struct MacroDefinition {
    std::uint16_t id{};
    std::wstring name;
    std::vector<MacroEvent> events;
};

struct TapDanceDefinition {
    std::uint16_t id{};
    std::wstring name;
    Action tap_action{Action::Transparent()};
    Action hold_action{Action::Transparent()};
    Action double_tap_action{Action::Transparent()};
    Action tap_hold_action{Action::Transparent()};
    std::uint16_t hold_term_ms{200};
    std::uint16_t multi_tap_term_ms{200};
    std::uint16_t quick_tap_term_ms{200};
};

struct ComboDefinition {
    std::uint16_t id{};
    std::wstring name;
    std::array<PhysicalKey, 4> members{};
    std::uint8_t member_count{};
    Action output_action{Action::Transparent()};
    std::uint16_t term_ms{50};
    std::uint32_t layer_mask{0xFFFFFFFFU};
};

struct KeyOverrideDefinition {
    std::uint16_t id{};
    std::wstring name;
    PhysicalKey trigger_key{};
    std::uint16_t required_modifiers{};
    std::uint16_t forbidden_modifiers{};
    std::uint16_t suppressed_modifiers{};
    bool exact_match{};
    Action replacement_action{Action::Transparent()};
    std::uint32_t layer_mask{0xFFFFFFFFU};
};

struct MouseSettings {
    std::uint16_t initial_speed{4};
    std::uint16_t maximum_speed{18};
    std::uint16_t acceleration_ms{500};
    std::uint16_t repeat_ms{16};
    std::int16_t wheel_step{120};
};

class Layer {
public:
    Layer();

    void SetAction(PhysicalKey key, Action action) noexcept;

    [[nodiscard]] const Action& GetAction(
        PhysicalKey key) const noexcept;

private:
    std::array<Action, kPhysicalKeySlotCount> actions_{};
};

class Profile {
public:
    explicit Profile(
        std::wstring name = L"未命名配置",
        std::size_t layer_count = kDefaultLayerCount,
        KeyboardLayoutPreset layout =
            KeyboardLayoutPreset::FullSize104);

    [[nodiscard]] static Profile NormalMode();

    [[nodiscard]] const std::wstring& name() const noexcept {
        return name_;
    }

    void SetName(std::wstring name);

    [[nodiscard]] std::size_t layer_count() const noexcept {
        return layers_.size();
    }

    bool AddLayer();
    bool RemoveLastLayer();

    [[nodiscard]] KeyboardLayoutPreset layout() const noexcept {
        return layout_;
    }

    void SetLayout(const KeyboardLayoutPreset layout) noexcept {
        layout_ = layout;
    }

    bool SetAction(
        std::size_t layer,
        PhysicalKey key,
        Action action) noexcept;

    [[nodiscard]] Action GetAction(
        std::size_t layer,
        PhysicalKey key) const noexcept;

    [[nodiscard]] Action Resolve(
        PhysicalKey key,
        std::uint32_t active_layers) const noexcept;

    [[nodiscard]] MacroDefinition* FindMacro(
        std::uint16_t id) noexcept;
    [[nodiscard]] const MacroDefinition* FindMacro(
        std::uint16_t id) const noexcept;
    [[nodiscard]] TapDanceDefinition* FindTapDance(
        std::uint16_t id) noexcept;
    [[nodiscard]] const TapDanceDefinition* FindTapDance(
        std::uint16_t id) const noexcept;

    [[nodiscard]] std::vector<MacroDefinition>& macros() noexcept {
        return macros_;
    }
    [[nodiscard]] const std::vector<MacroDefinition>& macros()
        const noexcept {
        return macros_;
    }
    [[nodiscard]] std::vector<TapDanceDefinition>& tap_dances()
        noexcept {
        return tap_dances_;
    }
    [[nodiscard]] const std::vector<TapDanceDefinition>& tap_dances()
        const noexcept {
        return tap_dances_;
    }
    [[nodiscard]] std::vector<ComboDefinition>& combos() noexcept {
        return combos_;
    }
    [[nodiscard]] const std::vector<ComboDefinition>& combos()
        const noexcept {
        return combos_;
    }
    [[nodiscard]] std::vector<KeyOverrideDefinition>& overrides()
        noexcept {
        return overrides_;
    }
    [[nodiscard]] const std::vector<KeyOverrideDefinition>& overrides()
        const noexcept {
        return overrides_;
    }

    [[nodiscard]] MouseSettings& mouse_settings() noexcept {
        return mouse_settings_;
    }
    [[nodiscard]] const MouseSettings& mouse_settings()
        const noexcept {
        return mouse_settings_;
    }

private:
    std::wstring name_;
    std::vector<Layer> layers_;
    KeyboardLayoutPreset layout_{
        KeyboardLayoutPreset::FullSize104};
    std::vector<MacroDefinition> macros_;
    std::vector<TapDanceDefinition> tap_dances_;
    std::vector<ComboDefinition> combos_;
    std::vector<KeyOverrideDefinition> overrides_;
    MouseSettings mouse_settings_{};
};

}  // namespace pckey
