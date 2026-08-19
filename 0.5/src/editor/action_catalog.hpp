#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "pckey/profile.hpp"

namespace pckey::editor {

template <typename Add>
void ForEachStandardKeyChoice(Add&& add) {
    static constexpr std::array<std::uint16_t, 26> letter_scans{
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22,
        0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31,
        0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16,
        0x2F, 0x11, 0x2D, 0x15, 0x2C};
    for (std::size_t index = 0;
         index < letter_scans.size();
         ++index) {
        add(
            std::wstring(
                1,
                static_cast<wchar_t>(L'A' + index)),
            PhysicalKey{
                letter_scans[index],
                KeyPrefix::None});
    }

    static constexpr std::array<std::uint16_t, 10> digit_scans{
        0x0B, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x0A};
    for (std::size_t index = 0;
         index < digit_scans.size();
         ++index) {
        add(
            std::wstring(
                1,
                static_cast<wchar_t>(L'0' + index)),
            PhysicalKey{
                digit_scans[index],
                KeyPrefix::None});
    }

    struct Entry {
        std::wstring_view label;
        PhysicalKey key;
    };
    static constexpr std::array entries{
        Entry{L"Escape", {0x01, KeyPrefix::None}},
        Entry{L"Tab", {0x0F, KeyPrefix::None}},
        Entry{L"Caps Lock", {0x3A, KeyPrefix::None}},
        Entry{L"Backspace", {0x0E, KeyPrefix::None}},
        Entry{L"Enter", {0x1C, KeyPrefix::None}},
        Entry{L"Space", {0x39, KeyPrefix::None}},
        Entry{L"Insert", {0x52, KeyPrefix::E0}},
        Entry{L"Delete", {0x53, KeyPrefix::E0}},
        Entry{L"Home", {0x47, KeyPrefix::E0}},
        Entry{L"End", {0x4F, KeyPrefix::E0}},
        Entry{L"Page Up", {0x49, KeyPrefix::E0}},
        Entry{L"Page Down", {0x51, KeyPrefix::E0}},
        Entry{L"↑", {0x48, KeyPrefix::E0}},
        Entry{L"↓", {0x50, KeyPrefix::E0}},
        Entry{L"←", {0x4B, KeyPrefix::E0}},
        Entry{L"→", {0x4D, KeyPrefix::E0}},
        Entry{L"LCtrl", {0x1D, KeyPrefix::None}},
        Entry{L"RCtrl", {0x1D, KeyPrefix::E0}},
        Entry{L"LShift", {0x2A, KeyPrefix::None}},
        Entry{L"RShift", {0x36, KeyPrefix::None}},
        Entry{L"LAlt", {0x38, KeyPrefix::None}},
        Entry{L"RAlt", {0x38, KeyPrefix::E0}},
        Entry{L"LWin", {0x5B, KeyPrefix::E0}},
        Entry{L"RWin", {0x5C, KeyPrefix::E0}},
        Entry{L"Menu", {0x5D, KeyPrefix::E0}},
    };
    for (const auto& entry : entries) {
        add(std::wstring(entry.label), entry.key);
    }

    for (std::uint16_t index = 0; index < 10; ++index) {
        add(
            L"F" + std::to_wstring(index + 1),
            PhysicalKey{
                static_cast<std::uint16_t>(0x3B + index),
                KeyPrefix::None});
    }
    add(L"F11", {0x57, KeyPrefix::None});
    add(L"F12", {0x58, KeyPrefix::None});
}

template <typename Add>
void ForEachMediaAndMouseAction(Add&& add) {
    const auto virtual_key =
        [&add](const wchar_t* label, const int key) {
            add(
                std::wstring(label),
                Action::VirtualKey(
                    static_cast<std::uint16_t>(key)));
        };
    virtual_key(L"静音", VK_VOLUME_MUTE);
    virtual_key(L"音量－", VK_VOLUME_DOWN);
    virtual_key(L"音量＋", VK_VOLUME_UP);
    virtual_key(L"播放/暂停", VK_MEDIA_PLAY_PAUSE);
    virtual_key(L"上一曲", VK_MEDIA_PREV_TRACK);
    virtual_key(L"下一曲", VK_MEDIA_NEXT_TRACK);
    virtual_key(L"停止播放", VK_MEDIA_STOP);
    virtual_key(L"浏览器后退", VK_BROWSER_BACK);
    virtual_key(L"浏览器前进", VK_BROWSER_FORWARD);
    virtual_key(L"浏览器主页", VK_BROWSER_HOME);
    virtual_key(L"浏览器搜索", VK_BROWSER_SEARCH);
    virtual_key(L"邮件", VK_LAUNCH_MAIL);
    virtual_key(L"媒体播放器", VK_LAUNCH_MEDIA_SELECT);
    virtual_key(L"计算器", VK_LAUNCH_APP2);

    add(
        L"鼠标左键",
        Action::MouseButtonAction(MouseButton::Left));
    add(
        L"鼠标右键",
        Action::MouseButtonAction(MouseButton::Right));
    add(
        L"鼠标中键",
        Action::MouseButtonAction(MouseButton::Middle));
    add(
        L"鼠标 X1",
        Action::MouseButtonAction(MouseButton::X1));
    add(
        L"鼠标 X2",
        Action::MouseButtonAction(MouseButton::X2));
    add(L"鼠标←", Action::MouseMove(-1, 0));
    add(L"鼠标→", Action::MouseMove(1, 0));
    add(L"鼠标↑", Action::MouseMove(0, -1));
    add(L"鼠标↓", Action::MouseMove(0, 1));
    add(L"滚轮↑", Action::MouseWheel(1));
    add(L"滚轮↓", Action::MouseWheel(-1));
    add(L"滚轮←", Action::MouseWheel(-1, true));
    add(L"滚轮→", Action::MouseWheel(1, true));
}

template <typename Add>
void ForEachShortcutAction(Add&& add) {
    const auto shortcut =
        [&add](
            const wchar_t* label,
            const PhysicalKey key,
            const std::uint16_t modifiers) {
            add(
                std::wstring(label),
                Action::Shortcut(key, modifiers));
        };

    constexpr auto ctrl =
        static_cast<std::uint16_t>(ModifierLeftControl);
    constexpr auto shift =
        static_cast<std::uint16_t>(ModifierLeftShift);
    constexpr auto alt =
        static_cast<std::uint16_t>(ModifierLeftAlt);
    constexpr auto win =
        static_cast<std::uint16_t>(ModifierLeftWin);

    shortcut(L"全选", {0x1E, KeyPrefix::None}, ctrl);
    shortcut(L"复制", {0x2E, KeyPrefix::None}, ctrl);
    shortcut(L"粘贴", {0x2F, KeyPrefix::None}, ctrl);
    shortcut(L"剪切", {0x2D, KeyPrefix::None}, ctrl);
    shortcut(L"撤销", {0x2C, KeyPrefix::None}, ctrl);
    shortcut(L"重做", {0x15, KeyPrefix::None}, ctrl);
    shortcut(L"保存", {0x1F, KeyPrefix::None}, ctrl);
    shortcut(L"查找", {0x21, KeyPrefix::None}, ctrl);
    shortcut(L"新建", {0x31, KeyPrefix::None}, ctrl);
    shortcut(L"打开", {0x18, KeyPrefix::None}, ctrl);
    shortcut(L"打印", {0x19, KeyPrefix::None}, ctrl);
    shortcut(
        L"放大",
        {0x0D, KeyPrefix::None},
        static_cast<std::uint16_t>(ctrl | shift));
    shortcut(L"缩小", {0x0C, KeyPrefix::None}, ctrl);
    shortcut(L"恢复缩放", {0x0B, KeyPrefix::None}, ctrl);
    shortcut(L"切换窗口", {0x0F, KeyPrefix::None}, alt);
    shortcut(L"关闭窗口", {0x3E, KeyPrefix::None}, alt);
    shortcut(L"显示桌面", {0x20, KeyPrefix::None}, win);
    shortcut(L"文件资源管理器", {0x12, KeyPrefix::None}, win);
    shortcut(L"锁定电脑", {0x26, KeyPrefix::None}, win);
    shortcut(
        L"任务管理器",
        {0x01, KeyPrefix::None},
        static_cast<std::uint16_t>(ctrl | shift));
    shortcut(
        L"系统截图",
        {0x1F, KeyPrefix::None},
        static_cast<std::uint16_t>(win | shift));
}

}  // namespace pckey::editor
