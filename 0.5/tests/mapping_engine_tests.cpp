#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

#include "pckey/config_store.hpp"
#include "pckey/mapping_engine.hpp"

namespace {

using pckey::Action;
using pckey::ActionKind;
using pckey::Configuration;
using pckey::ConfigStore;
using pckey::KeyEvent;
using pckey::KeyPrefix;
using pckey::KeyTransition;
using pckey::KeyboardLayoutPreset;
using pckey::MappingEngine;
using pckey::ModifierAnyShift;
using pckey::ModifierLeftControl;
using pckey::ModifierLeftShift;
using pckey::MouseButton;
using pckey::PhysicalKey;
using pckey::Profile;
using pckey::ProcessResult;
using pckey::SyntheticEventKind;

constexpr PhysicalKey kA{0x1E, KeyPrefix::None};
constexpr PhysicalKey kB{0x30, KeyPrefix::None};
constexpr PhysicalKey kC{0x2E, KeyPrefix::None};
constexpr PhysicalKey kS{0x1F, KeyPrefix::None};
constexpr PhysicalKey kE{0x12, KeyPrefix::None};
constexpr PhysicalKey kSpace{0x39, KeyPrefix::None};
constexpr PhysicalKey kArrowUp{0x48, KeyPrefix::E0};
constexpr PhysicalKey kLeftControl{0x1D, KeyPrefix::None};
constexpr PhysicalKey kLeftShift{0x2A, KeyPrefix::None};
constexpr PhysicalKey kJ{0x24, KeyPrefix::None};
constexpr PhysicalKey kK{0x25, KeyPrefix::None};
constexpr PhysicalKey kBackspace{0x0E, KeyPrefix::None};
constexpr PhysicalKey kDelete{0x53, KeyPrefix::E0};
constexpr PhysicalKey kEscape{0x01, KeyPrefix::None};
constexpr std::uint16_t kVolumeUpVirtualKey = 0xAF;

int failures = 0;

void Check(
    const bool condition,
    const std::string_view message) {
    if (condition) {
        return;
    }

    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

KeyEvent Press(
    const PhysicalKey key,
    const std::uint64_t timestamp = 0) {
    return KeyEvent{key, KeyTransition::Press, timestamp};
}

KeyEvent Repeat(
    const PhysicalKey key,
    const std::uint64_t timestamp = 0) {
    return KeyEvent{key, KeyTransition::Repeat, timestamp};
}

KeyEvent Release(
    const PhysicalKey key,
    const std::uint64_t timestamp = 0) {
    return KeyEvent{key, KeyTransition::Release, timestamp};
}

void ProcessResultReportsOverflow() {
    ProcessResult result{};
    for (std::size_t index = 0;
         index < ProcessResult::kMaximumSyntheticEvents;
         ++index) {
        result.AddSynthetic(kA, KeyTransition::Press);
    }
    result.AddSynthetic(kA, KeyTransition::Release);
    Check(
        result.overflowed &&
            result.synthetic_count ==
                ProcessResult::kMaximumSyntheticEvents,
        "synthetic result overflow should be explicit");

    ProcessResult malformed{};
    malformed.synthetic_count =
        malformed.synthetic_events.size() + 1;
    result.Append(malformed);
    Check(
        result.overflowed,
        "appending a malformed result should remain bounded");
}

void NormalModePassesThrough() {
    MappingEngine engine;

    const auto down = engine.Process(Press(kA));
    const auto up = engine.Process(Release(kA));

    Check(!down.suppress_original, "normal press should pass through");
    Check(!up.suppress_original, "normal release should pass through");
    Check(down.synthetic_count == 0, "normal press should not inject");
    Check(up.synthetic_count == 0, "normal release should not inject");
}

void RemapKeepsPressRepeatReleaseBalanced() {
    Profile profile(L"Test", 1);
    profile.SetAction(0, kA, Action::Key(kB));
    MappingEngine engine(std::move(profile));

    const auto down = engine.Process(Press(kA));
    const auto repeat = engine.Process(Repeat(kA));
    const auto up = engine.Process(Release(kA));

    Check(down.suppress_original, "mapped press should be suppressed");
    Check(down.synthetic_count == 1, "mapped press should inject");
    Check(
        down.synthetic_events[0].key == kB &&
            down.synthetic_events[0].transition ==
                KeyTransition::Press,
        "mapped press should inject B down");

    Check(repeat.suppress_original, "mapped repeat should be suppressed");
    Check(
        repeat.synthetic_count == 1 &&
            repeat.synthetic_events[0].transition ==
                KeyTransition::Repeat,
        "mapped repeat should inject B repeat");

    Check(up.suppress_original, "mapped release should be suppressed");
    Check(
        up.synthetic_count == 1 &&
            up.synthetic_events[0].key == kB &&
            up.synthetic_events[0].transition ==
                KeyTransition::Release,
        "mapped release should inject B up");
}

void ShortcutPressesAndReleasesAChord() {
    Profile profile(L"Shortcut", 1);
    profile.SetAction(
        0,
        kA,
        Action::Shortcut(
            kC,
            static_cast<std::uint16_t>(
                ModifierLeftControl)));
    MappingEngine engine(std::move(profile));

    const auto down = engine.Process(Press(kA));
    const auto repeat = engine.Process(Repeat(kA));
    const auto up = engine.Process(Release(kA));

    Check(
        down.suppress_original &&
            down.synthetic_count == 2 &&
            down.synthetic_events[0].key == kLeftControl &&
            down.synthetic_events[0].transition ==
                KeyTransition::Press &&
            down.synthetic_events[1].key == kC &&
            down.synthetic_events[1].transition ==
                KeyTransition::Press,
        "shortcut should press modifiers before its target");
    Check(
        repeat.suppress_original &&
            repeat.synthetic_count == 0,
        "shortcut repeat should be swallowed");
    Check(
        up.suppress_original &&
            up.synthetic_count == 2 &&
            up.synthetic_events[0].key == kC &&
            up.synthetic_events[0].transition ==
                KeyTransition::Release &&
            up.synthetic_events[1].key == kLeftControl &&
            up.synthetic_events[1].transition ==
                KeyTransition::Release,
        "shortcut should release target before modifiers");
}

void ShortcutPreservesARealHeldModifier() {
    Profile profile(L"Shortcut held modifier", 1);
    profile.SetAction(
        0,
        kA,
        Action::Shortcut(
            kC,
            static_cast<std::uint16_t>(
                ModifierLeftControl)));
    MappingEngine engine(std::move(profile));

    const auto control_down =
        engine.Process(Press(kLeftControl));
    const auto shortcut_down = engine.Process(Press(kA));
    const auto shortcut_up = engine.Process(Release(kA));
    const auto control_up =
        engine.Process(Release(kLeftControl));

    Check(
        !control_down.suppress_original,
        "physical control should remain pass-through");
    Check(
        shortcut_down.synthetic_count == 1 &&
            shortcut_down.synthetic_events[0].key == kC,
        "shortcut should not synthesize an already-held modifier");
    Check(
        shortcut_up.synthetic_count == 1 &&
            shortcut_up.synthetic_events[0].key == kC &&
            shortcut_up.synthetic_events[0].transition ==
                KeyTransition::Release,
        "shortcut should not release a physical modifier");
    Check(
        !control_up.suppress_original,
        "physical control release should remain pass-through");
}

void ConcurrentShortcutsShareSyntheticModifiers() {
    Profile profile(L"Concurrent shortcuts", 1);
    profile.SetAction(
        0,
        kA,
        Action::Shortcut(
            kC,
            static_cast<std::uint16_t>(
                ModifierLeftControl)));
    profile.SetAction(
        0,
        kS,
        Action::Shortcut(
            kB,
            static_cast<std::uint16_t>(
                ModifierLeftControl)));
    MappingEngine engine(std::move(profile));

    const auto first_down = engine.Process(Press(kA));
    const auto second_down = engine.Process(Press(kS));
    const auto first_up = engine.Process(Release(kA));
    const auto second_up = engine.Process(Release(kS));

    Check(
        first_down.synthetic_count == 2 &&
            first_down.synthetic_events[0].key ==
                kLeftControl &&
            first_down.synthetic_events[1].key == kC,
        "first shortcut should establish the shared modifier");
    Check(
        second_down.synthetic_count == 1 &&
            second_down.synthetic_events[0].key == kB,
        "second shortcut should reuse the held modifier");
    Check(
        first_up.synthetic_count == 1 &&
            first_up.synthetic_events[0].key == kC &&
            first_up.synthetic_events[0].transition ==
                KeyTransition::Release,
        "releasing one shortcut should keep the shared modifier held");
    Check(
        second_up.synthetic_count == 2 &&
            second_up.synthetic_events[0].key == kB &&
            second_up.synthetic_events[0].transition ==
                KeyTransition::Release &&
            second_up.synthetic_events[1].key ==
                kLeftControl &&
            second_up.synthetic_events[1].transition ==
                KeyTransition::Release,
        "the final shortcut should release the shared modifier");
}

void MomentaryLayerResolvesBeforeSecondKey() {
    Profile profile(L"Layer test", 2);
    profile.SetAction(
        0,
        kSpace,
        Action::MomentaryLayer(1));
    profile.SetAction(1, kE, Action::Key(kArrowUp));
    MappingEngine engine(std::move(profile));

    const auto layer_down = engine.Process(Press(kSpace));
    const auto arrow_down = engine.Process(Press(kE));
    const auto layer_up = engine.Process(Release(kSpace));
    const auto arrow_up = engine.Process(Release(kE));

    Check(layer_down.suppress_original, "layer key should be suppressed");
    Check(
        (engine.active_layers() & 1U) != 0,
        "base layer should remain active");
    Check(
        arrow_down.synthetic_count == 1 &&
            arrow_down.synthetic_events[0].key == kArrowUp,
        "E should resolve to arrow up on layer 1");
    Check(layer_up.suppress_original, "layer release should be suppressed");
    Check(
        arrow_up.synthetic_count == 1 &&
            arrow_up.synthetic_events[0].key == kArrowUp &&
            arrow_up.synthetic_events[0].transition ==
                KeyTransition::Release,
        "E release should release its original resolved action");
}

void SharedTargetUsesReferenceCounting() {
    Profile profile(L"Reference counting", 1);
    profile.SetAction(0, kA, Action::Key(kB));
    profile.SetAction(0, kS, Action::Key(kB));
    MappingEngine engine(std::move(profile));

    const auto first_down = engine.Process(Press(kA));
    const auto second_down = engine.Process(Press(kS));
    const auto first_up = engine.Process(Release(kA));
    const auto second_up = engine.Process(Release(kS));

    Check(first_down.synthetic_count == 1, "first source should press target");
    Check(
        second_down.synthetic_count == 0,
        "second source should share held target");
    Check(
        first_up.synthetic_count == 0,
        "first release should keep shared target held");
    Check(
        second_up.synthetic_count == 1 &&
            second_up.synthetic_events[0].transition ==
                KeyTransition::Release,
        "last release should release shared target");
}

void ReleaseAllClearsSyntheticState() {
    Profile profile(L"Release all", 1);
    profile.SetAction(0, kA, Action::Key(kB));
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kA));
    const auto releases = engine.ReleaseAll();
    const auto late_release = engine.Process(Release(kA));

