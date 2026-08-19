#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

#include "pckey/ipc_protocol.hpp"

namespace pckey::editor {

class IpcClient {
public:
    static bool ReloadConfiguration(
        std::wstring_view profile_name,
        std::wstring& error);
    static bool SetKeyTestSubscription(
        HWND window,
        ipc::KeyTestMode mode,
        std::wstring& error);

private:
    static bool SendReloadRequest(
        std::wstring_view profile_name,
        std::wstring& error);
    static bool SendKeyTestSubscription(
        HWND window,
        ipc::KeyTestMode mode,
        std::wstring& error);

    static void CloseUnhealthyCore() noexcept;
    static bool LaunchCore(std::wstring& error);
};

}  // namespace pckey::editor
