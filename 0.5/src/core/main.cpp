#include <Windows.h>

#include <string_view>

#include "core_app.hpp"

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR command_line,
    int) {
    const std::wstring_view arguments =
        command_line != nullptr
            ? std::wstring_view(command_line)
            : std::wstring_view{};
    const bool background_start =
        arguments.find(L"--background") !=
        std::wstring_view::npos;

    pckey::core::CoreApp app(instance, background_start);
    return app.Run();
}
