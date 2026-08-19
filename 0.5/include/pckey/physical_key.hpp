#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

namespace pckey {

enum class KeyPrefix : std::uint8_t {
    None = 0,
    E0 = 1,
    E1 = 2,
};

struct PhysicalKey {
    std::uint16_t scan_code{};
    KeyPrefix prefix{KeyPrefix::None};

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return scan_code != 0 &&
               scan_code <= 0xFF &&
               prefix >= KeyPrefix::None &&
               prefix <= KeyPrefix::E1;
    }

    auto operator<=>(const PhysicalKey&) const = default;
};

inline constexpr std::size_t kScanCodesPerPrefix = 256;
inline constexpr std::size_t kPhysicalKeySlotCount =
    kScanCodesPerPrefix * 3;

[[nodiscard]] constexpr std::size_t ToKeyIndex(
    const PhysicalKey key) noexcept {
    return static_cast<std::size_t>(key.prefix) * kScanCodesPerPrefix +
           static_cast<std::size_t>(key.scan_code & 0xFFU);
}

[[nodiscard]] constexpr PhysicalKey FromKeyIndex(
    const std::size_t index) noexcept {
    return PhysicalKey{
        static_cast<std::uint16_t>(index % kScanCodesPerPrefix),
        static_cast<KeyPrefix>(index / kScanCodesPerPrefix),
    };
}

enum class KeyTransition : std::uint8_t {
    Press,
    Repeat,
    Release,
};

struct KeyEvent {
    PhysicalKey key{};
    KeyTransition transition{KeyTransition::Press};
    std::uint64_t timestamp_us{};
};

}  // namespace pckey
