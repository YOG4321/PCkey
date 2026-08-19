#include <Windows.h>
#include <objbase.h>

#include "main_window.hpp"
#include "startup_trace.hpp"

namespace {

class StartupMutex {
public:
    explicit StartupMutex(const wchar_t* name) {
        handle_ = CreateMutexW(nullptr, FALSE, name);
        if (handle_ == nullptr) {
            return;
        }
        const auto result = WaitForSingleObject(handle_, 5000);
        if (result == WAIT_OBJECT_0 ||
            result == WAIT_ABANDONED) {
            owned_ = true;
        }
    }

    StartupMutex(const StartupMutex&) = delete;
    StartupMutex& operator=(const StartupMutex&) = delete;

    ~StartupMutex() {
        Release();
    }

    [[nodiscard]] bool acquired() const noexcept {
        return owned_;
    }

    void Release() noexcept {
        if (owned_) {
            ReleaseMutex(handle_);
            owned_ = false;
        }
        if (handle_ != nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

private:
    HANDLE handle_{};
    bool owned_{};
};

constexpr wchar_t kEditorStartupMutexName[] =
    L"Local\\PCkey.Editor.Startup.v1";

bool ActivateExistingEditor() {
    HWND existing = nullptr;
    while ((existing = FindWindowExW(
                nullptr,
                existing,
                L"PCkey.Editor.MainWindow",
                nullptr)) != nullptr) {
        DWORD_PTR health_result = 0;
        const auto delivered = SendMessageTimeoutW(
            existing,
            pckey::editor::MainWindow::kHealthCheckMessage,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            300,
            &health_result);
        if (delivered == 0 ||
            health_result != static_cast<DWORD_PTR>(
                pckey::editor::MainWindow::
                    kHealthCheckResult)) {
            continue;
        }

        ShowWindow(existing, SW_SHOW);
        ShowWindow(existing, SW_RESTORE);
        BringWindowToTop(existing);
        SetForegroundWindow(existing);
        return true;
    }
    return false;
}

}  // namespace

int WINAPI wWinMain(
    const HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    const int show_command) {
    pckey::editor::StartupTrace(L"wWinMain begin");
    StartupMutex startup_mutex(kEditorStartupMutexName);
    if (!startup_mutex.acquired()) {
        MessageBoxW(
            nullptr,
            L"PCkey Editor 无法同步启动，请稍后重试。",
            L"PCkey 错误",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    // A real editor window is the healthy-instance marker. This allows a new
    // editor to start even if security software leaves a no-window process
    // object from an earlier run behind.
    if (ActivateExistingEditor()) {
        pckey::editor::StartupTrace(
            L"existing healthy editor activated");
        return 0;
    }

    pckey::editor::StartupTrace(L"before CoInitializeEx");
    const auto com_result =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    pckey::editor::StartupTrace(L"after CoInitializeEx");

    pckey::editor::MainWindow window(instance);
    pckey::editor::StartupTrace(L"before MainWindow::Create");
    if (!window.Create()) {
        pckey::editor::StartupTrace(L"MainWindow::Create failed");
        MessageBoxW(
            nullptr,
            L"PCkey 编辑器窗口创建失败。",
            L"PCkey 错误",
            MB_OK | MB_ICONERROR);

        if (SUCCEEDED(com_result)) {
            CoUninitialize();
        }
        return 1;
    }

    pckey::editor::StartupTrace(L"before MainWindow::Show");
    window.Show(show_command);
    pckey::editor::StartupTrace(L"after MainWindow::Show");
    startup_mutex.Release();

    MSG message{};
    pckey::editor::StartupTrace(L"message loop begin");
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (SUCCEEDED(com_result)) {
        pckey::editor::StartupTrace(L"before CoUninitialize");
        CoUninitialize();
    }

    pckey::editor::StartupTrace(L"wWinMain end");
    return static_cast<int>(message.wParam);
}
