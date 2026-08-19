#pragma once

#include <Windows.h>

#include <optional>

#include "pckey/profile.hpp"

namespace pckey::editor {

[[nodiscard]] std::optional<TapDanceDefinition>
ShowTapDanceEditor(
    HWND owner,
    HINSTANCE instance,
    std::uint16_t definition_id,
    PhysicalKey source_key,
    const Profile& profile);

[[nodiscard]] std::optional<TapDanceDefinition>
ShowTapDanceEditor(
    HWND owner,
    HINSTANCE instance,
    const TapDanceDefinition& definition,
    PhysicalKey source_key,
    const Profile& profile);

}  // namespace pckey::editor
