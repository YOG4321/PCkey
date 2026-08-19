#include "pckey/mapping_engine.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace pckey {

namespace {

constexpr PhysicalKey kLeftControl{0x1D, KeyPrefix::None};
constexpr PhysicalKey kRightControl{0x1D, KeyPrefix::E0};
constexpr PhysicalKey kLeftShift{0x2A, KeyPrefix::None};
constexpr PhysicalKey kRightShift{0x36, KeyPrefix::None};
constexpr PhysicalKey kLeftAlt{0x38, KeyPrefix::None};
constexpr PhysicalKey kRightAlt{0x38, KeyPrefix::E0};
constexpr PhysicalKey kLeftWin{0x5B, KeyPrefix::E0};
constexpr PhysicalKey kRightWin{0x5C, KeyPrefix::E0};

constexpr std::uint64_t SaturatingAdd(
    const std::uint64_t left,
    const std::uint64_t right) noexcept {
    return right > std::numeric_limits<std::uint64_t>::max() - left
               ? std::numeric_limits<std::uint64_t>::max()
               : left + right;
}

constexpr std::uint64_t MillisecondsToMicros(
    const std::uint64_t milliseconds) noexcept {
    return milliseconds >
                   std::numeric_limits<std::uint64_t>::max() / 1'000ULL
               ? std::numeric_limits<std::uint64_t>::max()
               : milliseconds * 1'000ULL;
}

}  // namespace

void ProcessResult::AddSynthetic(
    const PhysicalKey key,
    const KeyTransition transition) noexcept {
    AddSynthetic(SyntheticKeyEvent{key, transition});
}

void ProcessResult::AddSynthetic(
    const SyntheticKeyEvent& event) noexcept {
    if (synthetic_count >= synthetic_events.size()) {
        overflowed = true;
        return;
    }
    synthetic_events[synthetic_count] = event;
    ++synthetic_count;
}

void ProcessResult::Append(
    const ProcessResult& other) noexcept {
    if (this == &other) {
        return;
    }

    suppress_original =
        suppress_original || other.suppress_original;
    overflowed = overflowed || other.overflowed;
    const auto other_count =
        std::min(other.synthetic_count, other.synthetic_events.size());
    if (other.synthetic_count > other.synthetic_events.size()) {
        overflowed = true;
    }
    for (std::size_t index = 0;
         index < other_count;
         ++index) {
        AddSynthetic(other.synthetic_events[index]);
    }
}

MappingEngine::MappingEngine(Profile profile)
    : profile_(std::move(profile)) {
    ResetRuntimeState();
}

ProcessResult MappingEngine::Process(
    const KeyEvent& event) noexcept {
    if (bypass_ || !event.key.IsValid() ||
        (event.transition != KeyTransition::Press &&
         event.transition != KeyTransition::Repeat &&
         event.transition != KeyTransition::Release)) {
        return {};
    }

    ProcessResult result = AdvanceTime(event.timestamp_us);
    const auto source_index = ToKeyIndex(event.key);
    auto& binding = bindings_[source_index];

    if (event.transition == KeyTransition::Release) {
        if (binding.kind == BindingKind::ComboPending) {
            result.suppress_original = true;
            FlushPendingCombo(
                source_index,
                event.timestamp_us,
                true,
                false,
                result);
            return result;
        }
        if (binding.kind == BindingKind::ComboMember) {
            result.suppress_original = true;
            if (binding.reference_id != 0) {
                ReleaseCombo(
                    static_cast<std::size_t>(
                        binding.reference_id - 1),
                    result);
            }
            binding = {};
            return result;
        }

        if (binding.kind == BindingKind::TapDanceFirstDown ||
            binding.kind == BindingKind::TapDanceSecondDown) {
            ProcessTapDanceRelease(
                event,
                source_index,
                binding,
                result);
            return result;
        }

        if (ReleaseDropsModifier(event, binding)) {
            SettlePendingTaps(result, event.timestamp_us);
        }
        result.Append(ProcessRelease(event, source_index));
        return result;
    }

    if (event.transition == KeyTransition::Press) {
        InterruptTapDances(
            source_index,
            event.timestamp_us,
            result);

        if (binding.kind ==
            BindingKind::TapDanceWaitingSecond) {
            ProcessTapDancePress(
                event,
                source_index,
                binding,
                result);
            return result;
        }

        ActivatePendingHolds(result, std::nullopt);

        if (binding.kind == BindingKind::None &&
            TryProcessComboPress(
                event,
                source_index,
                result)) {
            return result;
        }
    }

    if (binding.kind != BindingKind::None) {
        result.Append(ProcessRepeat(binding));
        return result;
    }

    result.Append(ProcessPress(event, source_index));
    return result;
}

ProcessResult MappingEngine::AdvanceTime(
    const std::uint64_t timestamp_us) noexcept {
    ProcessResult result{};
    AdvanceCombos(timestamp_us, result);
    ActivatePendingHolds(result, timestamp_us);
    AdvanceTapDances(timestamp_us, result);
    AdvanceMacros(timestamp_us, result);
    AdvanceMouseKeys(timestamp_us, result);
    return result;
}

std::vector<SyntheticKeyEvent> MappingEngine::ReleaseAll() {
    std::vector<SyntheticKeyEvent> releases;
    releases.reserve(
        kPhysicalKeySlotCount +
        virtual_key_holds_.size() +
        mouse_button_holds_.size() +
        modifier_suppression_counts_.size());

    for (std::size_t index = 0;
         index < synthetic_key_holds_.size();
         ++index) {
        if (synthetic_key_holds_[index] != 0) {
            releases.push_back(
                SyntheticKeyEvent{
                    FromKeyIndex(index),
                    KeyTransition::Release});
        }
    }

    for (std::size_t index = 0;
         index < virtual_key_holds_.size();
         ++index) {
        if (virtual_key_holds_[index] != 0) {
            releases.push_back(
                SyntheticKeyEvent::Virtual(
                    static_cast<std::uint16_t>(index),
                    KeyTransition::Release));
        }
    }

    for (std::size_t index = 0;
         index < mouse_button_holds_.size();
         ++index) {
        if (mouse_button_holds_[index] != 0) {
            releases.push_back(
                SyntheticKeyEvent::MouseButtonEvent(
                    static_cast<MouseButton>(index),
                    KeyTransition::Release));
        }
    }

    for (std::size_t index = 0;
         index < modifier_suppression_counts_.size();
         ++index) {
        const auto key = ModifierKey(index);
        const auto& source_binding =
            bindings_[ToKeyIndex(key)];
        if (modifier_suppression_counts_[index] != 0 &&
            source_binding.kind ==
                BindingKind::PassThrough) {
            releases.push_back(
                SyntheticKeyEvent{
                    key,
                    KeyTransition::Press});
        }
    }

    ResetRuntimeState();
    return releases;
}

void MappingEngine::LoadProfile(Profile profile) {
    profile_ = std::move(profile);
    ResetRuntimeState();
}

void MappingEngine::SetBypass(const bool bypass) noexcept {
    bypass_ = bypass;
}

std::uint32_t MappingEngine::active_layers() const noexcept {
    std::uint32_t mask = 1U;
    for (std::size_t layer = 1;
         layer < momentary_layer_holds_.size();
         ++layer) {
        if (momentary_layer_holds_[layer] != 0) {
            mask |= static_cast<std::uint32_t>(1U << layer);
        }
    }
    return mask;
}

