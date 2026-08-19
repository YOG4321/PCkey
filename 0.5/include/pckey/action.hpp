#pragma once

#include <cstdint>

#include "pckey/physical_key.hpp"

namespace pckey {

enum class ActionKind : std::uint8_t {
    Transparent,
    PassThrough,
    Block,
    Key,
    MomentaryLayer,
    LayerTap,
    ModTap,
    VirtualKey,
    MouseButton,
    MouseMove,
    MouseWheel,
    Macro,
    StopMacros,
    TapDance,
    Shortcut,
};

enum class MouseButton : std::uint8_t {
    Left,
    Right,
    Middle,
    X1,
    X2,
};

struct Action {
    ActionKind kind{ActionKind::Transparent};
    PhysicalKey target_key{};
    std::uint8_t target_layer{};
    PhysicalKey hold_key{};
    std::uint16_t tapping_term_ms{500};
    std::uint16_t quick_tap_term_ms{200};
    std::uint16_t virtual_key{};
    MouseButton mouse_button{MouseButton::Left};
    std::int16_t mouse_x{};
    std::int16_t mouse_y{};
    std::int16_t mouse_amount{};
    std::uint16_t reference_id{};
    std::uint16_t shortcut_modifiers{};

    [[nodiscard]] static constexpr Action Transparent() noexcept {
        return Action{ActionKind::Transparent};
    }

    [[nodiscard]] static constexpr Action PassThrough() noexcept {
        return Action{ActionKind::PassThrough};
    }

    [[nodiscard]] static constexpr Action Block() noexcept {
        return Action{ActionKind::Block};
    }

    [[nodiscard]] static constexpr Action Key(
        const PhysicalKey target) noexcept {
        Action action{ActionKind::Key};
        action.target_key = target;
        return action;
    }

    [[nodiscard]] static constexpr Action MomentaryLayer(
        const std::uint8_t layer) noexcept {
        Action action{ActionKind::MomentaryLayer};
        action.target_layer = layer;
        return action;
    }

    [[nodiscard]] static constexpr Action LayerTap(
        const PhysicalKey tap_key,
        const std::uint8_t layer,
        const std::uint16_t tapping_term = 500,
        const std::uint16_t quick_tap_term = 200) noexcept {
        Action action{ActionKind::LayerTap};
        action.target_key = tap_key;
        action.target_layer = layer;
        action.tapping_term_ms = tapping_term;
        action.quick_tap_term_ms = quick_tap_term;
        return action;
    }

    [[nodiscard]] static constexpr Action ModTap(
        const PhysicalKey tap_key,
        const PhysicalKey modifier,
        const std::uint16_t tapping_term = 500,
        const std::uint16_t quick_tap_term = 200) noexcept {
        Action action{ActionKind::ModTap};
        action.target_key = tap_key;
        action.hold_key = modifier;
        action.tapping_term_ms = tapping_term;
        action.quick_tap_term_ms = quick_tap_term;
        return action;
    }

    [[nodiscard]] static constexpr Action VirtualKey(
        const std::uint16_t key) noexcept {
        Action action{ActionKind::VirtualKey};
        action.virtual_key = key;
        return action;
    }

    [[nodiscard]] static constexpr Action MouseButtonAction(
        const MouseButton button) noexcept {
        Action action{ActionKind::MouseButton};
        action.mouse_button = button;
        return action;
    }

    [[nodiscard]] static constexpr Action MouseMove(
        const std::int16_t x,
        const std::int16_t y) noexcept {
        Action action{ActionKind::MouseMove};
        action.mouse_x = x;
        action.mouse_y = y;
        return action;
    }

    [[nodiscard]] static constexpr Action MouseWheel(
        const std::int16_t amount,
        const bool horizontal = false) noexcept {
        Action action{ActionKind::MouseWheel};
        action.mouse_amount = amount;
        action.mouse_x = horizontal ? 1 : 0;
        return action;
    }

    [[nodiscard]] static constexpr Action Macro(
        const std::uint16_t macro_id) noexcept {
        Action action{ActionKind::Macro};
        action.reference_id = macro_id;
        return action;
    }

    [[nodiscard]] static constexpr Action StopMacros() noexcept {
        return Action{ActionKind::StopMacros};
    }

    [[nodiscard]] static constexpr Action TapDance(
        const std::uint16_t tap_dance_id) noexcept {
        Action action{ActionKind::TapDance};
        action.reference_id = tap_dance_id;
        return action;
    }

    [[nodiscard]] static constexpr Action Shortcut(
        const PhysicalKey key,
        const std::uint16_t modifiers) noexcept {
        Action action{ActionKind::Shortcut};
        action.target_key = key;
        action.shortcut_modifiers = modifiers;
        return action;
    }

    [[nodiscard]] constexpr bool IsTapHold() const noexcept {
        return kind == ActionKind::LayerTap ||
               kind == ActionKind::ModTap;
    }

    [[nodiscard]] constexpr bool IsReferenceAction() const noexcept {
        return kind == ActionKind::Macro ||
               kind == ActionKind::TapDance;
    }
};

}  // namespace pckey
