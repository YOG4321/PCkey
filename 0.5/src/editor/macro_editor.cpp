#include "macro_editor.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

#include "modal_message_loop.hpp"
#include "pckey/physical_key.hpp"

namespace pckey::editor {

namespace {

class MacroEditorWindow {
public:
    MacroEditorWindow(
        const HINSTANCE instance,
        const HWND owner,
        MacroDefinition definition)
        : instance_(instance),
          owner_(owner),
          result_(std::move(definition)) {}

    std::optional<MacroDefinition> Run() {
        if (!Create()) {
            return std::nullopt;
        }

        EnableWindow(owner_, FALSE);
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);

        RunModalMessageLoop(window_);

        StopRecording();
        if (IsWindow(owner_)) {
            EnableWindow(owner_, TRUE);
            SetForegroundWindow(owner_);
        }
        return accepted_
                   ? std::optional<MacroDefinition>(
                         std::move(result_))
                   : std::nullopt;
    }

private:
    static inline constexpr wchar_t kClassName[] =
        L"PCkey.Editor.MacroWindow";
    static inline constexpr int kNameId = 3001;
    static inline constexpr int kListId = 3002;
    static inline constexpr int kRecordId = 3003;
    static inline constexpr int kStopId = 3004;
    static inline constexpr int kRemoveId = 3005;
    static inline constexpr int kClearId = 3006;
    static inline constexpr int kSaveId = 3007;
    static inline constexpr int kCancelId = 3008;
    static inline constexpr int kStatusId = 3009;