std::optional<std::uint64_t>
MappingEngine::NextDeadlineMicros() const noexcept {
    std::optional<std::uint64_t> earliest;
    const auto consider =
        [&earliest](const std::uint64_t deadline) {
            if (!earliest.has_value() || deadline < *earliest) {
                earliest = deadline;
            }
        };

    for (const auto& binding : bindings_) {
        if (binding.kind == BindingKind::PendingTapHold) {
            consider(
                SaturatingAdd(
                    binding.pressed_at_us,
                    MillisecondsToMicros(
                        binding.tapping_term_ms)));
        } else if (
            binding.kind == BindingKind::MouseMove ||
            binding.kind == BindingKind::MouseWheel) {
            consider(binding.next_repeat_us);
        } else if (
            binding.kind == BindingKind::TapDanceFirstDown ||
            binding.kind == BindingKind::TapDanceSecondDown) {
            const auto* definition =
                profile_.FindTapDance(binding.reference_id);
            if (definition != nullptr) {
                consider(
                    SaturatingAdd(
                        binding.pressed_at_us,
                        MillisecondsToMicros(
                            definition->hold_term_ms)));
            }
        } else if (
            binding.kind ==
            BindingKind::TapDanceWaitingSecond) {
            consider(binding.next_repeat_us);
        } else if (
            binding.kind == BindingKind::ComboPending) {
            consider(binding.next_repeat_us);
        }
    }

    for (const auto& macro : active_macros_) {
        if (macro.active) {
            consider(macro.next_event_us);
        }
    }

    return earliest;
}

ProcessResult MappingEngine::ProcessPress(
    const KeyEvent& event,
    const std::size_t source_index) noexcept {
    ProcessResult result{};
    auto action =
        profile_.Resolve(event.key, active_layers());
    auto& binding = bindings_[source_index];
    std::uint16_t override_suppressed = 0;
    if (const auto* rule =
            ResolveKeyOverride(action, event.key);
        rule != nullptr) {
        result.suppress_original = true;
        override_suppressed =
            SuppressModifiers(
                rule->suppressed_modifiers,
                result);
        action = rule->replacement_action;
    }

    switch (action.kind) {
    case ActionKind::Transparent:
    case ActionKind::PassThrough:
        binding.kind = BindingKind::PassThrough;
        break;

    case ActionKind::Block:
        binding.kind = BindingKind::Block;
        result.suppress_original = true;
        break;

    case ActionKind::Key:
        binding.kind = BindingKind::Key;
        binding.target_key = action.target_key;
        result.suppress_original = true;
        PressSyntheticKey(action.target_key, result);
        break;

    case ActionKind::Shortcut:
        binding.kind = BindingKind::Shortcut;
        binding.target_key = action.target_key;
        binding.shortcut_modifiers =
            PressShortcutModifiers(
                action.shortcut_modifiers,
                result);
        result.suppress_original = true;
        PressSyntheticKey(action.target_key, result);
        break;

    case ActionKind::MomentaryLayer:
        binding.kind = BindingKind::MomentaryLayer;
        binding.target_layer = action.target_layer;
        result.suppress_original = true;
        if (action.target_layer < momentary_layer_holds_.size()) {
            auto& hold_count =
                momentary_layer_holds_[action.target_layer];
            if (hold_count !=
                std::numeric_limits<std::uint16_t>::max()) {
                ++hold_count;
            }
        }
        break;

    case ActionKind::LayerTap:
    case ActionKind::ModTap: {
        result.suppress_original = true;
        const auto previous_tap =
            last_tap_release_us_[source_index];
        const auto quick_window_us =
            static_cast<std::uint64_t>(
                action.quick_tap_term_ms) *
            1'000ULL;
        const bool quick_tap =
            quick_window_us != 0 &&
            previous_tap != 0 &&
            event.timestamp_us >= previous_tap &&
            event.timestamp_us - previous_tap <=
                quick_window_us;

        if (quick_tap) {
            ActivateResolvedActionHold(
                Action::Key(action.target_key),
                event.key,
                event.timestamp_us,
                binding,
                result);
            break;
        }

        binding.kind = BindingKind::PendingTapHold;
        binding.target_key = action.target_key;
        binding.target_layer = action.target_layer;
        binding.hold_key = action.hold_key;
        binding.tap_hold_kind = action.kind;
        binding.pressed_at_us = event.timestamp_us;
        binding.tapping_term_ms =
            std::max<std::uint16_t>(
                action.tapping_term_ms,
                1);
        break;
    }

    case ActionKind::VirtualKey:
        binding.kind = BindingKind::VirtualKey;
        binding.virtual_key = action.virtual_key;
        result.suppress_original = true;
        PressVirtualKey(action.virtual_key, result);
        break;

    case ActionKind::MouseButton:
        binding.kind = BindingKind::MouseButton;
        binding.mouse_button = action.mouse_button;
        result.suppress_original = true;
        PressMouseButton(action.mouse_button, result);
        break;

    case ActionKind::MouseMove:
        binding.kind = BindingKind::MouseMove;
        binding.mouse_x = action.mouse_x;
        binding.mouse_y = action.mouse_y;
        binding.pressed_at_us = event.timestamp_us;
        binding.next_repeat_us =
            SaturatingAdd(
                event.timestamp_us,
                MillisecondsToMicros(
                    profile_.mouse_settings().repeat_ms));
        result.suppress_original = true;
        result.AddSynthetic(
            SyntheticKeyEvent::MouseMoveEvent(
                action.mouse_x *
                    profile_.mouse_settings().initial_speed,
                action.mouse_y *
                    profile_.mouse_settings().initial_speed));
        break;

    case ActionKind::MouseWheel:
        binding.kind = BindingKind::MouseWheel;
        binding.mouse_x = action.mouse_x;
        binding.mouse_amount = action.mouse_amount;
        binding.pressed_at_us = event.timestamp_us;
        binding.next_repeat_us =
            SaturatingAdd(
                event.timestamp_us,
                MillisecondsToMicros(
                    profile_.mouse_settings().repeat_ms));
        result.suppress_original = true;
        result.AddSynthetic(
            SyntheticKeyEvent::MouseWheelEvent(
                action.mouse_amount *
                    profile_.mouse_settings().wheel_step,
                action.mouse_x != 0));
        break;

    case ActionKind::Macro:
        binding.kind = BindingKind::Block;
        result.suppress_original = true;
        StartMacro(action.reference_id, event.timestamp_us);
        AdvanceMacros(event.timestamp_us, result);
        break;

    case ActionKind::StopMacros:
        binding.kind = BindingKind::Block;
        result.suppress_original = true;
        StopMacros(result);
        break;

    case ActionKind::TapDance:
        result.suppress_original = true;
        if (const auto* definition =
                profile_.FindTapDance(action.reference_id);
            definition != nullptr) {
            const auto previous_tap =
                last_tap_release_us_[source_index];
            const auto quick_window =
                static_cast<std::uint64_t>(
                    definition->quick_tap_term_ms) *
                1'000ULL;
            const bool quick_hold =
                quick_window != 0 &&
                previous_tap != 0 &&
                event.timestamp_us >= previous_tap &&
                event.timestamp_us - previous_tap <=
                    quick_window &&
                definition->double_tap_action.kind ==
                    ActionKind::Transparent &&
                definition->tap_hold_action.kind ==
                    ActionKind::Transparent;

            if (quick_hold) {
                ActivateActionHold(
                    definition->tap_action,
                    event.timestamp_us,
                    binding,
                    result);
            } else {
                binding.kind =
                    BindingKind::TapDanceFirstDown;
                binding.reference_id = action.reference_id;
                binding.pressed_at_us = event.timestamp_us;
            }
        } else {
            binding.kind = BindingKind::Block;
        }
        break;
    }

    binding.suppressed_modifiers =
        static_cast<std::uint16_t>(
            binding.suppressed_modifiers |
            override_suppressed);
    return result;
}

ProcessResult MappingEngine::ProcessRepeat(
    const ActiveBinding& binding) noexcept {
    ProcessResult result{};

    switch (binding.kind) {
    case BindingKind::None:
    case BindingKind::PassThrough:
        break;

    case BindingKind::Block:
    case BindingKind::MomentaryLayer:
    case BindingKind::PendingTapHold:
    case BindingKind::MouseButton:
    case BindingKind::MouseMove:
    case BindingKind::MouseWheel:
    case BindingKind::TapDanceFirstDown:
    case BindingKind::TapDanceWaitingSecond:
    case BindingKind::TapDanceSecondDown:
    case BindingKind::ComboPending:
    case BindingKind::ComboMember:
    case BindingKind::Shortcut:
        result.suppress_original = true;
        break;

    case BindingKind::Key:
        result.suppress_original = true;
        if (binding.target_key.IsValid()) {
            result.AddSynthetic(
                binding.target_key,
                KeyTransition::Repeat);
        }
        break;

    case BindingKind::VirtualKey:
        result.suppress_original = true;
        result.AddSynthetic(
            SyntheticKeyEvent::Virtual(
                binding.virtual_key,
                KeyTransition::Repeat));
        break;

    }

    return result;
}

