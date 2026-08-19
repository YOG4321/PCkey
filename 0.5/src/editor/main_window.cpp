#include "main_window.hpp"

#include <d2d1helper.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

#include "combo_editor.hpp"
#include "action_catalog.hpp"
#include "ipc_client.hpp"
#include "key_test_window.hpp"
#include "macro_editor.hpp"
#include "mouse_settings_editor.hpp"
#include "override_editor.hpp"
#include "pckey/version.hpp"
#include "resource.h"
#include "startup_trace.hpp"
#include "tap_dance_editor.hpp"

namespace pckey::editor {

namespace {

struct KeySpec {
    std::wstring_view label;
    PhysicalKey key;
    float width;
};

struct CategorySpec {
    MainWindow::ActionCategory category;
    std::wstring_view label;
};

const D2D1_COLOR_F kBackground = D2D1::ColorF(0xF4F6FA);
const D2D1_COLOR_F kCard = D2D1::ColorF(0xFFFFFF);
const D2D1_COLOR_F kText = D2D1::ColorF(0x172033);
const D2D1_COLOR_F kMutedText = D2D1::ColorF(0x687386);
const D2D1_COLOR_F kKey = D2D1::ColorF(0xEEF2F7);
const D2D1_COLOR_F kAccent = D2D1::ColorF(0x6558E8);
const D2D1_COLOR_F kAccentSoft = D2D1::ColorF(0xE9E7FF);
const D2D1_COLOR_F kWhite = D2D1::ColorF(0xFFFFFF);
const D2D1_COLOR_F kBorder = D2D1::ColorF(0xDCE2EA);
const D2D1_COLOR_F kDisabled = D2D1::ColorF(0xF6F7F9);
const D2D1_COLOR_F kWarning = D2D1::ColorF(0xD97706);

constexpr std::array kFunctionRow{
    KeySpec{L"Esc", {0x01, KeyPrefix::None}, 1.0F},
    KeySpec{L"F1", {0x3B, KeyPrefix::None}, 1.0F},
    KeySpec{L"F2", {0x3C, KeyPrefix::None}, 1.0F},
    KeySpec{L"F3", {0x3D, KeyPrefix::None}, 1.0F},
    KeySpec{L"F4", {0x3E, KeyPrefix::None}, 1.0F},
    KeySpec{L"F5", {0x3F, KeyPrefix::None}, 1.0F},
    KeySpec{L"F6", {0x40, KeyPrefix::None}, 1.0F},
    KeySpec{L"F7", {0x41, KeyPrefix::None}, 1.0F},
    KeySpec{L"F8", {0x42, KeyPrefix::None}, 1.0F},
    KeySpec{L"F9", {0x43, KeyPrefix::None}, 1.0F},
    KeySpec{L"F10", {0x44, KeyPrefix::None}, 1.0F},
    KeySpec{L"F11", {0x57, KeyPrefix::None}, 1.0F},
    KeySpec{L"F12", {0x58, KeyPrefix::None}, 1.0F},
};

constexpr std::array kNumberRow{
    KeySpec{L"`", {0x29, KeyPrefix::None}, 1.0F},
    KeySpec{L"1", {0x02, KeyPrefix::None}, 1.0F},
    KeySpec{L"2", {0x03, KeyPrefix::None}, 1.0F},
    KeySpec{L"3", {0x04, KeyPrefix::None}, 1.0F},
    KeySpec{L"4", {0x05, KeyPrefix::None}, 1.0F},
    KeySpec{L"5", {0x06, KeyPrefix::None}, 1.0F},
    KeySpec{L"6", {0x07, KeyPrefix::None}, 1.0F},
    KeySpec{L"7", {0x08, KeyPrefix::None}, 1.0F},
    KeySpec{L"8", {0x09, KeyPrefix::None}, 1.0F},
    KeySpec{L"9", {0x0A, KeyPrefix::None}, 1.0F},
    KeySpec{L"0", {0x0B, KeyPrefix::None}, 1.0F},
    KeySpec{L"-", {0x0C, KeyPrefix::None}, 1.0F},
    KeySpec{L"=", {0x0D, KeyPrefix::None}, 1.0F},
    KeySpec{L"Backspace", {0x0E, KeyPrefix::None}, 2.0F},
};

constexpr std::array kTopRow{
    KeySpec{L"Tab", {0x0F, KeyPrefix::None}, 1.5F},
    KeySpec{L"Q", {0x10, KeyPrefix::None}, 1.0F},
    KeySpec{L"W", {0x11, KeyPrefix::None}, 1.0F},
    KeySpec{L"E", {0x12, KeyPrefix::None}, 1.0F},
    KeySpec{L"R", {0x13, KeyPrefix::None}, 1.0F},
    KeySpec{L"T", {0x14, KeyPrefix::None}, 1.0F},
    KeySpec{L"Y", {0x15, KeyPrefix::None}, 1.0F},
    KeySpec{L"U", {0x16, KeyPrefix::None}, 1.0F},
    KeySpec{L"I", {0x17, KeyPrefix::None}, 1.0F},
    KeySpec{L"O", {0x18, KeyPrefix::None}, 1.0F},
    KeySpec{L"P", {0x19, KeyPrefix::None}, 1.0F},
    KeySpec{L"[", {0x1A, KeyPrefix::None}, 1.0F},
    KeySpec{L"]", {0x1B, KeyPrefix::None}, 1.0F},
    KeySpec{L"\\", {0x2B, KeyPrefix::None}, 1.5F},
};

constexpr std::array kHomeRow{
    KeySpec{L"Caps", {0x3A, KeyPrefix::None}, 1.75F},
    KeySpec{L"A", {0x1E, KeyPrefix::None}, 1.0F},
    KeySpec{L"S", {0x1F, KeyPrefix::None}, 1.0F},
    KeySpec{L"D", {0x20, KeyPrefix::None}, 1.0F},
    KeySpec{L"F", {0x21, KeyPrefix::None}, 1.0F},
    KeySpec{L"G", {0x22, KeyPrefix::None}, 1.0F},
    KeySpec{L"H", {0x23, KeyPrefix::None}, 1.0F},
    KeySpec{L"J", {0x24, KeyPrefix::None}, 1.0F},
    KeySpec{L"K", {0x25, KeyPrefix::None}, 1.0F},
    KeySpec{L"L", {0x26, KeyPrefix::None}, 1.0F},
    KeySpec{L";", {0x27, KeyPrefix::None}, 1.0F},
    KeySpec{L"'", {0x28, KeyPrefix::None}, 1.0F},
    KeySpec{L"Enter", {0x1C, KeyPrefix::None}, 2.25F},
};

constexpr std::array kBottomRow{
    KeySpec{L"LShift", {0x2A, KeyPrefix::None}, 2.25F},
    KeySpec{L"Z", {0x2C, KeyPrefix::None}, 1.0F},
    KeySpec{L"X", {0x2D, KeyPrefix::None}, 1.0F},
    KeySpec{L"C", {0x2E, KeyPrefix::None}, 1.0F},
    KeySpec{L"V", {0x2F, KeyPrefix::None}, 1.0F},
    KeySpec{L"B", {0x30, KeyPrefix::None}, 1.0F},
    KeySpec{L"N", {0x31, KeyPrefix::None}, 1.0F},
    KeySpec{L"M", {0x32, KeyPrefix::None}, 1.0F},
    KeySpec{L",", {0x33, KeyPrefix::None}, 1.0F},
    KeySpec{L".", {0x34, KeyPrefix::None}, 1.0F},
    KeySpec{L"/", {0x35, KeyPrefix::None}, 1.0F},
    KeySpec{L"RShift", {0x36, KeyPrefix::None}, 2.75F},
};

constexpr std::array kModifierRow{
    KeySpec{L"LCtrl", {0x1D, KeyPrefix::None}, 1.25F},
    KeySpec{L"LWin", {0x5B, KeyPrefix::E0}, 1.25F},
    KeySpec{L"LAlt", {0x38, KeyPrefix::None}, 1.25F},
    KeySpec{L"Space", {0x39, KeyPrefix::None}, 6.25F},
    KeySpec{L"RAlt", {0x38, KeyPrefix::E0}, 1.25F},
    KeySpec{L"RWin", {0x5C, KeyPrefix::E0}, 1.25F},
    KeySpec{L"Menu", {0x5D, KeyPrefix::E0}, 1.25F},
    KeySpec{L"RCtrl", {0x1D, KeyPrefix::E0}, 1.25F},
};

constexpr std::array kNavigationKeys{
    KeySpec{L"Insert", {0x52, KeyPrefix::E0}, 1.0F},
    KeySpec{L"Home", {0x47, KeyPrefix::E0}, 1.0F},
    KeySpec{L"PgUp", {0x49, KeyPrefix::E0}, 1.0F},
    KeySpec{L"Delete", {0x53, KeyPrefix::E0}, 1.0F},
    KeySpec{L"End", {0x4F, KeyPrefix::E0}, 1.0F},
    KeySpec{L"PgDn", {0x51, KeyPrefix::E0}, 1.0F},
};

constexpr std::array kArrowKeys{
    KeySpec{L"↑", {0x48, KeyPrefix::E0}, 1.0F},
    KeySpec{L"←", {0x4B, KeyPrefix::E0}, 1.0F},
    KeySpec{L"↓", {0x50, KeyPrefix::E0}, 1.0F},
    KeySpec{L"→", {0x4D, KeyPrefix::E0}, 1.0F},
};

constexpr std::array kSystemKeys{
    KeySpec{L"PrtSc", {0x37, KeyPrefix::E0}, 1.0F},
    KeySpec{L"Scroll", {0x46, KeyPrefix::None}, 1.0F},
    KeySpec{L"Pause", {0x45, KeyPrefix::E1}, 1.0F},
};

constexpr std::array kNumpadKeys{
    KeySpec{L"Num", {0x45, KeyPrefix::None}, 1.0F},
    KeySpec{L"/", {0x35, KeyPrefix::E0}, 1.0F},
    KeySpec{L"*", {0x37, KeyPrefix::None}, 1.0F},
    KeySpec{L"-", {0x4A, KeyPrefix::None}, 1.0F},
    KeySpec{L"7", {0x47, KeyPrefix::None}, 1.0F},
    KeySpec{L"8", {0x48, KeyPrefix::None}, 1.0F},
    KeySpec{L"9", {0x49, KeyPrefix::None}, 1.0F},
    KeySpec{L"+", {0x4E, KeyPrefix::None}, 1.0F},
    KeySpec{L"4", {0x4B, KeyPrefix::None}, 1.0F},
    KeySpec{L"5", {0x4C, KeyPrefix::None}, 1.0F},
    KeySpec{L"6", {0x4D, KeyPrefix::None}, 1.0F},
    KeySpec{L"Enter", {0x1C, KeyPrefix::E0}, 1.0F},
    KeySpec{L"1", {0x4F, KeyPrefix::None}, 1.0F},
    KeySpec{L"2", {0x50, KeyPrefix::None}, 1.0F},
    KeySpec{L"3", {0x51, KeyPrefix::None}, 1.0F},
    KeySpec{L"0", {0x52, KeyPrefix::None}, 2.0F},
    KeySpec{L".", {0x53, KeyPrefix::None}, 1.0F},
};

constexpr std::array kModifierKeys{
    KeySpec{L"LCtrl", {0x1D, KeyPrefix::None}, 1.0F},
    KeySpec{L"LShift", {0x2A, KeyPrefix::None}, 1.0F},
    KeySpec{L"LAlt", {0x38, KeyPrefix::None}, 1.0F},
    KeySpec{L"LWin", {0x5B, KeyPrefix::E0}, 1.0F},
    KeySpec{L"RCtrl", {0x1D, KeyPrefix::E0}, 1.0F},
    KeySpec{L"RShift", {0x36, KeyPrefix::None}, 1.0F},
    KeySpec{L"RAlt", {0x38, KeyPrefix::E0}, 1.0F},
    KeySpec{L"RWin", {0x5C, KeyPrefix::E0}, 1.0F},
};

constexpr std::array kCategories{
    CategorySpec{MainWindow::ActionCategory::Basic, L"基础键"},
    CategorySpec{MainWindow::ActionCategory::Function, L"F区/导航"},
    CategorySpec{MainWindow::ActionCategory::Modifiers, L"修饰键"},
    CategorySpec{MainWindow::ActionCategory::Layer, L"Layer"},
    CategorySpec{MainWindow::ActionCategory::System, L"快捷操作"},
    CategorySpec{MainWindow::ActionCategory::Media, L"媒体"},
    CategorySpec{MainWindow::ActionCategory::Mouse, L"鼠标"},
    CategorySpec{MainWindow::ActionCategory::Macro, L"宏"},
    CategorySpec{MainWindow::ActionCategory::TapDance, L"Tap Dance"},
    CategorySpec{MainWindow::ActionCategory::Combo, L"Combo"},
    CategorySpec{MainWindow::ActionCategory::Override, L"Override"},
    CategorySpec{MainWindow::ActionCategory::Advanced, L"高级"},
};

template <typename Callback>
void ForEachCatalogKey(Callback&& callback) {
    for (const auto& key : kFunctionRow) {
        callback(key);
    }
    for (const auto& key : kNumberRow) {
        callback(key);
    }
    for (const auto& key : kTopRow) {
        callback(key);
    }
    for (const auto& key : kHomeRow) {
        callback(key);
    }
    for (const auto& key : kBottomRow) {
        callback(key);
    }
    for (const auto& key : kModifierRow) {
        callback(key);
    }
    for (const auto& key : kNavigationKeys) {
        callback(key);
    }
    for (const auto& key : kArrowKeys) {
        callback(key);
    }
    for (const auto& key : kSystemKeys) {
        callback(key);
    }
    for (const auto& key : kNumpadKeys) {
        callback(key);
    }
}

std::wstring Trim(std::wstring text) {
    const auto is_space = [](const wchar_t character) {
        return std::iswspace(character) != 0;
    };

    const auto first = std::find_if_not(
        text.begin(),
        text.end(),
        is_space);
    const auto last = std::find_if_not(
        text.rbegin(),
        text.rend(),
        is_space)
                          .base();

    if (first >= last) {
        return {};
    }

    return std::wstring(first, last);
}

bool ActionsEqual(
    const Action& left,
    const Action& right) noexcept {
    return left.kind == right.kind &&
           left.target_key == right.target_key &&
           left.target_layer == right.target_layer &&
           left.hold_key == right.hold_key &&
           left.tapping_term_ms == right.tapping_term_ms &&
           left.quick_tap_term_ms == right.quick_tap_term_ms &&
           left.virtual_key == right.virtual_key &&
           left.mouse_button == right.mouse_button &&
           left.mouse_x == right.mouse_x &&
           left.mouse_y == right.mouse_y &&
           left.mouse_amount == right.mouse_amount &&
           left.reference_id == right.reference_id &&
           left.shortcut_modifiers ==
               right.shortcut_modifiers;
}

std::optional<unsigned int> ParseUnsigned(
    const std::wstring& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    wchar_t* parsed_end = nullptr;
    const auto parsed = std::wcstoul(
        text.c_str(),
        &parsed_end,
        10);
    if (parsed_end == text.c_str() ||
        parsed_end != text.c_str() + text.size() ||
        parsed > std::numeric_limits<unsigned int>::max()) {
        return std::nullopt;
    }

    return static_cast<unsigned int>(parsed);
}

std::wstring LayoutName(
    const KeyboardLayoutPreset layout) {
    switch (layout) {
    case KeyboardLayoutPreset::FullSize104:
        return L"104键全尺寸";
    case KeyboardLayoutPreset::Tkl87:
        return L"87键 TKL";
    case KeyboardLayoutPreset::Compact75:
        return L"75%";
    case KeyboardLayoutPreset::Compact65:
        return L"65%";
    case KeyboardLayoutPreset::Compact60:
        return L"60%";
    case KeyboardLayoutPreset::Laptop:
        return L"通用笔记本";
    }

    return L"104键全尺寸";
}

}  // namespace

MainWindow::MainWindow(const HINSTANCE instance)
    : instance_(instance) {}

bool MainWindow::Create() {
    StartupTrace(L"MainWindow::Create begin");
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = &MainWindow::WindowProcedure;
    window_class.lpszClassName = kWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = static_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_PCKEY),
        IMAGE_ICON,
        0,
        0,
        LR_DEFAULTSIZE | LR_SHARED));
    if (window_class.hIcon == nullptr) {
        window_class.hIcon =
            LoadIconW(nullptr, IDI_APPLICATION);
    }
    window_class.hbrBackground = nullptr;

    if (RegisterClassExW(&window_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        StartupTrace(L"RegisterClassEx failed");
        return false;
    }

    StartupTrace(L"before CreateWindowEx");
    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"PCkey",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1380,
        900,
        nullptr,
        nullptr,
        instance_,
        this);
    StartupTrace(L"after CreateWindowEx");

    if (window_ != nullptr) {
        dpi_ = GetDpiForWindow(window_);
        CHANGEFILTERSTRUCT filter{};
        filter.cbSize = sizeof(filter);
        ChangeWindowMessageFilterEx(
            window_,
            kHealthCheckMessage,
            MSGFLT_ALLOW,
            &filter);
    }

    return window_ != nullptr;
}

