#pragma once

#include <Windows.h>

#include <optional>

#include "pckey/profile.hpp"

namespace pckey::editor {

[[nodiscard]] std::optional<MouseSettings>
ShowMouseSettingsEditor(
    HWND owner,
    HINSTANCE instance,
    const MouseSettings& current);

}  // namespace pckey::editor
