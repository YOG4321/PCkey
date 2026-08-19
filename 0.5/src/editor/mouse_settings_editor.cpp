#include "mouse_settings_editor.hpp"

#include <array>
#include <cwchar>
#include <limits>

#include "modal_message_loop.hpp"

namespace pckey::editor {

namespace {

class MouseSettingsWindow {
public:
    MouseSettingsWindow(
        const HINSTANCE instance,
        const HWND owner,
        const MouseSettings& current)
        : instance_(instance),
          owner_(owner),
          result_(current) {}

    std::optional<MouseSettings> Run() {
        if (!Create()) {
            return std::nullopt;
        }
        EnableWindow(owner_, FALSE);
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        RunModalMessageLoop(window_);
        if (IsWindow(owner_)) {
            EnableWindow(owner_, TRUE);
            SetForegroundWindow(owner_);
        }
        return accepted_
                   ? std::optional<MouseSettings>(result_)
                   : std::nullopt;
    }

private:
    static inline constexpr wchar_t kClassName[] =
        L"PCkey.Editor.MouseSettingsWindow";
    static inline constexpr int kInitialId = 3401;
    static inline constexpr int kMaximumId = 3402;
    static inline constexpr int kAccelerationId = 3403;
    static inline constexpr int kRepeatId = 3404;
    static inline constexpr int kWheelId = 3405;
    static inline constexpr int kPreciseId = 3410;
    static inline constexpr int kStandardId = 3411;
    static inline constexpr int kFastId = 3412;
    static inline constexpr int kSaveId = 3413;
    static inline constexpr int kCancelId = 3414;
    static inline constexpr int kStatusId = 3415;

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
        constexpr int width = 520;
        constexpr int height = 430;
        window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kClassName,
            L"PCkey - 鼠标键参数",
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            owner_rect.left +
                (owner_rect.right - owner_rect.left - width) / 2,
            owner_rect.top +
                (owner_rect.bottom - owner_rect.top - height) / 2,
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
        auto* self = reinterpret_cast<MouseSettingsWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(l_param);
            self = static_cast<MouseSettingsWindow*>(
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
        case WM_COMMAND:
            if (HIWORD(w_param) == BN_CLICKED) {
                switch (LOWORD(w_param)) {
                case kPreciseId:
                    SetPreset(2, 8, 700);
                    break;
                case kStandardId:
                    SetPreset(4, 18, 500);
                    break;
                case kFastId:
                    SetPreset(8, 32, 350);
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
            L"速度预设",
            SS_LEFT,
            0,
            24,
            24,
            90,
            24);
        create(
            L"BUTTON",
            L"精细",
            BS_PUSHBUTTON,
            kPreciseId,
            120,
            18,
            85,
            30);
        create(
            L"BUTTON",
            L"标准",
            BS_PUSHBUTTON,
            kStandardId,
            212,
            18,
            85,
            30);
        create(
            L"BUTTON",
            L"快速",
            BS_PUSHBUTTON,
            kFastId,
            304,
            18,
            85,
            30);

        const std::array labels{
            std::wstring_view(L"初始速度"),
            std::wstring_view(L"最大速度"),
            std::wstring_view(L"加速时间(ms)"),
            std::wstring_view(L"重复间隔(ms)"),
            std::wstring_view(L"滚轮步长")};
        const std::array ids{
            kInitialId,
            kMaximumId,
            kAccelerationId,
            kRepeatId,
            kWheelId};
        const std::array values{
            result_.initial_speed,
            result_.maximum_speed,
            result_.acceleration_ms,
            result_.repeat_ms,
            static_cast<std::uint16_t>(
                std::abs(result_.wheel_step))};

        for (std::size_t index = 0; index < ids.size(); ++index) {
            const auto y = 78 + static_cast<int>(index) * 46;
            create(
                L"STATIC",
                labels[index].data(),
                SS_LEFT,
                0,
                24,
                y + 5,
                130,
                24);
            edits_[index] = create(
                L"EDIT",
                std::to_wstring(values[index]).c_str(),
                WS_BORDER | ES_NUMBER | ES_CENTER,
                ids[index],
                165,
                y,
                90,
                27);
        }

        status_ = create(
            L"STATIC",
            L"移动键按住后平滑加速，松开立即停止。",
            SS_LEFT,
            kStatusId,
            24,
            315,
            330,
            24);
        create(
            L"BUTTON",
            L"保存参数",
            BS_DEFPUSHBUTTON,
            kSaveId,
            350,
            320,
            92,
            32);
        create(
            L"BUTTON",
            L"取消",
            BS_PUSHBUTTON,
            kCancelId,
            448,
            320,
            55,
            32);
        return edits_[0] != nullptr && status_ != nullptr;
    }

    void SetPreset(
        const std::uint16_t initial,
        const std::uint16_t maximum,
        const std::uint16_t acceleration) {
        SetWindowTextW(
            edits_[0],
            std::to_wstring(initial).c_str());
        SetWindowTextW(
            edits_[1],
            std::to_wstring(maximum).c_str());
        SetWindowTextW(
            edits_[2],
            std::to_wstring(acceleration).c_str());
    }

    static std::optional<unsigned int> ReadNumber(
        const HWND edit) {
        std::array<wchar_t, 32> text{};
        GetWindowTextW(
            edit,
            text.data(),
            static_cast<int>(text.size()));
        wchar_t* end = nullptr;
        const auto value =
            std::wcstoul(text.data(), &end, 10);
        if (end == text.data() || *end != L'\0' ||
            value >
                std::numeric_limits<unsigned int>::max()) {
            return std::nullopt;
        }
        return static_cast<unsigned int>(value);
    }

    static std::optional<long> ReadSignedNumber(
        const HWND edit) {
        std::array<wchar_t, 32> text{};
        GetWindowTextW(
            edit,
            text.data(),
            static_cast<int>(text.size()));
        wchar_t* end = nullptr;
        const auto value =
            std::wcstol(text.data(), &end, 10);
        if (end == text.data() || *end != L'\0') {
            return std::nullopt;
        }
        return value;
    }

    void Save() {
        std::array<unsigned int, 4> values{};
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto value = ReadNumber(edits_[index]);
            if (!value.has_value()) {
                SetWindowTextW(status_, L"参数必须为有效数字。");
                return;
            }
            values[index] = *value;
        }

        const auto wheel_step = ReadSignedNumber(edits_[4]);
        if (!wheel_step.has_value()) {
            SetWindowTextW(status_, L"滚轮步进必须为有效整数。");
            return;
        }

        if (values[0] == 0 ||
            values[1] < values[0] ||
            values[1] > 100 ||
            values[2] > 5000 ||
            values[3] < 5 ||
            values[3] > 100 ||
            *wheel_step == 0 ||
            *wheel_step <
                std::numeric_limits<std::int16_t>::min() ||
            *wheel_step >
                std::numeric_limits<std::int16_t>::max()) {
            SetWindowTextW(status_, L"参数超出允许范围。");
            return;
        }

        result_ = MouseSettings{
            static_cast<std::uint16_t>(values[0]),
            static_cast<std::uint16_t>(values[1]),
            static_cast<std::uint16_t>(values[2]),
            static_cast<std::uint16_t>(values[3]),
            static_cast<std::int16_t>(*wheel_step)};
        accepted_ = true;
        DestroyWindow(window_);
    }

    HINSTANCE instance_{};
    HWND owner_{};
    HWND window_{};
    std::array<HWND, 5> edits_{};
    HWND status_{};
    MouseSettings result_{};
    bool accepted_{};
};

}  // namespace

std::optional<MouseSettings> ShowMouseSettingsEditor(
    const HWND owner,
    const HINSTANCE instance,
    const MouseSettings& current) {
    return MouseSettingsWindow(
               instance,
               owner,
               current)
        .Run();
}

}  // namespace pckey::editor