void MainWindow::Show(const int command) noexcept {
    StartupTrace(L"MainWindow::Show begin");
    const auto visible_command =
        command == SW_HIDE ? SW_SHOWNORMAL : command;
    ShowWindow(window_, visible_command);
    StartupTrace(L"after ShowWindow");
    UpdateWindow(window_);
    StartupTrace(L"after UpdateWindow");
    SetForegroundWindow(window_);
    PostMessageW(window_, kInitializeMessage, 0, 0);
}

LRESULT CALLBACK MainWindow::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param) {
    MainWindow* main_window = reinterpret_cast<MainWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create =
            reinterpret_cast<const CREATESTRUCTW*>(l_param);
        main_window =
            static_cast<MainWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(main_window));
        main_window->window_ = window;
    }

    if (main_window != nullptr) {
        return main_window->HandleMessage(
            message,
            w_param,
            l_param);
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT MainWindow::HandleMessage(
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param) {
    if (message == kHealthCheckMessage) {
        return kHealthCheckResult;
    }

    switch (message) {
    case WM_CREATE:
        StartupTrace(L"WM_CREATE begin");
        if (!CreateControls()) {
            StartupTrace(L"CreateControls failed");
            return -1;
        }
        StartupTrace(L"WM_CREATE end");
        return 0;

    case kInitializeMessage:
        if (!initialized_) {
            StartupTrace(L"initialize message begin");
            initialized_ = true;
            if (!CreateDeviceIndependentResources()) {
                StartupTrace(
                    L"CreateDeviceIndependentResources failed");
                ShowError(
                    L"PCkey 图形界面初始化失败。"
                    L"请更新显卡驱动或联系管理员检查"
                    L"终端安全软件的图形注入策略。");
                return 0;
            }
            StartupTrace(
                L"CreateDeviceIndependentResources succeeded");
            LoadConfiguration();
            StartupTrace(L"LoadConfiguration succeeded");
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;

    case WM_PAINT:
        Paint();
        return 0;

    case WM_SIZE:
        Resize(LOWORD(l_param), HIWORD(l_param));
        LayoutControls(LOWORD(l_param), HIWORD(l_param));
        return 0;

    case WM_LBUTTONDOWN: {
        const auto x = static_cast<float>(GET_X_LPARAM(l_param));
        const auto y = static_cast<float>(GET_Y_LPARAM(l_param));

        if (const auto layer =
                HitTestIndexed(layer_visuals_, x, y);
            layer.has_value()) {
            SelectLayer(*layer);
            return 0;
        }

        if (HitTestRect(add_layer_bounds_, x, y)) {
            AddLayer();
            return 0;
        }

        if (HitTestRect(remove_layer_bounds_, x, y)) {
            RemoveLayer();
            return 0;
        }

        if (const auto category =
                HitTestIndexed(category_visuals_, x, y);
            category.has_value() &&
            *category < kCategories.size()) {
            SelectCategory(kCategories[*category].category);
            return 0;
        }

        if (const auto action =
                HitTestIndexed(palette_visuals_, x, y);
            action.has_value()) {
            ApplyPaletteAction(*action);
            return 0;
        }

        if (HitTestRect(reset_key_bounds_, x, y)) {
            ResetSelectedMapping();
            return 0;
        }

        if (const auto key = HitTestKey(x, y);
            key.has_value() && SelectedProfile() != nullptr) {
            selected_key_ = key;
            PopulatePaletteActions();
            UpdateEditorState();
            SetStatus(
                L"已选择 " + LabelForKey(*key) +
                L"，请在下方动作面板中选择功能。");
            return 0;
        }
        return 0;
    }

    case WM_RBUTTONUP: {
        const auto x = static_cast<float>(
            GET_X_LPARAM(l_param));
        const auto y = static_cast<float>(
            GET_Y_LPARAM(l_param));
        if (const auto action =
                HitTestIndexed(palette_visuals_, x, y);
            action.has_value()) {
            POINT point{
                GET_X_LPARAM(l_param),
                GET_Y_LPARAM(l_param)};
            ClientToScreen(window_, &point);
            ShowPaletteContextMenu(*action, point);
        }
        return 0;
    }

    case WM_COMMAND: {
        if (loading_controls_) {
            return 0;
        }

        const auto control_id = LOWORD(w_param);
        const auto notification = HIWORD(w_param);

        if (control_id == kProfileListId &&
            notification == LBN_SELCHANGE) {
            SelectProfileFromList();
            return 0;
        }

        if (control_id == kLayoutComboId &&
            notification == CBN_SELCHANGE) {
            auto* profile = SelectedProfile();
            const auto selection = static_cast<int>(
                SendMessageW(layout_combo_, CB_GETCURSEL, 0, 0));
            if (profile != nullptr && selection >= 0 &&
                selection <= static_cast<int>(
                                 KeyboardLayoutPreset::Laptop)) {
                profile->SetLayout(
                    static_cast<KeyboardLayoutPreset>(selection));
                MarkDirty(L"键盘布局已保存到草稿。");
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        }

        if (control_id == kProfileNameId &&
            notification == EN_CHANGE) {
            bool changed = false;
            if (RenameSelectedProfile(
                    false,
                    &changed) &&
                changed) {
                MarkDirty(L"配置名称已保存到草稿。");
            }
            return 0;
        }

        if (control_id == kProfileNameId &&
            notification == EN_KILLFOCUS) {
            PopulateProfiles();
            UpdateEditorState();
            return 0;
        }

        if (notification == BN_CLICKED) {
            switch (control_id) {
            case kNewProfileId:
                CreateProfile();
                break;
            case kDeleteProfileId:
                DeleteProfile();
                break;
            case kSaveApplyId:
                SaveAndApply();
                break;
            case kDiscardId:
                DiscardChanges();
                break;
            case kKeyTestId:
                ShowKeyTestWindow(window_, instance_);
                break;
            case kUpdateTimingId:
                UpdateTapHoldTiming();
                break;
            default:
                break;
            }
        }
        return 0;
    }

    case WM_DPICHANGED: {
        dpi_ = HIWORD(w_param);
        const auto* suggested =
            reinterpret_cast<const RECT*>(l_param);
        SetWindowPos(
            window_,
            nullptr,
            suggested->left,
            suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        DiscardDeviceResources();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* info =
            reinterpret_cast<MINMAXINFO*>(l_param);
        info->ptMinTrackSize.x = 1180;
        info->ptMinTrackSize.y = 760;
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window_, message, w_param, l_param);
}

bool MainWindow::CreateDeviceIndependentResources() {
    StartupTrace(L"D2D1CreateFactory begin");
    if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            d2d_factory_.ReleaseAndGetAddressOf()))) {
        return false;
    }

    StartupTrace(L"DWriteCreateFactory begin");
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(
                dwrite_factory_.ReleaseAndGetAddressOf())))) {
        return false;
    }
    StartupTrace(L"DWriteCreateFactory succeeded");

    const auto create_format =
        [this](
            const float size,
            const DWRITE_FONT_WEIGHT weight,
            IDWriteTextFormat** output) {
            return dwrite_factory_->CreateTextFormat(
                L"Segoe UI",
                nullptr,
                weight,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                L"zh-CN",
                output);
        };

    if (FAILED(create_format(
            27.0F,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            title_format_.ReleaseAndGetAddressOf())) ||
        FAILED(create_format(
            16.0F,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            section_format_.ReleaseAndGetAddressOf())) ||
        FAILED(create_format(
            13.0F,
            DWRITE_FONT_WEIGHT_NORMAL,
            body_format_.ReleaseAndGetAddressOf())) ||
        FAILED(create_format(
            11.0F,
            DWRITE_FONT_WEIGHT_NORMAL,
            small_format_.ReleaseAndGetAddressOf())) ||
        FAILED(create_format(
            10.5F,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            key_format_.ReleaseAndGetAddressOf()))) {
        return false;
    }

    small_format_->SetTextAlignment(
        DWRITE_TEXT_ALIGNMENT_CENTER);
    small_format_->SetParagraphAlignment(
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    key_format_->SetTextAlignment(
        DWRITE_TEXT_ALIGNMENT_CENTER);
    key_format_->SetParagraphAlignment(
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return true;
}

bool MainWindow::CreateDeviceResources() {
    if (render_target_ != nullptr) {
        return true;
    }

    RECT client{};
    GetClientRect(window_, &client);
    const auto size = D2D1::SizeU(
        static_cast<UINT32>(client.right - client.left),
        static_cast<UINT32>(client.bottom - client.top));

    if (d2d_factory_ == nullptr ||
        FAILED(d2d_factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE),
            D2D1::HwndRenderTargetProperties(window_, size),
            render_target_.ReleaseAndGetAddressOf()))) {
        return false;
    }

    render_target_->SetDpi(96.0F, 96.0F);

    return SUCCEEDED(render_target_->CreateSolidColorBrush(
               kText,
               text_brush_.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(render_target_->CreateSolidColorBrush(
               kMutedText,
               muted_text_brush_.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(render_target_->CreateSolidColorBrush(
               kCard,
               card_brush_.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(render_target_->CreateSolidColorBrush(
               kKey,
               key_brush_.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(render_target_->CreateSolidColorBrush(
               kAccent,
               accent_brush_.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(render_target_->CreateSolidColorBrush(
               kAccentSoft,
               accent_soft_brush_.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(render_target_->CreateSolidColorBrush(
               kWhite,
               selected_text_brush_.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(render_target_->CreateSolidColorBrush(
               kBorder,
               border_brush_.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(render_target_->CreateSolidColorBrush(
               kDisabled,
               disabled_brush_.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(render_target_->CreateSolidColorBrush(
               kWarning,
               warning_brush_.ReleaseAndGetAddressOf()));
}

bool MainWindow::CreateControls() {
    StartupTrace(L"CreateControls begin");
    const auto create_control =
        [this](
            const wchar_t* class_name,
            const wchar_t* text,
            const DWORD style,
            const int id) {
            const auto control = CreateWindowExW(
                0,
                class_name,
                text,
                WS_CHILD | WS_VISIBLE | style,
                0,
                0,
                1,
                1,
                window_,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(id)),
                instance_,
                nullptr);
            SetControlFont(control);
            return control;
        };

    profile_list_ = create_control(
        L"LISTBOX",
        L"",
        LBS_NOTIFY | WS_BORDER | WS_VSCROLL,
        kProfileListId);
    new_profile_button_ = create_control(
        L"BUTTON",
        L"新建配置",
        BS_PUSHBUTTON,
        kNewProfileId);
    delete_profile_button_ = create_control(
        L"BUTTON",
        L"删除",
        BS_PUSHBUTTON,
        kDeleteProfileId);
    profile_name_edit_ = create_control(
        L"EDIT",
        L"",
        ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
        kProfileNameId);
    layout_combo_ = create_control(
        L"COMBOBOX",
        L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        kLayoutComboId);
    discard_button_ = create_control(
        L"BUTTON",
        L"放弃修改",
        BS_PUSHBUTTON,
        kDiscardId);
    key_test_button_ = create_control(
        L"BUTTON",
        L"按键测试",
        BS_PUSHBUTTON,
        kKeyTestId);
    save_apply_button_ = create_control(
        L"BUTTON",
        L"保存并应用",
        BS_DEFPUSHBUTTON,
        kSaveApplyId);
    tap_term_label_ = create_control(
        L"STATIC",
        L"长按阈值",
        SS_LEFT,
        kTapTermLabelId);
    tap_term_edit_ = create_control(
        L"EDIT",
        L"500",
        ES_NUMBER | ES_CENTER | WS_BORDER | WS_TABSTOP,
        kTapTermId);
    quick_tap_label_ = create_control(
        L"STATIC",
        L"Quick Tap",
        SS_LEFT,
        kQuickTapLabelId);
    quick_tap_edit_ = create_control(
        L"EDIT",
        L"200",
        ES_NUMBER | ES_CENTER | WS_BORDER | WS_TABSTOP,
        kQuickTapTermId);
    update_timing_button_ = create_control(
        L"BUTTON",
        L"更新参数",
        BS_PUSHBUTTON,
        kUpdateTimingId);
    status_label_ = create_control(
        L"STATIC",
        L"",
        SS_LEFT,
        kStatusId);

    PopulateLayoutChoices();

    const bool succeeded =
        profile_list_ != nullptr &&
           new_profile_button_ != nullptr &&
           delete_profile_button_ != nullptr &&
           profile_name_edit_ != nullptr &&
           layout_combo_ != nullptr &&
           discard_button_ != nullptr &&
           key_test_button_ != nullptr &&
           save_apply_button_ != nullptr &&
           tap_term_label_ != nullptr &&
           tap_term_edit_ != nullptr &&
           quick_tap_label_ != nullptr &&
           quick_tap_edit_ != nullptr &&
           update_timing_button_ != nullptr &&
           status_label_ != nullptr;
    StartupTrace(
        succeeded
            ? L"CreateControls succeeded"
            : L"CreateControls incomplete");
    return succeeded;
}

void MainWindow::DiscardDeviceResources() noexcept {
    warning_brush_.Reset();
    disabled_brush_.Reset();
    border_brush_.Reset();
    selected_text_brush_.Reset();
    accent_soft_brush_.Reset();
    accent_brush_.Reset();
    key_brush_.Reset();
    card_brush_.Reset();
    muted_text_brush_.Reset();
    text_brush_.Reset();
    render_target_.Reset();
}

void MainWindow::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);

    if (d2d_factory_ == nullptr ||
        dwrite_factory_ == nullptr) {
        FillRect(
            paint.hdc,
            &paint.rcPaint,
            GetSysColorBrush(COLOR_WINDOW));
        EndPaint(window_, &paint);
        return;
    }

    if (!CreateDeviceResources()) {
        EndPaint(window_, &paint);
        return;
    }

    render_target_->BeginDraw();
    render_target_->Clear(kBackground);

    const auto size = render_target_->GetSize();
    render_target_->FillRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(
                16.0F,
                64.0F,
                226.0F,
                size.height - 52.0F),
            12.0F,
            12.0F),
        card_brush_.Get());
    render_target_->FillRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(
                238.0F,
                64.0F,
                size.width - 16.0F,
                size.height - 52.0F),
            12.0F,
            12.0F),
        card_brush_.Get());

    DrawHeader();
    DrawSidebar();
    DrawLayerTabs();
    DrawKeyboard();
    DrawActionPanel();
    DrawTapHoldDetails();

    const auto result = render_target_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }

    EndPaint(window_, &paint);
}

void MainWindow::Resize(
    const UINT width,
    const UINT height) {
    if (render_target_ != nullptr) {
        render_target_->Resize(D2D1::SizeU(width, height));
    }
}

void MainWindow::LayoutControls(
    const UINT width,
    const UINT height) {
    const auto safe_width = static_cast<int>(width);
    const auto safe_height = static_cast<int>(height);
    const auto panel_top = std::max(500, safe_height - 280);

    MoveWindow(
        profile_list_,
        28,
        102,
        186,
        std::max(180, safe_height - 262),
        TRUE);
    MoveWindow(
        new_profile_button_,
        28,
        safe_height - 142,
        112,
        30,
        TRUE);
    MoveWindow(
        delete_profile_button_,
        146,
        safe_height - 142,
        68,
        30,
        TRUE);

    MoveWindow(profile_name_edit_, 258, 91, 190, 28, TRUE);
    MoveWindow(layout_combo_, 520, 91, 166, 260, TRUE);
    MoveWindow(
        key_test_button_,
        safe_width - 346,
        18,
        100,
        34,
        TRUE);
    MoveWindow(
        discard_button_,
        safe_width - 236,
        18,
        100,
        34,
        TRUE);
    MoveWindow(
        save_apply_button_,
        safe_width - 126,
        18,
        110,
        34,
        TRUE);

    const auto details_left = safe_width - 308;
    const auto details_top = panel_top + 106;
    MoveWindow(
        tap_term_label_,
        details_left,
        details_top,
        72,
        24,
        TRUE);
    MoveWindow(
        tap_term_edit_,
        details_left + 78,
        details_top - 3,
        72,
        27,
        TRUE);
    MoveWindow(
        quick_tap_label_,
        details_left,
        details_top + 38,
        72,
        24,
        TRUE);
    MoveWindow(
        quick_tap_edit_,
        details_left + 78,
        details_top + 35,
        72,
        27,
        TRUE);
    MoveWindow(
        update_timing_button_,
        details_left + 166,
        details_top + 15,
        94,
        31,
        TRUE);

    MoveWindow(
        status_label_,
        24,
        safe_height - 36,
        safe_width - 48,
        22,
        TRUE);

    UpdateTapHoldControls();
}

void MainWindow::DrawHeader() {
    const auto size = render_target_->GetSize();
    static constexpr wchar_t kTitle[] = L"PCkey";
    render_target_->DrawTextW(
        kTitle,
        static_cast<UINT32>(std::size(kTitle) - 1),
        title_format_.Get(),
        D2D1::RectF(22.0F, 15.0F, 180.0F, 54.0F),
        text_brush_.Get());

    const auto version =
        std::wstring(L"v") + std::wstring(kVersion);
    render_target_->DrawTextW(
        version.c_str(),
        static_cast<UINT32>(version.size()),
        body_format_.Get(),
        D2D1::RectF(114.0F, 26.0F, 220.0F, 50.0F),
        muted_text_brush_.Get());

    if (dirty_) {
        const auto badge = D2D1::RoundedRect(
            D2D1::RectF(212.0F, 20.0F, 324.0F, 48.0F),
            14.0F,
            14.0F);
        render_target_->FillRoundedRectangle(
            badge,
            accent_soft_brush_.Get());
        static constexpr wchar_t kDraft[] = L"● 有未应用草稿";
        render_target_->DrawTextW(
            kDraft,
            static_cast<UINT32>(std::size(kDraft) - 1),
            small_format_.Get(),
            badge.rect,
            accent_brush_.Get());
    }

    const auto selected =
        selected_key_.has_value()
            ? L"已选择：" + LabelForKey(*selected_key_)
            : L"点击键盘中的键位，然后从下方选择动作";
    render_target_->DrawTextW(
        selected.c_str(),
        static_cast<UINT32>(selected.size()),
        body_format_.Get(),
        D2D1::RectF(
            704.0F,
            24.0F,
            size.width - 360.0F,
            50.0F),
        selected_key_.has_value()
            ? text_brush_.Get()
            : muted_text_brush_.Get());
}

void MainWindow::DrawSidebar() {
    static constexpr wchar_t kProfiles[] = L"配置";
    render_target_->DrawTextW(
        kProfiles,
        static_cast<UINT32>(std::size(kProfiles) - 1),
        section_format_.Get(),
        D2D1::RectF(28.0F, 76.0F, 210.0F, 101.0F),
        text_brush_.Get());

    static constexpr wchar_t kHint[] =
        L"普通模式始终保持原始键盘状态";
    const auto size = render_target_->GetSize();
    render_target_->DrawTextW(
        kHint,
        static_cast<UINT32>(std::size(kHint) - 1),
        small_format_.Get(),
        D2D1::RectF(
            28.0F,
            size.height - 102.0F,
            214.0F,
            size.height - 70.0F),
        muted_text_brush_.Get());
}

void MainWindow::DrawLayerTabs() {
    layer_visuals_.clear();
    add_layer_bounds_ = {};
    remove_layer_bounds_ = {};

    static constexpr wchar_t kName[] = L"配置名称";
    static constexpr wchar_t kLayout[] = L"键盘布局";
    render_target_->DrawTextW(
        kName,
        static_cast<UINT32>(std::size(kName) - 1),
        small_format_.Get(),
        D2D1::RectF(258.0F, 70.0F, 448.0F, 91.0F),
        muted_text_brush_.Get());
    render_target_->DrawTextW(
        kLayout,
        static_cast<UINT32>(std::size(kLayout) - 1),
        small_format_.Get(),
        D2D1::RectF(520.0F, 70.0F, 686.0F, 91.0F),
        muted_text_brush_.Get());

    const auto* profile = SelectedProfile();
    if (profile == nullptr) {
        static constexpr wchar_t kNormal[] =
            L"普通模式不可编辑；点击“保存并应用”即可停用全部映射";
        render_target_->DrawTextW(
            kNormal,
            static_cast<UINT32>(std::size(kNormal) - 1),
            body_format_.Get(),
            D2D1::RectF(258.0F, 133.0F, 850.0F, 162.0F),
            muted_text_brush_.Get());
        return;
    }

    const auto size = render_target_->GetSize();
    const auto count = profile->layer_count();
    const float left = 258.0F;
    const float right = size.width - 122.0F;
    constexpr float gap = 5.0F;
    const auto available = right - left;
    const auto tab_width = std::clamp(
        (available - gap * static_cast<float>(count - 1)) /
            static_cast<float>(count),
        30.0F,
        76.0F);

    for (std::size_t index = 0; index < count; ++index) {
        const auto x =
            left + static_cast<float>(index) * (tab_width + gap);
        const auto bounds =
            D2D1::RectF(x, 132.0F, x + tab_width, 166.0F);
        const auto selected = index == selected_layer_;
        render_target_->FillRoundedRectangle(
            D2D1::RoundedRect(bounds, 8.0F, 8.0F),
            selected
                ? accent_brush_.Get()
                : key_brush_.Get());
        const auto text = L"L" + std::to_wstring(index);
        render_target_->DrawTextW(
            text.c_str(),
            static_cast<UINT32>(text.size()),
            small_format_.Get(),
            bounds,
            selected
                ? selected_text_brush_.Get()
                : text_brush_.Get());
        layer_visuals_.push_back(
            IndexedVisual{index, bounds});
    }

    const auto controls_left =
        left + static_cast<float>(count) * (tab_width + gap) + 4.0F;
    add_layer_bounds_ =
        D2D1::RectF(controls_left, 132.0F, controls_left + 34.0F, 166.0F);
    remove_layer_bounds_ =
        D2D1::RectF(
            controls_left + 39.0F,
            132.0F,
            controls_left + 73.0F,
            166.0F);

    for (const auto& button :
         std::array{
             std::pair{add_layer_bounds_, std::wstring_view(L"+")},
             std::pair{remove_layer_bounds_, std::wstring_view(L"−")}}) {
        render_target_->FillRoundedRectangle(
            D2D1::RoundedRect(button.first, 8.0F, 8.0F),
            key_brush_.Get());
        render_target_->DrawTextW(
            button.second.data(),
            static_cast<UINT32>(button.second.size()),
            section_format_.Get(),
            button.first,
            text_brush_.Get());
    }
}

void MainWindow::DrawKeyboard() {
    key_visuals_.clear();

    const auto* profile = SelectedProfile();
    const auto layout =
        profile != nullptr
            ? profile->layout()
            : KeyboardLayoutPreset::FullSize104;
    const auto size = render_target_->GetSize();
    const float panel_top =
        std::max(500.0F, size.height - 280.0F);
    const float left = 258.0F;
    const float right = size.width - 34.0F;
    const float top = 180.0F;
    const float bottom = panel_top - 18.0F;
    const float width = right - left;
    constexpr float gap = 5.0F;

    const bool show_function =
        layout == KeyboardLayoutPreset::FullSize104 ||
        layout == KeyboardLayoutPreset::Tkl87 ||
        layout == KeyboardLayoutPreset::Compact75 ||
        layout == KeyboardLayoutPreset::Laptop;
    const bool show_navigation =
        layout == KeyboardLayoutPreset::FullSize104 ||
        layout == KeyboardLayoutPreset::Tkl87;
    const bool show_numpad =
        layout == KeyboardLayoutPreset::FullSize104;
    const bool show_arrows =
        layout != KeyboardLayoutPreset::Compact60;

    const std::size_t row_count =
        show_function ? 6U : 5U;
    const auto key_height = std::clamp(
        (bottom - top -
         gap * static_cast<float>(row_count - 1)) /
            static_cast<float>(row_count),
        31.0F,
        52.0F);

    float alpha_width = width;
    float navigation_width = 0.0F;
    float numpad_width = 0.0F;
    if (show_numpad) {
        alpha_width = width * 0.59F;
        navigation_width = width * 0.15F;
        numpad_width =
            width - alpha_width - navigation_width - gap * 4.0F;
    } else if (show_navigation) {
        alpha_width = width * 0.76F;
        navigation_width = width - alpha_width - gap * 2.0F;
    }

    const auto draw_key =
        [this, profile](
            const KeySpec& spec,
            const D2D1_RECT_F bounds) {
            const auto selected =
                selected_key_.has_value() &&
                *selected_key_ == spec.key;
            const auto action =
                profile != nullptr
                    ? profile->GetAction(
                          selected_layer_,
                          spec.key)
                    : Action::PassThrough();

            auto label = std::wstring(spec.label);
            const auto action_label =
                ActionLabel(action, spec.key);
            if (!action_label.empty()) {
                label += L"\n";
                label += action_label;
            }

            const auto rounded =
                D2D1::RoundedRect(bounds, 6.0F, 6.0F);
            render_target_->FillRoundedRectangle(
                rounded,
                selected
                    ? accent_brush_.Get()
                    : key_brush_.Get());

            if (!selected &&
                action.kind != ActionKind::PassThrough &&
                action.kind != ActionKind::Transparent) {
                render_target_->DrawRoundedRectangle(
                    rounded,
                    accent_brush_.Get(),
                    1.2F);
            }

            render_target_->DrawTextW(
                label.c_str(),
                static_cast<UINT32>(label.size()),
                key_format_.Get(),
                bounds,
                selected
                    ? selected_text_brush_.Get()
                    : text_brush_.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);

            key_visuals_.push_back(
                KeyVisual{
                    spec.key,
                    std::wstring(spec.label),
                    bounds});
        };

    const auto draw_row =
        [&draw_key, key_height](
            const auto& row,
            const float x,
            const float y,
            const float available_width) {
            float total_units = 0.0F;
            for (const auto& key : row) {
                total_units += key.width;
            }

            const auto unit_width =
                (available_width -
                 gap * static_cast<float>(row.size() - 1)) /
                total_units;
            float cursor = x;
            for (const auto& key : row) {
                const auto key_width = unit_width * key.width;
                draw_key(
                    key,
                    D2D1::RectF(
                        cursor,
                        y,
                        cursor + key_width,
                        y + key_height));
                cursor += key_width + gap;
            }
        };

    float current_y = top;
    if (show_function) {
        draw_row(kFunctionRow, left, current_y, alpha_width);
        current_y += key_height + gap;
    }

    draw_row(kNumberRow, left, current_y, alpha_width);
    current_y += key_height + gap;
    draw_row(kTopRow, left, current_y, alpha_width);
    current_y += key_height + gap;
    draw_row(kHomeRow, left, current_y, alpha_width);
    current_y += key_height + gap;
    draw_row(kBottomRow, left, current_y, alpha_width);
    current_y += key_height + gap;
    draw_row(kModifierRow, left, current_y, alpha_width);

    if (show_navigation) {
        const auto nav_left = left + alpha_width + gap * 2.0F;
        const auto nav_cell =
            (navigation_width - gap * 2.0F) / 3.0F;
        const auto nav_top =
            top + (show_function ? key_height + gap : 0.0F);

        for (std::size_t index = 0;
             index < kNavigationKeys.size();
             ++index) {
            const auto row = index / 3;
            const auto column = index % 3;
            const auto x =
                nav_left +
                static_cast<float>(column) * (nav_cell + gap);
            const auto y =
                nav_top +
                static_cast<float>(row) * (key_height + gap);
            draw_key(
                kNavigationKeys[index],
                D2D1::RectF(
                    x,
                    y,
                    x + nav_cell,
                    y + key_height));
        }

        const auto arrow_y =
            nav_top + (key_height + gap) * 3.0F;
        draw_key(
            kArrowKeys[0],
            D2D1::RectF(
                nav_left + nav_cell + gap,
                arrow_y,
                nav_left + nav_cell * 2.0F + gap,
                arrow_y + key_height));
        for (std::size_t index = 1; index < 4; ++index) {
            const auto column = index - 1;
            const auto x =
                nav_left +
                static_cast<float>(column) * (nav_cell + gap);
            draw_key(
                kArrowKeys[index],
                D2D1::RectF(
                    x,
                    arrow_y + key_height + gap,
                    x + nav_cell,
                    arrow_y + key_height * 2.0F + gap));
        }

        if (show_function) {
            const auto system_width =
                (navigation_width - gap * 2.0F) / 3.0F;
            for (std::size_t index = 0;
                 index < kSystemKeys.size();
                 ++index) {
                const auto x =
                    nav_left +
                    static_cast<float>(index) *
                        (system_width + gap);
                draw_key(
                    kSystemKeys[index],
                    D2D1::RectF(
                        x,
                        top,
                        x + system_width,
                        top + key_height));
            }
        }
    } else if (show_arrows) {
        const auto arrow_width =
            std::min(52.0F, alpha_width / 16.0F);
        const auto arrow_left =
            left + alpha_width -
            arrow_width * 3.0F -
            gap * 2.0F;
        const auto arrow_top = current_y;
        draw_key(
            kArrowKeys[1],
            D2D1::RectF(
                arrow_left,
                arrow_top,
                arrow_left + arrow_width,
                arrow_top + key_height));
        draw_key(
            kArrowKeys[2],
            D2D1::RectF(
                arrow_left + arrow_width + gap,
                arrow_top,
                arrow_left + arrow_width * 2.0F + gap,
                arrow_top + key_height));
        draw_key(
            kArrowKeys[3],
            D2D1::RectF(
                arrow_left + (arrow_width + gap) * 2.0F,
                arrow_top,
                arrow_left + arrow_width * 3.0F + gap * 2.0F,
                arrow_top + key_height));
        draw_key(
            kArrowKeys[0],
            D2D1::RectF(
                arrow_left + arrow_width + gap,
                arrow_top - key_height - gap,
                arrow_left + arrow_width * 2.0F + gap,
                arrow_top - gap));
    }

    if (show_numpad) {
        const auto numpad_left =
            left + alpha_width + navigation_width + gap * 4.0F;
        const auto cell =
            (numpad_width - gap * 3.0F) / 4.0F;
        const auto numpad_top =
            top + (show_function ? key_height + gap : 0.0F);

        for (std::size_t index = 0; index < 16; ++index) {
            const auto row = index / 4;
            const auto column = index % 4;
            const auto x =
                numpad_left +
                static_cast<float>(column) * (cell + gap);
            const auto y =
                numpad_top +
                static_cast<float>(row) * (key_height + gap);
            draw_key(
                kNumpadKeys[index],
                D2D1::RectF(
                    x,
                    y,
                    x + cell,
                    y + key_height));
        }

        draw_key(
            kNumpadKeys[16],
            D2D1::RectF(
                numpad_left + (cell + gap) * 2.0F,
                numpad_top + (key_height + gap) * 4.0F,
                numpad_left + (cell + gap) * 3.0F - gap,
                numpad_top + key_height * 5.0F + gap * 4.0F));
    }
}

void MainWindow::DrawActionPanel() {
    category_visuals_.clear();
    palette_visuals_.clear();
    reset_key_bounds_ = {};

    const auto size = render_target_->GetSize();
    const auto panel_top =
        std::max(500.0F, size.height - 280.0F);
    const auto panel_bounds = D2D1::RectF(
        250.0F,
        panel_top,
        size.width - 28.0F,
        size.height - 64.0F);
    render_target_->FillRoundedRectangle(
        D2D1::RoundedRect(panel_bounds, 10.0F, 10.0F),
        disabled_brush_.Get());
    render_target_->DrawRoundedRectangle(
        D2D1::RoundedRect(panel_bounds, 10.0F, 10.0F),
        border_brush_.Get(),
        1.0F);

    const float left = panel_bounds.left + 12.0F;
    const float right = panel_bounds.right - 12.0F;
    const float tab_top = panel_bounds.top + 10.0F;
    constexpr float tab_gap = 4.0F;
    const auto tab_width =
        (right - left -
         tab_gap * static_cast<float>(kCategories.size() - 1)) /
        static_cast<float>(kCategories.size());

    for (std::size_t index = 0;
         index < kCategories.size();
         ++index) {
        const auto x =
            left + static_cast<float>(index) *
                       (tab_width + tab_gap);
        const auto bounds =
            D2D1::RectF(x, tab_top, x + tab_width, tab_top + 30.0F);
        const auto selected =
            kCategories[index].category == selected_category_;
        render_target_->FillRoundedRectangle(
            D2D1::RoundedRect(bounds, 7.0F, 7.0F),
            selected
                ? accent_brush_.Get()
                : card_brush_.Get());
        render_target_->DrawTextW(
            kCategories[index].label.data(),
            static_cast<UINT32>(
                kCategories[index].label.size()),
            small_format_.Get(),
            bounds,
            selected
                ? selected_text_brush_.Get()
                : text_brush_.Get());
        category_visuals_.push_back(
            IndexedVisual{index, bounds});
    }

    const auto action_top = tab_top + 42.0F;
    auto action_right = right;
    const bool details_visible =
        selected_key_.has_value() &&
        SelectedProfile() != nullptr &&
        SelectedProfile()
            ->GetAction(selected_layer_, *selected_key_)
            .IsTapHold() &&
        (selected_category_ == ActionCategory::Layer ||
         selected_category_ == ActionCategory::Modifiers);
    if (details_visible) {
        action_right -= 310.0F;
    }

    if (selected_key_.has_value()) {
        reset_key_bounds_ = D2D1::RectF(
            action_right - 88.0F,
            action_top,
            action_right,
            action_top + 29.0F);
        render_target_->FillRoundedRectangle(
            D2D1::RoundedRect(reset_key_bounds_, 7.0F, 7.0F),
            card_brush_.Get());
        render_target_->DrawRoundedRectangle(
            D2D1::RoundedRect(reset_key_bounds_, 7.0F, 7.0F),
            border_brush_.Get(),
            1.0F);
        static constexpr wchar_t kReset[] = L"恢复该键";
        render_target_->DrawTextW(
            kReset,
            static_cast<UINT32>(std::size(kReset) - 1),
            small_format_.Get(),
            reset_key_bounds_,
            text_brush_.Get());
        action_right -= 100.0F;
    }

    if (palette_actions_.empty()) {
        return;
    }

    const auto available_width = action_right - left;
    constexpr float gap = 5.0F;
    const auto columns = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(
            std::floor((available_width + gap) / 66.0F)));
    const auto button_width =
        (available_width -
         gap * static_cast<float>(columns - 1)) /
        static_cast<float>(columns);
    constexpr float button_height = 32.0F;

    for (std::size_t index = 0;
         index < palette_actions_.size();
         ++index) {
        const auto row = index / columns;
        const auto column = index % columns;
        const auto x =
            left + static_cast<float>(column) *
                       (button_width + gap);
        const auto y =
            action_top + static_cast<float>(row) *
                             (button_height + gap);
        const auto bounds = D2D1::RectF(
            x,
            y,
            x + button_width,
            y + button_height);
        if (bounds.bottom > panel_bounds.bottom - 8.0F) {
            break;
        }

        const auto& choice = palette_actions_[index];
        const auto selected = IsPaletteActionSelected(choice);
        render_target_->FillRoundedRectangle(
            D2D1::RoundedRect(bounds, 6.0F, 6.0F),
            selected
                ? accent_brush_.Get()
                : choice.enabled
                      ? card_brush_.Get()
                      : disabled_brush_.Get());
        render_target_->DrawRoundedRectangle(
            D2D1::RoundedRect(bounds, 6.0F, 6.0F),
            selected
                ? accent_brush_.Get()
                : border_brush_.Get(),
            1.0F);
        render_target_->DrawTextW(
            choice.label.c_str(),
            static_cast<UINT32>(choice.label.size()),
            small_format_.Get(),
            bounds,
            selected
                ? selected_text_brush_.Get()
                : choice.enabled
                      ? text_brush_.Get()
                      : muted_text_brush_.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
        palette_visuals_.push_back(
            IndexedVisual{index, bounds});
    }
}

void MainWindow::DrawTapHoldDetails() {
    if (!IsWindowVisible(tap_term_edit_)) {
        return;
    }

    const auto size = render_target_->GetSize();
    const auto panel_top =
        std::max(500.0F, size.height - 280.0F);
    const auto bounds = D2D1::RectF(
        size.width - 326.0F,
        panel_top + 53.0F,
        size.width - 42.0F,
        size.height - 78.0F);
    render_target_->FillRoundedRectangle(
        D2D1::RoundedRect(bounds, 9.0F, 9.0F),
        card_brush_.Get());
    render_target_->DrawRoundedRectangle(
        D2D1::RoundedRect(bounds, 9.0F, 9.0F),
        accent_brush_.Get(),
        1.2F);

    static constexpr wchar_t kTitle[] = L"Tap-Hold 参数";
    static constexpr wchar_t kHint[] =
        L"第二键快速Hold与修饰键释放保护固定启用";
    render_target_->DrawTextW(
        kTitle,
        static_cast<UINT32>(std::size(kTitle) - 1),
        section_format_.Get(),
        D2D1::RectF(
            bounds.left + 14.0F,
            bounds.top + 10.0F,
            bounds.right - 14.0F,
            bounds.top + 36.0F),
        text_brush_.Get());
    render_target_->DrawTextW(
        kHint,
        static_cast<UINT32>(std::size(kHint) - 1),
        small_format_.Get(),
        D2D1::RectF(
            bounds.left + 12.0F,
            bounds.bottom - 34.0F,
            bounds.right - 12.0F,
            bounds.bottom - 8.0F),
        muted_text_brush_.Get());
}

void MainWindow::LoadConfiguration() {
    std::wstring error;
    if (!ConfigStore::Load(
            ConfigStore::DefaultPath(),
            applied_configuration_,
            error)) {
        applied_configuration_ = {};
        ShowError(error);
    }

    configuration_ = applied_configuration_;
    dirty_ = false;

    const auto draft_path = ConfigStore::DraftPath();
    std::error_code filesystem_error;
    if (std::filesystem::exists(
            draft_path,
            filesystem_error)) {
        Configuration draft;
        if (ConfigStore::Load(draft_path, draft, error)) {
            const auto config_time =
                std::filesystem::exists(
                    ConfigStore::DefaultPath(),
                    filesystem_error)
                    ? std::filesystem::last_write_time(
                          ConfigStore::DefaultPath(),
                          filesystem_error)
                    : std::filesystem::file_time_type::min();
            const auto draft_time =
                std::filesystem::last_write_time(
                    draft_path,
                    filesystem_error);
            if (!filesystem_error &&
                draft_time >= config_time) {
                configuration_ = std::move(draft);
                dirty_ = true;
            }
        } else {
            ShowError(L"无法恢复配置草稿：" + error);
        }
    }

    PopulateProfiles();
    selected_layer_ = 0;
    selected_key_.reset();
    PopulatePaletteActions();
    UpdateEditorState();
    SetStatus(
        dirty_
            ? L"已恢复上次未应用的草稿。"
            : L"选择配置和键位后，在下方动作面板中设置映射。");
}

void MainWindow::PopulateProfiles() {
    loading_controls_ = true;
    SendMessageW(profile_list_, LB_RESETCONTENT, 0, 0);
    SendMessageW(
        profile_list_,
        LB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(
            std::wstring(kNormalModeName).c_str()));

    int selected_index = 0;
    for (std::size_t index = 0;
         index < configuration_.profiles.size();
         ++index) {
        const auto& profile = configuration_.profiles[index];
        SendMessageW(
            profile_list_,
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                profile.name().c_str()));

        if (profile.name() ==
            configuration_.active_profile) {
            selected_index = static_cast<int>(index + 1);
        }
    }

    SendMessageW(
        profile_list_,
        LB_SETCURSEL,
        static_cast<WPARAM>(selected_index),
        0);
    loading_controls_ = false;
}

void MainWindow::PopulateLayoutChoices() {
    loading_controls_ = true;
    SendMessageW(layout_combo_, CB_RESETCONTENT, 0, 0);
    for (unsigned int value = 0;
         value <= static_cast<unsigned int>(
                      KeyboardLayoutPreset::Laptop);
         ++value) {
        const auto name = LayoutName(
            static_cast<KeyboardLayoutPreset>(value));
        SendMessageW(
            layout_combo_,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(name.c_str()));
    }
    loading_controls_ = false;
}

void MainWindow::PopulatePaletteActions() {
    palette_actions_.clear();

    const auto add_key =
        [this](const KeySpec& spec) {
            palette_actions_.push_back(
                PaletteAction{
                    std::wstring(spec.label),
                    {},
                    Action::Key(spec.key),
                    true});
        };

    switch (selected_category_) {
    case ActionCategory::Basic:
        for (const auto& key : kNumberRow) {
            add_key(key);
        }
        for (const auto& key : kTopRow) {
            add_key(key);
        }
        for (const auto& key : kHomeRow) {
            add_key(key);
        }
        for (const auto& key : kBottomRow) {
            if (key.key != PhysicalKey{0x2A, KeyPrefix::None} &&
                key.key != PhysicalKey{0x36, KeyPrefix::None}) {
                add_key(key);
            }
        }
        add_key(KeySpec{L"Space", {0x39, KeyPrefix::None}, 1.0F});
        add_key(KeySpec{L"Tab", {0x0F, KeyPrefix::None}, 1.0F});
        add_key(KeySpec{L"Enter", {0x1C, KeyPrefix::None}, 1.0F});
        add_key(KeySpec{L"Esc", {0x01, KeyPrefix::None}, 1.0F});
        break;

    case ActionCategory::Function:
        for (const auto& key : kFunctionRow) {
            add_key(key);
        }
        for (const auto& key : kNavigationKeys) {
            add_key(key);
        }
        for (const auto& key : kArrowKeys) {
            add_key(key);
        }
        for (const auto& key : kSystemKeys) {
            add_key(key);
        }
        break;

    case ActionCategory::Modifiers:
        for (const auto& key : kModifierKeys) {
            add_key(key);
        }
        if (selected_key_.has_value()) {
            for (const auto& key : kModifierKeys) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"MT " + std::wstring(key.label),
                        L"短按原键，长按修饰键",
                        Action::ModTap(
                            *selected_key_,
                            key.key,
                            500,
                            200),
                        true});
            }
        }
        break;

    case ActionCategory::Layer: {
        const auto* profile = SelectedProfile();
        if (profile == nullptr) {
            break;
        }
        for (std::size_t layer = 0;
             layer < profile->layer_count();
             ++layer) {
            if (layer == selected_layer_) {
                continue;
            }
            palette_actions_.push_back(
                PaletteAction{
                    L"MO(" + std::to_wstring(layer) + L")",
                    L"按住启用层",
                    Action::MomentaryLayer(
                        static_cast<std::uint8_t>(layer)),
                    true});
            if (selected_key_.has_value()) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"LT(" + std::to_wstring(layer) + L")",
                        L"短按原键，长按启用层",
                        Action::LayerTap(
                            *selected_key_,
                            static_cast<std::uint8_t>(layer),
                            500,
                            200),
                        true});
            }
        }
        for (const auto label :
             {L"TG(n)", L"TO(n)", L"DF(n)", L"OSL(n)", L"TT(n)"}) {
            palette_actions_.push_back(
                PaletteAction{
                    label,
                    L"后续Vial兼容里程碑",
                    Action::Transparent(),
                    false});
        }
        break;
    }

    case ActionCategory::Advanced:
        palette_actions_.push_back(
            PaletteAction{
                selected_layer_ == 0
                    ? L"原样透传"
                    : L"透明",
                {},
                selected_layer_ == 0
                    ? Action::PassThrough()
                    : Action::Transparent(),
                true});
        palette_actions_.push_back(
            PaletteAction{
                L"禁用",
                {},
                Action::Block(),
                true});
        break;

    case ActionCategory::System:
        ForEachShortcutAction(
            [this](
                std::wstring label,
                const Action& action) {
                palette_actions_.push_back(
                    PaletteAction{
                        std::move(label),
                        L"常用系统快捷操作",
                        action,
                        true});
            });
        break;

    case ActionCategory::Media:
        for (const auto& item :
             std::array{
                 std::pair{L"静音", VK_VOLUME_MUTE},
                 std::pair{L"音量−", VK_VOLUME_DOWN},
                 std::pair{L"音量＋", VK_VOLUME_UP},
                 std::pair{L"播放/暂停", VK_MEDIA_PLAY_PAUSE},
                 std::pair{L"上一曲", VK_MEDIA_PREV_TRACK},
                 std::pair{L"下一曲", VK_MEDIA_NEXT_TRACK},
                 std::pair{L"停止", VK_MEDIA_STOP},
                 std::pair{L"浏览器后退", VK_BROWSER_BACK},
                 std::pair{L"浏览器前进", VK_BROWSER_FORWARD},
                 std::pair{L"浏览器主页", VK_BROWSER_HOME},
                 std::pair{L"浏览器搜索", VK_BROWSER_SEARCH},
                 std::pair{L"邮件", VK_LAUNCH_MAIL},
                 std::pair{L"媒体播放器", VK_LAUNCH_MEDIA_SELECT},
                 std::pair{L"计算器", VK_LAUNCH_APP2}}) {
            palette_actions_.push_back(
                PaletteAction{
                    item.first,
                    {},
                    Action::VirtualKey(
                        static_cast<std::uint16_t>(
                            item.second)),
                    true});
        }
        break;

    case ActionCategory::Mouse:
        palette_actions_.push_back(
            PaletteAction{
                L"⚙ 鼠标参数",
                L"速度、加速和滚轮步长",
                Action::Transparent(),
                true,
                PaletteCommand::EditMouseSettings});
        palette_actions_.push_back(
            {L"左键", {}, Action::MouseButtonAction(
                               MouseButton::Left), true});
        palette_actions_.push_back(
            {L"右键", {}, Action::MouseButtonAction(
                               MouseButton::Right), true});
        palette_actions_.push_back(
            {L"中键", {}, Action::MouseButtonAction(
                               MouseButton::Middle), true});
        palette_actions_.push_back(
            {L"X1", {}, Action::MouseButtonAction(
                            MouseButton::X1), true});
        palette_actions_.push_back(
            {L"X2", {}, Action::MouseButtonAction(
                            MouseButton::X2), true});
        palette_actions_.push_back(
            {L"鼠标←", {}, Action::MouseMove(-1, 0), true});
        palette_actions_.push_back(
            {L"鼠标→", {}, Action::MouseMove(1, 0), true});
        palette_actions_.push_back(
            {L"鼠标↑", {}, Action::MouseMove(0, -1), true});
        palette_actions_.push_back(
            {L"鼠标↓", {}, Action::MouseMove(0, 1), true});
        palette_actions_.push_back(
            {L"滚轮↑", {}, Action::MouseWheel(1), true});
        palette_actions_.push_back(
            {L"滚轮↓", {}, Action::MouseWheel(-1), true});
        palette_actions_.push_back(
            {L"滚轮←", {}, Action::MouseWheel(-1, true), true});
        palette_actions_.push_back(
            {L"滚轮→", {}, Action::MouseWheel(1, true), true});
        break;

    case ActionCategory::Macro:
        if (const auto* profile = SelectedProfile();
            profile != nullptr) {
            for (const auto& macro : profile->macros()) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"宏：" + macro.name,
                        L"单次播放；右键可编辑或删除",
                        Action::Macro(macro.id),
                        true,
                        PaletteCommand::None,
                        AdvancedRuleKind::Macro,
                        macro.id});
            }
            if (profile->macros().size() < kMaximumMacros) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"＋ 录制新宏",
                        L"记录按键按下、释放和间隔",
                        Action::Transparent(),
                        true,
                        PaletteCommand::CreateMacro});
            }
            palette_actions_.push_back(
                PaletteAction{
                    L"停止所有宏",
                    {},
                    Action::StopMacros(),
                    true});
        }
        break;

    case ActionCategory::TapDance:
        if (const auto* profile = SelectedProfile();
            profile != nullptr) {
            for (const auto& tap_dance :
                 profile->tap_dances()) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"TD：" + tap_dance.name,
                        L"Tap Dance四动作；右键可编辑或删除",
                        Action::TapDance(tap_dance.id),
                        true,
                        PaletteCommand::None,
                        AdvancedRuleKind::TapDance,
                        tap_dance.id});
            }
            if (profile->tap_dances().size() <
                kMaximumTapDances) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"＋ 新建 Tap Dance",
                        L"设置单击、长按、双击和单击后长按",
                        Action::Transparent(),
                        true,
                        PaletteCommand::CreateTapDance});
            }
        }
        break;

    case ActionCategory::Combo:
        if (const auto* profile = SelectedProfile();
            profile != nullptr) {
            for (const auto& combo : profile->combos()) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"Combo：" + combo.name,
                        std::to_wstring(combo.member_count) +
                            L"键 / " +
                            std::to_wstring(combo.term_ms) +
                            L"ms；左键编辑，右键可删除",
                        Action::Transparent(),
                        false,
                        PaletteCommand::None,
                        AdvancedRuleKind::Combo,
                        combo.id});
            }
            if (profile->combos().size() <
                kMaximumCombos) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"＋ 新建 Combo",
                        L"按映射后的键值识别",
                        Action::Transparent(),
                        true,
                        PaletteCommand::CreateCombo});
            }
        }
        break;

    case ActionCategory::Override:
        if (const auto* profile = SelectedProfile();
            profile != nullptr) {
            for (const auto& rule : profile->overrides()) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"Override：" + rule.name,
                        L"修饰键＋触发键；左键编辑，右键可删除",
                        Action::Transparent(),
                        false,
                        PaletteCommand::None,
                        AdvancedRuleKind::Override,
                        rule.id});
            }
            if (profile->overrides().size() <
                kMaximumOverrides) {
                palette_actions_.push_back(
                    PaletteAction{
                        L"＋ 新建 Override",
                        L"按映射后的键值和修饰键判断",
                        Action::Transparent(),
                        true,
                        PaletteCommand::CreateOverride});
            }
        }
        break;

    }
}

