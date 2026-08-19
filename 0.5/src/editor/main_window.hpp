#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pckey/config_store.hpp"

namespace pckey::editor {

class MainWindow {
public:
    static inline constexpr UINT kHealthCheckMessage =
        WM_APP + 20;
    static inline constexpr LRESULT kHealthCheckResult =
        0x50434B45;

    explicit MainWindow(HINSTANCE instance);

    bool Create();
    void Show(int command) noexcept;

    [[nodiscard]] HWND handle() const noexcept {
        return window_;
    }

    enum class ActionCategory : std::uint8_t {
        Basic,
        Function,
        Modifiers,
        Layer,
        System,
        Media,
        Mouse,
        Macro,
        TapDance,
        Combo,
        Override,
        Advanced,
    };

private:
    enum class PaletteCommand : std::uint8_t {
        None,
        CreateMacro,
        CreateTapDance,
        CreateCombo,
        CreateOverride,
        EditMouseSettings,
    };

    enum class AdvancedRuleKind : std::uint8_t {
        None,
        Macro,
        TapDance,
        Combo,
        Override,
    };

    struct KeyVisual {
        PhysicalKey key{};
        std::wstring label;
        D2D1_RECT_F bounds{};
    };

    struct IndexedVisual {
        std::size_t index{};
        D2D1_RECT_F bounds{};
    };

    struct PaletteAction {
        std::wstring label;
        std::wstring description;
        Action action{};
        bool enabled{true};
        PaletteCommand command{PaletteCommand::None};
        AdvancedRuleKind rule_kind{AdvancedRuleKind::None};
        std::uint16_t reference_id{};
    };

    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param);

    LRESULT HandleMessage(
        UINT message,
        WPARAM w_param,
        LPARAM l_param);

    bool CreateDeviceIndependentResources();
    bool CreateDeviceResources();
    bool CreateControls();
    void DiscardDeviceResources() noexcept;
    void Paint();
    void Resize(UINT width, UINT height);
    void LayoutControls(UINT width, UINT height);

    void DrawHeader();
    void DrawSidebar();
    void DrawLayerTabs();
    void DrawKeyboard();
    void DrawActionPanel();
    void DrawTapHoldDetails();

    void LoadConfiguration();
    void PopulateProfiles();
    void PopulateLayoutChoices();
    void PopulatePaletteActions();
    void UpdateEditorState();
    void UpdateTapHoldControls();
    void SelectProfileFromList();
    void SelectLayer(std::size_t layer);
    void SelectCategory(ActionCategory category);
    void ApplyPaletteAction(std::size_t index);
    void AddLayer();
    void RemoveLayer();
    void CreateProfile();
    void DeleteProfile();
    void DiscardChanges();
    void ResetSelectedMapping();
    void SaveAndApply();
    void SaveDraft();
    void MarkDirty(std::wstring_view status);
    void UpdateTapHoldTiming();
    void CreateAndBindMacro();
    void CreateAndBindTapDance();
    void CreateCombo();
    void CreateOverride();
    void EditMouseSettings();
    void EditAdvancedRule(
        AdvancedRuleKind kind,
        std::uint16_t reference_id);
    void ShowPaletteContextMenu(
        std::size_t index,
        POINT screen_point);
    void DeleteAdvancedRule(
        AdvancedRuleKind kind,
        std::uint16_t reference_id);
    void ResetDeletedRuleReferences(
        Profile& profile,
        AdvancedRuleKind kind,
        std::uint16_t reference_id);

    [[nodiscard]] Profile* SelectedProfile() noexcept;
    [[nodiscard]] const Profile* SelectedProfile() const noexcept;
    [[nodiscard]] std::wstring ReadWindowText(HWND control) const;
    [[nodiscard]] bool RenameSelectedProfile(
        bool strict,
        bool* changed = nullptr);
    [[nodiscard]] std::wstring UniqueProfileName() const;
    [[nodiscard]] std::wstring LabelForKey(
        PhysicalKey key) const;
    [[nodiscard]] std::wstring ActionLabel(
        const Action& action,
        PhysicalKey source) const;
    [[nodiscard]] bool CanRemoveLastLayer() const noexcept;
    [[nodiscard]] bool IsPaletteActionSelected(
        const PaletteAction& choice) const noexcept;

    [[nodiscard]] std::optional<PhysicalKey> HitTestKey(
        float x,
        float y) const noexcept;
    [[nodiscard]] std::optional<std::size_t> HitTestIndexed(
        const std::vector<IndexedVisual>& visuals,
        float x,
        float y) const noexcept;
    [[nodiscard]] bool HitTestRect(
        const D2D1_RECT_F& bounds,
        float x,
        float y) const noexcept;

    void SetStatus(std::wstring_view text);
    void ShowError(std::wstring_view text) const;
    void SetControlFont(HWND control) const noexcept;

    static inline constexpr wchar_t kWindowClassName[] =
        L"PCkey.Editor.MainWindow";

    static inline constexpr int kProfileListId = 2001;
    static inline constexpr int kNewProfileId = 2002;
    static inline constexpr int kDeleteProfileId = 2003;
    static inline constexpr int kProfileNameId = 2004;
    static inline constexpr int kSaveApplyId = 2005;
    static inline constexpr int kStatusId = 2006;
    static inline constexpr int kDiscardId = 2007;
    static inline constexpr int kLayoutComboId = 2008;
    static inline constexpr int kTapTermId = 2009;
    static inline constexpr int kQuickTapTermId = 2010;
    static inline constexpr int kUpdateTimingId = 2011;
    static inline constexpr int kTapTermLabelId = 2012;
    static inline constexpr int kQuickTapLabelId = 2013;
    static inline constexpr int kKeyTestId = 2014;
    static inline constexpr UINT kInitializeMessage = WM_APP + 1;

    HINSTANCE instance_{};
    HWND window_{};
    UINT dpi_{96};
    bool loading_controls_{};
    bool dirty_{};
    bool initialized_{};

    HWND profile_list_{};
    HWND new_profile_button_{};
    HWND delete_profile_button_{};
    HWND profile_name_edit_{};
    HWND save_apply_button_{};
    HWND discard_button_{};
    HWND key_test_button_{};
    HWND layout_combo_{};
    HWND tap_term_edit_{};
    HWND quick_tap_edit_{};
    HWND update_timing_button_{};
    HWND tap_term_label_{};
    HWND quick_tap_label_{};
    HWND status_label_{};

    Configuration applied_configuration_{};
    Configuration configuration_{};
    std::optional<PhysicalKey> selected_key_{};
    std::size_t selected_layer_{};
    ActionCategory selected_category_{ActionCategory::Basic};
    std::vector<PaletteAction> palette_actions_{};
    std::vector<KeyVisual> key_visuals_{};
    std::vector<IndexedVisual> layer_visuals_{};
    std::vector<IndexedVisual> category_visuals_{};
    std::vector<IndexedVisual> palette_visuals_{};
    D2D1_RECT_F add_layer_bounds_{};
    D2D1_RECT_F remove_layer_bounds_{};
    D2D1_RECT_F reset_key_bounds_{};

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> title_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> section_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> body_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> small_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> key_format_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> muted_text_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> card_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> key_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent_soft_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selected_text_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> disabled_brush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> warning_brush_;
};

}  // namespace pckey::editor
