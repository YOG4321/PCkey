#include "override_editor.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "action_catalog.hpp"
#include "modal_message_loop.hpp"

namespace pckey::editor {

namespace {

struct KeyChoice {
    std::wstring label;
    PhysicalKey key;
};

struct ActionChoice {
    std::wstring label;
    Action action;
};

class OverrideEditorWindow {
public:
    OverrideEditorWindow(
        const HINSTANCE instance,
        const HWND owner,
        KeyOverrideDefinition definition,
        const std::size_t current_layer,
        const Profile& profile)
        : instance_(instance),
          owner_(owner),
          current_layer_(current_layer),
          profile_(profile),
          result_(std::move(definition)) {
        BuildChoices();
    }

    std::optional<KeyOverrideDefinition> Run() {
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
                   ? std::optional<KeyOverrideDefinition>(
                         std::move(result_))
                   : std::nullopt;
    }

private:
    static inline constexpr wchar_t kClassName[] =
        L"PCkey.Editor.OverrideWindow";
    static inline constexpr int kNameId = 3301;
    static inline constexpr int kTriggerId = 3302;
    static inline constexpr int kOutputId = 3303;
    static inline constexpr int kRequiredBaseId = 3310;
    static inline constexpr int kForbiddenBaseId = 3330;
    static inline constexpr int kSuppressId = 3320;
    static inline constexpr int kExactId = 3321;
    static inline constexpr int kCurrentLayerOnlyId = 3322;
    static inline constexpr int kSaveId = 3323;
    static inline constexpr int kCancelId = 3324;
    static inline constexpr int kStatusId = 3325;

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
        constexpr int width = 650;
        constexpr int height = 560;
        window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kClassName,
            L"PCkey - Key Override",
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
        auto* self = reinterpret_cast<OverrideEditorWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(l_param);
            self = static_cast<OverrideEditorWindow*>(
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

        create(L"STATIC", L"名称", SS_LEFT, 0, 24, 22, 70, 24);
        name_edit_ = create(
            L"EDIT",
            result_.name.c_str(),
            WS_BORDER | ES_AUTOHSCROLL,
            kNameId,
            100,
            18,
            330,
            28);
        create(L"STATIC", L"触发键", SS_LEFT, 0, 24, 72, 70, 24);
        trigger_combo_ = create(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_VSCROLL,
            kTriggerId,
            100,
            66,
            250,
            300);
        for (const auto& choice : key_choices_) {
            SendMessageW(
                trigger_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    choice.label.c_str()));
        }
        std::size_t trigger_index = 0;
        for (std::size_t index = 0;
             index < key_choices_.size();
             ++index) {
            if (key_choices_[index].key ==
                result_.trigger_key) {
                trigger_index = index;
                break;
            }
        }
        SendMessageW(
            trigger_combo_,
            CB_SETCURSEL,
            static_cast<WPARAM>(trigger_index),
            0);

        create(L"STATIC", L"替换动作", SS_LEFT, 0, 24, 122, 70, 24);
        output_combo_ = create(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_VSCROLL,
            kOutputId,
            100,
            116,
            250,
            300);
        for (const auto& choice : action_choices_) {
            SendMessageW(
                output_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    choice.label.c_str()));
        }
        std::size_t output_index = 0;
        for (std::size_t index = 0;
             index < action_choices_.size();
             ++index) {
            const auto& left = action_choices_[index].action;
            const auto& right = result_.replacement_action;
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
                output_index = index;
                break;
            }
        }
        SendMessageW(
            output_combo_,
            CB_SETCURSEL,
            static_cast<WPARAM>(output_index),
            0);

        create(
            L"STATIC",
            L"必须按住的修饰键（任意一侧）",
            SS_LEFT,
            0,
            24,
            175,
            260,
            24);
        const std::array labels{
            std::wstring_view(L"Ctrl"),
            std::wstring_view(L"Shift"),
            std::wstring_view(L"Alt"),
            std::wstring_view(L"Win")};
        for (std::size_t index = 0;
             index < modifier_checks_.size();
             ++index) {
            modifier_checks_[index] = create(
                L"BUTTON",
                labels[index].data(),
                BS_AUTOCHECKBOX,
                kRequiredBaseId +
                    static_cast<int>(index),
                24 + static_cast<int>(index) * 110,
                205,
                100,
                28);
            constexpr std::array<std::uint16_t, 4> masks{
                ModifierAnyControl,
                ModifierAnyShift,
                ModifierAnyAlt,
                ModifierAnyWin};
            SendMessageW(
                modifier_checks_[index],
                BM_SETCHECK,
                (result_.required_modifiers &
                 masks[index]) != 0
                    ? BST_CHECKED
                    : BST_UNCHECKED,
                0);
        }