void MainWindow::UpdateEditorState() {
    const auto* profile = SelectedProfile();
    const bool editable = profile != nullptr;

    loading_controls_ = true;
    SetWindowTextW(
        profile_name_edit_,
        editable
            ? profile->name().c_str()
            : std::wstring(kNormalModeName).c_str());
    SendMessageW(
        layout_combo_,
        CB_SETCURSEL,
        editable
            ? static_cast<WPARAM>(profile->layout())
            : static_cast<WPARAM>(
                  KeyboardLayoutPreset::FullSize104),
        0);
    loading_controls_ = false;

    EnableWindow(profile_name_edit_, editable);
    EnableWindow(delete_profile_button_, editable);
    EnableWindow(layout_combo_, editable);
    EnableWindow(discard_button_, dirty_);
    EnableWindow(save_apply_button_, TRUE);

    if (!editable) {
        selected_key_.reset();
        selected_layer_ = 0;
    } else if (selected_layer_ >= profile->layer_count()) {
        selected_layer_ = profile->layer_count() - 1;
    }

    UpdateTapHoldControls();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::UpdateTapHoldControls() {
    bool visible = false;
    Action action{};
    if (selected_key_.has_value()) {
        if (const auto* profile = SelectedProfile();
            profile != nullptr) {
            action = profile->GetAction(
                selected_layer_,
                *selected_key_);
            visible =
                action.IsTapHold() &&
                (selected_category_ == ActionCategory::Layer ||
                 selected_category_ ==
                     ActionCategory::Modifiers);
        }
    }

    for (const auto control :
         {tap_term_label_,
          tap_term_edit_,
          quick_tap_label_,
          quick_tap_edit_,
          update_timing_button_}) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
        EnableWindow(control, visible);
    }

    if (visible) {
        loading_controls_ = true;
        SetWindowTextW(
            tap_term_edit_,
            std::to_wstring(action.tapping_term_ms).c_str());
        SetWindowTextW(
            quick_tap_edit_,
            std::to_wstring(action.quick_tap_term_ms).c_str());
        loading_controls_ = false;
    }
}

