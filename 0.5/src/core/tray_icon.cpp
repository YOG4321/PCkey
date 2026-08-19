#include "tray_icon.hpp"

#include <algorithm>
#include <cwchar>

#include "resource.h"

namespace pckey::core {

TrayIcon::~TrayIcon() {
    Remove();
}

bool TrayIcon::Add(
    const HWND owner,
    const UINT callback_message,
    const std::wstring_view tooltip) {
    Remove();

    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = callback_message;
    data_.hIcon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_PCKEY),
        IMAGE_ICON,
        0,
        0,
        LR_DEFAULTSIZE | LR_SHARED));
    if (data_.hIcon == nullptr) {
        data_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    const auto copy_count = std::min(
        tooltip.size(),
        std::size(data_.szTip) - 1);
    std::wmemcpy(data_.szTip, tooltip.data(), copy_count);
    data_.szTip[copy_count] = L'\0';

    SetLastError(ERROR_SUCCESS);
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    last_error_ = added_ ? ERROR_SUCCESS : GetLastError();
    if (!added_ && last_error_ == ERROR_SUCCESS) {
        last_error_ = ERROR_GEN_FAILURE;
    }

    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }

    return added_;
}

void TrayIcon::Remove() noexcept {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
}

void TrayIcon::UpdateTooltip(
    const std::wstring_view tooltip) noexcept {
    if (!added_) {
        return;
    }

    data_.uFlags = NIF_TIP;
    const auto copy_count = std::min(
        tooltip.size(),
        std::size(data_.szTip) - 1);
    std::wmemcpy(data_.szTip, tooltip.data(), copy_count);
    data_.szTip[copy_count] = L'\0';
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

}  // namespace pckey::core
