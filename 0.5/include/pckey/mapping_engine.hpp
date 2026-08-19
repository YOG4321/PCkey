#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "pckey/profile.hpp"

namespace pckey {

enum class SyntheticEventKind : std::uint8_t {
    Keyboard,
    VirtualKey,
    MouseButton,
    MouseMove,
    MouseWheel,
};

struct SyntheticKeyEvent {
    PhysicalKey key{};
    KeyTransition transition{KeyTransition::Press};
    SyntheticEventKind kind{SyntheticEventKind::Keyboard};
    std::uint16_t virtual_key{};
    MouseButton mouse_button{MouseButton::Left};
    std::int32_t mouse_x{};
    std::int32_t mouse_y{};
    std::int32_t mouse_amount{};
    bool horizontal{};

    [[nodiscard]] static constexpr SyntheticKeyEvent Virtual(
        const std::uint16_t key,
        const KeyTransition key_transition) noexcept {
        SyntheticKeyEvent event{};
        event.kind = SyntheticEventKind::VirtualKey;
        event.virtual_key = key;
        event.transition = key_transition;
        return event;
    }

    [[nodiscard]] static constexpr SyntheticKeyEvent MouseButtonEvent(
        const MouseButton button,
        const KeyTransition key_transition) noexcept {
        SyntheticKeyEvent event{};
        event.kind = SyntheticEventKind::MouseButton;
        event.mouse_button = button;
        event.transition = key_transition;
        return event;
    }

    [[nodiscard]] static constexpr SyntheticKeyEvent MouseMoveEvent(
        const std::int32_t x,
        const std::int32_t y) noexcept {
        SyntheticKeyEvent event{};
        event.kind = SyntheticEventKind::MouseMove;
        event.mouse_x = x;
        event.mouse_y = y;
        return event;
    }

    [[nodiscard]] static constexpr SyntheticKeyEvent MouseWheelEvent(
        const std::int32_t amount,
        const bool is_horizontal) noexcept {
        SyntheticKeyEvent event{};
        event.kind = SyntheticEventKind::MouseWheel;
        event.mouse_amount = amount;
        event.horizontal = is_horizontal;
        return event;
    }
};

struct ProcessResult {
    // A normal key event produces only a handful of synthetic events, but a
    // delayed macro/tap-dance flush can legitimately produce considerably
    // more.  Keep the result allocation-free on the hook path while making
    // exhaustion observable to callers instead of silently dropping events.
    static inline constexpr std::size_t kMaximumSyntheticEvents = 512;

    bool suppress_original{};
    std::array<SyntheticKeyEvent, kMaximumSyntheticEvents>
        synthetic_events{};
    std::size_t synthetic_count{};
    bool overflowed{};

    void AddSynthetic(
        PhysicalKey key,
        KeyTransition transition) noexcept;
    void AddSynthetic(const SyntheticKeyEvent& event) noexcept;

    void Append(const ProcessResult& other) noexcept;
};

class MappingEngine {
public:
    explicit MappingEngine(Profile profile = Profile::NormalMode());

    [[nodiscard]] ProcessResult Process(const KeyEvent& event) noexcept;
    [[nodiscard]] ProcessResult AdvanceTime(
        std::uint64_t timestamp_us) noexcept;

    [[nodiscard]] std::vector<SyntheticKeyEvent> ReleaseAll();

    void LoadProfile(Profile profile);

    void SetBypass(bool bypass) noexcept;

    [[nodiscard]] bool bypassed() const noexcept {
        return bypass_;
    }

    [[nodiscard]] std::uint32_t active_layers() const noexcept;

    [[nodiscard]] std::optional<std::uint64_t>
    NextDeadlineMicros() const noexcept;

private:
    enum class BindingKind : std::uint8_t {
        None,
        PassThrough,
        Block,
        Key,
        MomentaryLayer,
        PendingTapHold,
        VirtualKey,
        MouseButton,
        MouseMove,
        MouseWheel,
        TapDanceFirstDown,
        TapDanceWaitingSecond,
        TapDanceSecondDown,
        ComboPending,
        ComboMember,
        Shortcut,
    };

