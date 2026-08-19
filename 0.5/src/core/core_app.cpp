#include "core_app.hpp"

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pckey/ipc_protocol.hpp"
#include "resource.h"

namespace pckey::core {

namespace {

constexpr PhysicalKey kLeftShift{0x2A, KeyPrefix::None};
constexpr PhysicalKey kRightShift{0x36, KeyPrefix::None};
constexpr PhysicalKey kEscape{0x01, KeyPrefix::None};

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
        if (owned_) {
            ReleaseMutex(handle_);
        }
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }

    [[nodiscard]] bool acquired() const noexcept {
        return owned_;
    }

    void Release() noexcept {
        if (owned_) {
            ReleaseMutex(handle_);
            owned_ = false;
        }
    }

private:
    HANDLE handle_{};
    bool owned_{};
};

constexpr wchar_t kCoreStartupMutexName[] =
    L"Local\\PCkey.Core.Startup.v1";

}  // namespace

CoreApp::CoreApp(
    const HINSTANCE instance,
    const bool background_start)
    : instance_(instance),
      background_start_(background_start) {
    // Recovery is entered from the low-level hook on exceptional paths. Keep
    // its bounded cleanup list allocated up front so a full injection queue
    // cannot turn an allocation failure into an exception from the hook.
    injection_cleanup_.reserve(
        kPhysicalKeySlotCount + 256 + 16);
}

CoreApp::~CoreApp() {
    StopServices();
}

