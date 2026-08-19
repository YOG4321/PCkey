#include "keyboard_hook.hpp"

#include <array>
#include <utility>

namespace pckey::core {

KeyboardHook::~KeyboardHook() {
    Uninstall();
}

bool KeyboardHook::Install(Processor processor) {
    if (hook_ != nullptr || active_instance_ != nullptr) {
        return false;
    }

    if (QueryPerformanceFrequency(&performance_frequency_) == FALSE ||
        performance_frequency_.QuadPart <= 0) {
        return false;
    }

    processor_ = std::move(processor);
    physical_down_.fill(false);
    active_instance_ = this;

    hook_ = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        &KeyboardHook::HookProcedure,
        GetModuleHandleW(nullptr),
        0);

    if (hook_ == nullptr) {
        active_instance_ = nullptr;
        processor_ = {};
        return false;
    }

    return true;
}

void KeyboardHook::Uninstall() noexcept {
    if (hook_ != nullptr) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }

    if (active_instance_ == this) {
        active_instance_ = nullptr;
    }

    physical_down_.fill(false);
    processor_ = {};
}

LRESULT CALLBACK KeyboardHook::HookProcedure(
    const int code,
    const WPARAM message,
    const LPARAM data) {
    if (active_instance_ == nullptr) {
        return CallNextHookEx(nullptr, code, message, data);
    }

    return active_instance_->HandleHook(code, message, data);
}

LRESULT KeyboardHook::HandleHook(
    const int code,
    const WPARAM message,
    const LPARAM data) {
    if (code < 0) {
        return CallNextHookEx(hook_, code, message, data);
    }

    const auto* keyboard_data =
        reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
    if (keyboard_data == nullptr) {
        return CallNextHookEx(hook_, code, message, data);
    }

    if ((keyboard_data->flags & LLKHF_INJECTED) != 0) {
        return CallNextHookEx(hook_, code, message, data);
    }

    const bool is_down =
        message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool is_up =
        message == WM_KEYUP || message == WM_SYSKEYUP;

    if (!is_down && !is_up) {
        return CallNextHookEx(hook_, code, message, data);
    }

    const auto key = ToPhysicalKey(*keyboard_data);
    if (!key.IsValid()) {
        return CallNextHookEx(hook_, code, message, data);
    }

    const auto key_index = ToKeyIndex(key);
    KeyTransition transition = KeyTransition::Release;

    if (is_down) {
        transition = physical_down_[key_index]
                         ? KeyTransition::Repeat
                         : KeyTransition::Press;
        physical_down_[key_index] = true;
    } else {
        physical_down_[key_index] = false;
    }

    const KeyEvent event{
        key,
        transition,
        TimestampMicros(),
    };

    const auto result =
        processor_ ? processor_(event) : ProcessResult{};

    // Synthetic output is injected asynchronously by the caller-owned
    // InputInjector, never from inside this hook callback: SendInput can
    // block, and a blocked hook callback would freeze the message pump and
    // stall keyboard input system-wide.
    if (result.suppress_original) {
        return 1;
    }

    return CallNextHookEx(hook_, code, message, data);
}

PhysicalKey KeyboardHook::ToPhysicalKey(
    const KBDLLHOOKSTRUCT& data) noexcept {
    KeyPrefix prefix = KeyPrefix::None;

    if ((data.flags & LLKHF_EXTENDED) != 0) {
        prefix = KeyPrefix::E0;
    } else if (data.vkCode == VK_PAUSE) {
        prefix = KeyPrefix::E1;
    }

    return PhysicalKey{
        static_cast<std::uint16_t>(data.scanCode & 0xFFU),
        prefix,
    };
}

std::uint64_t KeyboardHook::TimestampMicros() const noexcept {
    LARGE_INTEGER counter{};
    if (QueryPerformanceCounter(&counter) == FALSE) {
        return 0;
    }

    const auto frequency =
        static_cast<std::uint64_t>(
            performance_frequency_.QuadPart);
    const auto value =
        static_cast<std::uint64_t>(counter.QuadPart);
    const auto seconds = value / frequency;
    const auto remainder = value % frequency;

    return seconds * 1'000'000ULL +
           (remainder * 1'000'000ULL) / frequency;
}

}  // namespace pckey::core
