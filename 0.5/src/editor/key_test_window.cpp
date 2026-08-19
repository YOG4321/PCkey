#include "key_test_window.hpp"

#include <array>
#include <cstdint>
#include <string>

#include "ipc_client.hpp"
#include "modal_message_loop.hpp"
#include "pckey/action.hpp"
#include "pckey/ipc_protocol.hpp"
#include "pckey/physical_key.hpp"

namespace pckey::editor {

namespace {

class KeyTestWindow {
public:
    KeyTestWindow(
        const HINSTANCE instance,
        const HWND owner)
        : instance_(instance),
          owner_(owner) {}

    void Run() {
        if (!Create()) {
            MessageBoxW(
                owner_,
                L"无法创建按键测试页面。",
                L"PCkey 错误",
                MB_OK | MB_ICONERROR);
            return;
        }

        EnableWindow(owner_, FALSE);
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        RunModalMessageLoop(window_, false);
        Unsubscribe();
        if (IsWindow(owner_)) {
            EnableWindow(owner_, TRUE);
            SetForegroundWindow(owner_);
        }
    }

private:
    static inline constexpr int kPhysicalModeId = 3401;
    static inline constexpr int kMappedModeId = 3402;
    static inline constexpr int kListId = 3403;
    static inline constexpr int kLatestId = 3404;
    static inline constexpr int kStatusId = 3405;
    static inline constexpr int kClearId = 3406;
    static inline constexpr int kCloseId = 3407;
    static inline constexpr std::size_t kMaximumHistory = 200;

    bool Create() {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.hInstance = instance_;
        window_class.lpfnWndProc = &WindowProcedure;
        window_class.lpszClassName =
            ipc::kKeyTestWindowClass;
        window_class.hCursor =
            LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(
                COLOR_WINDOW + 1);
        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        RECT owner_rect{};
        GetWindowRect(owner_, &owner_rect);
        constexpr int width = 760;
        constexpr int height = 590;
        window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            ipc::kKeyTestWindowClass,
            L"PCkey - 按键测试",
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            owner_rect.left +
                (owner_rect.right -
                 owner_rect.left - width) /
                    2,
            owner_rect.top +
                (owner_rect.bottom -
                 owner_rect.top - height) /
                    2,
            width,
            height,
            owner_,
            nullptr,
            instance_,
            this);
        return window_ != nullptr;
    }

    static LRESULT CALLBACK WindowProcedure(
        const HWND window,
        const UINT message,
        const WPARAM w_param,
        const LPARAM l_param) {
        auto* self = reinterpret_cast<KeyTestWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(
                    l_param);
            self = static_cast<KeyTestWindow*>(
                create->lpCreateParams);
            SetWindowLongPtrW(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        }
        return self != nullptr
                   ? self->HandleMessage(
                         message,
                         w_param,
                         l_param)
                   : DefWindowProcW(
                         window,
                         message,
                         w_param,
                         l_param);
    }