ProcessResult MappingEngine::ProcessRelease(
    const KeyEvent& event,
    const std::size_t source_index) noexcept {
    ProcessResult result{};
    auto& binding = bindings_[source_index];
    const auto suppressed_modifiers =
        binding.suppressed_modifiers;

    switch (binding.kind) {
    case BindingKind::None:
    case BindingKind::PassThrough:
        break;

    case BindingKind::Block:
        result.suppress_original = true;
        break;

    case BindingKind::Key:
        result.suppress_original = true;
        ReleaseSyntheticKey(binding.target_key, result);
        break;

    case BindingKind::MomentaryLayer:
        result.suppress_original = true;
        if (binding.target_layer <
            momentary_layer_holds_.size()) {
            auto& hold_count =
                momentary_layer_holds_[binding.target_layer];
            if (hold_count > 0) {
                --hold_count;
            }
        }
        break;

    case BindingKind::PendingTapHold:
        result.suppress_original = true;
        ExecuteResolvedActionTap(
            Action::Key(binding.target_key),
            event.key,
            event.timestamp_us,
            result);
        last_tap_release_us_[source_index] =
            event.timestamp_us;
        break;

    case BindingKind::VirtualKey:
        result.suppress_original = true;
        ReleaseVirtualKey(binding.virtual_key, result);
        break;

    case BindingKind::MouseButton:
        result.suppress_original = true;
        ReleaseMouseButton(binding.mouse_button, result);
        break;

    case BindingKind::Shortcut:
        result.suppress_original = true;
        ReleaseSyntheticKey(binding.target_key, result);
        ReleaseShortcutModifiers(
            binding.shortcut_modifiers,
            result);
        break;

    case BindingKind::MouseMove:
    case BindingKind::MouseWheel:
    case BindingKind::TapDanceFirstDown:
    case BindingKind::TapDanceWaitingSecond:
    case BindingKind::TapDanceSecondDown:
    case BindingKind::ComboPending:
    case BindingKind::ComboMember:
        result.suppress_original = true;
        break;
    }

    if (suppressed_modifiers != 0) {
        RestoreModifiers(
            suppressed_modifiers,
            result);
    }
    binding = {};
    return result;
}

void MappingEngine::ActivatePendingHolds(
    ProcessResult& result,
    const std::optional<std::uint64_t> due_at_us) noexcept {
    for (auto& binding : bindings_) {
        if (binding.kind != BindingKind::PendingTapHold) {
            continue;
        }

        if (due_at_us.has_value()) {
            const auto deadline =
                SaturatingAdd(
                    binding.pressed_at_us,
                    MillisecondsToMicros(
                        binding.tapping_term_ms));
            if (deadline > *due_at_us) {
                continue;
            }
        }
        ActivatePendingHold(binding, result);
    }
}

void MappingEngine::ActivatePendingHold(
    ActiveBinding& binding,
    ProcessResult& result) noexcept {
    if (binding.tap_hold_kind == ActionKind::LayerTap) {
        binding.kind = BindingKind::MomentaryLayer;
        if (binding.target_layer <
            momentary_layer_holds_.size()) {
            auto& hold_count =
                momentary_layer_holds_[binding.target_layer];
            if (hold_count !=
                std::numeric_limits<std::uint16_t>::max()) {
                ++hold_count;
            }
        }
        return;
    }

    if (binding.tap_hold_kind == ActionKind::ModTap) {
        binding.kind = BindingKind::Key;
        binding.target_key = binding.hold_key;
        PressSyntheticKey(binding.target_key, result);
        return;
    }

    binding.kind = BindingKind::Block;
}

void MappingEngine::SettlePendingTaps(
    ProcessResult& result,
    const std::uint64_t timestamp_us) noexcept {
    for (std::size_t index = 0;
         index < bindings_.size();
         ++index) {
        auto& binding = bindings_[index];
        if (binding.kind != BindingKind::PendingTapHold) {
            continue;
        }
        ExecuteResolvedActionTap(
            Action::Key(binding.target_key),
            FromKeyIndex(index),
            timestamp_us,
            result);
        last_tap_release_us_[index] = timestamp_us;
        binding = {};
        binding.kind = BindingKind::Block;
    }
}

void MappingEngine::ExecuteTap(
    const PhysicalKey key,
    ProcessResult& result) noexcept {
    PressSyntheticKey(key, result);
    ReleaseSyntheticKey(key, result);
}

void MappingEngine::PressSyntheticKey(
    const PhysicalKey key,
    ProcessResult& result) noexcept {
    if (!key.IsValid()) {
        return;
    }

    const auto target_index = ToKeyIndex(key);
    auto& hold_count = synthetic_key_holds_[target_index];
    if (hold_count == 0) {
        result.AddSynthetic(key, KeyTransition::Press);
    }
    if (hold_count !=
        std::numeric_limits<std::uint16_t>::max()) {
        ++hold_count;
    }
}

void MappingEngine::ReleaseSyntheticKey(
    const PhysicalKey key,
    ProcessResult& result) noexcept {
    if (!key.IsValid()) {
        return;
    }

    const auto target_index = ToKeyIndex(key);
    auto& hold_count = synthetic_key_holds_[target_index];
    if (hold_count == 0) {
        return;
    }

    --hold_count;
    if (hold_count == 0) {
        result.AddSynthetic(key, KeyTransition::Release);
    }
}

std::uint16_t MappingEngine::PressShortcutModifiers(
    const std::uint16_t mask,
    ProcessResult& result) noexcept {
    std::uint16_t pressed = 0;
    constexpr std::array<std::uint16_t, 4> groups{
        ModifierAnyControl,
        ModifierAnyShift,
        ModifierAnyAlt,
        ModifierAnyWin};

    for (const auto group : groups) {
        const auto requested =
            static_cast<std::uint16_t>(mask & group);
        if (requested == 0) {
            continue;
        }

        bool physical_active = false;
        for (std::size_t index = 0; index < 8; ++index) {
            const auto bit =
                static_cast<std::uint16_t>(1U << index);
            if ((group & bit) == 0) {
                continue;
            }
            const auto key = ModifierKey(index);
            if (bindings_[ToKeyIndex(key)].kind ==
                    BindingKind::PassThrough &&
                modifier_suppression_counts_[index] == 0) {
                physical_active = true;
                break;
            }
        }
        if (physical_active) {
            continue;
        }

        std::optional<std::size_t> owner_index;
        for (std::size_t index = 0; index < 8; ++index) {
            const auto bit =
                static_cast<std::uint16_t>(1U << index);
            if ((group & bit) == 0) {
                continue;
            }
            const auto key = ModifierKey(index);
            if (synthetic_key_holds_[ToKeyIndex(key)] != 0 &&
                modifier_suppression_counts_[index] == 0) {
                owner_index = index;
                break;
            }
        }
        if (!owner_index.has_value()) {
            for (std::size_t index = 0; index < 8; ++index) {
                const auto bit =
                    static_cast<std::uint16_t>(1U << index);
                if ((requested & bit) != 0) {
                    owner_index = index;
                    break;
                }
            }
        }
        if (!owner_index.has_value()) {
            continue;
        }

        const auto bit = static_cast<std::uint16_t>(
            1U << *owner_index);
        PressSyntheticKey(
            ModifierKey(*owner_index),
            result);
        pressed = static_cast<std::uint16_t>(
            pressed | bit);
    }
    return pressed;
}

void MappingEngine::ReleaseShortcutModifiers(
    const std::uint16_t mask,
    ProcessResult& result) noexcept {
    for (std::size_t offset = 0; offset < 8; ++offset) {
        const auto index = 7U - offset;
        const auto bit =
            static_cast<std::uint16_t>(1U << index);
        if ((mask & bit) != 0) {
            ReleaseSyntheticKey(
                ModifierKey(index),
                result);
        }
    }
}

