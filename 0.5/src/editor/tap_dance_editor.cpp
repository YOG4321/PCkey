#include "tap_dance_editor.hpp"

#include <array>
#include <cwchar>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "action_catalog.hpp"
#include "modal_message_loop.hpp"

namespace pckey::editor {

namespace {

struct Choice {
    std::wstring label;
    Action action;
};

class TapDanceEditorWindow {
public:
    TapDanceEditorWindow(
        const HINSTANCE instance,
        const HWND owner,
        TapDanceDefinition definition,
        const PhysicalKey source_key,
        const Profile& profile)
        : instance_(instance),
          owner_(owner),
          source_key_(source_key),
          profile_(profile),
          result_(std::move(definition)) {
        BuildChoices();
    }

    std::optional<TapDanceDefinition> Run() {
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
                   ? std::optional<TapDanceDefinition>(
                         std::move(result_))
                   : std::nullopt;
    }

private:
    static inline constexpr wchar_t kClassName[] =
        L"PCkey.Editor.TapDanceWindow";
    static inline constexpr int kNameId = 3101;
    static inline constexpr int kTapId = 3102;
    static inline constexpr int kHoldId = 3103;
    static inline constexpr int kDoubleId = 3104;
    static inline constexpr int kTapHoldId = 3105;
    static inline constexpr int kHoldTermId = 3106;
    static inline constexpr int kMultiTermId = 3107;
    static inline constexpr int kQuickTermId = 3108;
    static inline constexpr int kSaveId = 3109;
    static inline constexpr int kCancelId = 3110;
    static inline constexpr int kStatusId = 3111;

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
        constexpr int width = 620;
        constexpr int height = 500;
        window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kClassName,
            L"PCkey - Tap Dance",
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
        auto* self = reinterpret_cast<TapDanceEditorWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(l_param);
            self = static_cast<TapDanceEditorWindow*>(
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
                if (LOWORD(w_param) == kSaveId) {
                    Save();
                } else if (LOWORD(w_param) == kCancelId) {
                    DestroyWindow(window_);
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

        create(L"STATIC", L"名称", SS_LEFT, 0, 24, 22, 72, 24);
        name_edit_ = create(
            L"EDIT",
            result_.name.c_str(),
            WS_BORDER | ES_AUTOHSCROLL,
            kNameId,
            100,
            18,
            330,
            28);

        const std::array labels{
            std::wstring_view(L"单击"),
            std::wstring_view(L"长按"),
            std::wstring_view(L"双击"),
            std::wstring_view(L"单击后长按")};
        const std::array ids{
            kTapId,
            kHoldId,
            kDoubleId,
            kTapHoldId};

        for (std::size_t index = 0; index < ids.size(); ++index) {
            const auto y = 70 + static_cast<int>(index) * 55;
            create(
                L"STATIC",
                labels[index].data(),
                SS_LEFT,
                0,
                24,
                y + 5,
                80,
                24);
            action_combos_[index] = create(
                L"COMBOBOX",
                L"",
                CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                ids[index],
                100,
                y,
                360,
                280);
            for (const auto& choice : choices_[index]) {
                SendMessageW(
                    action_combos_[index],
                    CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(
                        choice.label.c_str()));
            }
            const std::array actions{
                result_.tap_action,
                result_.hold_action,
                result_.double_tap_action,
                result_.tap_hold_action};
            std::size_t selected = 0;
            for (std::size_t choice = 0;
                 choice < choices_[index].size();
                 ++choice) {
                const auto& left =
                    choices_[index][choice].action;
                const auto& right = actions[index];
                if (left.kind == right.kind &&
                    left.target_key == right.target_key &&
                    left.target_layer == right.target_layer &&
                    left.virtual_key == right.virtual_key &&
                    left.mouse_button == right.mouse_button &&
                    left.mouse_x == right.mouse_x &&
                    left.mouse_y == right.mouse_y &&
                    left.mouse_amount == right.mouse_amount &&
                    left.reference_id == right.reference_id &&
                    left.shortcut_modifiers ==
                        right.shortcut_modifiers) {
                    selected = choice;
                    break;
                }
            }
            SendMessageW(
                action_combos_[index],
                CB_SETCURSEL,
                static_cast<WPARAM>(selected),
                0);
        }

        const std::array timing_labels{
            std::wstring_view(L"长按阈值"),
            std::wstring_view(L"多击间隔"),
            std::wstring_view(L"Quick Tap")};
        const std::array timing_ids{
            kHoldTermId,
            kMultiTermId,
            kQuickTermId};
        const std::array timing_values{
            std::to_wstring(result_.hold_term_ms),
            std::to_wstring(result_.multi_tap_term_ms),
            std::to_wstring(result_.quick_tap_term_ms)};

        for (std::size_t index = 0;
             index < timing_ids.size();
             ++index) {
            const auto x = 24 + static_cast<int>(index) * 185;
            create(
                L"STATIC",
                timing_labels[index].data(),
                SS_LEFT,
                0,
                x,
                305,
                82,
                22);
            timing_edits_[index] = create(
                L"EDIT",
                timing_values[index].c_str(),
                WS_BORDER | ES_NUMBER | ES_CENTER,
                timing_ids[index],
                x + 88,
                300,
                72,
                27);
        }

        status_ = create(
            L"STATIC",
            L"切层请在“长按”或“单击后长按”中选择MO(n)；不允许嵌套Tap-Hold。",
            SS_LEFT,
            kStatusId,
            24,
            355,
            410,
            25);
        create(
            L"BUTTON",
            L"保存并绑定",
            BS_DEFPUSHBUTTON,
            kSaveId,
            420,
            400,
            110,
            32);
        create(
            L"BUTTON",
            L"取消",
            BS_PUSHBUTTON,
            kCancelId,
            536,
            400,
            62,
            32);
        return name_edit_ != nullptr &&
               status_ != nullptr &&
               action_combos_[0] != nullptr;
    }

    void BuildChoices() {
        std::vector<Choice> common;
        common.push_back({L"未设置", Action::Transparent()});
        common.push_back(
            {L"当前原键", Action::Key(source_key_)});
        ForEachStandardKeyChoice(
            [&common](
                std::wstring label,
                const PhysicalKey key) {
                common.push_back(
                    {std::move(label), Action::Key(key)});
            });
        ForEachMediaAndMouseAction(
            [&common](
                std::wstring label,
                const Action& action) {
                common.push_back(
                    {std::move(label), action});
                });
        ForEachShortcutAction(
            [&common](
                std::wstring label,
                const Action& action) {
                common.push_back(
                    {std::move(label), action});
            });

        for (const auto& macro : profile_.macros()) {
            common.push_back(
                {L"宏：" + macro.name,
                 Action::Macro(macro.id)});
        }

        for (auto& choices : choices_) {
            choices = common;
        }
        for (std::size_t layer = 1;
             layer < profile_.layer_count();
             ++layer) {
            const Choice layer_choice{
                L"MO(" + std::to_wstring(layer) +
                    L") - 按住启用 Layer " +
                    std::to_wstring(layer),
                Action::MomentaryLayer(
                    static_cast<std::uint8_t>(layer))};
            choices_[1].push_back(layer_choice);
            choices_[3].push_back(layer_choice);
        }
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

    void Save() {
        std::array<wchar_t, 256> name{};
        GetWindowTextW(
            name_edit_,
            name.data(),
            static_cast<int>(name.size()));
        if (name[0] == L'\0') {
            SetWindowTextW(status_, L"名称不能为空。");
            return;
        }

        std::array<Action, 4> actions{};
        for (std::size_t index = 0;
             index < actions.size();
             ++index) {
            const auto selection = static_cast<int>(
                SendMessageW(
                    action_combos_[index],
                    CB_GETCURSEL,
                    0,
                    0));
            if (selection < 0 ||
                static_cast<std::size_t>(selection) >=
                    choices_[index].size()) {
                SetWindowTextW(status_, L"动作选择无效。");
                return;
            }
            actions[index] =
                choices_[index][
                    static_cast<std::size_t>(selection)]
                    .action;
        }

        if (actions[0].kind ==
                ActionKind::MomentaryLayer ||
            actions[2].kind ==
                ActionKind::MomentaryLayer) {
            SetWindowTextW(
                status_,
                L"MO(n)需要持续按住，只能用于“长按”或“单击后长按”。");
            return;
        }

        const auto hold = ReadNumber(timing_edits_[0]);
        const auto multi = ReadNumber(timing_edits_[1]);
        const auto quick = ReadNumber(timing_edits_[2]);
        if (!hold.has_value() ||
            *hold < 100 || *hold > 1000 ||
            !multi.has_value() ||
            *multi < 50 || *multi > 500 ||
            !quick.has_value() || *quick > 500) {
            SetWindowTextW(status_, L"时间参数超出允许范围。");
            return;
        }

        result_.name = name.data();
        result_.tap_action = actions[0];
        result_.hold_action = actions[1];
        result_.double_tap_action = actions[2];
        result_.tap_hold_action = actions[3];
        result_.hold_term_ms =
            static_cast<std::uint16_t>(*hold);
        result_.multi_tap_term_ms =
            static_cast<std::uint16_t>(*multi);
        result_.quick_tap_term_ms =
            static_cast<std::uint16_t>(*quick);
        accepted_ = true;
        DestroyWindow(window_);
    }

    HINSTANCE instance_{};
    HWND owner_{};
    HWND window_{};
    HWND name_edit_{};
    std::array<HWND, 4> action_combos_{};
    std::array<HWND, 3> timing_edits_{};
    HWND status_{};
    PhysicalKey source_key_{};
    const Profile& profile_;
    std::array<std::vector<Choice>, 4> choices_;
    TapDanceDefinition result_{};
    bool accepted_{};
};

}  // namespace

std::optional<TapDanceDefinition> ShowTapDanceEditor(
    const HWND owner,
    const HINSTANCE instance,
    const std::uint16_t definition_id,
    const PhysicalKey source_key,
    const Profile& profile) {
    return TapDanceEditorWindow(
               instance,
               owner,
               TapDanceDefinition{
                   definition_id,
                   L"Tap Dance " +
                       std::to_wstring(definition_id),
                   Action::Key(source_key)},
               source_key,
               profile)
        .Run();
}

std::optional<TapDanceDefinition> ShowTapDanceEditor(
    const HWND owner,
    const HINSTANCE instance,
    const TapDanceDefinition& definition,
    const PhysicalKey source_key,
    const Profile& profile) {
    return TapDanceEditorWindow(
               instance,
               owner,
               definition,
               source_key,
               profile)
        .Run();
}

}  // namespace pckey::editor
