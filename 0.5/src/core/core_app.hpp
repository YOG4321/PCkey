#pragma once

#include <Windows.h>

#include <string>
#include <vector>

#include "input_injector.hpp"
#include "keyboard_hook.hpp"
#include "pckey/config_store.hpp"
#include "pckey/ipc_protocol.hpp"
#include "pckey/mapping_engine.hpp"
#include "tray_icon.hpp"

namespace pckey::core {

class CoreApp {
public:
    CoreApp(HINSTANCE instance, bool background_start);
    ~CoreApp();

    CoreApp(const CoreApp&) = delete;
    CoreApp& operator=(const CoreApp&) = delete;

    int Run();

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param);

    LRESULT HandleMessage(
        HWND source_window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param);

    bool CreateWindows();
    void DestroyWindows() noexcept;
    bool StartServices();
    void StopServices() noexcept;
    void ShowTrayMenu();
    [[noreturn]] void ExitImmediately() noexcept;
    void CloseEditorWindows() noexcept;
    bool LaunchEditor();
    bool TryAddTrayIcon(bool show_final_error);
    void CancelTrayRetry() noexcept;
    [[nodiscard]] std::wstring TrayTooltip() const;
    void UpdateEmergencyState(const KeyEvent& event);
    void ActivateEmergencyBypass();
    void ArmTapHoldTimer();
    void CancelTapHoldTimer() noexcept;
    void ArmInjectionRetryTimer();
    void CancelInjectionRetryTimer() noexcept;
    void BeginInjectionRecovery(
        const ProcessResult* failed_result);
    bool TryRecoverInjection();
    void AddInjectionCleanup(
        const SyntheticKeyEvent& event);
    void AddInjectionRestore(
        const SyntheticKeyEvent& event);
    void PublishPhysicalKeyTest(const KeyEvent& event) noexcept;
    void PublishMappedKeyTest(
        const KeyEvent& original,
        const ProcessResult& result) noexcept;
    void PublishMappedSynthetic(
        const ProcessResult& result) noexcept;
    bool PostKeyTestEvent(
        ipc::KeyTestEventKind kind,
        std::uint8_t transition,
        std::intptr_t data) noexcept;
    bool UpdateKeyTestSubscription(
        const COPYDATASTRUCT& copy_data) noexcept;
    bool LoadConfiguredProfile(
        std::wstring_view requested_profile,
        bool show_error);

    [[nodiscard]] ProcessResult ProcessKeyboardEvent(
        const KeyEvent& event);

    [[nodiscard]] bool EmergencyChordHeld() const noexcept;
    [[nodiscard]] std::uint64_t TimestampMicros() const noexcept;

    static inline constexpr wchar_t kMessageWindowClassName[] =
        L"PCkey.Core.HiddenWindow";
    static inline constexpr wchar_t kTrayWindowClassName[] =
        L"PCkey.Core.TrayWindow";
    static inline constexpr UINT kTrayMessage = WM_APP + 1;
    static inline constexpr UINT_PTR kEmergencyTimerId = 1;
    static inline constexpr UINT_PTR kTapHoldTimerId = 2;
    static inline constexpr UINT_PTR kTrayRetryTimerId = 3;
    static inline constexpr UINT_PTR kInjectionRetryTimerId = 4;
    static inline constexpr UINT kEmergencyHoldMs = 2000;
    static inline constexpr UINT kInjectionRetryDelayMs = 25;
    static inline constexpr UINT kTrayRetryDelayMs = 1000;
    static inline constexpr UINT kTrayRetryLimit = 5;
    static inline constexpr UINT kOpenEditorMessage = WM_APP + 3;
    static inline constexpr UINT kHealthCheckMessage = WM_APP + 4;
    static inline constexpr UINT kShutdownMessage = WM_APP + 5;
    static inline constexpr UINT kInjectionFailureMessage = WM_APP + 6;
    static inline constexpr LRESULT kHealthCheckResult =
        0x50434B43;
    static inline constexpr UINT kCommandOpen = 1001;
    static inline constexpr UINT kCommandNormalMode = 1002;
    static inline constexpr UINT kCommandExit = 1003;

    HINSTANCE instance_{};
    HWND message_window_{};
    HWND tray_window_{};
    UINT taskbar_created_message_{};
    bool background_start_{};
    bool services_started_{};
    bool shutdown_started_{};
    bool emergency_timer_armed_{};
    bool tap_hold_timer_armed_{};
    bool injection_retry_timer_armed_{};
    bool tray_retry_armed_{};
    bool tray_error_reported_{};
    UINT tray_retry_attempts_{};
    bool emergency_bypass_{};
    bool left_shift_down_{};
    bool right_shift_down_{};
    bool escape_down_{};
    HWND key_test_window_{};
    ipc::KeyTestMode key_test_mode_{ipc::KeyTestMode::None};
    LARGE_INTEGER performance_frequency_{};
    bool injection_recovery_pending_{};
    std::vector<SyntheticKeyEvent> injection_cleanup_{};

    MappingEngine engine_{Profile::NormalMode()};
    KeyboardHook keyboard_hook_{};
    InputInjector input_injector_{};
    TrayIcon tray_icon_{};
    std::wstring current_profile_name_{
        std::wstring(kNormalModeName)};
    std::wstring startup_error_message_{
        L"PCkey 后台服务启动失败。"};
};

}  // namespace pckey::core