void MappingEngine::PressVirtualKey(
    const std::uint16_t key,
    ProcessResult& result) noexcept {
    if (key == 0 || key >= virtual_key_holds_.size()) {
        return;
    }
    auto& count = virtual_key_holds_[key];
    if (count == 0) {
        result.AddSynthetic(
            SyntheticKeyEvent::Virtual(
                key,
                KeyTransition::Press));
    }
    if (count != std::numeric_limits<std::uint16_t>::max()) {
        ++count;
    }
}

void MappingEngine::ReleaseVirtualKey(
    const std::uint16_t key,
    ProcessResult& result) noexcept {
    if (key == 0 || key >= virtual_key_holds_.size()) {
        return;
    }
    auto& count = virtual_key_holds_[key];
    if (count == 0) {
        return;
    }
    --count;
    if (count == 0) {
        result.AddSynthetic(
            SyntheticKeyEvent::Virtual(
                key,
                KeyTransition::Release));
    }
}

void MappingEngine::PressMouseButton(
    const MouseButton button,
    ProcessResult& result) noexcept {
    const auto index = static_cast<std::size_t>(button);
    if (index >= mouse_button_holds_.size()) {
        return;
    }
    auto& count = mouse_button_holds_[index];
    if (count == 0) {
        result.AddSynthetic(
            SyntheticKeyEvent::MouseButtonEvent(
                button,
                KeyTransition::Press));
    }
    if (count != std::numeric_limits<std::uint16_t>::max()) {
        ++count;
    }
}

void MappingEngine::ReleaseMouseButton(
    const MouseButton button,
    ProcessResult& result) noexcept {
    const auto index = static_cast<std::size_t>(button);
    if (index >= mouse_button_holds_.size()) {
        return;
    }
    auto& count = mouse_button_holds_[index];
    if (count == 0) {
        return;
    }
    --count;
    if (count == 0) {
        result.AddSynthetic(
            SyntheticKeyEvent::MouseButtonEvent(
                button,
                KeyTransition::Release));
    }
}

void MappingEngine::StartMacro(
    const std::uint16_t macro_id,
    const std::uint64_t timestamp_us) noexcept {
    const auto* definition = profile_.FindMacro(macro_id);
    if (definition == nullptr || definition->events.empty()) {
        return;
    }

    for (const auto& macro : active_macros_) {
        if (macro.active && macro.macro_id == macro_id) {
            return;
        }
    }

    auto* available = static_cast<ActiveMacro*>(nullptr);
    for (auto& macro : active_macros_) {
        if (!macro.active) {
            available = &macro;
            break;
        }
    }
    if (available == nullptr) {
        return;
    }

    *available = {};
    available->active = true;
    available->macro_id = macro_id;
    available->next_event_us =
        SaturatingAdd(
            timestamp_us,
            MillisecondsToMicros(
                definition->events.front().delay_ms));
}

void MappingEngine::AdvanceMacros(
    const std::uint64_t timestamp_us,
    ProcessResult& result) noexcept {
    std::size_t event_budget = 96;

    for (auto& macro : active_macros_) {
        if (!macro.active || event_budget == 0) {
            continue;
        }

        const auto* definition =
            profile_.FindMacro(macro.macro_id);
        if (definition == nullptr ||
            definition->events.empty()) {
            macro = {};
            continue;
        }

        while (macro.active &&
               macro.next_event_us <= timestamp_us &&
               event_budget > 0) {
            const auto& event =
                definition->events[macro.event_index];
            if (!event.key.IsValid()) {
                // Profiles built in memory can bypass ConfigStore's
                // validation. Never index the fixed runtime arrays with an
                // invalid macro event; discard the malformed macro safely.
                macro = {};
                break;
            }
            const auto key_index = ToKeyIndex(event.key);

            if (event.transition == KeyTransition::Press) {
                if (!macro.held_keys[key_index]) {
                    PressSyntheticKey(event.key, result);
                    macro.held_keys[key_index] = true;
                }
            } else if (
                event.transition == KeyTransition::Release &&
                macro.held_keys[key_index]) {
                ReleaseSyntheticKey(event.key, result);
                macro.held_keys[key_index] = false;
            }

            --event_budget;
            ++macro.event_index;
            if (macro.event_index >=
                definition->events.size()) {
                for (std::size_t index = 0;
                     index < macro.held_keys.size();
                     ++index) {
                    if (macro.held_keys[index]) {
                        ReleaseSyntheticKey(
                            FromKeyIndex(index),
                            result);
                    }
                }
                macro = {};
                break;
            }

            macro.next_event_us = SaturatingAdd(
                macro.next_event_us,
                MillisecondsToMicros(
                    definition->events[macro.event_index]
                        .delay_ms));
        }
    }
}

void MappingEngine::StopMacros(
    ProcessResult& result) noexcept {
    for (auto& macro : active_macros_) {
        if (!macro.active) {
            continue;
        }
        for (std::size_t index = 0;
             index < macro.held_keys.size();
             ++index) {
            if (macro.held_keys[index]) {
                ReleaseSyntheticKey(
                    FromKeyIndex(index),
                    result);
            }
        }
        macro = {};
    }
}