int CoreApp::Run() {
    StartupMutex startup_mutex(kCoreStartupMutexName);
    if (!startup_mutex.acquired()) {
        MessageBoxW(
            nullptr,
            L"PCkey 无法同步启动后台服务，请稍后重试。",
            L"PCkey 错误",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    // A healthy Core always owns this message window. Some endpoint-security
    // products can leave a terminated, no-window process object alive; a
    // named mutex would incorrectly let that residue block all future starts.
    HWND existing = nullptr;
    while ((existing = FindWindowExW(
                HWND_MESSAGE,
                existing,
                kMessageWindowClassName,
                nullptr)) != nullptr) {
        DWORD_PTR health_result = 0;
        const auto healthy = SendMessageTimeoutW(
            existing,
            kHealthCheckMessage,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            300,
            &health_result);
        if (healthy == 0 ||
            health_result !=
                static_cast<DWORD_PTR>(
                    kHealthCheckResult)) {
            continue;
        }

        if (!background_start_) {
            LaunchEditor();
        }
        return 0;
    }

    if (!CreateWindows()) {
        MessageBoxW(
            nullptr,
            L"PCkey 无法创建后台窗口。",
            L"PCkey 错误",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!StartServices()) {
        MessageBoxW(
            nullptr,
            startup_error_message_.c_str(),
            L"PCkey 错误",
            MB_OK | MB_ICONERROR);
        DestroyWindows();
        return 1;
    }

    if (!background_start_) {
        PostMessageW(
            message_window_,
            kOpenEditorMessage,
            0,
            0);
    }

    // The message window and services are now visible to other starters;
    // release the short-lived startup lock before entering the message loop.
    startup_mutex.Release();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK CoreApp::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param) {
    CoreApp* app = reinterpret_cast<CoreApp*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create =
            reinterpret_cast<const CREATESTRUCTW*>(l_param);
        app = static_cast<CoreApp*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(app));
    }

    if (app != nullptr) {
        return app->HandleMessage(
            window,
            message,
            w_param,
            l_param);
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CoreApp::HandleMessage(
    const HWND source_window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param) {
    if (source_window == tray_window_ &&
        message == taskbar_created_message_) {
        CancelTrayRetry();
        tray_retry_attempts_ = 0;
        tray_error_reported_ = false;
        TryAddTrayIcon(false);
        return 0;
    }
    if (source_window == message_window_ &&
        message == kHealthCheckMessage) {
        return kHealthCheckResult;
    }
    if (source_window == message_window_ &&
        message == kInjectionFailureMessage) {
        if (input_injector_.FailurePending()) {
            BeginInjectionRecovery(nullptr);
        }
        return 0;
    }

    switch (message) {
    case kOpenEditorMessage:
        if (source_window == message_window_) {
            LaunchEditor();
        }
        return 0;

    case kTrayMessage:
        if (source_window == tray_window_) {
            if (LOWORD(l_param) == WM_CONTEXTMENU ||
                LOWORD(l_param) == WM_RBUTTONUP) {
                ShowTrayMenu();
            } else if (
                LOWORD(l_param) == WM_LBUTTONDBLCLK) {
                LaunchEditor();
            }
        }
        return 0;

    case kShutdownMessage:
        if (source_window == message_window_) {
            ExitImmediately();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(w_param)) {
        case kCommandOpen:
            LaunchEditor();
            break;
        case kCommandNormalMode:
            break;
        case kCommandExit:
            ExitImmediately();
            break;
        default:
            break;
        }
        return 0;

    case WM_COPYDATA: {
        if (source_window != message_window_) {
            return 0;
        }
        const auto* copy_data =
            reinterpret_cast<const COPYDATASTRUCT*>(l_param);
        if (copy_data != nullptr &&
            copy_data->dwData ==
                ipc::kKeyTestCopyDataId) {
            return UpdateKeyTestSubscription(*copy_data)
                       ? 1
                       : 0;
        }
        if (copy_data == nullptr ||
            copy_data->dwData != ipc::kReloadCopyDataId ||
            copy_data->cbData >
                ipc::kMaximumPayloadSize ||
            copy_data->cbData % sizeof(wchar_t) != 0 ||
            (copy_data->cbData > 0 &&
             copy_data->lpData == nullptr)) {
            return 0;
        }

        const auto character_count =
            copy_data->cbData / sizeof(wchar_t);
        const auto* characters =
            static_cast<const wchar_t*>(
                copy_data->lpData);
        return LoadConfiguredProfile(
                   std::wstring_view(
                       characters,
                       character_count),
                   false)
                   ? 1
                   : 0;
    }

    case WM_TIMER:
        if (source_window != message_window_) {
            return 0;
        }
        if (w_param == kEmergencyTimerId) {
            KillTimer(
                message_window_,
                kEmergencyTimerId);
            emergency_timer_armed_ = false;

            if (EmergencyChordHeld()) {
                ActivateEmergencyBypass();
            }
        } else if (w_param == kTapHoldTimerId) {
            KillTimer(
                message_window_,
                kTapHoldTimerId);
            tap_hold_timer_armed_ = false;
            const auto timed =
                engine_.AdvanceTime(TimestampMicros());
            if (timed.overflowed) {
                if (key_test_mode_ == ipc::KeyTestMode::None) {
                    BeginInjectionRecovery(&timed);
                } else {
                    (void)engine_.ReleaseAll();
                    CancelTapHoldTimer();
                }
            } else if (timed.synthetic_count > 0) {
                PublishMappedSynthetic(timed);
                if (key_test_mode_ ==
                    ipc::KeyTestMode::None) {
                    if (!input_injector_.Enqueue(
                            timed.synthetic_events.data(),
                            timed.synthetic_count)) {
                        BeginInjectionRecovery(&timed);
                    }
                }
            }
            ArmTapHoldTimer();
        } else if (w_param == kInjectionRetryTimerId) {
            KillTimer(
                message_window_,
                kInjectionRetryTimerId);
            injection_retry_timer_armed_ = false;
            if (!TryRecoverInjection()) {
                ArmInjectionRetryTimer();
            }
        } else if (w_param == kTrayRetryTimerId) {
            KillTimer(
                message_window_,
                kTrayRetryTimerId);
            tray_retry_armed_ = false;
            TryAddTrayIcon(
                tray_retry_attempts_ >= kTrayRetryLimit);
        }
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (w_param != FALSE) {
            StopServices();
        }
        return 0;

    case WM_CLOSE:
        ExitImmediately();

    case WM_DESTROY:
        if (source_window == message_window_) {
            message_window_ = nullptr;
        } else if (source_window == tray_window_) {
            tray_window_ = nullptr;
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(
        source_window,
        message,
        w_param,
        l_param);
}

bool CoreApp::CreateWindows() {
    auto icon = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_PCKEY),
        IMAGE_ICON,
        0,
        0,
        LR_DEFAULTSIZE | LR_SHARED));
    if (icon == nullptr) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    const auto register_class =
        [this, icon](const wchar_t* class_name) {
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.hInstance = instance_;
            window_class.lpfnWndProc =
                &CoreApp::WindowProcedure;
            window_class.lpszClassName = class_name;
            window_class.hIcon = icon;

            return RegisterClassExW(&window_class) != 0 ||
                   GetLastError() ==
                       ERROR_CLASS_ALREADY_EXISTS;
        };

    taskbar_created_message_ =
        RegisterWindowMessageW(L"TaskbarCreated");

    if (!register_class(kMessageWindowClassName) ||
        !register_class(kTrayWindowClassName)) {
        return false;
    }

    message_window_ = CreateWindowExW(
        0,
        kMessageWindowClassName,
        L"PCkey Core",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance_,
        this);

    if (message_window_ == nullptr) {
        return false;
    }

    tray_window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kTrayWindowClassName,
        L"PCkey Tray",
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance_,
        this);

    if (tray_window_ == nullptr) {
        DestroyWindows();
        return false;
    }

    {
        CHANGEFILTERSTRUCT filter{};
        filter.cbSize = sizeof(filter);
        ChangeWindowMessageFilterEx(
            message_window_,
            kHealthCheckMessage,
            MSGFLT_ALLOW,
            &filter);
        ChangeWindowMessageFilterEx(
            message_window_,
            kShutdownMessage,
            MSGFLT_ALLOW,
            &filter);
        ChangeWindowMessageFilterEx(
            message_window_,
            WM_COPYDATA,
            MSGFLT_ALLOW,
            &filter);
    }

    {
        CHANGEFILTERSTRUCT filter{};
        filter.cbSize = sizeof(filter);
        ChangeWindowMessageFilterEx(
            tray_window_,
            kTrayMessage,
            MSGFLT_ALLOW,
            &filter);
        if (taskbar_created_message_ != 0) {
            ChangeWindowMessageFilterEx(
                tray_window_,
                taskbar_created_message_,
                MSGFLT_ALLOW,
                &filter);
        }
    }

    return true;
}

void CoreApp::DestroyWindows() noexcept {
    if (tray_window_ != nullptr) {
        DestroyWindow(tray_window_);
        tray_window_ = nullptr;
    }
    if (message_window_ != nullptr) {
        DestroyWindow(message_window_);
        message_window_ = nullptr;
    }
}

bool CoreApp::StartServices() {
    if (services_started_) {
        return true;
    }

    if (QueryPerformanceFrequency(
            &performance_frequency_) == FALSE ||
        performance_frequency_.QuadPart <= 0) {
        startup_error_message_ =
            L"PCkey 无法初始化高精度计时器。";
        return false;
    }

    LoadConfiguredProfile({}, true);

    if (!keyboard_hook_.Install(
            [this](const KeyEvent& event) {
                return ProcessKeyboardEvent(event);
            })) {
        startup_error_message_ =
            L"PCkey 无法安装键盘钩子。错误代码：" +
            std::to_wstring(GetLastError());
        return false;
    }

    // Enqueue falls back to synchronous SendInput if the worker thread
    // could not be created, so a failure here is not fatal.
    (void)input_injector_.Start(
        message_window_,
        kInjectionFailureMessage);

    // Explorer can temporarily reject tray icons during login or restart.
    // Keep the keyboard service alive and retry instead of silently losing
    // the only visible entry point.
    TryAddTrayIcon(false);

    services_started_ = true;
    return true;
}

void CoreApp::StopServices() noexcept {
    if (!services_started_) {
        return;
    }

    if (emergency_timer_armed_ &&
        message_window_ != nullptr) {
        KillTimer(
            message_window_,
            kEmergencyTimerId);
        emergency_timer_armed_ = false;
    }

    CancelTapHoldTimer();
    CancelInjectionRetryTimer();
    CancelTrayRetry();

    const auto releases = engine_.ReleaseAll();
    if (!input_injector_.Enqueue(releases)) {
        for (const auto& event : releases) {
            if (event.transition == KeyTransition::Release) {
                AddInjectionCleanup(event);
            }
        }
        for (const auto& event : releases) {
            AddInjectionRestore(event);
        }
        (void)input_injector_.ReplacePending(injection_cleanup_);
        injection_cleanup_.clear();
    }
    // Drain the queue so release events are injected before the hook is
    // removed. If SendInput is stuck, Stop gives up after a bounded wait.
    input_injector_.Stop();
    keyboard_hook_.Uninstall();
    tray_icon_.Remove();
    services_started_ = false;
}

void CoreApp::ShowTrayMenu() {
    POINT cursor{};
    GetCursorPos(&cursor);

    const auto menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    AppendMenuW(menu, MF_STRING, kCommandOpen, L"打开 PCkey");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(
        menu,
        MF_STRING | MF_CHECKED | MF_DISABLED,
        kCommandNormalMode,
        emergency_bypass_
            ? L"安全旁路已启用"
            : current_profile_name_.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, L"退出");

    SetForegroundWindow(tray_window_);
    const auto command = TrackPopupMenuEx(
        menu,
        TPM_RIGHTBUTTON |
            TPM_BOTTOMALIGN |
            TPM_LEFTALIGN |
            TPM_RETURNCMD |
            TPM_NONOTIFY,
        cursor.x,
        cursor.y,
        tray_window_,
        nullptr);
    PostMessageW(tray_window_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (command) {
    case kCommandOpen:
        LaunchEditor();
        break;
    case kCommandExit:
        ExitImmediately();
        break;
    default:
        break;
    }
}

[[noreturn]] void CoreApp::ExitImmediately() noexcept {
    if (!shutdown_started_) {
        shutdown_started_ = true;
        StopServices();
        CloseEditorWindows();
    }

    // Do not use TerminateProcess here. On the affected machine, forced
    // termination leaves a one-thread endpoint-security residue. ExitProcess
    // performs normal DLL detach after PCkey has removed its hook, released
    // synthetic keys and deleted the tray icon.
    ExitProcess(0);
}

void CoreApp::CloseEditorWindows() noexcept {
    std::vector<DWORD> process_ids;
    std::vector<HANDLE> processes;
    HWND editor = nullptr;
    while ((editor = FindWindowExW(
                nullptr,
                editor,
                L"PCkey.Editor.MainWindow",
                nullptr)) != nullptr) {
        DWORD process_id = 0;
        GetWindowThreadProcessId(
            editor,
            &process_id);
        if (process_id != 0 &&
            std::find(
                process_ids.begin(),
                process_ids.end(),
                process_id) == process_ids.end()) {
            const auto process = OpenProcess(
                SYNCHRONIZE,
                FALSE,
                process_id);
            if (process != nullptr) {
                process_ids.push_back(process_id);
                processes.push_back(process);
            }
        }

        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(
            editor,
            WM_CLOSE,
            0,
            0,
            SMTO_ABORTIFHUNG,
            500,
            &ignored);
    }

    const auto deadline = GetTickCount64() + 1500;
    for (const auto process : processes) {
        const auto now = GetTickCount64();
        const auto remaining =
            now < deadline
                ? static_cast<DWORD>(deadline - now)
                : 0;
        WaitForSingleObject(process, remaining);
        CloseHandle(process);
    }
}

bool CoreApp::LaunchEditor() {
    // GetModuleFileNameW may return a truncated path when the executable is
    // installed below a long path. Launching a truncated sibling path gives
    // a misleading "editor not found" error, so use the extended buffer and
    // reject truncation explicitly.
    std::wstring module_path(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr,
        module_path.data(),
        static_cast<DWORD>(module_path.size()));

    if (length == 0 ||
        static_cast<std::size_t>(length) >=
            module_path.size()) {
        MessageBoxW(
            tray_window_,
            L"PCkey 无法确定程序所在目录，编辑器未启动。",
            L"PCkey 错误",
            MB_OK | MB_ICONERROR);
        return false;
    }

    module_path.resize(length);
    const auto editor_path =
        std::filesystem::path(module_path)
            .parent_path() /
        L"PCkeyEditor.exe";

    std::wstring command_line =
        L"\"" + editor_path.wstring() + L"\"";
    std::vector<wchar_t> mutable_command(
        command_line.begin(),
        command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (CreateProcessW(
            editor_path.c_str(),
            mutable_command.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            editor_path.parent_path().c_str(),
            &startup,
            &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    const auto error = GetLastError();
    MessageBoxW(
        tray_window_,
        (L"PCkeyEditor.exe 启动失败。\n错误代码：" +
         std::to_wstring(error) +
         L"\n请确认两个 EXE 位于同一目录。")
            .c_str(),
        L"PCkey 错误",
        MB_OK | MB_ICONERROR);
    return false;
}

bool CoreApp::TryAddTrayIcon(
    const bool show_final_error) {
    ++tray_retry_attempts_;
    if (tray_icon_.Add(
            tray_window_,
            kTrayMessage,
            TrayTooltip())) {
        CancelTrayRetry();
        tray_retry_attempts_ = 0;
        tray_error_reported_ = false;
        return true;
    }

    if (show_final_error ||
        tray_retry_attempts_ >= kTrayRetryLimit) {
        if (!tray_error_reported_) {
            tray_error_reported_ = true;
            MessageBoxW(
                tray_window_,
                (L"PCkey 后台已启动，但 Windows 未能创建托盘图标。"
                 L"\n错误代码：" +
                 std::to_wstring(tray_icon_.last_error()) +
                 L"\n可以再次双击 PCkeyCore.exe 打开编辑器；"
                 L"重启资源管理器后托盘图标也会自动恢复。")
                    .c_str(),
                L"PCkey 托盘错误",
                MB_OK | MB_ICONERROR);
        }
        return false;
    }

    tray_retry_armed_ =
        SetTimer(
            message_window_,
            kTrayRetryTimerId,
            kTrayRetryDelayMs,
            nullptr) != 0;
    return false;
}

void CoreApp::CancelTrayRetry() noexcept {
    if (tray_retry_armed_ &&
        message_window_ != nullptr) {
        KillTimer(
            message_window_,
            kTrayRetryTimerId);
    }
    tray_retry_armed_ = false;
}

std::wstring CoreApp::TrayTooltip() const {
    return emergency_bypass_
               ? L"PCkey - 安全旁路"
               : L"PCkey - " + current_profile_name_;
}

void CoreApp::UpdateEmergencyState(const KeyEvent& event) {
    const bool down =
        event.transition != KeyTransition::Release;

    if (event.key == kLeftShift) {
        left_shift_down_ = down;
    } else if (event.key == kRightShift) {
        right_shift_down_ = down;
    } else if (event.key == kEscape) {
        escape_down_ = down;
    }

    if (EmergencyChordHeld()) {
        if (!emergency_timer_armed_ &&
            !emergency_bypass_) {
            emergency_timer_armed_ =
                SetTimer(
                    message_window_,
                    kEmergencyTimerId,
                    kEmergencyHoldMs,
                    nullptr) != 0;
        }
    } else if (emergency_timer_armed_) {
        KillTimer(
            message_window_,
            kEmergencyTimerId);
        emergency_timer_armed_ = false;
    }
}

void CoreApp::ActivateEmergencyBypass() {
    if (emergency_bypass_) {
        return;
    }

    const auto releases = engine_.ReleaseAll();
    CancelTapHoldTimer();
    if (!input_injector_.Enqueue(releases)) {
        for (const auto& event : releases) {
            if (event.transition == KeyTransition::Release) {
                AddInjectionCleanup(event);
            }
        }
        for (const auto& event : releases) {
            AddInjectionRestore(event);
        }
        injection_recovery_pending_ = true;
        (void)TryRecoverInjection();
        if (injection_recovery_pending_) {
            ArmInjectionRetryTimer();
        }
    }
    engine_.SetBypass(true);
    emergency_bypass_ = true;
    tray_icon_.UpdateTooltip(L"PCkey - 安全旁路");
}

bool CoreApp::LoadConfiguredProfile(
    const std::wstring_view requested_profile,
    const bool show_error) {
    Configuration configuration;
    std::wstring error;

    if (!ConfigStore::Load(
            ConfigStore::DefaultPath(),
            configuration,
            error)) {
        if (show_error) {
            MessageBoxW(
                tray_window_,
                error.c_str(),
                L"PCkey 配置错误",
                MB_OK | MB_ICONERROR);
        }
        return false;
    }

    const auto profile_name = requested_profile.empty()
                                  ? std::wstring_view(
                                        configuration.active_profile)
                                  : requested_profile;

    Profile next_profile = Profile::NormalMode();
    std::wstring next_name(kNormalModeName);

    if (profile_name != kNormalModeName) {
        const auto* configured =
            configuration.FindProfile(profile_name);
        if (configured == nullptr) {
            return false;
        }

        next_profile = *configured;
        next_name = configured->name();
    }

    const auto releases = engine_.ReleaseAll();
    CancelTapHoldTimer();
    if (!input_injector_.Enqueue(releases)) {
        for (const auto& event : releases) {
            if (event.transition == KeyTransition::Release) {
                AddInjectionCleanup(event);
            }
        }
        for (const auto& event : releases) {
            AddInjectionRestore(event);
        }
        injection_recovery_pending_ = true;
        (void)TryRecoverInjection();
        if (injection_recovery_pending_) {
            ArmInjectionRetryTimer();
        }
    }
    engine_.LoadProfile(std::move(next_profile));
    current_profile_name_ = std::move(next_name);

    if (!emergency_bypass_) {
        tray_icon_.UpdateTooltip(
            L"PCkey - " + current_profile_name_);
    }

    return true;
}

ProcessResult CoreApp::ProcessKeyboardEvent(
    const KeyEvent& event) {
    PublishPhysicalKeyTest(event);
    UpdateEmergencyState(event);

    if (emergency_bypass_) {
        ProcessResult result{};
        PublishMappedKeyTest(event, result);
        return result;
    }

    if (event.key == kEscape &&
        left_shift_down_ &&
        right_shift_down_) {
        ProcessResult result{};
        result.suppress_original = true;
        PublishMappedKeyTest(event, result);
        return result;
    }

    if (injection_recovery_pending_) {
        (void)TryRecoverInjection();
        ProcessResult result{};
        PublishMappedKeyTest(event, result);
        return result;
    }

    auto result = engine_.Process(event);
    PublishMappedKeyTest(event, result);
    ArmTapHoldTimer();
    if (key_test_mode_ != ipc::KeyTestMode::None) {
        result.suppress_original = true;
        result.synthetic_count = 0;
    }

    if (result.overflowed) {
        const bool testing =
            key_test_mode_ != ipc::KeyTestMode::None;
        if (!testing) {
            BeginInjectionRecovery(&result);
        } else {
            (void)engine_.ReleaseAll();
            CancelTapHoldTimer();
        }
        result.suppress_original = testing;
        result.synthetic_count = 0;
        result.overflowed = false;
        return result;
    }

    // Enqueue synthetic output for the dedicated injection thread. If the
    // queue is full or the worker reported a short SendInput, discard stale
    // queued output, reset the mapping state, and let this physical event
    // through instead of leaving a synthetic key held forever.
    if (result.synthetic_count > 0 &&
        !input_injector_.Enqueue(
            result.synthetic_events.data(),
            result.synthetic_count)) {
        BeginInjectionRecovery(&result);
        result.suppress_original = false;
        result.synthetic_count = 0;
        result.overflowed = false;
    }
    return result;
}

void CoreApp::PublishPhysicalKeyTest(
    const KeyEvent& event) noexcept {
    if (key_test_mode_ != ipc::KeyTestMode::Physical) {
        return;
    }
    PostKeyTestEvent(
        ipc::KeyTestEventKind::PhysicalKeyboard,
        static_cast<std::uint8_t>(event.transition),
        ipc::PackKeyTestPair(
            event.key.scan_code,
            static_cast<std::uint16_t>(event.key.prefix)));
}

void CoreApp::PublishMappedKeyTest(
    const KeyEvent& original,
    const ProcessResult& result) noexcept {
    if (key_test_mode_ != ipc::KeyTestMode::Mapped) {
        return;
    }

    if (!result.suppress_original) {
        PostKeyTestEvent(
            ipc::KeyTestEventKind::MappedKeyboard,
            static_cast<std::uint8_t>(original.transition),
            ipc::PackKeyTestPair(
                original.key.scan_code,
                static_cast<std::uint16_t>(
                    original.key.prefix)));
    }
    PublishMappedSynthetic(result);

    if (result.suppress_original &&
        result.synthetic_count == 0 &&
        original.transition == KeyTransition::Press) {
        PostKeyTestEvent(
            ipc::KeyTestEventKind::NoImmediateOutput,
            0,
            0);
    }
}

void CoreApp::PublishMappedSynthetic(
    const ProcessResult& result) noexcept {
    if (key_test_mode_ != ipc::KeyTestMode::Mapped) {
        return;
    }

    const auto count = std::min(
        result.synthetic_count,
        result.synthetic_events.size());
    for (std::size_t index = 0;
         index < count;
         ++index) {
        const auto& event = result.synthetic_events[index];
        switch (event.kind) {
        case SyntheticEventKind::Keyboard:
            PostKeyTestEvent(
                ipc::KeyTestEventKind::MappedKeyboard,
                static_cast<std::uint8_t>(event.transition),
                ipc::PackKeyTestPair(
                    event.key.scan_code,
                    static_cast<std::uint16_t>(
                        event.key.prefix)));
            break;
        case SyntheticEventKind::VirtualKey:
            PostKeyTestEvent(
                ipc::KeyTestEventKind::VirtualKey,
                static_cast<std::uint8_t>(event.transition),
                event.virtual_key);
            break;
        case SyntheticEventKind::MouseButton:
            PostKeyTestEvent(
                ipc::KeyTestEventKind::MouseButton,
                static_cast<std::uint8_t>(event.transition),
                static_cast<std::intptr_t>(
                    event.mouse_button));
            break;
        case SyntheticEventKind::MouseMove:
            PostKeyTestEvent(
                ipc::KeyTestEventKind::MouseMove,
                0,
                ipc::PackKeyTestPair(
                    static_cast<std::uint16_t>(
                        event.mouse_x),
                    static_cast<std::uint16_t>(
                        event.mouse_y)));
            break;
        case SyntheticEventKind::MouseWheel:
            PostKeyTestEvent(
                ipc::KeyTestEventKind::MouseWheel,
                event.horizontal ? 1 : 0,
                event.mouse_amount);
            break;
        }
    }
}

bool CoreApp::PostKeyTestEvent(
    const ipc::KeyTestEventKind kind,
    const std::uint8_t transition,
    const std::intptr_t data) noexcept {
    if (key_test_window_ == nullptr ||
        !IsWindow(key_test_window_)) {
        // Test mode suppresses all synthetic injection, so its runtime state
        // can be discarded safely when the subscriber disappears. Do not
        // release the live mapping state when no test subscription is active.
        if (key_test_mode_ != ipc::KeyTestMode::None) {
            (void)engine_.ReleaseAll();
            CancelTapHoldTimer();
        }
        key_test_window_ = nullptr;
        key_test_mode_ = ipc::KeyTestMode::None;
        return false;
    }

    if (PostMessageW(
            key_test_window_,
            ipc::kKeyTestEventMessage,
            ipc::PackKeyTestHeader(kind, transition),
            data) == FALSE) {
        // A subscriber can disappear between IsWindow and PostMessageW.
        // Test mode suppresses real input, so release its transient mapping
        // state immediately instead of waiting for another physical event.
        if (key_test_mode_ != ipc::KeyTestMode::None) {
            (void)engine_.ReleaseAll();
            CancelTapHoldTimer();
        }
        key_test_window_ = nullptr;
        key_test_mode_ = ipc::KeyTestMode::None;
        return false;
    }
    return true;
}

bool CoreApp::UpdateKeyTestSubscription(
    const COPYDATASTRUCT& copy_data) noexcept {
    if (copy_data.cbData !=
            sizeof(ipc::KeyTestSubscription) ||
        copy_data.lpData == nullptr) {
        return false;
    }
    const auto& subscription =
        *static_cast<const ipc::KeyTestSubscription*>(
            copy_data.lpData);
    if (subscription.magic != ipc::kMagic ||
        subscription.version != ipc::kProtocolVersion ||
        subscription.mode > ipc::KeyTestMode::Mapped) {
        return false;
    }

    if (subscription.mode == ipc::KeyTestMode::None) {
        if (key_test_mode_ != ipc::KeyTestMode::None) {
            // No synthetic events are injected while testing, so resetting
            // the engine is sufficient and the returned releases are only
            // bookkeeping rather than input that must be sent.
            (void)engine_.ReleaseAll();
            CancelTapHoldTimer();
        }
        key_test_window_ = nullptr;
        key_test_mode_ = ipc::KeyTestMode::None;
        return true;
    }

    const auto window = reinterpret_cast<HWND>(
        subscription.window_handle);
    std::array<wchar_t, 128> class_name{};
    if (window == nullptr ||
        !IsWindow(window) ||
        GetClassNameW(
            window,
            class_name.data(),
            static_cast<int>(class_name.size())) == 0 ||
        std::wstring_view(class_name.data()) !=
            ipc::kKeyTestWindowClass) {
        return false;
    }

    const auto releases = engine_.ReleaseAll();
    CancelTapHoldTimer();
    if (key_test_mode_ == ipc::KeyTestMode::None) {
        if (!input_injector_.Enqueue(releases)) {
            for (const auto& event : releases) {
                if (event.transition == KeyTransition::Release) {
                    AddInjectionCleanup(event);
                }
            }
            for (const auto& event : releases) {
                AddInjectionRestore(event);
            }
            injection_recovery_pending_ = true;
            (void)TryRecoverInjection();
            if (injection_recovery_pending_) {
                ArmInjectionRetryTimer();
            }
        }
    }
    key_test_window_ = window;
    key_test_mode_ = subscription.mode;
    return true;
}

bool CoreApp::EmergencyChordHeld() const noexcept {
    return left_shift_down_ &&
           right_shift_down_ &&
           escape_down_;
}

void CoreApp::ArmTapHoldTimer() {
    if (message_window_ == nullptr) {
        return;
    }

    CancelTapHoldTimer();

    const auto deadline = engine_.NextDeadlineMicros();
    if (!deadline.has_value()) {
        return;
    }

    const auto now = TimestampMicros();
    const auto remaining_us =
        *deadline > now ? *deadline - now : 0;
    // Round up without adding to remaining_us: a saturated deadline can be
    // UINT64_MAX, and adding 999 would wrap back to a very short timer.
    const auto rounded_ms =
        remaining_us / 1'000ULL +
        (remaining_us % 1'000ULL != 0 ? 1ULL : 0ULL);
    const auto delay_ms_64 =
        std::max<std::uint64_t>(1, rounded_ms);
    const auto delay_ms = static_cast<UINT>(
        std::min<std::uint64_t>(
            delay_ms_64,
            std::numeric_limits<UINT>::max()));

    tap_hold_timer_armed_ =
        SetTimer(
            message_window_,
            kTapHoldTimerId,
            delay_ms,
            nullptr) != 0;
}

void CoreApp::CancelTapHoldTimer() noexcept {
    if (tap_hold_timer_armed_ &&
        message_window_ != nullptr) {
        KillTimer(
            message_window_,
            kTapHoldTimerId);
    }
    tap_hold_timer_armed_ = false;
}

void CoreApp::ArmInjectionRetryTimer() {
    if (message_window_ == nullptr ||
        injection_retry_timer_armed_) {
        return;
    }
    injection_retry_timer_armed_ =
        SetTimer(
            message_window_,
            kInjectionRetryTimerId,
            kInjectionRetryDelayMs,
            nullptr) != 0;
}

void CoreApp::CancelInjectionRetryTimer() noexcept {
    if (injection_retry_timer_armed_ &&
        message_window_ != nullptr) {
        KillTimer(
            message_window_,
            kInjectionRetryTimerId);
    }
    injection_retry_timer_armed_ = false;
}

void CoreApp::AddInjectionCleanup(
    const SyntheticKeyEvent& event) {
    const bool stateful =
        event.kind == SyntheticEventKind::Keyboard ||
        event.kind == SyntheticEventKind::VirtualKey ||
        event.kind == SyntheticEventKind::MouseButton;
    if (!stateful) {
        return;
    }

    SyntheticKeyEvent release = event;
    release.transition = KeyTransition::Release;
    const auto same_identity = [&release](
                                  const SyntheticKeyEvent& existing) {
        if (existing.kind != release.kind) {
            return false;
        }
        if (existing.transition != KeyTransition::Release) {
            return false;
        }
        switch (release.kind) {
        case SyntheticEventKind::Keyboard:
            return existing.key == release.key;
        case SyntheticEventKind::VirtualKey:
            return existing.virtual_key == release.virtual_key;
        case SyntheticEventKind::MouseButton:
            return existing.mouse_button == release.mouse_button;
        default:
            return false;
        }
    };

    if (std::find_if(
            injection_cleanup_.begin(),
            injection_cleanup_.end(),
            same_identity) == injection_cleanup_.end()) {
        injection_cleanup_.push_back(release);
    }
}

void CoreApp::AddInjectionRestore(
    const SyntheticKeyEvent& event) {
    if (event.transition == KeyTransition::Release ||
        (event.kind != SyntheticEventKind::Keyboard &&
         event.kind != SyntheticEventKind::VirtualKey &&
         event.kind != SyntheticEventKind::MouseButton)) {
        return;
    }

    const auto duplicate = std::find_if(
        injection_cleanup_.begin(),
        injection_cleanup_.end(),
        [&event](const SyntheticKeyEvent& existing) {
            if (existing.kind != event.kind ||
                existing.transition == KeyTransition::Release) {
                return false;
            }
            switch (event.kind) {
            case SyntheticEventKind::Keyboard:
                return existing.key == event.key;
            case SyntheticEventKind::VirtualKey:
                return existing.virtual_key == event.virtual_key;
            case SyntheticEventKind::MouseButton:
                return existing.mouse_button == event.mouse_button;
            default:
                return false;
            }
        });
    if (duplicate == injection_cleanup_.end()) {
        injection_cleanup_.push_back(event);
    }
}

void CoreApp::BeginInjectionRecovery(
    const ProcessResult* failed_result) {
    if (!injection_recovery_pending_) {
        injection_cleanup_.clear();
        injection_recovery_pending_ = true;
    }

    if (failed_result != nullptr) {
        const auto count = std::min(
            failed_result->synthetic_count,
            failed_result->synthetic_events.size());
        for (std::size_t index = 0;
             index < count;
             ++index) {
            AddInjectionCleanup(
                failed_result->synthetic_events[index]);
        }
    }

    const auto releases = engine_.ReleaseAll();
    for (const auto& event : releases) {
        if (event.transition == KeyTransition::Release) {
            AddInjectionCleanup(event);
        }
    }
    for (const auto& event : releases) {
        AddInjectionRestore(event);
    }

    CancelTapHoldTimer();
    if (!TryRecoverInjection()) {
        ArmInjectionRetryTimer();
    }
}

bool CoreApp::TryRecoverInjection() {
    if (!injection_recovery_pending_) {
        return true;
    }
    if (!input_injector_.ReplacePending(injection_cleanup_)) {
        return false;
    }

    injection_cleanup_.clear();
    injection_recovery_pending_ = false;
    CancelInjectionRetryTimer();
    return true;
}

std::uint64_t CoreApp::TimestampMicros() const noexcept {
    if (performance_frequency_.QuadPart <= 0) {
        return 0;
    }

    LARGE_INTEGER counter{};
    if (QueryPerformanceCounter(&counter) == FALSE) {
        return 0;
    }

    const auto frequency =
        static_cast<std::uint64_t>(
            performance_frequency_.QuadPart);
    const auto value =
        static_cast<std::uint64_t>(counter.QuadPart);
    const auto seconds = value / frequency;
    const auto remainder = value % frequency;

    return seconds * 1'000'000ULL +
           (remainder * 1'000'000ULL) / frequency;
}

}  // namespace pckey::core