    struct ActiveBinding {
        BindingKind kind{BindingKind::None};
        PhysicalKey target_key{};
        std::uint8_t target_layer{};
        PhysicalKey hold_key{};
        ActionKind tap_hold_kind{ActionKind::Transparent};
        std::uint64_t pressed_at_us{};
        std::uint16_t tapping_term_ms{};
        std::uint16_t virtual_key{};
        MouseButton mouse_button{MouseButton::Left};
        std::int16_t mouse_x{};
        std::int16_t mouse_y{};
        std::int16_t mouse_amount{};
        std::uint64_t next_repeat_us{};
        std::uint16_t reference_id{};
        PhysicalKey identity_key{};
        Action deferred_action{};
        std::uint16_t suppressed_modifiers{};
        std::uint16_t shortcut_modifiers{};
    };

    static inline constexpr std::size_t kMaximumConcurrentMacros = 8;

    struct ActiveMacro {
        bool active{};
        std::uint16_t macro_id{};
        std::size_t event_index{};
        std::uint64_t next_event_us{};
        std::array<bool, kPhysicalKeySlotCount> held_keys{};
    };

    static inline constexpr std::size_t kMaximumActiveCombos = 8;

    struct ActiveCombo {
        bool active{};
        std::uint16_t definition_id{};
        std::array<std::size_t, 4> sources{};
        std::uint8_t source_count{};
        ActiveBinding output_binding{};
    };

    [[nodiscard]] ProcessResult ProcessPress(
        const KeyEvent& event,
        std::size_t source_index) noexcept;

    [[nodiscard]] ProcessResult ProcessRepeat(
        const ActiveBinding& binding) noexcept;

    [[nodiscard]] ProcessResult ProcessRelease(
        const KeyEvent& event,
        std::size_t source_index) noexcept;

    void ActivatePendingHolds(
        ProcessResult& result,
        std::optional<std::uint64_t> due_at_us) noexcept;

    void ActivatePendingHold(
        ActiveBinding& binding,
        ProcessResult& result) noexcept;

    void SettlePendingTaps(
        ProcessResult& result,
        std::uint64_t timestamp_us) noexcept;

    void ExecuteTap(
        PhysicalKey key,
        ProcessResult& result) noexcept;

    void PressSyntheticKey(
        PhysicalKey key,
        ProcessResult& result) noexcept;

    void ReleaseSyntheticKey(
        PhysicalKey key,
        ProcessResult& result) noexcept;
    [[nodiscard]] std::uint16_t PressShortcutModifiers(
        std::uint16_t mask,
        ProcessResult& result) noexcept;
    void ReleaseShortcutModifiers(
        std::uint16_t mask,
        ProcessResult& result) noexcept;

