#pragma once

#include <Windows.h>

#include <optional>

#include "pckey/profile.hpp"

namespace pckey::editor {

[[nodiscard]] std::optional<KeyOverrideDefinition>
ShowOverrideEditor(
    HWND owner,
    HINSTANCE instance,
    std::uint16_t definition_id,
    std::size_t current_layer,
    const Profile& profile);

[[nodiscard]] std::optional<KeyOverrideDefinition>
ShowOverrideEditor(
    HWND owner,
    HINSTANCE instance,
    const KeyOverrideDefinition& definition,
    std::size_t current_layer,
    const Profile& profile);

}  // namespace pckey::editor