void MainWindow::SelectProfileFromList() {
    const auto selection = static_cast<int>(
        SendMessageW(profile_list_, LB_GETCURSEL, 0, 0));
    if (selection <= 0) {
        configuration_.active_profile =
            std::wstring(kNormalModeName);
    } else {
        const auto index =
            static_cast<std::size_t>(selection - 1);
        if (index < configuration_.profiles.size()) {
            configuration_.active_profile =
                configuration_.profiles[index].name();
        }
    }

    selected_layer_ = 0;
    selected_key_.reset();
    selected_category_ = ActionCategory::Basic;
    PopulatePaletteActions();
    MarkDirty(
        L"当前配置选择已保存到草稿，点击“保存并应用”后生效。");
    UpdateEditorState();
}

void MainWindow::SelectLayer(const std::size_t layer) {
    const auto* profile = SelectedProfile();
    if (profile == nullptr || layer >= profile->layer_count()) {
        return;
    }

    selected_layer_ = layer;
    PopulatePaletteActions();
    UpdateEditorState();
    SetStatus(
        L"正在编辑 Layer " + std::to_wstring(layer) + L"。");
}

void MainWindow::SelectCategory(
    const ActionCategory category) {
    selected_category_ = category;
    PopulatePaletteActions();
    UpdateTapHoldControls();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::ApplyPaletteAction(
    const std::size_t index) {
    if (index >= palette_actions_.size()) {
        return;
    }

    const auto& choice = palette_actions_[index];
    if (choice.command == PaletteCommand::CreateMacro) {
        CreateAndBindMacro();
        return;
    }
    if (choice.command == PaletteCommand::CreateTapDance) {
        CreateAndBindTapDance();
        return;
    }
    if (choice.command == PaletteCommand::CreateCombo) {
        CreateCombo();
        return;
    }
    if (choice.command == PaletteCommand::CreateOverride) {
        CreateOverride();
        return;
    }
    if (choice.command == PaletteCommand::EditMouseSettings) {
        EditMouseSettings();
        return;
    }
    if (!choice.enabled &&
        choice.rule_kind != AdvancedRuleKind::None &&
        choice.reference_id != 0) {
        EditAdvancedRule(
            choice.rule_kind,
            choice.reference_id);
        return;
    }
    if (!choice.enabled) {
        SetStatus(
            choice.description.empty()
                ? L"该功能将在后续Vial兼容里程碑接入。"
                : choice.description);
        return;
    }

    auto* profile = SelectedProfile();
    if (profile == nullptr || !selected_key_.has_value()) {
        SetStatus(L"请先点击上方键盘中的一个键位。");
        return;
    }

    if ((choice.action.kind == ActionKind::MomentaryLayer ||
         choice.action.kind == ActionKind::LayerTap) &&
        choice.action.target_layer == selected_layer_) {
        SetStatus(L"不能从当前层瞬时切换到自身。");
        return;
    }

    profile->SetAction(
        selected_layer_,
        *selected_key_,
        choice.action);
    MarkDirty(
        L"键位已写入草稿，当前生效配置尚未改变。");
    UpdateTapHoldControls();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::CreateAndBindMacro() {
    auto* profile = SelectedProfile();
    if (profile == nullptr || !selected_key_.has_value()) {
        SetStatus(L"请先选择配置和需要绑定宏的键位。");
        return;
    }
    if (profile->macros().size() >= kMaximumMacros) {
        SetStatus(L"当前配置已达到32个宏的上限。");
        return;
    }

    std::uint16_t next_id = 1;
    for (const auto& macro : profile->macros()) {
        if (macro.id >= next_id &&
            macro.id <
                std::numeric_limits<std::uint16_t>::max()) {
            next_id =
                static_cast<std::uint16_t>(macro.id + 1);
        }
    }

    auto macro = ShowMacroEditor(
        window_,
        instance_,
        next_id,
        L"宏 " + std::to_wstring(next_id));
    if (!macro.has_value()) {
        return;
    }

    const auto macro_id = macro->id;
    profile->macros().push_back(std::move(*macro));
    profile->SetAction(
        selected_layer_,
        *selected_key_,
        Action::Macro(macro_id));
    PopulatePaletteActions();
    MarkDirty(L"宏已录制并绑定到当前键位。");
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::CreateAndBindTapDance() {
    auto* profile = SelectedProfile();
    if (profile == nullptr || !selected_key_.has_value()) {
        SetStatus(
            L"请先选择配置和需要绑定Tap Dance的键位。");
        return;
    }
    if (profile->tap_dances().size() >=
        kMaximumTapDances) {
        SetStatus(L"当前配置已达到64个Tap Dance上限。");
        return;
    }

    std::uint16_t next_id = 1;
    for (const auto& definition : profile->tap_dances()) {
        if (definition.id >= next_id &&
            definition.id <
                std::numeric_limits<std::uint16_t>::max()) {
            next_id = static_cast<std::uint16_t>(
                definition.id + 1);
        }
    }

    auto definition = ShowTapDanceEditor(
        window_,
        instance_,
        next_id,
        *selected_key_,
        *profile);
    if (!definition.has_value()) {
        return;
    }

    const auto id = definition->id;
    profile->tap_dances().push_back(
        std::move(*definition));
    profile->SetAction(
        selected_layer_,
        *selected_key_,
        Action::TapDance(id));
    PopulatePaletteActions();
    MarkDirty(L"Tap Dance已创建并绑定到当前键位。");
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::CreateCombo() {
    auto* profile = SelectedProfile();
    if (profile == nullptr) {
        SetStatus(L"请先选择一个可编辑配置。");
        return;
    }
    if (profile->combos().size() >= kMaximumCombos) {
        SetStatus(L"当前配置已达到64个Combo上限。");
        return;
    }

    std::uint16_t next_id = 1;
    for (const auto& combo : profile->combos()) {
        if (combo.id >= next_id &&
            combo.id <
                std::numeric_limits<std::uint16_t>::max()) {
            next_id =
                static_cast<std::uint16_t>(combo.id + 1);
        }
    }

    auto combo = ShowComboEditor(
        window_,
        instance_,
        next_id,
        selected_layer_,
        *profile);
    if (!combo.has_value()) {
        return;
    }

    profile->combos().push_back(std::move(*combo));
    PopulatePaletteActions();
    MarkDirty(L"Combo规则已保存到草稿。");
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::CreateOverride() {
    auto* profile = SelectedProfile();
    if (profile == nullptr) {
        SetStatus(L"请先选择一个可编辑配置。");
        return;
    }
    if (profile->overrides().size() >=
        kMaximumOverrides) {
        SetStatus(
            L"当前配置已达到64个Key Override上限。");
        return;
    }

    std::uint16_t next_id = 1;
    for (const auto& rule : profile->overrides()) {
        if (rule.id >= next_id &&
            rule.id <
                std::numeric_limits<std::uint16_t>::max()) {
            next_id =
                static_cast<std::uint16_t>(rule.id + 1);
        }
    }

    auto rule = ShowOverrideEditor(
        window_,
        instance_,
        next_id,
        selected_layer_,
        *profile);
    if (!rule.has_value()) {
        return;
    }

    profile->overrides().push_back(std::move(*rule));
    PopulatePaletteActions();
    MarkDirty(L"Key Override规则已保存到草稿。");
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::EditMouseSettings() {
    auto* profile = SelectedProfile();
    if (profile == nullptr) {
        SetStatus(L"请先选择一个可编辑配置。");
        return;
    }

    auto settings = ShowMouseSettingsEditor(
        window_,
        instance_,
        profile->mouse_settings());
    if (!settings.has_value()) {
        return;
    }

    profile->mouse_settings() = *settings;
    MarkDirty(L"鼠标键参数已保存到草稿。");
}

void MainWindow::ShowPaletteContextMenu(
    const std::size_t index,
    const POINT screen_point) {
    if (index >= palette_actions_.size()) {
        return;
    }

    const auto& choice = palette_actions_[index];
    if (choice.rule_kind == AdvancedRuleKind::None ||
        choice.reference_id == 0) {
        return;
    }

    const auto menu = CreatePopupMenu();
    if (menu == nullptr) {
        ShowError(L"无法创建高级规则菜单。");
        return;
    }

    AppendMenuW(
        menu,
        MF_STRING,
        1,
        L"编辑此规则");
    AppendMenuW(
        menu,
        MF_STRING,
        2,
        L"删除此规则");
    const auto command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        screen_point.x,
        screen_point.y,
        0,
        window_,
        nullptr);
    DestroyMenu(menu);

    if (command == 1) {
        EditAdvancedRule(
            choice.rule_kind,
            choice.reference_id);
    } else if (command == 2) {
        DeleteAdvancedRule(
            choice.rule_kind,
            choice.reference_id);
    }
}

void MainWindow::EditAdvancedRule(
    const AdvancedRuleKind kind,
    const std::uint16_t reference_id) {
    auto* profile = SelectedProfile();
    if (profile == nullptr || reference_id == 0) {
        return;
    }

    const auto layer_for_mask =
        [this](const std::uint32_t mask) {
            if (mask != 0xFFFFFFFFU) {
                for (std::size_t layer = 0;
                     layer < kMaximumLayerCount;
                     ++layer) {
                    if (mask ==
                        static_cast<std::uint32_t>(
                            1U << layer)) {
                        return layer;
                    }
                }
            }
            return selected_layer_;
        };

    bool changed = false;
    switch (kind) {
    case AdvancedRuleKind::Macro: {
        const auto iterator = std::find_if(
            profile->macros().begin(),
            profile->macros().end(),
            [reference_id](const MacroDefinition& item) {
                return item.id == reference_id;
            });
        if (iterator != profile->macros().end()) {
            auto edited = ShowMacroEditor(
                window_,
                instance_,
                *iterator);
            if (edited.has_value()) {
                *iterator = std::move(*edited);
                changed = true;
            }
        }
        break;
    }
    case AdvancedRuleKind::TapDance: {
        const auto iterator = std::find_if(
            profile->tap_dances().begin(),
            profile->tap_dances().end(),
            [reference_id](const TapDanceDefinition& item) {
                return item.id == reference_id;
            });
        if (iterator != profile->tap_dances().end()) {
            const auto source =
                selected_key_.value_or(
                    iterator->tap_action.target_key.IsValid()
                        ? iterator->tap_action.target_key
                        : PhysicalKey{
                              0x01,
                              KeyPrefix::None});
            auto edited = ShowTapDanceEditor(
                window_,
                instance_,
                *iterator,
                source,
                *profile);
            if (edited.has_value()) {
                *iterator = std::move(*edited);
                changed = true;
            }
        }
        break;
    }
    case AdvancedRuleKind::Combo: {
        const auto iterator = std::find_if(
            profile->combos().begin(),
            profile->combos().end(),
            [reference_id](const ComboDefinition& item) {
                return item.id == reference_id;
            });
        if (iterator != profile->combos().end()) {
            auto edited = ShowComboEditor(
                window_,
                instance_,
                *iterator,
                layer_for_mask(iterator->layer_mask),
                *profile);
            if (edited.has_value()) {
                *iterator = std::move(*edited);
                changed = true;
            }
        }
        break;
    }
    case AdvancedRuleKind::Override: {
        const auto iterator = std::find_if(
            profile->overrides().begin(),
            profile->overrides().end(),
            [reference_id](
                const KeyOverrideDefinition& item) {
                return item.id == reference_id;
            });
        if (iterator != profile->overrides().end()) {
            auto edited = ShowOverrideEditor(
                window_,
                instance_,
                *iterator,
                layer_for_mask(iterator->layer_mask),
                *profile);
            if (edited.has_value()) {
                *iterator = std::move(*edited);
                changed = true;
            }
        }
        break;
    }
    case AdvancedRuleKind::None:
        break;
    }

    if (!changed) {
        return;
    }

    PopulatePaletteActions();
    MarkDirty(L"高级规则修改已保存到草稿。");
    UpdateTapHoldControls();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::DeleteAdvancedRule(
    const AdvancedRuleKind kind,
    const std::uint16_t reference_id) {
    auto* profile = SelectedProfile();
    if (profile == nullptr || reference_id == 0) {
        return;
    }

    ResetDeletedRuleReferences(
        *profile,
        kind,
        reference_id);

    bool removed = false;
    switch (kind) {
    case AdvancedRuleKind::Macro: {
        auto& definitions = profile->macros();
        const auto old_size = definitions.size();
        std::erase_if(
            definitions,
            [reference_id](const MacroDefinition& definition) {
                return definition.id == reference_id;
            });
        removed = definitions.size() != old_size;
        break;
    }
    case AdvancedRuleKind::TapDance: {
        auto& definitions = profile->tap_dances();
        const auto old_size = definitions.size();
        std::erase_if(
            definitions,
            [reference_id](
                const TapDanceDefinition& definition) {
                return definition.id == reference_id;
            });
        removed = definitions.size() != old_size;
        break;
    }
    case AdvancedRuleKind::Combo: {
        auto& definitions = profile->combos();
        const auto old_size = definitions.size();
        std::erase_if(
            definitions,
            [reference_id](const ComboDefinition& definition) {
                return definition.id == reference_id;
            });
        removed = definitions.size() != old_size;
        break;
    }
    case AdvancedRuleKind::Override: {
        auto& definitions = profile->overrides();
        const auto old_size = definitions.size();
        std::erase_if(
            definitions,
            [reference_id](
                const KeyOverrideDefinition& definition) {
                return definition.id == reference_id;
            });
        removed = definitions.size() != old_size;
        break;
    }
    case AdvancedRuleKind::None:
        break;
    }

    if (!removed) {
        SetStatus(L"规则已经不存在。");
        return;
    }

    PopulatePaletteActions();
    MarkDirty(
        L"高级规则已从草稿中删除，相关键位引用已恢复默认。");
    UpdateTapHoldControls();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::ResetDeletedRuleReferences(
    Profile& profile,
    const AdvancedRuleKind kind,
    const std::uint16_t reference_id) {
    const auto matches =
        [kind, reference_id](const Action& action) {
            return (kind == AdvancedRuleKind::Macro &&
                    action.kind == ActionKind::Macro &&
                    action.reference_id == reference_id) ||
                   (kind == AdvancedRuleKind::TapDance &&
                    action.kind == ActionKind::TapDance &&
                    action.reference_id == reference_id);
        };

    if (kind == AdvancedRuleKind::Macro ||
        kind == AdvancedRuleKind::TapDance) {
        for (std::size_t layer = 0;
             layer < profile.layer_count();
             ++layer) {
            for (std::size_t index = 0;
                 index < kPhysicalKeySlotCount;
                 ++index) {
                const auto key = FromKeyIndex(index);
                if (matches(profile.GetAction(layer, key))) {
                    profile.SetAction(
                        layer,
                        key,
                        layer == 0
                            ? Action::PassThrough()
                            : Action::Transparent());
                }
            }
        }
    }

    if (kind != AdvancedRuleKind::Macro) {
        return;
    }

    for (auto& definition : profile.tap_dances()) {
        for (auto* action :
             {&definition.tap_action,
              &definition.hold_action,
              &definition.double_tap_action,
              &definition.tap_hold_action}) {
            if (matches(*action)) {
                *action = Action::Transparent();
            }
        }
    }
    for (auto& definition : profile.combos()) {
        if (matches(definition.output_action)) {
            definition.output_action = Action::Block();
        }
    }
    for (auto& definition : profile.overrides()) {
        if (matches(definition.replacement_action)) {
            definition.replacement_action = Action::Block();
        }
    }
}

void MainWindow::AddLayer() {
    auto* profile = SelectedProfile();
    if (profile == nullptr) {
        return;
    }

    if (!profile->AddLayer()) {
        SetStatus(L"当前配置已达到32层上限。");
        return;
    }

    selected_layer_ = profile->layer_count() - 1;
    PopulatePaletteActions();
    MarkDirty(L"已新增Layer并保存到草稿。");
    UpdateEditorState();
}

void MainWindow::RemoveLayer() {
    auto* profile = SelectedProfile();
    if (profile == nullptr || profile->layer_count() <= 1) {
        SetStatus(L"Layer 0不能删除。");
        return;
    }

    if (!CanRemoveLastLayer()) {
        SetStatus(
            L"最高层仍被MO或LT引用，请先移除引用后再删除。");
        return;
    }

    if (!profile->RemoveLastLayer()) {
        return;
    }

    if (selected_layer_ >= profile->layer_count()) {
        selected_layer_ = profile->layer_count() - 1;
    }
    selected_key_.reset();
    PopulatePaletteActions();
    MarkDirty(L"最高Layer已从草稿中删除。");
    UpdateEditorState();
}

void MainWindow::CreateProfile() {
    if (configuration_.profiles.size() >= kMaximumProfiles) {
        SetStatus(L"配置数量已达到上限。请先删除不再使用的配置。");
        return;
    }

    configuration_.profiles.emplace_back(
        UniqueProfileName(),
        kDefaultLayerCount,
        KeyboardLayoutPreset::FullSize104);
    configuration_.active_profile =
        configuration_.profiles.back().name();
    selected_layer_ = 0;
    selected_key_.reset();
    selected_category_ = ActionCategory::Basic;
    PopulateProfiles();
    PopulatePaletteActions();
    MarkDirty(
        L"新配置已创建为草稿，点击“保存并应用”后生效。");
    UpdateEditorState();
    SetFocus(profile_name_edit_);
    SendMessageW(profile_name_edit_, EM_SETSEL, 0, -1);
}

void MainWindow::DeleteProfile() {
    const auto selection = static_cast<int>(
        SendMessageW(profile_list_, LB_GETCURSEL, 0, 0));
    if (selection <= 0) {
        return;
    }

    const auto index =
        static_cast<std::size_t>(selection - 1);
    if (index >= configuration_.profiles.size()) {
        return;
    }

    configuration_.profiles.erase(
        configuration_.profiles.begin() +
        static_cast<std::ptrdiff_t>(index));
    configuration_.active_profile =
        std::wstring(kNormalModeName);
    selected_key_.reset();
    selected_layer_ = 0;
    PopulateProfiles();
    PopulatePaletteActions();
    MarkDirty(
        L"配置已从草稿中删除；可点击“放弃修改”恢复。");
    UpdateEditorState();
}

void MainWindow::DiscardChanges() {
    configuration_ = applied_configuration_;
    dirty_ = false;
    selected_key_.reset();
    selected_layer_ = 0;
    selected_category_ = ActionCategory::Basic;

    std::error_code error;
    std::filesystem::remove(ConfigStore::DraftPath(), error);

    PopulateProfiles();
    PopulatePaletteActions();
    UpdateEditorState();
    SetStatus(L"已放弃草稿，恢复到当前正式配置。");
}

void MainWindow::ResetSelectedMapping() {
    auto* profile = SelectedProfile();
    if (profile == nullptr || !selected_key_.has_value()) {
        return;
    }

    profile->SetAction(
        selected_layer_,
        *selected_key_,
        selected_layer_ == 0
            ? Action::PassThrough()
            : Action::Transparent());
    MarkDirty(L"该键已在草稿中恢复为当前层默认行为。");
    UpdateTapHoldControls();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::SaveAndApply() {
    if (SelectedProfile() != nullptr &&
        !RenameSelectedProfile(true)) {
        return;
    }

    const auto selection = static_cast<int>(
        SendMessageW(profile_list_, LB_GETCURSEL, 0, 0));
    if (selection <= 0) {
        configuration_.active_profile =
            std::wstring(kNormalModeName);
    } else {
        const auto index =
            static_cast<std::size_t>(selection - 1);
        if (index < configuration_.profiles.size()) {
            configuration_.active_profile =
                configuration_.profiles[index].name();
        }
    }

    const auto formal_path = ConfigStore::DefaultPath();
    auto rollback_path = formal_path;
    rollback_path += L".rollback";

    std::error_code filesystem_error;
    const bool had_formal_configuration =
        std::filesystem::exists(
            formal_path,
            filesystem_error);
    if (filesystem_error) {
        ShowError(L"无法检查当前正式配置文件。");
        return;
    }

    if (had_formal_configuration) {
        std::filesystem::copy_file(
            formal_path,
            rollback_path,
            std::filesystem::copy_options::overwrite_existing,
            filesystem_error);
        if (filesystem_error) {
            ShowError(L"无法创建保存前的配置回滚副本。");
            return;
        }
    } else {
        std::filesystem::remove(
            rollback_path,
            filesystem_error);
        filesystem_error.clear();
    }

    std::wstring error;
    if (!ConfigStore::SaveAtomic(
            formal_path,
            configuration_,
            error)) {
        std::filesystem::remove(
            rollback_path,
            filesystem_error);
        ShowError(error);
        return;
    }

    if (!IpcClient::ReloadConfiguration(
            configuration_.active_profile,
            error)) {
        bool rolled_back = false;
        if (had_formal_configuration) {
            std::filesystem::copy_file(
                rollback_path,
                formal_path,
                std::filesystem::copy_options::overwrite_existing,
                filesystem_error);
            rolled_back = !filesystem_error;
        } else {
            rolled_back =
                std::filesystem::remove(
                    formal_path,
                    filesystem_error) ||
                !std::filesystem::exists(
                    formal_path,
                    filesystem_error);
        }
        const auto rollback_error =
            filesystem_error.message();
        filesystem_error.clear();
        std::filesystem::remove(
            rollback_path,
            filesystem_error);
        dirty_ = true;
        SaveDraft();
        UpdateEditorState();
        ShowError(
            rolled_back
                ? L"核心程序未能加载新配置，正式配置已回滚，"
                  L"新内容仍保留在草稿中。可在核心恢复后再次"
                  L"点击“保存并应用”。\n" +
                      error
                : L"核心程序未能加载新配置，并且正式配置回滚"
                  L"失败。请保留当前编辑器窗口并检查文件权限。\n" +
                      error + L"\n回滚错误：" +
                      std::wstring(
                          rollback_error.begin(),
                          rollback_error.end()));
        return;
    }

    applied_configuration_ = configuration_;
    dirty_ = false;
    std::filesystem::remove(
        rollback_path,
        filesystem_error);
    filesystem_error.clear();
    std::filesystem::remove(
        ConfigStore::DraftPath(),
        filesystem_error);

    PopulateProfiles();
    PopulatePaletteActions();
    UpdateEditorState();
    SetStatus(L"配置已保存并应用到所有键盘。");
}

void MainWindow::SaveDraft() {
    std::wstring error;
    if (!ConfigStore::SaveAtomic(
            ConfigStore::DraftPath(),
            configuration_,
            error)) {
        ShowError(L"无法保存配置草稿：" + error);
    }
}

void MainWindow::MarkDirty(
    const std::wstring_view status) {
    dirty_ = true;
    SaveDraft();
    EnableWindow(discard_button_, TRUE);
    SetStatus(status);
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::UpdateTapHoldTiming() {
    auto* profile = SelectedProfile();
    if (profile == nullptr || !selected_key_.has_value()) {
        return;
    }

    auto action =
        profile->GetAction(selected_layer_, *selected_key_);
    if (!action.IsTapHold()) {
        return;
    }

    const auto tapping =
        ParseUnsigned(ReadWindowText(tap_term_edit_));
    const auto quick =
        ParseUnsigned(ReadWindowText(quick_tap_edit_));
    if (!tapping.has_value() ||
        *tapping < 100 ||
        *tapping > 1500 ||
        *tapping % 10 != 0) {
        SetStatus(
            L"长按阈值必须在100～1500ms之间，并以10ms递增。");
        return;
    }
    if (!quick.has_value() ||
        *quick > 500 ||
        *quick % 10 != 0) {
        SetStatus(
            L"Quick Tap必须在0～500ms之间，并以10ms递增。");
        return;
    }

    action.tapping_term_ms =
        static_cast<std::uint16_t>(*tapping);
    action.quick_tap_term_ms =
        static_cast<std::uint16_t>(*quick);
    profile->SetAction(
        selected_layer_,
        *selected_key_,
        action);
    MarkDirty(L"Tap-Hold参数已保存到草稿。");
}

Profile* MainWindow::SelectedProfile() noexcept {
    const auto selection = static_cast<int>(
        SendMessageW(profile_list_, LB_GETCURSEL, 0, 0));
    if (selection <= 0) {
        return nullptr;
    }

    const auto index =
        static_cast<std::size_t>(selection - 1);
    return index < configuration_.profiles.size()
               ? &configuration_.profiles[index]
               : nullptr;
}

const Profile* MainWindow::SelectedProfile() const noexcept {
    return const_cast<MainWindow*>(this)->SelectedProfile();
}

std::wstring MainWindow::ReadWindowText(
    const HWND control) const {
    const auto length = GetWindowTextLengthW(control);
    std::wstring text(
        static_cast<std::size_t>(length) + 1,
        L'\0');
    if (length > 0) {
        GetWindowTextW(control, text.data(), length + 1);
    }
    text.resize(static_cast<std::size_t>(length));
    return text;
}

bool MainWindow::RenameSelectedProfile(
    const bool strict,
    bool* const changed) {
    if (changed != nullptr) {
        *changed = false;
    }

    auto* profile = SelectedProfile();
    if (profile == nullptr) {
        return true;
    }

    auto name = Trim(ReadWindowText(profile_name_edit_));
    if (name.empty()) {
        if (strict) {
            SetStatus(L"配置名称不能为空。");
        }
        return false;
    }

    if (name == kNormalModeName) {
        if (strict) {
            SetStatus(L"“普通模式”是保留名称。");
        }
        return false;
    }

    for (const auto& candidate : configuration_.profiles) {
        if (&candidate != profile &&
            candidate.name() == name) {
            if (strict) {
                SetStatus(L"已经存在同名配置。");
            }
            return false;
        }
    }

    if (profile->name() == name) {
        return true;
    }

    const auto old_name = profile->name();
    profile->SetName(std::move(name));
    if (configuration_.active_profile == old_name) {
        configuration_.active_profile = profile->name();
    }
    if (changed != nullptr) {
        *changed = true;
    }
    return true;
}

std::wstring MainWindow::UniqueProfileName() const {
    for (std::size_t number = 1;; ++number) {
        auto candidate =
            L"新配置 " + std::to_wstring(number);
        if (configuration_.FindProfile(candidate) == nullptr) {
            return candidate;
        }
    }
}

std::wstring MainWindow::LabelForKey(
    const PhysicalKey key) const {
    std::wstring label;
    ForEachCatalogKey(
        [&label, key](const KeySpec& spec) {
            if (label.empty() && spec.key == key) {
                label = spec.label;
            }
        });

    if (!label.empty()) {
        return label;
    }

    wchar_t buffer[32]{};
    std::swprintf(
        buffer,
        std::size(buffer),
        L"SC%02X%s",
        key.scan_code,
        key.prefix == KeyPrefix::E0
            ? L"-E0"
            : key.prefix == KeyPrefix::E1
                  ? L"-E1"
                  : L"");
    return buffer;
}

std::wstring MainWindow::ActionLabel(
    const Action& action,
    const PhysicalKey source) const {
    switch (action.kind) {
    case ActionKind::Transparent:
        return selected_layer_ == 0 ? L"" : L"▽";
    case ActionKind::PassThrough:
        return L"";
    case ActionKind::Block:
        return L"禁用";
    case ActionKind::Key:
        if (action.target_key == source) {
            return L"";
        }
        return L"→" + LabelForKey(action.target_key);
    case ActionKind::MomentaryLayer:
        return L"MO(" +
               std::to_wstring(action.target_layer) +
               L")";
    case ActionKind::LayerTap:
        return LabelForKey(action.target_key) +
               L"/L" +
               std::to_wstring(action.target_layer);
    case ActionKind::ModTap:
        return LabelForKey(action.target_key) +
               L"/" +
               LabelForKey(action.hold_key);
    case ActionKind::VirtualKey:
        switch (action.virtual_key) {
        case VK_VOLUME_MUTE:
            return L"静音";
        case VK_VOLUME_DOWN:
            return L"音量−";
        case VK_VOLUME_UP:
            return L"音量＋";
        case VK_MEDIA_PLAY_PAUSE:
            return L"播放";
        case VK_MEDIA_PREV_TRACK:
            return L"上一曲";
        case VK_MEDIA_NEXT_TRACK:
            return L"下一曲";
        case VK_MEDIA_STOP:
            return L"停止";
        default:
            return L"媒体键";
        }
    case ActionKind::MouseButton:
        switch (action.mouse_button) {
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
        break;
    case ActionKind::MouseMove:
        if (action.mouse_x < 0) {
            return L"鼠标←";
        }
        if (action.mouse_x > 0) {
            return L"鼠标→";
        }
        return action.mouse_y < 0 ? L"鼠标↑" : L"鼠标↓";
    case ActionKind::MouseWheel:
        if (action.mouse_x != 0) {
            return action.mouse_amount < 0
                       ? L"滚轮←"
                       : L"滚轮→";
        }
        return action.mouse_amount < 0
                   ? L"滚轮↓"
                   : L"滚轮↑";
    case ActionKind::Macro:
        return L"宏#" + std::to_wstring(action.reference_id);
    case ActionKind::StopMacros:
        return L"停止宏";
    case ActionKind::TapDance:
        return L"TD#" + std::to_wstring(action.reference_id);
    case ActionKind::Shortcut:
        {
            std::wstring label;
            ForEachShortcutAction(
                [&label, &action](
                    std::wstring candidate,
                    const Action& shortcut) {
                    if (ActionsEqual(action, shortcut)) {
                        label = std::move(candidate);
                    }
                });
            return label.empty() ? L"快捷操作" : label;
        }
    }
    return {};
}

bool MainWindow::CanRemoveLastLayer() const noexcept {
    const auto* profile = SelectedProfile();
    if (profile == nullptr || profile->layer_count() <= 1) {
        return false;
    }

    const auto target =
        static_cast<std::uint8_t>(profile->layer_count() - 1);
    for (std::size_t layer = 0;
         layer < profile->layer_count();
         ++layer) {
        for (std::size_t index = 0;
             index < kPhysicalKeySlotCount;
             ++index) {
            const auto action =
                profile->GetAction(layer, FromKeyIndex(index));
            if ((action.kind == ActionKind::MomentaryLayer ||
                 action.kind == ActionKind::LayerTap) &&
                 action.target_layer == target) {
                return false;
            }
        }
    }

    for (const auto& tap_dance : profile->tap_dances()) {
        for (const auto* action :
             {&tap_dance.tap_action,
              &tap_dance.hold_action,
              &tap_dance.double_tap_action,
              &tap_dance.tap_hold_action}) {
            if (action->kind == ActionKind::MomentaryLayer &&
                action->target_layer == target) {
                return false;
            }
        }
    }

    for (const auto& combo : profile->combos()) {
        if (combo.output_action.kind ==
                ActionKind::MomentaryLayer &&
            combo.output_action.target_layer == target) {
            return false;
        }
    }
    for (const auto& override_rule : profile->overrides()) {
        if (override_rule.replacement_action.kind ==
                ActionKind::MomentaryLayer &&
            override_rule.replacement_action.target_layer == target) {
            return false;
        }
    }
    return true;
}

bool MainWindow::IsPaletteActionSelected(
    const PaletteAction& choice) const noexcept {
    if (!choice.enabled ||
        !selected_key_.has_value() ||
        SelectedProfile() == nullptr) {
        return false;
    }

    const auto action = SelectedProfile()->GetAction(
        selected_layer_,
        *selected_key_);
    return ActionsEqual(action, choice.action);
}

std::optional<PhysicalKey> MainWindow::HitTestKey(
    const float x,
    const float y) const noexcept {
    const auto iterator = std::find_if(
        key_visuals_.begin(),
        key_visuals_.end(),
        [this, x, y](const KeyVisual& visual) {
            return HitTestRect(visual.bounds, x, y);
        });
    return iterator == key_visuals_.end()
               ? std::nullopt
               : std::optional<PhysicalKey>(iterator->key);
}

std::optional<std::size_t> MainWindow::HitTestIndexed(
    const std::vector<IndexedVisual>& visuals,
    const float x,
    const float y) const noexcept {
    const auto iterator = std::find_if(
        visuals.begin(),
        visuals.end(),
        [this, x, y](const IndexedVisual& visual) {
            return HitTestRect(visual.bounds, x, y);
        });
    return iterator == visuals.end()
               ? std::nullopt
               : std::optional<std::size_t>(iterator->index);
}

bool MainWindow::HitTestRect(
    const D2D1_RECT_F& bounds,
    const float x,
    const float y) const noexcept {
    return bounds.right > bounds.left &&
           bounds.bottom > bounds.top &&
           x >= bounds.left &&
           x <= bounds.right &&
           y >= bounds.top &&
           y <= bounds.bottom;
}

void MainWindow::SetStatus(
    const std::wstring_view text) {
    SetWindowTextW(
        status_label_,
        std::wstring(text).c_str());
}

void MainWindow::ShowError(
    const std::wstring_view text) const {
    MessageBoxW(
        window_,
        std::wstring(text).c_str(),
        L"PCkey 错误",
        MB_OK | MB_ICONERROR);
}

void MainWindow::SetControlFont(
    const HWND control) const noexcept {
    if (control == nullptr) {
        return;
    }

    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(
            GetStockObject(DEFAULT_GUI_FONT)),
        TRUE);
}

}  // namespace pckey::editor