    bool Create() {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.hInstance = instance_;
        window_class.lpfnWndProc = &WindowProcedure;
        window_class.lpszClassName = kClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        if (RegisterClassExW(&window_class) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        RECT owner_rect{};
        GetWindowRect(owner_, &owner_rect);
        constexpr int width = 610;
        constexpr int height = 560;
        const auto x =
            owner_rect.left +
            (owner_rect.right - owner_rect.left - width) / 2;
        const auto y =
            owner_rect.top +
            (owner_rect.bottom - owner_rect.top - height) / 2;

        window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kClassName,
            L"PCkey - 按键宏录制",
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x,
            y,
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
        auto* self = reinterpret_cast<MacroEditorWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(l_param);
            self = static_cast<MacroEditorWindow*>(
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
        switch (message) {
        case WM_CREATE:
            return CreateControls() ? 0 : -1;

        case kRefreshMessage:
            RefreshList();
            return 0;

        case WM_COMMAND:
            if (HIWORD(w_param) == BN_CLICKED) {
                switch (LOWORD(w_param)) {
                case kRecordId:
                    StartRecording();
                    break;
                case kStopId:
                    StopRecording();
                    break;
                case kRemoveId:
                    if (!result_.events.empty()) {
                        result_.events.pop_back();
                        RefreshList();
                    }
                    break;
                case kClearId:
                    result_.events.clear();
                    RefreshList();
                    break;
                case kSaveId:
                    Save();
                    break;
                case kCancelId:
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
            StopRecording();
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
                            GetStockObject(DEFAULT_GUI_FONT)),
                        TRUE);
                }
                return control;
            };

        create(
            L"STATIC",
            L"宏名称",
            SS_LEFT,
            0,
            20,
            18,
            70,
            24);
        name_edit_ = create(
            L"EDIT",
            result_.name.c_str(),
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            kNameId,
            92,
            15,
            300,
            28);
        list_ = create(
            L"LISTBOX",
            L"",
            WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            kListId,
            20,
            58,
            552,
            360);
        record_button_ = create(
            L"BUTTON",
            L"开始录制",
            BS_PUSHBUTTON,
            kRecordId,
            20,
            432,
            96,
            31);
        stop_button_ = create(
            L"BUTTON",
            L"停止录制",
            BS_PUSHBUTTON,
            kStopId,
            122,
            432,
            96,
            31);
        create(
            L"BUTTON",
            L"删除最后一步",
            BS_PUSHBUTTON,
            kRemoveId,
            232,
            432,
            112,
            31);
        create(
            L"BUTTON",
            L"清空",
            BS_PUSHBUTTON,
            kClearId,
            350,
            432,
            72,
            31);
        status_ = create(
            L"STATIC",
            L"宏只记录按键按下、释放和事件间隔。",
            SS_LEFT,
            kStatusId,
            20,
            475,
            390,
            25);
        create(
            L"BUTTON",
            L"保存宏",
            BS_DEFPUSHBUTTON,
            kSaveId,
            430,
            474,
            92,
            32);
        create(
            L"BUTTON",
            L"取消",
            BS_PUSHBUTTON,
            kCancelId,
            528,
            474,
            62,
            32);

        EnableWindow(stop_button_, FALSE);
        RefreshList();
        return name_edit_ != nullptr &&
               list_ != nullptr &&
               record_button_ != nullptr &&
               stop_button_ != nullptr &&
               status_ != nullptr;
    }

    void StartRecording() {
        if (recording_) {
            return;
        }

        physical_down_.fill(false);
        last_event_ms_ = GetTickCount64();
        active_instance_ = this;
        hook_ = SetWindowsHookExW(
            WH_KEYBOARD_LL,
            &KeyboardHookProcedure,
            GetModuleHandleW(nullptr),
            0);
        if (hook_ == nullptr) {
            active_instance_ = nullptr;
            SetWindowTextW(
                status_,
                L"无法安装录制钩子。");
            return;
        }

        recording_ = true;
        EnableWindow(record_button_, FALSE);
        EnableWindow(stop_button_, TRUE);
        SetWindowTextW(
            status_,
            L"正在录制。请使用鼠标点击“停止录制”。");
    }

    void StopRecording() noexcept {
        if (hook_ != nullptr) {
            UnhookWindowsHookEx(hook_);
            hook_ = nullptr;
        }
        if (active_instance_ == this) {
            active_instance_ = nullptr;
        }
        if (recording_) {
            recording_ = false;
            EnableWindow(record_button_, TRUE);
            EnableWindow(stop_button_, FALSE);
            SetWindowTextW(status_, L"录制已停止，可检查并保存。");
        }
    }

    static LRESULT CALLBACK KeyboardHookProcedure(
        const int code,
        const WPARAM message,
        const LPARAM data) {
        if (code < 0 || active_instance_ == nullptr) {
            return CallNextHookEx(nullptr, code, message, data);
        }
        return active_instance_->HandleKeyboard(
            message,
            data);
    }

    LRESULT HandleKeyboard(
        const WPARAM message,
        const LPARAM data) {
        const auto* keyboard =
            reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if (keyboard == nullptr ||
            (keyboard->flags & LLKHF_INJECTED) != 0) {
            return CallNextHookEx(hook_, 0, message, data);
        }

        const bool down =
            message == WM_KEYDOWN ||
            message == WM_SYSKEYDOWN;
        const bool up =
            message == WM_KEYUP ||
            message == WM_SYSKEYUP;
        if (!down && !up) {
            return CallNextHookEx(hook_, 0, message, data);
        }

        KeyPrefix prefix = KeyPrefix::None;
        if ((keyboard->flags & LLKHF_EXTENDED) != 0) {
            prefix = KeyPrefix::E0;
        } else if (keyboard->vkCode == VK_PAUSE) {
            prefix = KeyPrefix::E1;
        }
        const PhysicalKey key{
            static_cast<std::uint16_t>(
                keyboard->scanCode & 0xFFU),
            prefix};
        if (!key.IsValid()) {
            return CallNextHookEx(
                hook_,
                0,
                message,
                data);
        }
        const auto index = ToKeyIndex(key);

        if (down && physical_down_[index]) {
            return 1;
        }
        physical_down_[index] = down;

        if (result_.events.size() >= kMaximumMacroEvents) {
            PostMessageW(
                window_,
                WM_COMMAND,
                MAKEWPARAM(kStopId, BN_CLICKED),
                reinterpret_cast<LPARAM>(stop_button_));
            return 1;
        }

        const auto now = GetTickCount64();
        const auto elapsed =
            std::min<ULONGLONG>(
                now - last_event_ms_,
                std::numeric_limits<std::uint16_t>::max());
        result_.events.push_back(
            MacroEvent{
                static_cast<std::uint16_t>(elapsed),
                key,
                down
                    ? KeyTransition::Press
                    : KeyTransition::Release});
        last_event_ms_ = now;
        PostMessageW(window_, kRefreshMessage, 0, 0);
        return 1;
    }

    void RefreshList() {
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        for (const auto& event : result_.events) {
            const auto label =
                std::to_wstring(event.delay_ms) +
                L" ms    " +
                KeyLabel(event.key) +
                (event.transition == KeyTransition::Press
                     ? L"  ↓"
                     : L"  ↑");
            SendMessageW(
                list_,
                LB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(label.c_str()));
        }
        const auto count = static_cast<int>(
            SendMessageW(list_, LB_GETCOUNT, 0, 0));
        if (count > 0) {
            SendMessageW(
                list_,
                LB_SETTOPINDEX,
                static_cast<WPARAM>(count - 1),
                0);
        }
    }

    static std::wstring KeyLabel(
        const PhysicalKey key) {
        LONG key_name_data =
            static_cast<LONG>(key.scan_code) << 16;
        if (key.prefix != KeyPrefix::None) {
            key_name_data |= 1L << 24;
        }
        std::array<wchar_t, 64> buffer{};
        if (GetKeyNameTextW(
                key_name_data,
                buffer.data(),
                static_cast<int>(buffer.size())) > 0) {
            return buffer.data();
        }
        return L"SC" + std::to_wstring(key.scan_code);
    }

    void Save() {
        StopRecording();
        const auto length = GetWindowTextLengthW(name_edit_);
        std::wstring name(
            static_cast<std::size_t>(length) + 1,
            L'\0');
        GetWindowTextW(
            name_edit_,
            name.data(),
            length + 1);
        name.resize(static_cast<std::size_t>(length));
        if (name.empty()) {
            SetWindowTextW(status_, L"宏名称不能为空。");
            return;
        }
        if (result_.events.empty()) {
            SetWindowTextW(status_, L"请至少录制一个按键事件。");
            return;
        }
        result_.name = std::move(name);
        accepted_ = true;
        DestroyWindow(window_);
    }

    static inline constexpr UINT kRefreshMessage = WM_APP + 20;
    static inline MacroEditorWindow* active_instance_{};

    HINSTANCE instance_{};
    HWND owner_{};
    HWND window_{};
    HWND name_edit_{};
    HWND list_{};
    HWND record_button_{};
    HWND stop_button_{};
    HWND status_{};
    HHOOK hook_{};
    bool recording_{};
    bool accepted_{};
    ULONGLONG last_event_ms_{};
    std::array<bool, kPhysicalKeySlotCount> physical_down_{};
    MacroDefinition result_{};
};

}  // namespace

std::optional<MacroDefinition> ShowMacroEditor(
    const HWND owner,
    const HINSTANCE instance,
    const std::uint16_t macro_id,
    std::wstring default_name) {
    return MacroEditorWindow(
               instance,
               owner,
               MacroDefinition{
                   macro_id,
                   std::move(default_name),
                   {}})
        .Run();
}

std::optional<MacroDefinition> ShowMacroEditor(
    const HWND owner,
    const HINSTANCE instance,
    const MacroDefinition& definition) {
    return MacroEditorWindow(
               instance,
               owner,
               definition)
        .Run();
}

}  // namespace pckey::editor
