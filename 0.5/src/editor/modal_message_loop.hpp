#pragma once

#include <Windows.h>

namespace pckey::editor {

inline void RunModalMessageLoop(
    const HWND window,
    const bool dialog_navigation = true) noexcept {
    MSG message{};
    while (IsWindow(window)) {
        const auto result = GetMessageW(
            &message,
            nullptr,
            0,
            0);
        if (result == 0) {
            const auto exit_code = static_cast<int>(message.wParam);
            if (IsWindow(window)) {
                DestroyWindow(window);
            }
            // A nested modal loop must not consume the application's quit
            // request before the outer editor loop can observe it.
            PostQuitMessage(exit_code);
            return;
        }
        if (result < 0) {
            if (IsWindow(window)) {
                DestroyWindow(window);
            }
            PostQuitMessage(1);
            return;
        }

        if (!dialog_navigation ||
            !IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}

}  // namespace pckey::editor