    Check(
        releases.size() == 1 &&
            releases[0].key == kB &&
            releases[0].transition == KeyTransition::Release,
        "ReleaseAll should release every synthetic key");
    Check(
        !late_release.suppress_original,
        "state should be empty after ReleaseAll");
}

void LayerTapTapsOnEarlyRelease() {
    Profile profile(L"Layer tap", 2);
    profile.SetAction(
        0,
        kSpace,
        Action::LayerTap(kSpace, 1, 500, 200));
    MappingEngine engine(std::move(profile));

    const auto down = engine.Process(Press(kSpace, 10'000));
    const auto up = engine.Process(Release(kSpace, 110'000));

    Check(down.suppress_original, "layer-tap press should be suppressed");
    Check(
        down.synthetic_count == 0,
        "layer-tap press should wait for a decision");
    Check(up.suppress_original, "layer-tap release should be suppressed");
    Check(
        up.synthetic_count == 2 &&
            up.synthetic_events[0].key == kSpace &&
            up.synthetic_events[0].transition ==
                KeyTransition::Press &&
            up.synthetic_events[1].key == kSpace &&
            up.synthetic_events[1].transition ==
                KeyTransition::Release,
        "early layer-tap release should emit a complete tap");
}

void LayerTapHoldActivatesAtDeadline() {
    Profile profile(L"Layer tap timer", 2);
    profile.SetAction(
        0,
        kSpace,
        Action::LayerTap(kSpace, 1, 500, 200));
    profile.SetAction(1, kE, Action::Key(kArrowUp));
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kSpace, 10'000));
    const auto early = engine.AdvanceTime(509'999);
    Check(
        early.synthetic_count == 0 &&
            (engine.active_layers() & (1U << 1)) == 0,
        "layer hold should remain pending before its deadline");
    const auto due = engine.AdvanceTime(510'000);
    const auto arrow = engine.Process(Press(kE, 520'000));

    Check(
        (engine.active_layers() & (1U << 1)) != 0,
        "deadline processing should activate the hold layer");
    Check(
        due.synthetic_count == 0,
        "layer hold activation should not inject a key");
    Check(
        arrow.synthetic_count == 1 &&
            arrow.synthetic_events[0].key == kArrowUp,
        "a timed layer hold should affect the next key");

    (void)engine.Process(Release(kE, 530'000));
    (void)engine.Process(Release(kSpace, 540'000));
    Check(
        (engine.active_layers() & (1U << 1)) == 0,
        "releasing layer-tap hold should release its layer");
}

void SecondKeyImmediatelyChoosesHold() {
    Profile profile(L"Layer tap chord", 2);
    profile.SetAction(
        0,
        kSpace,
        Action::LayerTap(kSpace, 1, 500, 200));
    profile.SetAction(1, kE, Action::Key(kArrowUp));
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kSpace, 10'000));
    const auto arrow = engine.Process(Press(kE, 20'000));

    Check(
        arrow.synthetic_count == 1 &&
            arrow.synthetic_events[0].key == kArrowUp,
        "a following key should immediately choose layer hold");
}

void PreheldModifierDoesNotForceHold() {
    Profile profile(L"Ctrl space", 2);
    profile.SetAction(
        0,
        kSpace,
        Action::LayerTap(kSpace, 1, 500, 200));
    MappingEngine engine(std::move(profile));

    const auto control_down =
        engine.Process(Press(kLeftControl, 10'000));
    const auto space_down =
        engine.Process(Press(kSpace, 20'000));
    const auto space_up =
        engine.Process(Release(kSpace, 80'000));

    Check(
        !control_down.suppress_original,
        "pre-held Ctrl should pass through");
    Check(
        space_down.synthetic_count == 0,
        "pre-held Ctrl should not force Space hold");
    Check(
        space_up.synthetic_count == 2 &&
            space_up.synthetic_events[0].key == kSpace,
        "Ctrl then Space tap should emit Space while Ctrl remains down");

    (void)engine.Process(Release(kLeftControl, 90'000));
}

void ModifierReleaseSettlesPendingTapFirst() {
    Profile profile(L"Ctrl release protection", 2);
    profile.SetAction(
        0,
        kSpace,
        Action::LayerTap(kSpace, 1, 500, 200));
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kLeftControl, 10'000));
    (void)engine.Process(Press(kSpace, 20'000));
    const auto control_up =
        engine.Process(Release(kLeftControl, 70'000));
    const auto space_up =
        engine.Process(Release(kSpace, 80'000));

    Check(
        !control_up.suppress_original &&
            control_up.synthetic_count == 2 &&
            control_up.synthetic_events[0].key == kSpace &&
            control_up.synthetic_events[1].key == kSpace,
        "modifier release should emit pending tap before passing key-up");
    Check(
        space_up.suppress_original &&
            space_up.synthetic_count == 0,
        "settled tap source release should only be swallowed");
}

void QuickTapKeepsTapKeyHeld() {
    Profile profile(L"Quick tap", 2);
    profile.SetAction(
        0,
        kSpace,
        Action::LayerTap(kSpace, 1, 500, 200));
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kSpace, 10'000));
    (void)engine.Process(Release(kSpace, 50'000));
    const auto second_down =
        engine.Process(Press(kSpace, 180'000));
    const auto repeated =
        engine.Process(Repeat(kSpace, 300'000));
    const auto second_up =
        engine.Process(Release(kSpace, 350'000));

    Check(
        second_down.synthetic_count == 1 &&
            second_down.synthetic_events[0].transition ==
                KeyTransition::Press,
        "quick second press should hold the tap key immediately");
    Check(
        repeated.synthetic_count == 1 &&
            repeated.synthetic_events[0].transition ==
                KeyTransition::Repeat,
        "quick tap hold should preserve key repeat");
    Check(
        second_up.synthetic_count == 1 &&
            second_up.synthetic_events[0].transition ==
                KeyTransition::Release,
        "quick tap release should release the tap key");
}

void MediaKeyUsesVirtualKeyInjection() {
    Profile profile(L"Media", 1);
    profile.SetAction(
        0,
        kA,
        Action::VirtualKey(kVolumeUpVirtualKey));
    MappingEngine engine(std::move(profile));

    const auto down = engine.Process(Press(kA, 10'000));
    const auto up = engine.Process(Release(kA, 20'000));

    Check(
        down.suppress_original &&
            down.synthetic_count == 1 &&
            down.synthetic_events[0].kind ==
                SyntheticEventKind::VirtualKey &&
            down.synthetic_events[0].virtual_key ==
                kVolumeUpVirtualKey &&
            down.synthetic_events[0].transition ==
                KeyTransition::Press,
        "media action should inject virtual-key down");
    Check(
        up.synthetic_count == 1 &&
            up.synthetic_events[0].kind ==
                SyntheticEventKind::VirtualKey &&
            up.synthetic_events[0].transition ==
                KeyTransition::Release,
        "media action should inject virtual-key up");
}

void MouseKeysRepeatWithoutKeyboardEvents() {
    Profile profile(L"Mouse", 1);
    profile.SetAction(0, kA, Action::MouseMove(1, 0));
    profile.SetAction(
        0,
        kB,
        Action::MouseButtonAction(MouseButton::Left));
    MappingEngine engine(std::move(profile));

    const auto move_down =
        engine.Process(Press(kA, 100'000));
    const auto repeated =
        engine.AdvanceTime(116'000);
    const auto move_up =
        engine.Process(Release(kA, 120'000));
    const auto button_down =
        engine.Process(Press(kB, 130'000));
    const auto button_up =
        engine.Process(Release(kB, 140'000));

    Check(
        move_down.synthetic_count == 1 &&
            move_down.synthetic_events[0].kind ==
                SyntheticEventKind::MouseMove &&
            move_down.synthetic_events[0].mouse_x > 0,
        "mouse movement should start immediately");
    Check(
        repeated.synthetic_count == 1 &&
            repeated.synthetic_events[0].kind ==
                SyntheticEventKind::MouseMove,
        "held mouse movement should repeat on the timer");
    Check(
        move_up.synthetic_count == 0 &&
            move_up.suppress_original,
        "releasing mouse movement should stop without key output");
    Check(
        button_down.synthetic_count == 1 &&
            button_down.synthetic_events[0].kind ==
                SyntheticEventKind::MouseButton &&
            button_down.synthetic_events[0].transition ==
                KeyTransition::Press,
        "mouse button action should press a mouse button");
    Check(
        button_up.synthetic_count == 1 &&
            button_up.synthetic_events[0].transition ==
                KeyTransition::Release,
        "mouse button release should be balanced");
}

void MouseRepeatSkipsMissedTicks() {
    Profile profile(L"Mouse catch-up", 1);
    profile.SetAction(0, kA, Action::MouseMove(1, 0));
    profile.mouse_settings().repeat_ms = 5;
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kA, 1'000));
    const auto delayed = engine.AdvanceTime(60'000'000'000ULL);

    Check(
        delayed.synthetic_count == 1 &&
            delayed.synthetic_events[0].kind ==
                SyntheticEventKind::MouseMove,
        "a delayed scheduler tick should emit one mouse repeat");

    const auto released = engine.Process(
        Release(kA, 60'000'000'001ULL));
    Check(
        released.suppress_original &&
            released.synthetic_count == 0,
        "mouse repeat catch-up should remain releasable");
}

void InvalidInMemoryMacroIsIgnoredSafely() {
    Profile profile(L"Invalid macro", 1);
    profile.macros().push_back(
        pckey::MacroDefinition{
            1,
            L"Malformed",
            {{0, PhysicalKey{}, KeyTransition::Press}}});
    profile.SetAction(0, kA, Action::Macro(1));
    MappingEngine engine(std::move(profile));

    const auto trigger = engine.Process(Press(kA, 10'000));
    Check(
        trigger.suppress_original &&
            trigger.synthetic_count == 0,
        "invalid in-memory macro events should not index runtime state");
}

void InvalidInMemoryComboIsIgnoredSafely() {
    Profile profile(L"Invalid combo", 1);
    profile.combos().push_back(
        pckey::ComboDefinition{
            1,
            L"Too many members",
            {kA, kB, kC, kSpace},
            5,
            Action::Key(kDelete),
            50,
            0xFFFFFFFFU});
    profile.SetAction(0, kA, Action::Key(kB));
    MappingEngine engine(std::move(profile));

    const auto down = engine.Process(Press(kA, 10'000));
    Check(
        down.suppress_original &&
            down.synthetic_count == 1 &&
            down.synthetic_events[0].key == kB,
        "invalid in-memory combo definitions should be ignored");
    (void)engine.Process(Release(kA, 20'000));
}

void MacroRunsOnTheSharedScheduler() {
    Profile profile(L"Macro", 1);
    profile.macros().push_back(
        pckey::MacroDefinition{
            1,
            L"Copy",
            {
                {0, kLeftControl, KeyTransition::Press},
                {10, kC, KeyTransition::Press},
                {10, kC, KeyTransition::Release},
                {0, kLeftControl, KeyTransition::Release},
            }});
    profile.SetAction(0, kA, Action::Macro(1));
    MappingEngine engine(std::move(profile));

    const auto start = engine.Process(Press(kA, 100'000));
    const auto second = engine.AdvanceTime(110'000);
    const auto finish = engine.AdvanceTime(120'000);
    const auto trigger_up =
        engine.Process(Release(kA, 130'000));

    Check(
        start.synthetic_count == 1 &&
            start.synthetic_events[0].key == kLeftControl &&
            start.synthetic_events[0].transition ==
                KeyTransition::Press,
        "macro should execute its zero-delay first event");
    Check(
        second.synthetic_count == 1 &&
            second.synthetic_events[0].key == kC &&
            second.synthetic_events[0].transition ==
                KeyTransition::Press,
        "macro scheduler should execute delayed key-down");
    Check(
        finish.synthetic_count == 2 &&
            finish.synthetic_events[0].key == kC &&
            finish.synthetic_events[0].transition ==
                KeyTransition::Release &&
            finish.synthetic_events[1].key == kLeftControl &&
            finish.synthetic_events[1].transition ==
                KeyTransition::Release,
        "macro completion should balance held keys");
    Check(
        trigger_up.suppress_original,
        "macro trigger release should remain swallowed");
}

void TapDanceSupportsTapDoubleAndHold() {
    Profile profile(L"Tap Dance", 1);
    profile.tap_dances().push_back(
        pckey::TapDanceDefinition{
            1,
            L"Test",
            Action::Key(kA),
            Action::Key(kLeftControl),
            Action::Key(kB),
            Action::Key(kLeftShift),
            200,
            200,
            200});
    profile.SetAction(0, kSpace, Action::TapDance(1));
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kSpace, 10'000));
    const auto first_release =
        engine.Process(Release(kSpace, 50'000));
    const auto single =
        engine.AdvanceTime(250'000);
    Check(
        first_release.synthetic_count == 0,
        "tap dance should wait for a possible second tap");
    Check(
        single.synthetic_count == 2 &&
            single.synthetic_events[0].key == kA &&
            single.synthetic_events[1].key == kA,
        "tap dance timeout should emit the single-tap action");

    (void)engine.Process(Press(kSpace, 500'000));
    (void)engine.Process(Release(kSpace, 540'000));
    (void)engine.Process(Press(kSpace, 600'000));
    const auto double_tap =
        engine.Process(Release(kSpace, 640'000));
    Check(
        double_tap.synthetic_count == 2 &&
            double_tap.synthetic_events[0].key == kB &&
            double_tap.synthetic_events[1].key == kB,
        "second release should emit the double-tap action");

    (void)engine.Process(Press(kSpace, 1'000'000));
    const auto hold =
        engine.AdvanceTime(1'200'000);
    const auto hold_up =
        engine.Process(Release(kSpace, 1'250'000));
    Check(
        hold.synthetic_count == 1 &&
            hold.synthetic_events[0].key == kLeftControl &&
            hold.synthetic_events[0].transition ==
                KeyTransition::Press,
        "tap dance hold should activate at its deadline");
    Check(
        hold_up.synthetic_count == 1 &&
            hold_up.synthetic_events[0].key == kLeftControl &&
            hold_up.synthetic_events[0].transition ==
                KeyTransition::Release,
        "tap dance hold should release with the source");
}

void TapDanceMomentaryLayerWorks() {
    Profile profile(L"Tap Dance Layer", 2);
    profile.tap_dances().push_back(
        pckey::TapDanceDefinition{
            1,
            L"Space Layer",
            Action::Key(kSpace),
            Action::MomentaryLayer(1),
            Action::Transparent(),
            Action::Transparent(),
            200,
            200,
            0});
    profile.SetAction(
        0,
        kSpace,
        Action::TapDance(1));
    profile.SetAction(
        1,
        kE,
        Action::Key(kArrowUp));
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kSpace, 100'000));
    const auto timer =
        engine.AdvanceTime(300'000);
    const auto arrow =
        engine.Process(Press(kE, 310'000));

    Check(
        timer.synthetic_count == 0 &&
            (engine.active_layers() & (1U << 1)) != 0,
        "Tap Dance hold should activate a momentary layer");
    Check(
        arrow.synthetic_count == 1 &&
            arrow.synthetic_events[0].key == kArrowUp,
        "Tap Dance momentary layer should affect following keys");

    (void)engine.Process(Release(kE, 320'000));
    (void)engine.Process(Release(kSpace, 330'000));
    Check(
        (engine.active_layers() & (1U << 1)) == 0,
        "releasing Tap Dance source should release its layer");

    (void)engine.Process(Press(kSpace, 500'000));
    const auto interrupted =
        engine.Process(Press(kE, 550'000));
    Check(
        interrupted.synthetic_count == 1 &&
            interrupted.synthetic_events[0].key == kArrowUp &&
            (engine.active_layers() & (1U << 1)) != 0,
        "a following key should immediately choose Tap Dance layer hold");

    (void)engine.Process(Release(kE, 560'000));
    (void)engine.Process(Release(kSpace, 570'000));

    (void)engine.Process(Press(kSpace, 700'000));
    const auto tap =
        engine.Process(Release(kSpace, 750'000));
    Check(
        tap.synthetic_count == 2 &&
            tap.synthetic_events[0].key == kSpace &&
            tap.synthetic_events[0].transition ==
                KeyTransition::Press &&
            tap.synthetic_events[1].key == kSpace &&
            tap.synthetic_events[1].transition ==
                KeyTransition::Release,
        "short Tap Dance press should retain its tap key");
}

void ComboUsesMappedKeyIdentity() {
    Profile profile(L"Combo", 1);
    profile.SetAction(0, kJ, Action::Key(kA));
    profile.SetAction(0, kK, Action::Key(kB));
    profile.combos().push_back(
        pckey::ComboDefinition{
            1,
            L"AB Escape",
            {kA, kB, {}, {}},
            2,
            Action::Key(kEscape),
            50,
            0xFFFFFFFFU});
    MappingEngine engine(std::move(profile));

    const auto first =
        engine.Process(Press(kJ, 100'000));
    const auto second =
        engine.Process(Press(kK, 120'000));
    const auto release =
        engine.Process(Release(kJ, 130'000));
    const auto final_release =
        engine.Process(Release(kK, 140'000));

    Check(
        first.suppress_original &&
            first.synthetic_count == 0,
        "first combo member should wait inside the combo term");
    Check(
        second.synthetic_count == 1 &&
            second.synthetic_events[0].key == kEscape &&
            second.synthetic_events[0].transition ==
                KeyTransition::Press,
        "mapped A+B identities should trigger combo output");
    Check(
        release.synthetic_count == 1 &&
            release.synthetic_events[0].key == kEscape &&
            release.synthetic_events[0].transition ==
                KeyTransition::Release,
        "releasing any combo member should release combo output");
    Check(
        final_release.suppress_original &&
            final_release.synthetic_count == 0,
        "remaining combo member release should stay swallowed");
}

void ComboFailureRestoresNormalAction() {
    Profile profile(L"Combo failure", 1);
    profile.SetAction(0, kJ, Action::Key(kA));
    profile.combos().push_back(
        pckey::ComboDefinition{
            1,
            L"AB",
            {kA, kB, {}, {}},
            2,
            Action::Key(kEscape),
            50,
            0xFFFFFFFFU});
    MappingEngine engine(std::move(profile));

    const auto pending =
        engine.Process(Press(kJ, 100'000));
    const auto timeout =
        engine.AdvanceTime(150'000);
    const auto released =
        engine.Process(Release(kJ, 160'000));

    Check(
        pending.synthetic_count == 0,
        "combo member should initially be deferred");
    Check(
        timeout.synthetic_count == 1 &&
            timeout.synthetic_events[0].key == kA &&
            timeout.synthetic_events[0].transition ==
                KeyTransition::Press,
        "failed combo should restore held mapped action");
    Check(
        released.synthetic_count == 1 &&
            released.synthetic_events[0].key == kA &&
            released.synthetic_events[0].transition ==
                KeyTransition::Release,
        "restored combo member should release normally");
}

void LongerOverlappingComboWins() {
    Profile profile(L"Combo overlap", 1);
    profile.combos().push_back(
        pckey::ComboDefinition{
            1,
            L"AB",
            {kA, kB, {}, {}},
            2,
            Action::Key(kEscape),
            50,
            0xFFFFFFFFU});
    profile.combos().push_back(
        pckey::ComboDefinition{
            2,
            L"ABC",
            {kA, kB, kC, {}},
            3,
            Action::Key(kDelete),
            50,
            0xFFFFFFFFU});
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kA, 100'000));
    const auto second =
        engine.Process(Press(kB, 115'000));
    const auto third =
        engine.Process(Press(kC, 125'000));

    Check(
        second.synthetic_count == 0,
        "shorter overlapping combo should wait for disambiguation");
    Check(
        third.synthetic_count == 1 &&
            third.synthetic_events[0].key == kDelete,
        "three-key combo should beat its two-key subset");

    (void)engine.Process(Release(kA, 130'000));
    (void)engine.Process(Release(kB, 140'000));
    (void)engine.Process(Release(kC, 150'000));
}

void KeyOverrideSuppressesAndRestoresModifiers() {
    Profile profile(L"Override", 1);
    profile.overrides().push_back(
        pckey::KeyOverrideDefinition{
            1,
            L"Shift Backspace",
            kBackspace,
            ModifierAnyShift,
            0,
            ModifierAnyShift,
            false,
            Action::Key(kDelete),
            0xFFFFFFFFU});
    MappingEngine engine(std::move(profile));

    const auto shift_down =
        engine.Process(Press(kLeftShift, 10'000));
    const auto delete_down =
        engine.Process(Press(kBackspace, 20'000));
    const auto delete_up =
        engine.Process(Release(kBackspace, 30'000));
    (void)engine.Process(Release(kLeftShift, 40'000));

    Check(
        !shift_down.suppress_original,
        "required modifier should initially pass through");
    Check(
        delete_down.suppress_original &&
            delete_down.synthetic_count == 2 &&
            delete_down.synthetic_events[0].key == kLeftShift &&
            delete_down.synthetic_events[0].transition ==
                KeyTransition::Release &&
            delete_down.synthetic_events[1].key == kDelete &&
            delete_down.synthetic_events[1].transition ==
                KeyTransition::Press,
        "override should suppress Shift before replacement key-down");
    Check(
        delete_up.synthetic_count == 2 &&
            delete_up.synthetic_events[0].key == kDelete &&
            delete_up.synthetic_events[0].transition ==
                KeyTransition::Release &&
            delete_up.synthetic_events[1].key == kLeftShift &&
            delete_up.synthetic_events[1].transition ==
                KeyTransition::Press,
        "override release should restore a still-held modifier");
}

void DecisionActionsFeedKeyOverrides() {
    {
        Profile profile(L"Layer-Tap Override", 2);
        profile.SetAction(
            0,
            kSpace,
            Action::LayerTap(
                kBackspace,
                1,
                500,
                0));
        profile.overrides().push_back(
            pckey::KeyOverrideDefinition{
                1,
                L"Shift Backspace",
                kBackspace,
                ModifierAnyShift,
                0,
                ModifierAnyShift,
                false,
                Action::Key(kDelete),
                0xFFFFFFFFU});
        MappingEngine engine(std::move(profile));

        (void)engine.Process(
            Press(kLeftShift, 10'000));
        (void)engine.Process(Press(kSpace, 20'000));
        const auto tap =
            engine.Process(Release(kSpace, 60'000));

        Check(
            tap.synthetic_count == 4 &&
                tap.synthetic_events[0].key ==
                    kLeftShift &&
                tap.synthetic_events[0].transition ==
                    KeyTransition::Release &&
                tap.synthetic_events[1].key == kDelete &&
                tap.synthetic_events[1].transition ==
                    KeyTransition::Press &&
                tap.synthetic_events[2].key == kDelete &&
                tap.synthetic_events[2].transition ==
                    KeyTransition::Release &&
                tap.synthetic_events[3].key ==
                    kLeftShift &&
                tap.synthetic_events[3].transition ==
                    KeyTransition::Press,
            "Layer-Tap tap output should feed Key Override");

        (void)engine.Process(
            Release(kLeftShift, 70'000));
    }

    {
        Profile profile(L"Tap Dance Override", 1);
        profile.tap_dances().push_back(
            pckey::TapDanceDefinition{
                1,
                L"Backspace",
                Action::Key(kBackspace),
                Action::Transparent(),
                Action::Transparent(),
                Action::Transparent(),
                200,
                200,
                0});
        profile.SetAction(
            0,
            kSpace,
            Action::TapDance(1));
        profile.overrides().push_back(
            pckey::KeyOverrideDefinition{
                1,
                L"Shift Backspace",
                kBackspace,
                ModifierAnyShift,
                0,
                ModifierAnyShift,
                false,
                Action::Key(kDelete),
                0xFFFFFFFFU});
        MappingEngine engine(std::move(profile));

        (void)engine.Process(
            Press(kLeftShift, 100'000));
        (void)engine.Process(
            Press(kSpace, 110'000));
        const auto tap =
            engine.Process(
                Release(kSpace, 140'000));

        Check(
            tap.synthetic_count == 4 &&
                tap.synthetic_events[0].key ==
                    kLeftShift &&
                tap.synthetic_events[1].key == kDelete &&
                tap.synthetic_events[2].key == kDelete &&
                tap.synthetic_events[3].key ==
                    kLeftShift,
            "Tap Dance output should feed Key Override");

        (void)engine.Process(
            Release(kLeftShift, 150'000));
    }

    {
        Profile profile(L"Combo Override", 1);
        profile.combos().push_back(
            pckey::ComboDefinition{
                1,
                L"AB Backspace",
                {kA, kB, {}, {}},
                2,
                Action::Key(kBackspace),
                50,
                0xFFFFFFFFU});
        profile.overrides().push_back(
            pckey::KeyOverrideDefinition{
                1,
                L"Shift Backspace",
                kBackspace,
                ModifierAnyShift,
                0,
                ModifierAnyShift,
                false,
                Action::Key(kDelete),
                0xFFFFFFFFU});
        MappingEngine engine(std::move(profile));

        (void)engine.Process(
            Press(kLeftShift, 200'000));
        (void)engine.Process(Press(kA, 210'000));
        const auto combo =
            engine.Process(Press(kB, 220'000));
        const auto release =
            engine.Process(Release(kA, 230'000));

        Check(
            combo.synthetic_count == 2 &&
                combo.synthetic_events[0].key ==
                    kLeftShift &&
                combo.synthetic_events[0].transition ==
                    KeyTransition::Release &&
                combo.synthetic_events[1].key == kDelete &&
                combo.synthetic_events[1].transition ==
                    KeyTransition::Press,
            "Combo output should feed Key Override");
        Check(
            release.synthetic_count == 2 &&
                release.synthetic_events[0].key == kDelete &&
                release.synthetic_events[0].transition ==
                    KeyTransition::Release &&
                release.synthetic_events[1].key ==
                    kLeftShift &&
                release.synthetic_events[1].transition ==
                    KeyTransition::Press,
            "Combo override release should restore modifiers");

        (void)engine.Process(Release(kB, 240'000));
        (void)engine.Process(
            Release(kLeftShift, 250'000));
    }
}

void ReleaseAllRestoresSuppressedPhysicalModifiers() {
    Profile profile(L"Override cleanup", 1);
    profile.overrides().push_back(
        pckey::KeyOverrideDefinition{
            1,
            L"Shift Backspace",
            kBackspace,
            ModifierAnyShift,
            0,
            ModifierAnyShift,
            false,
            Action::Key(kDelete),
            0xFFFFFFFFU});
    MappingEngine engine(std::move(profile));

    (void)engine.Process(Press(kLeftShift, 10'000));
    (void)engine.Process(Press(kBackspace, 20'000));
    const auto releases = engine.ReleaseAll();

    bool released_replacement = false;
    bool restored_shift = false;
    for (const auto& event : releases) {
        released_replacement =
            released_replacement ||
            (event.key == kDelete &&
             event.transition == KeyTransition::Release);
        restored_shift =
            restored_shift ||
            (event.key == kLeftShift &&
             event.transition == KeyTransition::Press);
    }

    Check(
        released_replacement,
        "ReleaseAll should release an active override replacement");
    Check(
        restored_shift,
        "ReleaseAll should restore a suppressed physical modifier");
}

void ConfigurationRoundTrips() {
    Configuration configuration;
    configuration.active_profile = L"办公";
    configuration.profiles.emplace_back(L"办公", 4);

    auto& profile = configuration.profiles.back();
    profile.SetAction(0, kA, Action::Key(kB));
    profile.SetAction(
        0,
        kSpace,
        Action::MomentaryLayer(1));
    profile.SetAction(1, kE, Action::Key(kArrowUp));
    profile.SetLayout(KeyboardLayoutPreset::Compact65);
    profile.SetAction(
        0,
        kS,
        Action::ModTap(
            kS,
            kLeftControl,
            470,
            180));
    profile.macros().push_back(
        pckey::MacroDefinition{
            1,
            L"测试宏",
            {
                {0, kA, KeyTransition::Press},
                {20, kA, KeyTransition::Release},
            }});
    profile.tap_dances().push_back(
        pckey::TapDanceDefinition{
            1,
            L"测试TD",
            Action::Key(kA),
            Action::Key(kLeftControl),
            Action::Key(kB),
            Action::Transparent(),
            200,
            200,
            200});
    profile.combos().push_back(
        pckey::ComboDefinition{
            1,
            L"测试Combo",
            {kA, kB, {}, {}},
            2,
            Action::Key(kEscape),
            50,
            0xFFFFFFFFU});
    profile.overrides().push_back(
        pckey::KeyOverrideDefinition{
            1,
            L"测试Override",
            kBackspace,
            ModifierAnyShift,
            0,
            ModifierAnyShift,
            false,
            Action::Key(kDelete),
            0xFFFFFFFFU});
    profile.SetAction(0, kJ, Action::Macro(1));
    profile.SetAction(0, kK, Action::TapDance(1));
    profile.SetAction(
        0,
        kB,
        Action::VirtualKey(kVolumeUpVirtualKey));
    profile.SetAction(
        0,
        kC,
        Action::Shortcut(
            kS,
            static_cast<std::uint16_t>(
                ModifierLeftControl |
                ModifierLeftShift)));
    profile.mouse_settings().maximum_speed = 24;

    const auto path =
        std::filesystem::temp_directory_path() /
        L"pckey-config-roundtrip-test.pckey";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);

    std::wstring error;
    Check(
        ConfigStore::SaveAtomic(
            path,
            configuration,
            error),
        "configuration should save");

    Configuration loaded;
    Check(
        ConfigStore::Load(path, loaded, error),
        "configuration should load");
    Check(
        loaded.active_profile == L"办公",
        "active profile should round-trip");
    Check(
        loaded.profiles.size() == 1,
        "profile count should round-trip");

    const auto* loaded_profile =
        loaded.FindProfile(L"办公");
    Check(
        loaded_profile != nullptr,
        "named profile should round-trip");

    if (loaded_profile != nullptr) {
        const auto mapped =
            loaded_profile->GetAction(0, kA);
        const auto layer_key =
            loaded_profile->GetAction(0, kSpace);
        const auto arrow =
            loaded_profile->GetAction(1, kE);
        const auto mod_tap =
            loaded_profile->GetAction(0, kS);
        const auto macro_action =
            loaded_profile->GetAction(0, kJ);
        const auto tap_dance_action =
            loaded_profile->GetAction(0, kK);
        const auto shortcut =
            loaded_profile->GetAction(0, kC);

        Check(
            mapped.kind == ActionKind::Key &&
                mapped.target_key == kB,
            "basic remap should round-trip");
        Check(
            layer_key.kind ==
                    ActionKind::MomentaryLayer &&
                layer_key.target_layer == 1,
            "momentary layer should round-trip");
        Check(
            arrow.kind == ActionKind::Key &&
                arrow.target_key == kArrowUp,
            "layer mapping should round-trip");
        Check(
            loaded_profile->layout() ==
                KeyboardLayoutPreset::Compact65,
            "keyboard layout should round-trip");
        Check(
            mod_tap.kind == ActionKind::ModTap &&
                mod_tap.target_key == kS &&
                mod_tap.hold_key == kLeftControl &&
                mod_tap.tapping_term_ms == 470 &&
                mod_tap.quick_tap_term_ms == 180,
            "mod-tap settings should round-trip");
        Check(
            loaded_profile->macros().size() == 1 &&
                loaded_profile->tap_dances().size() == 1 &&
                loaded_profile->combos().size() == 1 &&
                loaded_profile->overrides().size() == 1,
            "advanced rule libraries should round-trip");
        Check(
            macro_action.kind == ActionKind::Macro &&
                macro_action.reference_id == 1 &&
                tap_dance_action.kind ==
                    ActionKind::TapDance &&
                tap_dance_action.reference_id == 1,
            "reference actions should round-trip");
        Check(
            loaded_profile->mouse_settings().maximum_speed ==
                24,
            "mouse settings should round-trip");
        Check(
            shortcut.kind == ActionKind::Shortcut &&
                shortcut.target_key == kS &&
                shortcut.shortcut_modifiers ==
                    static_cast<std::uint16_t>(
                        ModifierLeftControl |
                        ModifierLeftShift),
            "shortcut chord should round-trip");
    }

    std::filesystem::remove(path, filesystem_error);
}

void InvalidZeroScanCodeIsRejected() {
    Check(
        !PhysicalKey{}.IsValid(),
        "scan code zero should be invalid");

    Configuration configuration;
    configuration.active_profile = L"Invalid";
    configuration.profiles.emplace_back(L"Invalid", 1);
    configuration.profiles.back().SetAction(
        0,
        kA,
        Action::Key({}));

    const auto path =
        std::filesystem::temp_directory_path() /
        L"pckey-invalid-zero-scan.pckey";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);

    std::wstring error;
    Check(
        !ConfigStore::SaveAtomic(
            path,
            configuration,
            error),
        "configuration should reject scan code zero actions");
    std::filesystem::remove(path, filesystem_error);
}

void RelativeConfigurationSaveWorks() {
    const std::filesystem::path path =
        L"pckey-relative-save-test.pckey";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);

    Configuration configuration;
    configuration.active_profile = L"Relative";
    configuration.profiles.emplace_back(L"Relative", 1);

    std::wstring error;
    Check(
        ConfigStore::SaveAtomic(path, configuration, error),
        "saving to a relative configuration path should work");

    Configuration loaded;
    Check(
        ConfigStore::Load(path, loaded, error),
        "relative configuration should load");
    Check(
        loaded.active_profile == L"Relative" &&
            loaded.FindProfile(L"Relative") != nullptr,
        "relative configuration should retain its active profile");

    std::filesystem::remove(path, filesystem_error);
    auto temporary = path;
    temporary += L".tmp";
    std::filesystem::remove(temporary, filesystem_error);
}

void UnknownActiveConfigurationIsRejectedOnSave() {
    const auto path =
        std::filesystem::temp_directory_path() /
        L"pckey-unknown-active-save-test.pckey";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);

    Configuration configuration;
    configuration.active_profile = L"Missing";
    configuration.profiles.emplace_back(L"Office", 1);

    std::wstring error;
    Check(
        !ConfigStore::SaveAtomic(path, configuration, error),
        "saving an unknown active profile should be rejected");
    std::filesystem::remove(path, filesystem_error);
}

void TapDanceRejectsMomentaryLayerTapSlots() {
    Configuration configuration;
    configuration.active_profile = L"Invalid TD";
    configuration.profiles.emplace_back(L"Invalid TD", 2);
    auto& profile = configuration.profiles.back();
    profile.tap_dances().push_back(
        pckey::TapDanceDefinition{
            1,
            L"Invalid MO Tap",
            Action::MomentaryLayer(1),
            Action::Transparent(),
            Action::Transparent(),
            Action::Transparent(),
            200,
            200,
            0});
    profile.SetAction(0, kSpace, Action::TapDance(1));

    const auto path =
        std::filesystem::temp_directory_path() /
        L"pckey-invalid-td-mo-tap.pckey";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);

    std::wstring error;
    Check(
        !ConfigStore::SaveAtomic(
            path,
            configuration,
            error),
        "Tap Dance MO should be rejected in tap slots");
    std::filesystem::remove(path, filesystem_error);
}

void UserStyleTapDanceLayerConfigurationLoads() {
    const auto path =
        std::filesystem::temp_directory_path() /
        L"pckey-user-style-td-layer.pckey";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);

    {
        std::ofstream stream(
            path,
            std::ios::binary | std::ios::trunc);
        stream
            << "PCKEY_CONFIG 3\n"
            << "active \"Office\"\n"
            << "profile \"Office\" 4 0\n"
            << "mouse 4 18 500 16 120\n"
            << "tapdance 1 \"Space Layer\" 200 200 200\n"
            << "tdaction 0 3 57 0 0 0 0 500 200 0 0 0 0 0 0\n"
            << "tdaction 1 4 0 0 1 0 0 500 200 0 0 0 0 0 0\n"
            << "tdaction 2 3 57 0 0 0 0 500 200 0 0 0 0 0 0\n"
            << "tdaction 3 3 57 0 0 0 0 500 200 0 0 0 0 0 0\n"
            << "endtapdance\n"
            << "action 0 57 0 13 0 0 0 0 0 500 200 0 0 0 0 0 1\n"
            << "action 1 18 0 3 72 1 0 0 0 500 200 0 0 0 0 0 0\n"
            << "endprofile\n";
    }

    Configuration configuration;
    std::wstring error;
    Check(
        ConfigStore::Load(path, configuration, error),
        "user-style Tap Dance layer configuration should load");

    const auto* loaded =
        configuration.FindProfile(L"Office");
    Check(
        loaded != nullptr,
        "loaded Tap Dance layer profile should exist");
    if (loaded != nullptr) {
        MappingEngine engine(*loaded);
        (void)engine.Process(Press(kSpace, 100'000));
        const auto arrow =
            engine.Process(Press(kE, 150'000));
        Check(
            arrow.synthetic_count == 1 &&
                arrow.synthetic_events[0].key == kArrowUp,
            "loaded Tap Dance MO(1) should affect a following key");
        (void)engine.Process(Release(kE, 160'000));
        (void)engine.Process(Release(kSpace, 170'000));
    }

    std::filesystem::remove(path, filesystem_error);
}

void MalformedAdvancedDefinitionIsRejected() {
    const auto path =
        std::filesystem::temp_directory_path() /
        L"pckey-malformed-advanced-definition.pckey";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);

    {
        std::ofstream stream(
            path,
            std::ios::binary | std::ios::trunc);
        stream
            << "PCKEY_CONFIG 3\n"
            << "active \"Broken\"\n"
            << "profile \"Broken\" 1 0\n"
            << "mouse 4 18 500 16 120\n"
            << "macro 1 \"Unclosed\"\n"
            << "macroevent 0 30 0 0\n"
            << "endprofile\n";
    }

    Configuration configuration;
    std::wstring error;
    Check(
        !ConfigStore::Load(
            path,
            configuration,
            error),
        "unclosed advanced definitions should be rejected");
    std::filesystem::remove(path, filesystem_error);
}

void LegacyConfigurationMigrates() {
    const auto path =
        std::filesystem::temp_directory_path() /
        L"pckey-config-v1-migration-test.pckey";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);

    {
        std::ofstream stream(
            path,
            std::ios::binary | std::ios::trunc);
        stream
            << "PCKEY_CONFIG 1\n"
            << "active \"Legacy\"\n"
            << "profile \"Legacy\" 2\n"
            << "action 0 57 0 4 0 0 1\n"
            << "endprofile\n";
    }

    Configuration loaded;
    std::wstring error;
    Check(
        ConfigStore::Load(path, loaded, error),
        "v1 configuration should migrate");
    const auto* profile = loaded.FindProfile(L"Legacy");
    Check(
        profile != nullptr &&
            profile->layout() ==
                KeyboardLayoutPreset::FullSize104,
        "v1 profile should receive the default layout");
    if (profile != nullptr) {
        const auto action = profile->GetAction(0, kSpace);
        Check(
            action.kind == ActionKind::MomentaryLayer &&
                action.target_layer == 1,
            "v1 actions should retain their behavior");
    }

    std::filesystem::remove(path, filesystem_error);
}

void VersionTwoConfigurationMigrates() {
    const auto path =
        std::filesystem::temp_directory_path() /
        L"pckey-config-v2-migration-test.pckey";
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);

    {
        std::ofstream stream(
            path,
            std::ios::binary | std::ios::trunc);
        stream
            << "PCKEY_CONFIG 2\n"
            << "active \"Version Two\"\n"
            << "profile \"Version Two\" 2 3\n"
            << "action 0 57 0 5 57 0 1 0 0 500 200\n"
            << "endprofile\n";
    }

    Configuration loaded;
    std::wstring error;
    Check(
        ConfigStore::Load(path, loaded, error),
        "v2 configuration should migrate");
    const auto* profile =
        loaded.FindProfile(L"Version Two");
    Check(
        profile != nullptr &&
            profile->layout() ==
                KeyboardLayoutPreset::Compact65,
        "v2 layout metadata should be retained");
    if (profile != nullptr) {
        const auto action =
            profile->GetAction(0, kSpace);
        Check(
            action.kind == ActionKind::LayerTap &&
                action.target_layer == 1 &&
                action.tapping_term_ms == 500,
            "v2 layer-tap should retain timing and layer");
    }

    std::filesystem::remove(path, filesystem_error);
}

}  // namespace

int main() {
    ProcessResultReportsOverflow();
    NormalModePassesThrough();
    RemapKeepsPressRepeatReleaseBalanced();
    ShortcutPressesAndReleasesAChord();
    ShortcutPreservesARealHeldModifier();
    ConcurrentShortcutsShareSyntheticModifiers();
    MomentaryLayerResolvesBeforeSecondKey();
    SharedTargetUsesReferenceCounting();
    ReleaseAllClearsSyntheticState();
    LayerTapTapsOnEarlyRelease();
    LayerTapHoldActivatesAtDeadline();
    SecondKeyImmediatelyChoosesHold();
    PreheldModifierDoesNotForceHold();
    ModifierReleaseSettlesPendingTapFirst();
    QuickTapKeepsTapKeyHeld();
    MediaKeyUsesVirtualKeyInjection();
    MouseKeysRepeatWithoutKeyboardEvents();
    MouseRepeatSkipsMissedTicks();
    MacroRunsOnTheSharedScheduler();
    InvalidInMemoryMacroIsIgnoredSafely();
    InvalidInMemoryComboIsIgnoredSafely();
    TapDanceSupportsTapDoubleAndHold();
    TapDanceMomentaryLayerWorks();
    ComboUsesMappedKeyIdentity();
    ComboFailureRestoresNormalAction();
    LongerOverlappingComboWins();
    KeyOverrideSuppressesAndRestoresModifiers();
    DecisionActionsFeedKeyOverrides();
    ReleaseAllRestoresSuppressedPhysicalModifiers();
    ConfigurationRoundTrips();
    RelativeConfigurationSaveWorks();
    UnknownActiveConfigurationIsRejectedOnSave();
    InvalidZeroScanCodeIsRejected();
    TapDanceRejectsMomentaryLayerTapSlots();
    UserStyleTapDanceLayerConfigurationLoads();
    MalformedAdvancedDefinitionIsRejected();
    LegacyConfigurationMigrates();
    VersionTwoConfigurationMigrates();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "All mapping engine tests passed.\n";
    return EXIT_SUCCESS;
}
