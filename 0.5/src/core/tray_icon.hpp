#pragma once

#include <Windows.h>
#include <shellapi.h>

#include <string_view>

namespace pckey::core {

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Add(
        HWND owner,
        UINT callback_message,
        std::wstring_view tooltip);

    void Remove() noexcept;
    void UpdateTooltip(std::wstring_view tooltip) noexcept;

    [[nodiscard]] bool added() const noexcept {
        return added_;
    }

    [[nodiscard]] DWORD last_error() const noexcept {
        return last_error_;
    }

private:
    NOTIFYICONDATAW data_{};
    bool added_{};
    DWORD last_error_{ERROR_SUCCESS};
};

}  // namespace pckey::core