    LRESULT HandleMessage(
        const UINT message,
        const WPARAM w_param,
        const LPARAM l_param) {
        if (message == ipc::kKeyTestEventMessage) {
            AddEvent(w_param, l_param);
            return 0;
        }

        switch (message) {
        case WM_CREATE:
            if (!CreateControls()) {
                return -1;
            }
            Subscribe(ipc::KeyTestMode::Physical);
            return 0;
        case WM_COMMAND:
            if (HIWORD(w_param) == BN_CLICKED) {
                switch (LOWORD(w_param)) {
                case kPhysicalModeId:
                    Subscribe(ipc::KeyTestMode::Physical);
                    break;
                case kMappedModeId:
                    Subscribe(ipc::KeyTestMode::Mapped);
                    break;
                case kClearId:
                    Clear();
                    break;
                case kCloseId:
                    DestroyWindow(window_);
                    break;
                default:
                    break;
                }
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
            Unsubscribe();
            window_ = nullptr;
            return 0;
        default:
            break;
        }
        return DefWindowProcW(
            window_,
            message,
            w_param,
            l_param);
    }

    bool CreateControls() {
        const auto create =
            [this](
                const wchar_t* class_name,
                const wchar_t* text,
                const DWORD style,
                const int id,
                const int x,
                const int y,
                const int width,
                const int height) {
                const auto control = CreateWindowExW(
                    0,
                    class_name,
                    text,
                    WS_CHILD | WS_VISIBLE | style,
                    x,
                    y,
                    width,
                    height,
                    window_,
                    reinterpret_cast<HMENU>(
                        static_cast<INT_PTR>(id)),
                    instance_,
                    nullptr);
                if (control != nullptr) {
                    SendMessageW(
                        control,
                        WM_SETFONT,
                        reinterpret_cast<WPARAM>(
                            GetStockObject(
                                DEFAULT_GUI_FONT)),
                        TRUE);
                }
                return control;
            };

        create(
            L"STATIC",
            L"测试模式",
            SS_LEFT,
            0,
            24,
            22,
            90,
            24);
        physical_mode_ = create(
            L"BUTTON",
            L"物理按键（映射前）",
            BS_AUTORADIOBUTTON | WS_GROUP,
            kPhysicalModeId,
            118,
            16,
            180,
            30);
        mapped_mode_ = create(
            L"BUTTON",
            L"配置输出（映射后）",
            BS_AUTORADIOBUTTON,
            kMappedModeId,
            306,
            16,
            190,
            30);
        SendMessageW(
            physical_mode_,
            BM_SETCHECK,
            BST_CHECKED,
            0);

        create(
            L"STATIC",
            L"最近一次结果",
            SS_LEFT,
            0,
            24,
            68,
            110,
            24);
        latest_ = create(
            L"STATIC",
            L"请按下键盘上的任意按键",
            SS_LEFT | SS_CENTERIMAGE | WS_BORDER,
            kLatestId,
            24,
            94,
            690,
            58);
        create(
            L"STATIC",
            L"事件记录",
            SS_LEFT,
            0,
            24,
            170,
            100,
            24);
        list_ = create(
            L"LISTBOX",
            L"",
            WS_BORDER | WS_VSCROLL |
                LBS_NOINTEGRALHEIGHT,
            kListId,
            24,
            196,
            690,
            280);
        status_ = create(
            L"STATIC",
            L"测试期间键盘输出会被拦截，只显示结果，不会执行快捷操作。",
            SS_LEFT,
            kStatusId,
            24,
            492,
            530,
            25);
        create(
            L"BUTTON",
            L"清空",
            BS_PUSHBUTTON,
            kClearId,
            560,
            486,
            72,
            32);
        create(
            L"BUTTON",
            L"关闭",
            BS_DEFPUSHBUTTON,
            kCloseId,
            640,
            486,
            72,
            32);

        return physical_mode_ != nullptr &&
               mapped_mode_ != nullptr &&
               latest_ != nullptr &&
               list_ != nullptr &&
               status_ != nullptr;
    }

    void Subscribe(const ipc::KeyTestMode mode) {
        if (mode_ == mode && subscribed_) {
            return;
        }
        const auto was_subscribed = subscribed_;
        const auto previous_mode = mode_;
        std::wstring error;
        if (!IpcClient::SetKeyTestSubscription(
                window_,
                mode,
                error)) {
            // Keep the previous subscription bookkeeping intact. A failed
            // mode switch must still be unsubscribed when this window closes;
            // otherwise Core may continue suppressing keyboard input for a
            // window that no longer receives test events.
            subscribed_ = was_subscribed;
            mode_ = previous_mode;
            SetWindowTextW(status_, error.c_str());
            return;
        }

        mode_ = mode;
        subscribed_ = true;
        SendMessageW(
            physical_mode_,
            BM_SETCHECK,
            mode == ipc::KeyTestMode::Physical
                ? BST_CHECKED
                : BST_UNCHECKED,
            0);
        SendMessageW(
            mapped_mode_,
            BM_SETCHECK,
            mode == ipc::KeyTestMode::Mapped
                ? BST_CHECKED
                : BST_UNCHECKED,
            0);
        Clear();
        SetWindowTextW(
            status_,
            mode == ipc::KeyTestMode::Physical
                ? L"物理模式：显示硬件扫描码和原始按键名称；测试按键不会发送给其他窗口。"
                : L"配置模式：模拟当前已应用配置的输出并显示结果，不实际执行快捷键、媒体键或鼠标动作。");
    }

    void Unsubscribe() noexcept {
        if (!subscribed_) {
            return;
        }
        std::wstring ignored;
        IpcClient::SetKeyTestSubscription(
            nullptr,
            ipc::KeyTestMode::None,
            ignored);
        subscribed_ = false;
        mode_ = ipc::KeyTestMode::None;
    }

    void Clear() {
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        SetWindowTextW(
            latest_,
            L"请按下键盘上的任意按键");
    }

    void AddEvent(
        const WPARAM w_param,
        const LPARAM l_param) {
        const auto header =
            static_cast<std::uintptr_t>(w_param);
        const auto kind =
            static_cast<ipc::KeyTestEventKind>(
                header & 0xFFU);
        const auto transition =
            static_cast<KeyTransition>(
                (header >> 8U) & 0xFFU);
        const auto packed =
            static_cast<std::uint32_t>(l_param);

        std::wstring label;
        switch (kind) {
        case ipc::KeyTestEventKind::PhysicalKeyboard:
        case ipc::KeyTestEventKind::MappedKeyboard: {
            const PhysicalKey key{
                static_cast<std::uint16_t>(
                    packed & 0xFFFFU),
                static_cast<KeyPrefix>(
                    (packed >> 16U) & 0xFFFFU)};
            label =
                kind ==
                        ipc::KeyTestEventKind::
                            PhysicalKeyboard
                    ? L"物理输入："
                    : L"配置输出：";
            label += KeyLabel(key);
            label += TransitionLabel(transition);
            label += L"    [SC " +
                     std::to_wstring(key.scan_code);
            if (key.prefix == KeyPrefix::E0) {
                label += L" / E0";
            } else if (key.prefix == KeyPrefix::E1) {
                label += L" / E1";
            }
            label += L"]";
            break;
        }
        case ipc::KeyTestEventKind::VirtualKey:
            label = L"配置输出：" +
                    VirtualKeyLabel(
                        static_cast<std::uint16_t>(
                            packed & 0xFFFFU)) +
                    TransitionLabel(transition);
            break;
        case ipc::KeyTestEventKind::MouseButton:
            label = L"配置输出：" +
                    MouseButtonLabel(
                        static_cast<MouseButton>(
                            packed & 0xFFU)) +
                    TransitionLabel(transition);
            break;
        case ipc::KeyTestEventKind::MouseMove: {
            const auto x = static_cast<std::int16_t>(
                packed & 0xFFFFU);
            const auto y = static_cast<std::int16_t>(
                (packed >> 16U) & 0xFFFFU);
            label = L"配置输出：鼠标移动 (" +
                    std::to_wstring(x) + L", " +
                    std::to_wstring(y) + L")";
            break;
        }
        case ipc::KeyTestEventKind::MouseWheel:
            label =
                (header >> 8U) & 0xFFU
                    ? L"配置输出：水平滚轮 "
                    : L"配置输出：垂直滚轮 ";
            label += std::to_wstring(
                static_cast<std::int32_t>(l_param));
            break;
        case ipc::KeyTestEventKind::NoImmediateOutput:
            label =
                L"配置输出：暂无直接输出（禁用、Layer、Combo或正在等待Tap Dance判定）";
            break;
        }

        if (label.empty()) {
            return;
        }
        SetWindowTextW(latest_, label.c_str());
        SendMessageW(
            list_,
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(label.c_str()));
        while (static_cast<std::size_t>(
                   SendMessageW(
                       list_,
                       LB_GETCOUNT,
                       0,
                       0)) > kMaximumHistory) {
            SendMessageW(list_, LB_DELETESTRING, 0, 0);
        }
        const auto count = SendMessageW(
            list_,
            LB_GETCOUNT,
            0,
            0);
        if (count > 0) {
            SendMessageW(
                list_,
                LB_SETTOPINDEX,
                count - 1,
                0);
        }
    }

    static std::wstring KeyLabel(
        const PhysicalKey key) {
        LONG data =
            static_cast<LONG>(key.scan_code) << 16;
        if (key.prefix != KeyPrefix::None) {
            data |= 1L << 24;
        }
        std::array<wchar_t, 64> buffer{};
        if (GetKeyNameTextW(
                data,
                buffer.data(),
                static_cast<int>(buffer.size())) > 0) {
            return buffer.data();
        }
        return L"未知按键";
    }

    static std::wstring TransitionLabel(
        const KeyTransition transition) {
        switch (transition) {
        case KeyTransition::Press:
            return L"  ↓";
        case KeyTransition::Repeat:
            return L"  ↻";
        case KeyTransition::Release:
            return L"  ↑";
        }
        return {};
    }

    static std::wstring VirtualKeyLabel(
        const std::uint16_t key) {
        switch (key) {
        case VK_VOLUME_MUTE:
            return L"静音";
        case VK_VOLUME_DOWN:
            return L"音量－";
        case VK_VOLUME_UP:
            return L"音量＋";
        case VK_MEDIA_PLAY_PAUSE:
            return L"播放/暂停";
        case VK_MEDIA_PREV_TRACK:
            return L"上一曲";
        case VK_MEDIA_NEXT_TRACK:
            return L"下一曲";
        case VK_MEDIA_STOP:
            return L"停止播放";
        case VK_BROWSER_BACK:
            return L"浏览器后退";
        case VK_BROWSER_FORWARD:
            return L"浏览器前进";
        case VK_BROWSER_HOME:
            return L"浏览器主页";
        case VK_LAUNCH_MAIL:
            return L"邮件";
        case VK_LAUNCH_APP2:
            return L"计算器";
        default:
            return L"虚拟键 VK " +
                   std::to_wstring(key);
        }
    }

    static std::wstring MouseButtonLabel(
        const MouseButton button) {
        switch (button) {
        case MouseButton::Left:
            return L"鼠标左键";
        case MouseButton::Right:
            return L"鼠标右键";
        case MouseButton::Middle:
            return L"鼠标中键";
        case MouseButton::X1:
            return L"鼠标X1";
        case MouseButton::X2:
            return L"鼠标X2";
        }
        return L"鼠标键";
    }

    HINSTANCE instance_{};
    HWND owner_{};
    HWND window_{};
    HWND physical_mode_{};
    HWND mapped_mode_{};
    HWND latest_{};
    HWND list_{};
    HWND status_{};
    ipc::KeyTestMode mode_{ipc::KeyTestMode::None};
    bool subscribed_{};
};

}  // namespace

void ShowKeyTestWindow(
    const HWND owner,
    const HINSTANCE instance) {
    KeyTestWindow(instance, owner).Run();
}

}  // namespace pckey::editor
