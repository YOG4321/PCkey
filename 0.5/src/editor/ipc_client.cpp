#include "ipc_client.hpp"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "pckey/config_store.hpp"
#include "pckey/ipc_protocol.hpp"

namespace pckey::editor {

namespace {

constexpr wchar_t kCoreWindowClass[] =
    L"PCkey.Core.HiddenWindow";
constexpr UINT kCoreHealthCheckMessage = WM_APP + 4;
constexpr UINT kCoreShutdownMessage = WM_APP + 5;
constexpr DWORD_PTR kCoreHealthCheckResult =
    0x50434B43;

HWND FindHealthyCoreWindow() noexcept {
    HWND candidate = nullptr;
    while ((candidate = FindWindowExW(
                HWND_MESSAGE,
                candidate,
                kCoreWindowClass,
                nullptr)) != nullptr) {
        DWORD_PTR result = 0;
        const auto delivered = SendMessageTimeoutW(
            candidate,
            kCoreHealthCheckMessage,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            300,
            &result);
        if (delivered != 0 &&
            result == kCoreHealthCheckResult) {
            return candidate;
        }
    }
    return nullptr;
}

HWND FindAnyCoreWindow() noexcept {
    return FindWindowExW(
        HWND_MESSAGE,
        nullptr,
        kCoreWindowClass,
        nullptr);
}

}  // namespace

bool IpcClient::ReloadConfiguration(
    const std::wstring_view profile_name,
    std::wstring& error) {
    // Verify the exact on-disk file before involving Core. If this succeeds
    // but Core rejects it, the problem is a stale/unhealthy Core rather than
    // invalid editor data.
    Configuration local_configuration;
    if (!ConfigStore::Load(
            ConfigStore::DefaultPath(),
            local_configuration,
            error)) {
        error =
            L"编辑器刚写入的配置无法重新读取：" +
            error;
        return false;
    }
    if (profile_name != kNormalModeName &&
        local_configuration.FindProfile(
            profile_name) == nullptr) {
        error = L"准备应用的配置名称不存在。";
        return false;
    }

    if (SendReloadRequest(profile_name, error)) {
        return true;
    }

    const auto first_error = error;
    // A healthy Core can reject a request for configuration reasons. Do not
    // shut down a working service merely because the application-level IPC
    // response was negative or a transient timeout occurred. If a window is
    // present but fails the health check, it is the stale/unresponsive Core
    // that this recovery path is meant to replace.
    if (FindHealthyCoreWindow() != nullptr) {
        return false;
    }

    if (FindAnyCoreWindow() != nullptr) {
        CloseUnhealthyCore();
        if (FindAnyCoreWindow() != nullptr) {
            error =
                first_error +
                L"\n检测到无响应的后台核心，但它未能在限定时间内退出；"
                L"请从任务管理器结束旧 PCkeyCore.exe 后重试。";
            return false;
        }
    }

    std::wstring launch_error;
    if (!LaunchCore(launch_error)) {
        error =
            first_error + L"\n" +
            launch_error;
        return false;
    }

    for (int attempt = 0; attempt < 50; ++attempt) {
        Sleep(50);
        if (SendReloadRequest(profile_name, error)) {
            return true;
        }
    }

    error =
        first_error +
        L"\n已自动重启后台核心，但新核心仍未能应用配置：" +
        error;
    return false;
}

bool IpcClient::SetKeyTestSubscription(
    const HWND window,
    const ipc::KeyTestMode mode,
    std::wstring& error) {
    if (SendKeyTestSubscription(window, mode, error)) {
        return true;
    }
    if (mode == ipc::KeyTestMode::None) {
        return false;
    }

    if (FindHealthyCoreWindow() != nullptr) {
        return false;
    }

    if (FindAnyCoreWindow() != nullptr) {
        CloseUnhealthyCore();
        if (FindAnyCoreWindow() != nullptr) {
            error +=
                L"\n检测到无响应的后台核心，但它未能在限定时间内退出；"
                L"请从任务管理器结束旧 PCkeyCore.exe 后重试。";
            return false;
        }
    }

    std::wstring launch_error;
    if (!LaunchCore(launch_error)) {
        error += L"\n" + launch_error;
        return false;
    }
    for (int attempt = 0; attempt < 50; ++attempt) {
        Sleep(50);
        if (SendKeyTestSubscription(
                window,
                mode,
                error)) {
            return true;
        }
    }
    return false;
}

bool IpcClient::SendKeyTestSubscription(
    const HWND window,
    const ipc::KeyTestMode mode,
    std::wstring& error) {
    error.clear();
    const auto core_window = FindHealthyCoreWindow();
    if (core_window == nullptr) {
        error = L"没有找到正在运行的PCkey后台核心。";
        return false;
    }

    ipc::KeyTestSubscription subscription{};
    subscription.window_handle =
        reinterpret_cast<std::uintptr_t>(window);
    subscription.mode = mode;
    COPYDATASTRUCT copy_data{
        static_cast<ULONG_PTR>(
            ipc::kKeyTestCopyDataId),
        static_cast<DWORD>(sizeof(subscription)),
        &subscription};
    DWORD_PTR result = 0;
    SetLastError(ERROR_SUCCESS);
    const auto delivered = SendMessageTimeoutW(
        core_window,
        WM_COPYDATA,
        0,
        reinterpret_cast<LPARAM>(&copy_data),
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        1500,
        &result);
    if (delivered == 0 || result != 1) {
        error =
            L"PCkey后台核心未能启用按键测试通道。错误代码：" +
            std::to_wstring(GetLastError());
        return false;
    }
    return true;
}

bool IpcClient::SendReloadRequest(
    const std::wstring_view profile_name,
    std::wstring& error) {
    error.clear();

    if (profile_name.size() >
        ipc::kMaximumPayloadSize / sizeof(wchar_t)) {
        error = L"配置名称过长。";
        return false;
    }

    const auto core_window = FindHealthyCoreWindow();
    if (core_window == nullptr) {
        error = L"没有找到正在运行的 PCkey 后台核心。";
        return false;
    }

    const auto payload_size = static_cast<std::uint32_t>(
        profile_name.size() * sizeof(wchar_t));
    COPYDATASTRUCT copy_data{
        static_cast<ULONG_PTR>(
            ipc::kReloadCopyDataId),
        payload_size,
        const_cast<wchar_t*>(
            profile_name.data())};

    DWORD_PTR result = 0;
    SetLastError(ERROR_SUCCESS);
    const auto delivered = SendMessageTimeoutW(
        core_window,
        WM_COPYDATA,
        0,
        reinterpret_cast<LPARAM>(&copy_data),
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        3000,
        &result);

    if (delivered == 0) {
        const auto system_error = GetLastError();
        error =
            system_error == ERROR_ACCESS_DENIED
                ? L"编辑器与后台核心的权限级别不一致。"
                  L"请关闭二者后，仅双击 PCkeyCore.exe 启动。"
                : L"PCkey 后台核心没有响应配置请求。错误代码：" +
                      std::to_wstring(system_error);
        return false;
    }

    if (result != 1) {
        error =
            L"当前后台核心状态异常；配置文件本身已经通过"
            L"编辑器校验，将自动重启核心后再次应用。";
        return false;
    }

    return true;
}

void IpcClient::CloseUnhealthyCore() noexcept {
    // The caller has already established that no healthy window exists. Use
    // the message window itself here so a stale/unresponsive instance can be
    // asked to shut down instead of silently being left behind.
    const auto core_window = FindAnyCoreWindow();
    if (core_window == nullptr) {
        return;
    }

    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(
        core_window,
        kCoreShutdownMessage,
        0,
        0,
        SMTO_ABORTIFHUNG,
        1500,
        &ignored);

    for (int attempt = 0; attempt < 30; ++attempt) {
        if (FindAnyCoreWindow() == nullptr) {
            return;
        }
        Sleep(50);
    }
}

bool IpcClient::LaunchCore(std::wstring& error) {
    error.clear();

    std::wstring module_path(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr,
        module_path.data(),
        static_cast<DWORD>(module_path.size()));
    if (length == 0 ||
        static_cast<std::size_t>(length) >=
            module_path.size()) {
        error = L"无法确定编辑器所在目录。";
        return false;
    }

    module_path.resize(length);
    const auto core_path =
        std::filesystem::path(module_path)
            .parent_path() /
        L"PCkeyCore.exe";

    std::wstring command_line =
        L"\"" + core_path.wstring() +
        L"\" --background";
    std::vector<wchar_t> mutable_command(
        command_line.begin(),
        command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (CreateProcessW(
            core_path.c_str(),
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            core_path.parent_path().c_str(),
            &startup,
            &process) == FALSE) {
        error =
            L"无法启动 PCkey 后台核心。错误代码：" +
            std::to_wstring(GetLastError());
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

}  // namespace pckey::editor