        create(
            L"STATIC",
            L"禁止同时按住的修饰键（任意一侧）",
            SS_LEFT,
            0,
            24,
            245,
            260,
            24);
        for (std::size_t index = 0;
             index < forbidden_checks_.size();
             ++index) {
            forbidden_checks_[index] = create(
                L"BUTTON",
                labels[index].data(),
                BS_AUTOCHECKBOX,
                kForbiddenBaseId +
                    static_cast<int>(index),
                24 + static_cast<int>(index) * 110,
                275,
                100,
                28);
            constexpr std::array<std::uint16_t, 4> masks{
                ModifierAnyControl,
                ModifierAnyShift,
                ModifierAnyAlt,
                ModifierAnyWin};
            SendMessageW(
                forbidden_checks_[index],
                BM_SETCHECK,
                (result_.forbidden_modifiers &
                 masks[index]) != 0
                    ? BST_CHECKED
                    : BST_UNCHECKED,
                0);
        }

        suppress_check_ = create(
            L"BUTTON",
            L"替换时抑制上述修饰键",
            BS_AUTOCHECKBOX,
            kSuppressId,
            24,
            320,
            230,
            28);
        SendMessageW(
            suppress_check_,
            BM_SETCHECK,
            result_.suppressed_modifiers != 0
                ? BST_CHECKED
                : BST_UNCHECKED,
            0);
        exact_check_ = create(
            L"BUTTON",
            L"精确匹配，不允许额外修饰键",
            BS_AUTOCHECKBOX,
            kExactId,
            24,
            355,
            260,
            28);
        SendMessageW(
            exact_check_,
            BM_SETCHECK,
            result_.exact_match
                ? BST_CHECKED
                : BST_UNCHECKED,
            0);
        current_layer_only_ = create(
            L"BUTTON",
            L"仅当前Layer生效",
            BS_AUTOCHECKBOX,
            kCurrentLayerOnlyId,
            300,
            320,
            180,
            28);
        SendMessageW(
            current_layer_only_,
            BM_SETCHECK,
            result_.layer_mask == 0xFFFFFFFFU
                ? BST_UNCHECKED
                : BST_CHECKED,
            0);
        status_ = create(
            L"STATIC",
            L"规则按映射后的触发键和有效修饰键状态判断。",
            SS_LEFT,
            kStatusId,
            24,
            410,
            450,
            25);
        create(
            L"BUTTON",
            L"保存Override",
            BS_DEFPUSHBUTTON,
            kSaveId,
            430,
            455,
            115,
            32);
        create(
            L"BUTTON",
            L"取消",
            BS_PUSHBUTTON,
            kCancelId,
            552,
            455,
            62,
            32);
        return name_edit_ != nullptr &&
               trigger_combo_ != nullptr &&
               output_combo_ != nullptr;
    }

    void BuildChoices() {
        ForEachStandardKeyChoice(
            [this](
                std::wstring label,
                const PhysicalKey key) {
                key_choices_.push_back(
                    {std::move(label), key});
            });

        for (const auto& key : key_choices_) {
            action_choices_.push_back(
                {key.label, Action::Key(key.key)});
        }
        ForEachMediaAndMouseAction(
            [this](
                std::wstring label,
                const Action& action) {
                action_choices_.push_back(
                    {std::move(label), action});
                });
        ForEachShortcutAction(
            [this](
                std::wstring label,
                const Action& action) {
                action_choices_.push_back(
                    {std::move(label), action});
            });
        for (std::size_t layer = 1;
             layer < profile_.layer_count();
             ++layer) {
            action_choices_.push_back(
                {L"按住 Layer " + std::to_wstring(layer),
                 Action::MomentaryLayer(
                     static_cast<std::uint8_t>(layer))});
        }
        for (const auto& macro : profile_.macros()) {
            action_choices_.push_back(
                {L"宏：" + macro.name,
                 Action::Macro(macro.id)});
        }
    }

    void Save() {
        std::array<wchar_t, 256> name{};
        GetWindowTextW(
            name_edit_,
            name.data(),
            static_cast<int>(name.size()));
        const auto trigger = static_cast<int>(
            SendMessageW(trigger_combo_, CB_GETCURSEL, 0, 0));
        const auto output = static_cast<int>(
            SendMessageW(output_combo_, CB_GETCURSEL, 0, 0));
        if (name[0] == L'\0' ||
            trigger < 0 ||
            output < 0 ||
            static_cast<std::size_t>(trigger) >=
                key_choices_.size() ||
            static_cast<std::size_t>(output) >=
                action_choices_.size()) {
            SetWindowTextW(status_, L"名称或动作选择无效。");
            return;
        }

        constexpr std::array<std::uint16_t, 4> masks{
            ModifierAnyControl,
            ModifierAnyShift,
            ModifierAnyAlt,
            ModifierAnyWin};
        std::uint16_t required = 0;
        std::uint16_t forbidden = 0;
        for (std::size_t index = 0;
             index < modifier_checks_.size();
             ++index) {
            if (SendMessageW(
                    modifier_checks_[index],
                    BM_GETCHECK,
                    0,
                    0) == BST_CHECKED) {
                required = static_cast<std::uint16_t>(
                    required | masks[index]);
            }
            if (SendMessageW(
                    forbidden_checks_[index],
                    BM_GETCHECK,
                    0,
                    0) == BST_CHECKED) {
                forbidden = static_cast<std::uint16_t>(
                    forbidden | masks[index]);
            }
        }
        if (required == 0) {
            SetWindowTextW(
                status_,
                L"请至少选择一个必须按住的修饰键。");
            return;
        }
        if ((required & forbidden) != 0) {
            SetWindowTextW(
                status_,
                L"同一组修饰键不能同时设为“必须”和“禁止”。");
            return;
        }

        result_.name = name.data();
        result_.trigger_key =
            key_choices_[static_cast<std::size_t>(trigger)].key;
        result_.required_modifiers = required;
        result_.forbidden_modifiers = forbidden;
        result_.suppressed_modifiers =
            SendMessageW(
                suppress_check_,
                BM_GETCHECK,
                0,
                0) == BST_CHECKED
                ? required
                : 0;
        result_.exact_match =
            SendMessageW(
                exact_check_,
                BM_GETCHECK,
                0,
                0) == BST_CHECKED;
        result_.replacement_action =
            action_choices_[static_cast<std::size_t>(output)]
                .action;
        result_.layer_mask =
            SendMessageW(
                current_layer_only_,
                BM_GETCHECK,
                0,
                0) == BST_CHECKED
                ? static_cast<std::uint32_t>(
                      1U << current_layer_)
                : 0xFFFFFFFFU;
        accepted_ = true;
        DestroyWindow(window_);
    }

    HINSTANCE instance_{};
    HWND owner_{};
    HWND window_{};
    HWND name_edit_{};
    HWND trigger_combo_{};
    HWND output_combo_{};
    std::array<HWND, 4> modifier_checks_{};
    std::array<HWND, 4> forbidden_checks_{};
    HWND suppress_check_{};
    HWND exact_check_{};
    HWND current_layer_only_{};
    HWND status_{};
    std::size_t current_layer_{};
    const Profile& profile_;
    std::vector<KeyChoice> key_choices_;
    std::vector<ActionChoice> action_choices_;
    KeyOverrideDefinition result_{};
    bool accepted_{};
};

}  // namespace

std::optional<KeyOverrideDefinition> ShowOverrideEditor(
    const HWND owner,
    const HINSTANCE instance,
    const std::uint16_t definition_id,
    const std::size_t current_layer,
    const Profile& profile) {
    return OverrideEditorWindow(
               instance,
               owner,
               KeyOverrideDefinition{
                   definition_id,
                   L"Override " +
                       std::to_wstring(definition_id)},
               current_layer,
               profile)
        .Run();
}

std::optional<KeyOverrideDefinition> ShowOverrideEditor(
    const HWND owner,
    const HINSTANCE instance,
    const KeyOverrideDefinition& definition,
    const std::size_t current_layer,
    const Profile& profile) {
    return OverrideEditorWindow(
               instance,
               owner,
               definition,
               current_layer,
               profile)
        .Run();
}

}  // namespace pckey::editor