    void PressVirtualKey(
        std::uint16_t key,
        ProcessResult& result) noexcept;
    void ReleaseVirtualKey(
        std::uint16_t key,
        ProcessResult& result) noexcept;
    void PressMouseButton(
        MouseButton button,
        ProcessResult& result) noexcept;
    void ReleaseMouseButton(
        MouseButton button,
        ProcessResult& result) noexcept;
    void StartMacro(
        std::uint16_t macro_id,
        std::uint64_t timestamp_us) noexcept;
    void AdvanceMacros(
        std::uint64_t timestamp_us,
        ProcessResult& result) noexcept;
    void StopMacros(ProcessResult& result) noexcept;
    void AdvanceMouseKeys(
        std::uint64_t timestamp_us,
        ProcessResult& result) noexcept;
    void InterruptTapDances(
        std::size_t except_source,
        std::uint64_t timestamp_us,
        ProcessResult& result) noexcept;
    void AdvanceTapDances(
        std::uint64_t timestamp_us,
        ProcessResult& result) noexcept;
    void ProcessTapDancePress(
        const KeyEvent& event,
        std::size_t source_index,
        ActiveBinding& binding,
        ProcessResult& result) noexcept;
    void ProcessTapDanceRelease(
        const KeyEvent& event,
        std::size_t source_index,
        ActiveBinding& binding,
        ProcessResult& result) noexcept;
    void ExecuteActionTap(
        const Action& action,
        std::uint64_t timestamp_us,
        ProcessResult& result) noexcept;
    void ExecuteResolvedActionTap(
        const Action& action,
        PhysicalKey source,
        std::uint64_t timestamp_us,
        ProcessResult& result) noexcept;
    void ActivateActionHold(
        const Action& action,
        std::uint64_t timestamp_us,
        ActiveBinding& binding,
        ProcessResult& result) noexcept;
    void ActivateResolvedActionHold(
        const Action& action,
        PhysicalKey source,
        std::uint64_t timestamp_us,
        ActiveBinding& binding,
        ProcessResult& result) noexcept;
    [[nodiscard]] bool TryProcessComboPress(
        const KeyEvent& event,
        std::size_t source_index,
        ProcessResult& result) noexcept;
    void AdvanceCombos(
        std::uint64_t timestamp_us,
        ProcessResult& result) noexcept;
    void FlushPendingCombos(
        std::uint64_t timestamp_us,
        ProcessResult& result,
        bool all) noexcept;
    void FlushPendingCombo(
        std::size_t source_index,
        std::uint64_t timestamp_us,
        bool released,
        bool interrupted,
        ProcessResult& result) noexcept;
    [[nodiscard]] std::optional<PhysicalKey> ActionIdentity(
        const Action& action,
        PhysicalKey source) const noexcept;
    [[nodiscard]] const ComboDefinition* BestCompleteCombo(
        std::array<std::size_t, 4>& sources,
        std::uint8_t& source_count) const noexcept;
    [[nodiscard]] bool HasPotentialLargerCombo(
        const ComboDefinition& complete) const noexcept;
    void ActivateCombo(
        const ComboDefinition& combo,
        const std::array<std::size_t, 4>& sources,
        std::uint8_t source_count,
        std::uint64_t timestamp_us,
        ProcessResult& result) noexcept;
    void ReleaseCombo(
        std::size_t combo_index,
        ProcessResult& result) noexcept;
    void ReleaseHeldBinding(
        ActiveBinding& binding,
        ProcessResult& result) noexcept;
    [[nodiscard]] const KeyOverrideDefinition*
    ResolveKeyOverride(
        const Action& action,
        PhysicalKey source) const noexcept;
    [[nodiscard]] std::uint16_t CurrentModifierMask()
        const noexcept;
    [[nodiscard]] std::uint16_t SuppressModifiers(
        std::uint16_t mask,
        ProcessResult& result) noexcept;
    void RestoreModifiers(
        std::uint16_t mask,
        ProcessResult& result) noexcept;
    [[nodiscard]] static std::optional<std::size_t>
    ModifierIndex(PhysicalKey key) noexcept;
    [[nodiscard]] static PhysicalKey ModifierKey(
        std::size_t index) noexcept;

    [[nodiscard]] bool ReleaseDropsModifier(
        const KeyEvent& event,
        const ActiveBinding& binding) const noexcept;

    [[nodiscard]] static bool IsModifierKey(
        PhysicalKey key) noexcept;

    void ResetRuntimeState() noexcept;

    Profile profile_;
    bool bypass_{};
    std::array<ActiveBinding, kPhysicalKeySlotCount> bindings_{};
    std::array<std::uint16_t, kPhysicalKeySlotCount>
        synthetic_key_holds_{};
    std::array<std::uint16_t, kMaximumLayerCount>
        momentary_layer_holds_{};
    std::array<std::uint64_t, kPhysicalKeySlotCount>
        last_tap_release_us_{};
    std::array<std::uint16_t, 256> virtual_key_holds_{};
    std::array<std::uint16_t, 5> mouse_button_holds_{};
    std::array<ActiveMacro, kMaximumConcurrentMacros>
        active_macros_{};
    std::array<ActiveCombo, kMaximumActiveCombos>
        active_combos_{};
    std::array<std::uint16_t, 8>
        modifier_suppression_counts_{};
};

}  // namespace pckey