void MappingEngine::AdvanceMouseKeys(
    const std::uint64_t timestamp_us,
    ProcessResult& result) noexcept {
    const auto& settings = profile_.mouse_settings();

    for (auto& binding : bindings_) {
        if ((binding.kind != BindingKind::MouseMove &&
             binding.kind != BindingKind::MouseWheel) ||
            binding.next_repeat_us > timestamp_us) {
            continue;
        }

        // A Profile can be assembled directly without going through the
        // configuration validator. A zero repeat interval would otherwise
        // make the catch-up loop below spin forever.
        const auto repeat_us = std::max<std::uint64_t>(
            1,
            MillisecondsToMicros(settings.repeat_ms));
        const auto elapsed_ms =
            static_cast<std::uint64_t>(
                (timestamp_us - binding.pressed_at_us) /
                1'000ULL);

        if (binding.kind == BindingKind::MouseMove) {
            auto speed =
                static_cast<double>(settings.maximum_speed);
            if (settings.acceleration_ms != 0 &&
                elapsed_ms < settings.acceleration_ms) {
                const auto fraction =
                    static_cast<double>(elapsed_ms) /
                    static_cast<double>(
                        settings.acceleration_ms);
                speed =
                    static_cast<double>(
                        settings.initial_speed) +
                    static_cast<double>(
                        settings.maximum_speed -
                        settings.initial_speed) *
                        fraction;
            }
            const auto rounded =
                static_cast<std::int32_t>(
                    std::max(1.0, std::round(speed)));
            result.AddSynthetic(
                SyntheticKeyEvent::MouseMoveEvent(
                    binding.mouse_x * rounded,
                    binding.mouse_y * rounded));
        } else {
            result.AddSynthetic(
                SyntheticKeyEvent::MouseWheelEvent(
                    binding.mouse_amount *
                        settings.wheel_step,
                    binding.mouse_x != 0));
        }

        if (binding.next_repeat_us ==
            std::numeric_limits<std::uint64_t>::max()) {
            continue;
        }

        // Emit at most one repeat per scheduler tick. If the message pump
        // was asleep for a long time, skip missed ticks instead of looping
        // once per interval on the hook thread.
        const auto elapsed =
            timestamp_us >= binding.next_repeat_us
                ? timestamp_us - binding.next_repeat_us
                : 0;
        const auto elapsed_intervals = elapsed / repeat_us;
        const auto intervals =
            elapsed_intervals ==
                    std::numeric_limits<std::uint64_t>::max()
                ? elapsed_intervals
                : elapsed_intervals + 1;
        const auto advance =
            intervals >
                    std::numeric_limits<std::uint64_t>::max() /
                        repeat_us
                ? std::numeric_limits<std::uint64_t>::max()
                : intervals * repeat_us;
        binding.next_repeat_us = SaturatingAdd(
            binding.next_repeat_us,
            advance);
    }
}

void MappingEngine::InterruptTapDances(
    const std::size_t except_source,
    const std::uint64_t timestamp_us,
    ProcessResult& result) noexcept {
    for (std::size_t index = 0;
         index < bindings_.size();
         ++index) {
        if (index == except_source) {
            continue;
        }

        auto& binding = bindings_[index];
        const auto* definition =
            profile_.FindTapDance(binding.reference_id);
        if (definition == nullptr) {
            continue;
        }

        if (binding.kind ==
            BindingKind::TapDanceWaitingSecond) {
            ExecuteResolvedActionTap(
                definition->tap_action,
                FromKeyIndex(index),
                timestamp_us,
                result);
            last_tap_release_us_[index] = timestamp_us;
            binding = {};
        } else if (
            binding.kind ==
            BindingKind::TapDanceFirstDown) {
            const auto& hold =
                definition->hold_action.kind ==
                        ActionKind::Transparent
                    ? definition->tap_action
                    : definition->hold_action;
            ActivateResolvedActionHold(
                hold,
                FromKeyIndex(index),
                timestamp_us,
                binding,
                result);
        } else if (
            binding.kind ==
            BindingKind::TapDanceSecondDown) {
            const auto& tap_hold =
                definition->tap_hold_action.kind ==
                        ActionKind::Transparent
                    ? definition->tap_action
                    : definition->tap_hold_action;
            ActivateResolvedActionHold(
                tap_hold,
                FromKeyIndex(index),
                timestamp_us,
                binding,
                result);
        }
    }
}

void MappingEngine::AdvanceTapDances(
    const std::uint64_t timestamp_us,
    ProcessResult& result) noexcept {
    for (std::size_t index = 0;
         index < bindings_.size();
         ++index) {
        auto& binding = bindings_[index];
        const auto* definition =
            profile_.FindTapDance(binding.reference_id);
        if (definition == nullptr) {
            continue;
        }

        if (binding.kind ==
            BindingKind::TapDanceWaitingSecond) {
            if (binding.next_repeat_us <= timestamp_us) {
                ExecuteResolvedActionTap(
                    definition->tap_action,
                    FromKeyIndex(index),
                    timestamp_us,
                    result);
                last_tap_release_us_[index] = timestamp_us;
                binding = {};
            }
            continue;
        }

        if (binding.kind !=
                BindingKind::TapDanceFirstDown &&
            binding.kind !=
                BindingKind::TapDanceSecondDown) {
            continue;
        }

        const auto deadline =
            SaturatingAdd(
                binding.pressed_at_us,
                MillisecondsToMicros(
                    definition->hold_term_ms));
        if (deadline > timestamp_us) {
            continue;
        }

        const auto& action =
            binding.kind ==
                    BindingKind::TapDanceFirstDown
                ? (definition->hold_action.kind ==
                           ActionKind::Transparent
                       ? definition->tap_action
                       : definition->hold_action)
                : (definition->tap_hold_action.kind ==
                           ActionKind::Transparent
                       ? definition->tap_action
                       : definition->tap_hold_action);
        ActivateResolvedActionHold(
            action,
            FromKeyIndex(index),
            timestamp_us,
            binding,
            result);
    }
}

void MappingEngine::ProcessTapDancePress(
    const KeyEvent& event,
    const std::size_t,
    ActiveBinding& binding,
    ProcessResult& result) noexcept {
    result.suppress_original = true;
    const auto* definition =
        profile_.FindTapDance(binding.reference_id);
    if (definition == nullptr) {
        binding = {};
        binding.kind = BindingKind::Block;
        return;
    }

    binding.kind = BindingKind::TapDanceSecondDown;
    binding.pressed_at_us = event.timestamp_us;
}

void MappingEngine::ProcessTapDanceRelease(
    const KeyEvent& event,
    const std::size_t source_index,
    ActiveBinding& binding,
    ProcessResult& result) noexcept {
    result.suppress_original = true;
    const auto* definition =
        profile_.FindTapDance(binding.reference_id);
    if (definition == nullptr) {
        binding = {};
        return;
    }

    if (binding.kind == BindingKind::TapDanceFirstDown) {
        const bool needs_second_tap =
            definition->double_tap_action.kind !=
                ActionKind::Transparent ||
            definition->tap_hold_action.kind !=
                ActionKind::Transparent;
        if (!needs_second_tap) {
            ExecuteResolvedActionTap(
                definition->tap_action,
                event.key,
                event.timestamp_us,
                result);
            last_tap_release_us_[source_index] =
                event.timestamp_us;
            binding = {};
            return;
        }

        binding.kind =
            BindingKind::TapDanceWaitingSecond;
        binding.next_repeat_us =
            SaturatingAdd(
                event.timestamp_us,
                MillisecondsToMicros(
                    definition->multi_tap_term_ms));
        return;
    }

    if (binding.kind == BindingKind::TapDanceSecondDown) {
        if (definition->double_tap_action.kind !=
            ActionKind::Transparent) {
            ExecuteResolvedActionTap(
                definition->double_tap_action,
                event.key,
                event.timestamp_us,
                result);
        } else {
            ExecuteResolvedActionTap(
                definition->tap_action,
                event.key,
                event.timestamp_us,
                result);
            ExecuteResolvedActionTap(
                definition->tap_action,
                event.key,
                event.timestamp_us,
                result);
        }
        last_tap_release_us_[source_index] =
            event.timestamp_us;
        binding = {};
    }
}

void MappingEngine::ExecuteActionTap(
    const Action& action,
    const std::uint64_t timestamp_us,
    ProcessResult& result) noexcept {
    switch (action.kind) {
    case ActionKind::Transparent:
    case ActionKind::PassThrough:
    case ActionKind::Block:
        break;

    case ActionKind::Key:
        PressSyntheticKey(action.target_key, result);
        ReleaseSyntheticKey(action.target_key, result);
        break;

    case ActionKind::MomentaryLayer:
        if (action.target_layer <
            momentary_layer_holds_.size()) {
            auto& count =
                momentary_layer_holds_[action.target_layer];
            if (count !=
                std::numeric_limits<std::uint16_t>::max()) {
                ++count;
                --count;
            }
        }
        break;

    case ActionKind::VirtualKey:
        PressVirtualKey(action.virtual_key, result);
        ReleaseVirtualKey(action.virtual_key, result);
        break;

    case ActionKind::Shortcut: {
        const auto pressed =
            PressShortcutModifiers(
                action.shortcut_modifiers,
                result);
        PressSyntheticKey(action.target_key, result);
        ReleaseSyntheticKey(action.target_key, result);
        ReleaseShortcutModifiers(pressed, result);
        break;
    }

    case ActionKind::MouseButton:
        PressMouseButton(action.mouse_button, result);
        ReleaseMouseButton(action.mouse_button, result);
        break;

    case ActionKind::MouseMove:
        result.AddSynthetic(
            SyntheticKeyEvent::MouseMoveEvent(
                action.mouse_x *
                    profile_.mouse_settings().initial_speed,
                action.mouse_y *
                    profile_.mouse_settings().initial_speed));
        break;

    case ActionKind::MouseWheel:
        result.AddSynthetic(
            SyntheticKeyEvent::MouseWheelEvent(
                action.mouse_amount *
                    profile_.mouse_settings().wheel_step,
                action.mouse_x != 0));
        break;

    case ActionKind::Macro:
        StartMacro(action.reference_id, timestamp_us);
        AdvanceMacros(timestamp_us, result);
        break;

    case ActionKind::StopMacros:
        StopMacros(result);
        break;

    case ActionKind::LayerTap:
    case ActionKind::ModTap:
    case ActionKind::TapDance:
        break;
    }
}

void MappingEngine::ExecuteResolvedActionTap(
    const Action& action,
    const PhysicalKey source,
    const std::uint64_t timestamp_us,
    ProcessResult& result) noexcept {
    auto resolved = action;
    std::uint16_t suppressed = 0;
    if (const auto* rule =
            ResolveKeyOverride(action, source);
        rule != nullptr) {
        suppressed = SuppressModifiers(
            rule->suppressed_modifiers,
            result);
        resolved = rule->replacement_action;
    }

    ExecuteActionTap(resolved, timestamp_us, result);
    if (suppressed != 0) {
        RestoreModifiers(suppressed, result);
    }
}

void MappingEngine::ActivateActionHold(
    const Action& action,
    const std::uint64_t timestamp_us,
    ActiveBinding& binding,
    ProcessResult& result) noexcept {
    switch (action.kind) {
    case ActionKind::Key:
        binding = {};
        binding.kind = BindingKind::Key;
        binding.target_key = action.target_key;
        PressSyntheticKey(action.target_key, result);
        break;

    case ActionKind::MomentaryLayer:
        binding = {};
        binding.kind = BindingKind::MomentaryLayer;
        binding.target_layer = action.target_layer;
        if (action.target_layer <
            momentary_layer_holds_.size()) {
            auto& count =
                momentary_layer_holds_[action.target_layer];
            if (count !=
                std::numeric_limits<std::uint16_t>::max()) {
                ++count;
            }
        }
        break;

    case ActionKind::VirtualKey:
        binding = {};
        binding.kind = BindingKind::VirtualKey;
        binding.virtual_key = action.virtual_key;
        PressVirtualKey(action.virtual_key, result);
        break;

    case ActionKind::Shortcut:
        binding = {};
        binding.kind = BindingKind::Shortcut;
        binding.target_key = action.target_key;
        binding.shortcut_modifiers =
            PressShortcutModifiers(
                action.shortcut_modifiers,
                result);
        PressSyntheticKey(action.target_key, result);
        break;

    case ActionKind::MouseButton:
        binding = {};
        binding.kind = BindingKind::MouseButton;
        binding.mouse_button = action.mouse_button;
        PressMouseButton(action.mouse_button, result);
        break;

    case ActionKind::MouseMove:
        binding = {};
        binding.kind = BindingKind::MouseMove;
        binding.mouse_x = action.mouse_x;
        binding.mouse_y = action.mouse_y;
        binding.pressed_at_us = timestamp_us;
        binding.next_repeat_us =
            SaturatingAdd(
                timestamp_us,
                MillisecondsToMicros(
                    profile_.mouse_settings().repeat_ms));
        result.AddSynthetic(
            SyntheticKeyEvent::MouseMoveEvent(
                action.mouse_x *
                    profile_.mouse_settings().initial_speed,
                action.mouse_y *
                    profile_.mouse_settings().initial_speed));
        break;

    case ActionKind::MouseWheel:
        binding = {};
        binding.kind = BindingKind::MouseWheel;
        binding.mouse_x = action.mouse_x;
        binding.mouse_amount = action.mouse_amount;
        binding.pressed_at_us = timestamp_us;
        binding.next_repeat_us =
            SaturatingAdd(
                timestamp_us,
                MillisecondsToMicros(
                    profile_.mouse_settings().repeat_ms));
        result.AddSynthetic(
            SyntheticKeyEvent::MouseWheelEvent(
                action.mouse_amount *
                    profile_.mouse_settings().wheel_step,
                action.mouse_x != 0));
        break;

    case ActionKind::Macro:
        binding = {};
        binding.kind = BindingKind::Block;
        StartMacro(action.reference_id, timestamp_us);
        AdvanceMacros(timestamp_us, result);
        break;

    case ActionKind::StopMacros:
        binding = {};
        binding.kind = BindingKind::Block;
        StopMacros(result);
        break;

    case ActionKind::Transparent:
    case ActionKind::PassThrough:
    case ActionKind::Block:
    case ActionKind::LayerTap:
    case ActionKind::ModTap:
    case ActionKind::TapDance:
        binding = {};
        binding.kind = BindingKind::Block;
        break;
    }
}

void MappingEngine::ActivateResolvedActionHold(
    const Action& action,
    const PhysicalKey source,
    const std::uint64_t timestamp_us,
    ActiveBinding& binding,
    ProcessResult& result) noexcept {
    auto resolved = action;
    std::uint16_t suppressed = 0;
    if (const auto* rule =
            ResolveKeyOverride(action, source);
        rule != nullptr) {
        suppressed = SuppressModifiers(
            rule->suppressed_modifiers,
            result);
        resolved = rule->replacement_action;
    }

    ActivateActionHold(
        resolved,
        timestamp_us,
        binding,
        result);
    binding.suppressed_modifiers = suppressed;
}

bool MappingEngine::TryProcessComboPress(
    const KeyEvent& event,
    const std::size_t source_index,
    ProcessResult& result) noexcept {
    const auto action =
        profile_.Resolve(event.key, active_layers());
    const auto identity = ActionIdentity(action, event.key);

    std::uint16_t maximum_term = 0;
    if (identity.has_value()) {
        for (const auto& combo : profile_.combos()) {
            if ((combo.layer_mask & active_layers()) == 0 ||
                combo.member_count < 2 ||
                combo.member_count > combo.members.size()) {
                continue;
            }
            for (std::size_t index = 0;
                 index < combo.member_count;
                 ++index) {
                if (combo.members[index] == *identity) {
                    maximum_term =
                        std::max(maximum_term, combo.term_ms);
                    break;
                }
            }
        }
    }

    if (!identity.has_value() || maximum_term == 0) {
        FlushPendingCombos(
            event.timestamp_us,
            result,
            true);
        return false;
    }

    auto& binding = bindings_[source_index];
    binding = {};
    binding.kind = BindingKind::ComboPending;
    binding.identity_key = *identity;
    binding.deferred_action = action;
    binding.pressed_at_us = event.timestamp_us;
    binding.next_repeat_us =
        SaturatingAdd(
            event.timestamp_us,
            MillisecondsToMicros(maximum_term));
    result.suppress_original = true;

    std::array<std::size_t, 4> sources{};
    std::uint8_t source_count = 0;
    if (const auto* complete =
            BestCompleteCombo(sources, source_count);
        complete != nullptr &&
        !HasPotentialLargerCombo(*complete)) {
        ActivateCombo(
            *complete,
            sources,
            source_count,
            event.timestamp_us,
            result);
    }
    return true;
}

void MappingEngine::AdvanceCombos(
    const std::uint64_t timestamp_us,
    ProcessResult& result) noexcept {
    std::array<std::size_t, 4> sources{};
    std::uint8_t source_count = 0;
    if (const auto* complete =
            BestCompleteCombo(sources, source_count);
        complete != nullptr) {
        bool deadline_reached =
            !HasPotentialLargerCombo(*complete);
        if (!deadline_reached) {
            for (std::size_t index = 0;
                 index < source_count;
                 ++index) {
                if (bindings_[sources[index]].next_repeat_us <=
                    timestamp_us) {
                    deadline_reached = true;
                    break;
                }
            }
        }

        if (deadline_reached) {
            ActivateCombo(
                *complete,
                sources,
                source_count,
                timestamp_us,
                result);
        }
    }

    FlushPendingCombos(
        timestamp_us,
        result,
        false);
}

void MappingEngine::FlushPendingCombos(
    const std::uint64_t timestamp_us,
    ProcessResult& result,
    const bool all) noexcept {
    for (std::size_t index = 0;
         index < bindings_.size();
         ++index) {
        if (bindings_[index].kind !=
            BindingKind::ComboPending) {
            continue;
        }
        if (!all &&
            bindings_[index].next_repeat_us >
                timestamp_us) {
            continue;
        }
        FlushPendingCombo(
            index,
            timestamp_us,
            false,
            all,
            result);
    }
}

void MappingEngine::FlushPendingCombo(
    const std::size_t source_index,
    const std::uint64_t timestamp_us,
    const bool released,
    const bool interrupted,
    ProcessResult& result) noexcept {
    auto& binding = bindings_[source_index];
    if (binding.kind != BindingKind::ComboPending) {
        return;
    }

    const auto action = binding.deferred_action;
    const auto source = FromKeyIndex(source_index);
    const auto pressed_at = binding.pressed_at_us;
    binding = {};

    if (released) {
        if (action.kind == ActionKind::PassThrough ||
            action.kind == ActionKind::Transparent) {
            ExecuteResolvedActionTap(
                Action::Key(source),
                source,
                timestamp_us,
                result);
        } else if (action.kind == ActionKind::LayerTap ||
                   action.kind == ActionKind::ModTap) {
            ExecuteResolvedActionTap(
                Action::Key(action.target_key),
                source,
                timestamp_us,
                result);
            last_tap_release_us_[source_index] = timestamp_us;
        } else if (action.kind == ActionKind::TapDance) {
            if (const auto* definition =
                    profile_.FindTapDance(
                        action.reference_id);
                definition != nullptr) {
                ExecuteResolvedActionTap(
                    definition->tap_action,
                    source,
                    timestamp_us,
                    result);
            }
            last_tap_release_us_[source_index] = timestamp_us;
        } else {
            ExecuteResolvedActionTap(
                action,
                source,
                timestamp_us,
                result);
        }
        return;
    }

    if (action.kind == ActionKind::LayerTap ||
        action.kind == ActionKind::ModTap) {
        if (interrupted ||
            timestamp_us >=
                SaturatingAdd(
                    pressed_at,
                    MillisecondsToMicros(
                        action.tapping_term_ms))) {
            if (action.kind == ActionKind::LayerTap) {
                ActivateResolvedActionHold(
                    Action::MomentaryLayer(
                        action.target_layer),
                    source,
                    timestamp_us,
                    binding,
                    result);
            } else {
                ActivateResolvedActionHold(
                    Action::Key(action.hold_key),
                    source,
                    timestamp_us,
                    binding,
                    result);
            }
        } else {
            binding.kind = BindingKind::PendingTapHold;
            binding.target_key = action.target_key;
            binding.target_layer = action.target_layer;
            binding.hold_key = action.hold_key;
            binding.tap_hold_kind = action.kind;
            binding.pressed_at_us = pressed_at;
            binding.tapping_term_ms =
                action.tapping_term_ms;
        }
        return;
    }

    if (action.kind == ActionKind::TapDance) {
        const auto* definition =
            profile_.FindTapDance(action.reference_id);
        if (definition == nullptr) {
            binding.kind = BindingKind::Block;
            return;
        }
        if (interrupted ||
            timestamp_us >=
                SaturatingAdd(
                    pressed_at,
                    MillisecondsToMicros(
                        definition->hold_term_ms))) {
            const auto& hold =
                definition->hold_action.kind ==
                        ActionKind::Transparent
                    ? definition->tap_action
                    : definition->hold_action;
            ActivateResolvedActionHold(
                hold,
                source,
                timestamp_us,
                binding,
                result);
        } else {
            binding.kind =
                BindingKind::TapDanceFirstDown;
            binding.reference_id = action.reference_id;
            binding.pressed_at_us = pressed_at;
        }
        return;
    }

    if (action.kind == ActionKind::PassThrough ||
        action.kind == ActionKind::Transparent) {
        ActivateResolvedActionHold(
            Action::Key(source),
            source,
            timestamp_us,
            binding,
            result);
    } else {
        ActivateResolvedActionHold(
            action,
            source,
            timestamp_us,
            binding,
            result);
    }
}

std::optional<PhysicalKey> MappingEngine::ActionIdentity(
    const Action& action,
    const PhysicalKey source) const noexcept {
    switch (action.kind) {
    case ActionKind::Transparent:
    case ActionKind::PassThrough:
        return source.IsValid()
                   ? std::optional<PhysicalKey>(source)
                   : std::nullopt;
    case ActionKind::Key:
    case ActionKind::LayerTap:
    case ActionKind::ModTap:
        return action.target_key.IsValid()
                   ? std::optional<PhysicalKey>(action.target_key)
                   : std::nullopt;
    case ActionKind::TapDance:
        if (const auto* definition =
                profile_.FindTapDance(action.reference_id);
            definition != nullptr) {
            return ActionIdentity(
                definition->tap_action,
                source);
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

const ComboDefinition* MappingEngine::BestCompleteCombo(
    std::array<std::size_t, 4>& sources,
    std::uint8_t& source_count) const noexcept {
    const ComboDefinition* best = nullptr;
    std::array<std::size_t, 4> best_sources{};

    for (const auto& combo : profile_.combos()) {
        if ((combo.layer_mask & active_layers()) == 0 ||
            combo.member_count < 2 ||
            combo.member_count > combo.members.size()) {
            continue;
        }

        std::array<std::size_t, 4> matched{};
        std::uint64_t earliest =
            std::numeric_limits<std::uint64_t>::max();
        std::uint64_t latest = 0;
        bool complete = true;

        for (std::size_t member = 0;
             member < combo.member_count;
             ++member) {
            bool found = false;
            for (std::size_t source = 0;
                 source < bindings_.size();
                 ++source) {
                const auto& binding = bindings_[source];
                if (binding.kind !=
                        BindingKind::ComboPending ||
                    binding.identity_key !=
                        combo.members[member]) {
                    continue;
                }

                bool already_used = false;
                for (std::size_t prior = 0;
                     prior < member;
                     ++prior) {
                    if (matched[prior] == source) {
                        already_used = true;
                        break;
                    }
                }
                if (already_used) {
                    continue;
                }

                matched[member] = source;
                earliest =
                    std::min(
                        earliest,
                        binding.pressed_at_us);
                latest =
                    std::max(
                        latest,
                        binding.pressed_at_us);
                found = true;
                break;
            }
            if (!found) {
                complete = false;
                break;
            }
        }

        if (!complete ||
            latest - earliest >
                static_cast<std::uint64_t>(
                    combo.term_ms) *
                    1'000ULL) {
            continue;
        }

        if (best == nullptr ||
            combo.member_count > best->member_count) {
            best = &combo;
            best_sources = matched;
        }
    }

    if (best == nullptr) {
        source_count = 0;
        return nullptr;
    }
    sources = best_sources;
    source_count = best->member_count;
    return best;
}

bool MappingEngine::HasPotentialLargerCombo(
    const ComboDefinition& complete) const noexcept {
    for (const auto& candidate : profile_.combos()) {
        if (candidate.member_count <=
                complete.member_count ||
            candidate.member_count > candidate.members.size() ||
            (candidate.layer_mask & active_layers()) == 0) {
            continue;
        }

        bool subset = true;
        for (std::size_t member = 0;
             member < complete.member_count;
             ++member) {
            bool found = false;
            for (std::size_t candidate_member = 0;
                 candidate_member <
                     candidate.member_count;
                 ++candidate_member) {
                if (complete.members[member] ==
                    candidate.members[candidate_member]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                subset = false;
                break;
            }
        }
        if (subset) {
            return true;
        }
    }
    return false;
}

void MappingEngine::ActivateCombo(
    const ComboDefinition& combo,
    const std::array<std::size_t, 4>& sources,
    const std::uint8_t source_count,
    const std::uint64_t timestamp_us,
    ProcessResult& result) noexcept {
    std::size_t slot = active_combos_.size();
    for (std::size_t index = 0;
         index < active_combos_.size();
         ++index) {
        if (!active_combos_[index].active) {
            slot = index;
            break;
        }
    }
    if (slot >= active_combos_.size()) {
        return;
    }

    auto& runtime = active_combos_[slot];
    runtime = {};
    runtime.active = true;
    runtime.definition_id = combo.id;
    runtime.sources = sources;
    runtime.source_count = source_count;
    const auto source =
        combo.output_action.kind == ActionKind::Key
            ? combo.output_action.target_key
            : PhysicalKey{};
    ActivateResolvedActionHold(
        combo.output_action,
        source,
        timestamp_us,
        runtime.output_binding,
        result);

    for (std::size_t index = 0;
         index < source_count;
         ++index) {
        auto& binding = bindings_[sources[index]];
        binding = {};
        binding.kind = BindingKind::ComboMember;
        binding.reference_id =
            static_cast<std::uint16_t>(slot + 1);
    }
}

void MappingEngine::ReleaseCombo(
    const std::size_t combo_index,
    ProcessResult& result) noexcept {
    if (combo_index >= active_combos_.size() ||
        !active_combos_[combo_index].active) {
        return;
    }

    auto& runtime = active_combos_[combo_index];
    ReleaseHeldBinding(runtime.output_binding, result);
    for (std::size_t index = 0;
         index < runtime.source_count;
         ++index) {
        auto& binding = bindings_[runtime.sources[index]];
        if (binding.kind == BindingKind::ComboMember &&
            binding.reference_id ==
                combo_index + 1) {
            binding = {};
            binding.kind = BindingKind::Block;
        }
    }
    runtime = {};
}

void MappingEngine::ReleaseHeldBinding(
    ActiveBinding& binding,
    ProcessResult& result) noexcept {
    const auto suppressed_modifiers =
        binding.suppressed_modifiers;
    switch (binding.kind) {
    case BindingKind::Key:
        ReleaseSyntheticKey(binding.target_key, result);
        break;
    case BindingKind::MomentaryLayer:
        if (binding.target_layer <
            momentary_layer_holds_.size()) {
            auto& count =
                momentary_layer_holds_[binding.target_layer];
            if (count > 0) {
                --count;
            }
        }
        break;
    case BindingKind::VirtualKey:
        ReleaseVirtualKey(binding.virtual_key, result);
        break;
    case BindingKind::MouseButton:
        ReleaseMouseButton(binding.mouse_button, result);
        break;
    case BindingKind::Shortcut:
        ReleaseSyntheticKey(binding.target_key, result);
        ReleaseShortcutModifiers(
            binding.shortcut_modifiers,
            result);
        break;
    default:
        break;
    }
    if (suppressed_modifiers != 0) {
        RestoreModifiers(
            suppressed_modifiers,
            result);
    }
    binding = {};
}

const KeyOverrideDefinition*
MappingEngine::ResolveKeyOverride(
    const Action& action,
    const PhysicalKey source) const noexcept {
    if (action.kind == ActionKind::LayerTap ||
        action.kind == ActionKind::ModTap ||
        action.kind == ActionKind::TapDance) {
        return nullptr;
    }

    const auto identity = ActionIdentity(action, source);
    if (!identity.has_value()) {
        return nullptr;
    }

    const auto modifiers = CurrentModifierMask();
    const KeyOverrideDefinition* best = nullptr;
    unsigned int best_score = 0;
    const auto group_matches =
        [modifiers](
            const std::uint16_t rule_mask,
            const bool forbidden) {
            constexpr std::array<std::uint16_t, 4> groups{
                ModifierAnyControl,
                ModifierAnyShift,
                ModifierAnyAlt,
                ModifierAnyWin};
            for (const auto group : groups) {
                const auto requested =
                    static_cast<std::uint16_t>(
                        rule_mask & group);
                if (requested == 0) {
                    continue;
                }
                const auto present =
                    static_cast<std::uint16_t>(
                        modifiers & group);
                const bool matches =
                    requested == group
                        ? present != 0
                        : (present & requested) == requested;
                if ((!forbidden && !matches) ||
                    (forbidden && matches)) {
                    return false;
                }
            }
            return true;
        };

    for (const auto& rule : profile_.overrides()) {
        if (rule.trigger_key != *identity ||
            (rule.layer_mask & active_layers()) == 0 ||
            !group_matches(
                rule.required_modifiers,
                false) ||
            !group_matches(
                rule.forbidden_modifiers,
                true)) {
            continue;
        }

        if (rule.exact_match &&
            (modifiers &
             static_cast<std::uint16_t>(
                 ~rule.required_modifiers) &
             0xFFU) != 0) {
            continue;
        }

        const auto layer_specific =
            rule.layer_mask != 0xFFFFFFFFU ? 1U : 0U;
        const auto score =
            layer_specific * 1'000U +
            (rule.exact_match ? 500U : 0U) +
            std::popcount(
                static_cast<unsigned int>(
                    rule.required_modifiers)) *
                10U +
            std::popcount(
                static_cast<unsigned int>(
                    rule.forbidden_modifiers));
        if (best == nullptr || score > best_score) {
            best = &rule;
            best_score = score;
        }
    }
    return best;
}

std::uint16_t MappingEngine::CurrentModifierMask()
    const noexcept {
    std::uint16_t mask = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        const auto key = ModifierKey(index);
        const auto& source_binding =
            bindings_[ToKeyIndex(key)];
        const bool pass_through =
            source_binding.kind ==
            BindingKind::PassThrough;
        const bool synthetic =
            synthetic_key_holds_[ToKeyIndex(key)] != 0;
        if (pass_through || synthetic) {
            mask |= static_cast<std::uint16_t>(1U << index);
        }
    }
    return mask;
}

std::uint16_t MappingEngine::SuppressModifiers(
    const std::uint16_t mask,
    ProcessResult& result) noexcept {
    const auto active = CurrentModifierMask();
    const auto actual =
        static_cast<std::uint16_t>(mask & active & 0xFFU);

    for (std::size_t index = 0; index < 8; ++index) {
        const auto bit =
            static_cast<std::uint16_t>(1U << index);
        if ((actual & bit) == 0) {
            continue;
        }
        auto& count = modifier_suppression_counts_[index];
        if (count == 0) {
            result.AddSynthetic(
                ModifierKey(index),
                KeyTransition::Release);
        }
        if (count !=
            std::numeric_limits<std::uint16_t>::max()) {
            ++count;
        }
    }
    return actual;
}

void MappingEngine::RestoreModifiers(
    const std::uint16_t mask,
    ProcessResult& result) noexcept {
    for (std::size_t index = 0; index < 8; ++index) {
        const auto bit =
            static_cast<std::uint16_t>(1U << index);
        if ((mask & bit) == 0) {
            continue;
        }
        auto& count = modifier_suppression_counts_[index];
        if (count == 0) {
            continue;
        }
        --count;
        if (count == 0 &&
            (CurrentModifierMask() & bit) != 0) {
            result.AddSynthetic(
                ModifierKey(index),
                KeyTransition::Press);
        }
    }
}

std::optional<std::size_t> MappingEngine::ModifierIndex(
    const PhysicalKey key) noexcept {
    for (std::size_t index = 0; index < 8; ++index) {
        if (ModifierKey(index) == key) {
            return index;
        }
    }
    return std::nullopt;
}

PhysicalKey MappingEngine::ModifierKey(
    const std::size_t index) noexcept {
    switch (index) {
    case 0:
        return kLeftControl;
    case 1:
        return kLeftShift;
    case 2:
        return kLeftAlt;
    case 3:
        return kLeftWin;
    case 4:
        return kRightControl;
    case 5:
        return kRightShift;
    case 6:
        return kRightAlt;
    case 7:
        return kRightWin;
    default:
        return {};
    }
}

bool MappingEngine::ReleaseDropsModifier(
    const KeyEvent& event,
    const ActiveBinding& binding) const noexcept {
    if (binding.kind == BindingKind::PassThrough) {
        return IsModifierKey(event.key);
    }
    if (binding.kind != BindingKind::Key ||
        !IsModifierKey(binding.target_key)) {
        return false;
    }
    return synthetic_key_holds_[
               ToKeyIndex(binding.target_key)] <= 1;
}

bool MappingEngine::IsModifierKey(
    const PhysicalKey key) noexcept {
    return key == kLeftControl ||
           key == kRightControl ||
           key == kLeftShift ||
           key == kRightShift ||
           key == kLeftAlt ||
           key == kRightAlt ||
           key == kLeftWin ||
           key == kRightWin;
}

void MappingEngine::ResetRuntimeState() noexcept {
    bindings_.fill({});
    synthetic_key_holds_.fill(0);
    momentary_layer_holds_.fill(0);
    last_tap_release_us_.fill(0);
    virtual_key_holds_.fill(0);
    mouse_button_holds_.fill(0);
    active_macros_.fill({});
    active_combos_.fill({});
    modifier_suppression_counts_.fill(0);
}

}  // namespace pckey
