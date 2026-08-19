#include "pckey/profile.hpp"

#include <algorithm>
#include <utility>

namespace pckey {

Layer::Layer() {
    actions_.fill(Action::Transparent());
}

void Layer::SetAction(
    const PhysicalKey key,
    const Action action) noexcept {
    if (!key.IsValid()) {
        return;
    }

    actions_[ToKeyIndex(key)] = action;
}

const Action& Layer::GetAction(
    const PhysicalKey key) const noexcept {
    static constexpr Action kTransparent = Action::Transparent();

    if (!key.IsValid()) {
        return kTransparent;
    }

    return actions_[ToKeyIndex(key)];
}

Profile::Profile(
    std::wstring name,
    const std::size_t layer_count,
    const KeyboardLayoutPreset layout)
    : name_(std::move(name)),
      layout_(layout) {
    const auto safe_count = std::clamp<std::size_t>(
        layer_count,
        1,
        kMaximumLayerCount);

    layers_.resize(safe_count);

    for (std::size_t index = 0;
         index < kPhysicalKeySlotCount;
         ++index) {
        const auto key = FromKeyIndex(index);
        if (!key.IsValid()) {
            continue;
        }
        layers_[0].SetAction(
            key,
            Action::PassThrough());
    }
}

Profile Profile::NormalMode() {
    return Profile(L"普通模式", 1);
}

void Profile::SetName(std::wstring name) {
    name_ = std::move(name);
}

bool Profile::AddLayer() {
    if (layers_.size() >= kMaximumLayerCount) {
        return false;
    }

    layers_.emplace_back();
    return true;
}

bool Profile::RemoveLastLayer() {
    if (layers_.size() <= 1) {
        return false;
    }

    layers_.pop_back();
    return true;
}

bool Profile::SetAction(
    const std::size_t layer,
    const PhysicalKey key,
    const Action action) noexcept {
    if (layer >= layers_.size() || !key.IsValid()) {
        return false;
    }

    layers_[layer].SetAction(key, action);
    return true;
}

Action Profile::GetAction(
    const std::size_t layer,
    const PhysicalKey key) const noexcept {
    if (layer >= layers_.size() || !key.IsValid()) {
        return Action::Transparent();
    }

    return layers_[layer].GetAction(key);
}

Action Profile::Resolve(
    const PhysicalKey key,
    std::uint32_t active_layers) const noexcept {
    if (!key.IsValid() || layers_.empty()) {
        return Action::PassThrough();
    }

    active_layers |= 1U;

    for (std::size_t reverse_index = layers_.size();
         reverse_index > 0;
         --reverse_index) {
        const auto layer_index = reverse_index - 1;
        const auto layer_bit =
            static_cast<std::uint32_t>(1U << layer_index);

        if ((active_layers & layer_bit) == 0) {
            continue;
        }

        const auto action = layers_[layer_index].GetAction(key);
        if (action.kind != ActionKind::Transparent) {
            return action;
        }
    }

    return Action::PassThrough();
}

MacroDefinition* Profile::FindMacro(
    const std::uint16_t id) noexcept {
    const auto iterator = std::find_if(
        macros_.begin(),
        macros_.end(),
        [id](const MacroDefinition& macro) {
            return macro.id == id;
        });
    return iterator == macros_.end() ? nullptr : &*iterator;
}

const MacroDefinition* Profile::FindMacro(
    const std::uint16_t id) const noexcept {
    return const_cast<Profile*>(this)->FindMacro(id);
}

TapDanceDefinition* Profile::FindTapDance(
    const std::uint16_t id) noexcept {
    const auto iterator = std::find_if(
        tap_dances_.begin(),
        tap_dances_.end(),
        [id](const TapDanceDefinition& tap_dance) {
            return tap_dance.id == id;
        });
    return iterator == tap_dances_.end() ? nullptr : &*iterator;
}

const TapDanceDefinition* Profile::FindTapDance(
    const std::uint16_t id) const noexcept {
    return const_cast<Profile*>(this)->FindTapDance(id);
}

}  // namespace pckey
