#pragma once

#include <Windows.h>

#include <optional>

#include "pckey/profile.hpp"

namespace pckey::editor {

[[nodiscard]] std::optional<MacroDefinition> ShowMacroEditor(
    HWND owner,
    HINSTANCE instance,
    std::uint16_t macro_id,
    std::wstring default_name);

[[nodiscard]] std::optional<MacroDefinition> ShowMacroEditor(
    HWND owner,
    HINSTANCE instance,
    const MacroDefinition& definition);

}  // namespace pckey::editor
