#pragma once

#include <Windows.h>

#include <array>
#include <functional>

#include "pckey/mapping_engine.hpp"

namespace pckey::core {

class KeyboardHook {
public:
    using Processor =
        std::function<ProcessResult(const KeyEvent&)>;

    KeyboardHook() = default;
    ~KeyboardHook();

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

    bool Install(Processor processor);
    void Uninstall() noexcept;

private:
    static LRESULT CALLBACK HookProcedure(
        int code,
        WPARAM message,
        LPARAM data);

    LRESULT HandleHook(
        int code,
        WPARAM message,
        LPARAM data);

    [[nodiscard]] static PhysicalKey ToPhysicalKey(
        const KBDLLHOOKSTRUCT& data) noexcept;

    [[nodiscard]] std::uint64_t TimestampMicros() const noexcept;

    static inline KeyboardHook* active_instance_{};

    HHOOK hook_{};
    Processor processor_{};
    std::array<bool, kPhysicalKeySlotCount> physical_down_{};
    LARGE_INTEGER performance_frequency_{};
};

}  // namespace pckey::core
