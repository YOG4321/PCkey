#include "combo_editor.hpp"

#include <algorithm>
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

struct KeyChoice {
    std::wstring label;
    PhysicalKey key;
};

struct ActionChoice {
    std::wstring label;
    Action action;
};

class ComboEditorWindow {
public:
    ComboEditorWindow(
        const HINSTANCE instance,
        const HWND owner,
        ComboDefinition definition,
        const std::size_t current_layer,
        const Profile& profile)
        : instance_(instance),
          owner_(owner),
          current_layer_(current_layer),
          profile_(profile),
          result_(std::move(definition)) {
        BuildChoices();
    }

    std::optional<ComboDefinition> Run() {
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
                   ? std::optional<ComboDefinition>(
                         std::move(result_))
                   : std::nullopt;
    }

private:
    static inline constexpr wchar_t kClassName[] =
        L"PCkey.Editor.ComboWindow";
    static inline constexpr int kNameId = 3201;
    static inline constexpr int kCountId = 3202;
    static inline constexpr int kMemberBaseId = 3210;
    static inline constexpr int kOutputId = 3220;
    static inline constexpr int kTermId = 3221;
    static inline constexpr int kCurrentLayerOnlyId = 3222;
    static inline constexpr int kSaveId = 3223;
    static inline constexpr int kCancelId = 3224;
    static inline constexpr int kStatusId = 3225;

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
        constexpr int width = 640;
        constexpr int height = 520;
        window_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kClassName,
            L"PCkey - Combo",
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
        auto* self = reinterpret_cast<ComboEditorWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(l_param);
            self = static_cast<ComboEditorWindow*>(
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
            if (LOWORD(w_param) == kCountId &&
                HIWORD(w_param) == CBN_SELCHANGE) {
                UpdateMemberControls();
            } else if (HIWORD(w_param) == BN_CLICKED) {
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
            95,
            18,
            330,
            28);
        create(L"STATIC", L"成员数量", SS_LEFT, 0, 24, 68, 70, 24);
        count_combo_ = create(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST,
            kCountId,
            95,
            63,
            100,
            120);
        for (const auto count : {2, 3, 4}) {
            const auto text = std::to_wstring(count);
            SendMessageW(
                count_combo_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(text.c_str()));
        }
        const auto initial_count = std::clamp<int>(
            result_.member_count,
            2,
            4);
        SendMessageW(
            count_combo_,
            CB_SETCURSEL,
            initial_count - 2,
            0);

        for (std::size_t index = 0;
             index < member_combos_.size();
             ++index) {
            const auto y = 110 + static_cast<int>(index) * 48;
            const auto label =
                L"成员 " + std::to_wstring(index + 1);
            create(
                L"STATIC",
                label.c_str(),
                SS_LEFT,
                0,
                24,
                y + 5,
                70,
                24);
            member_combos_[index] = create(
                L"COMBOBOX",
                L"",
                CBS_DROPDOWNLIST | WS_VSCROLL,
                kMemberBaseId +
                    static_cast<int>(index),
                95,
                y,
                250,
                300);
            for (const auto& choice : key_choices_) {
                SendMessageW(
                    member_combos_[index],
                    CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(
                        choice.label.c_str()));
            }
            std::size_t selected = index;
            if (index < result_.member_count) {
                for (std::size_t choice = 0;
                     choice < key_choices_.size();
                     ++choice) {
                    if (key_choices_[choice].key ==
                        result_.members[index]) {
                        selected = choice;
                        break;
                    }
                }
            }
            SendMessageW(
                member_combos_[index],
                CB_SETCURSEL,
                static_cast<WPARAM>(selected),
                0);
        }

        create(L"STATIC", L"输出动作", SS_LEFT, 0, 370, 110, 80, 24);
        output_combo_ = create(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_VSCROLL,
            kOutputId,
            455,
            105,
            150,
            320);
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
            const auto& right = result_.output_action;
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

        create(L"STATIC", L"判定窗口", SS_LEFT, 0, 370, 160, 80, 24);
        term_edit_ = create(
            L"EDIT",
            std::to_wstring(result_.term_ms).c_str(),
            WS_BORDER | ES_NUMBER | ES_CENTER,
            kTermId,
            455,
            155,
            72,
            27);
        current_layer_only_ = create(
            L"BUTTON",
            L"仅当前Layer生效",
            BS_AUTOCHECKBOX,
            kCurrentLayerOnlyId,
            370,
            205,
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
            L"Combo按映射后的键值识别，成员顺序不限。",
            SS_LEFT,
            kStatusId,
            24,
            355,
            430,
            26);
        create(
            L"BUTTON",
            L"保存Combo",
            BS_DEFPUSHBUTTON,
            kSaveId,
            430,
            410,
            105,
            32);
        create(
            L"BUTTON",
            L"取消",
            BS_PUSHBUTTON,
            kCancelId,
            542,
            410,
            62,
            32);
        UpdateMemberControls();
        return name_edit_ != nullptr &&
               count_combo_ != nullptr &&
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

    void UpdateMemberControls() {
        const auto selection = static_cast<int>(
            SendMessageW(count_combo_, CB_GETCURSEL, 0, 0));
        const auto count =
            selection >= 0 ? selection + 2 : 2;
        for (std::size_t index = 0;
             index < member_combos_.size();
             ++index) {
            EnableWindow(
                member_combos_[index],
                index < static_cast<std::size_t>(count));
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

        const auto count_selection = static_cast<int>(
            SendMessageW(count_combo_, CB_GETCURSEL, 0, 0));
        const auto member_count =
            static_cast<std::uint8_t>(
                std::max(0, count_selection) + 2);
        std::array<PhysicalKey, 4> members{};
        for (std::size_t index = 0;
             index < member_count;
             ++index) {
            const auto selection = static_cast<int>(
                SendMessageW(
                    member_combos_[index],
                    CB_GETCURSEL,
                    0,
                    0));
            if (selection < 0 ||
                static_cast<std::size_t>(selection) >=
                    key_choices_.size()) {
                SetWindowTextW(status_, L"Combo成员选择无效。");
                return;
            }
            members[index] =
                key_choices_[static_cast<std::size_t>(selection)]
                    .key;
            for (std::size_t prior = 0;
                 prior < index;
                 ++prior) {
                if (members[prior] == members[index]) {
                    SetWindowTextW(
                        status_,
                        L"Combo成员不能重复。");
                    return;
                }
            }
        }

        const auto output_selection = static_cast<int>(
            SendMessageW(output_combo_, CB_GETCURSEL, 0, 0));
        const auto term = ReadNumber(term_edit_);
        if (output_selection < 0 ||
            static_cast<std::size_t>(output_selection) >=
                action_choices_.size() ||
            !term.has_value() ||
            *term < 20 || *term > 300) {
            SetWindowTextW(
                status_,
                L"输出动作或20～300ms判定窗口无效。");
            return;
        }

        result_.name = name.data();
        result_.members = members;
        result_.member_count = member_count;
        result_.output_action =
            action_choices_[
                static_cast<std::size_t>(output_selection)]
                .action;
        result_.term_ms =
            static_cast<std::uint16_t>(*term);
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
    HWND count_combo_{};
    std::array<HWND, 4> member_combos_{};
    HWND output_combo_{};
    HWND term_edit_{};
    HWND current_layer_only_{};
    HWND status_{};
    std::size_t current_layer_{};
    const Profile& profile_;
    std::vector<KeyChoice> key_choices_;
    std::vector<ActionChoice> action_choices_;
    ComboDefinition result_{};
    bool accepted_{};
};

}  // namespace

std::optional<ComboDefinition> ShowComboEditor(
    const HWND owner,
    const HINSTANCE instance,
    const std::uint16_t definition_id,
    const std::size_t current_layer,
    const Profile& profile) {
    return ComboEditorWindow(
               instance,
               owner,
               ComboDefinition{
                   definition_id,
                   L"Combo " +
                       std::to_wstring(definition_id)},
               current_layer,
               profile)
        .Run();
}

std::optional<ComboDefinition> ShowComboEditor(
    const HWND owner,
    const HINSTANCE instance,
    const ComboDefinition& definition,
    const std::size_t current_layer,
    const Profile& profile) {
    return ComboEditorWindow(
               instance,
               owner,
               definition,
               current_layer,
               profile)
        .Run();
}

}  // namespace pckey::editor
