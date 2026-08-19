#pragma once

#include <cstdint>

namespace pckey::ipc {

inline constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\PCkey.Core.v1";
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::uint32_t kMagic = 0x50434B59;  // "PCKY"
inline constexpr std::uint32_t kMaximumPayloadSize = 64 * 1024;
inline constexpr std::uintptr_t kReloadCopyDataId =
    0x50434B01U;
inline constexpr std::uintptr_t kKeyTestCopyDataId =
    0x50434B02U;
inline constexpr std::uint32_t kKeyTestEventMessage =
    0x8029U;
inline constexpr wchar_t kKeyTestWindowClass[] =
    L"PCkey.Editor.KeyTestWindow";

enum class KeyTestMode : std::uint32_t {
    None = 0,
    Physical = 1,
    Mapped = 2,
};

enum class KeyTestEventKind : std::uint8_t {
    PhysicalKeyboard = 1,
    MappedKeyboard = 2,
    VirtualKey = 3,
    MouseButton = 4,
    MouseMove = 5,
    MouseWheel = 6,
    NoImmediateOutput = 7,
};

struct KeyTestSubscription {
    std::uint32_t magic{kMagic};
    std::uint32_t version{kProtocolVersion};
    std::uintptr_t window_handle{};
    KeyTestMode mode{KeyTestMode::None};
};

[[nodiscard]] constexpr std::uintptr_t PackKeyTestHeader(
    const KeyTestEventKind kind,
    const std::uint8_t transition) noexcept {
    return static_cast<std::uintptr_t>(kind) |
           (static_cast<std::uintptr_t>(transition) << 8U);
}

[[nodiscard]] constexpr std::intptr_t PackKeyTestPair(
    const std::uint16_t first,
    const std::uint16_t second) noexcept {
    return static_cast<std::intptr_t>(
        static_cast<std::uint32_t>(first) |
        (static_cast<std::uint32_t>(second) << 16U));
}

enum class Command : std::uint32_t {
    Ping = 1,
    GetStatus = 2,
    ReloadConfiguration = 3,
    OpenEditor = 4,
};

struct Header {
    std::uint32_t magic{kMagic};
    std::uint32_t version{kProtocolVersion};
    Command command{Command::Ping};
    std::uint32_t payload_size{};
};

enum class Status : std::uint32_t {
    Ok = 0,
    InvalidRequest = 1,
    InvalidConfiguration = 2,
    InternalError = 3,
};

struct Response {
    std::uint32_t magic{kMagic};
    std::uint32_t version{kProtocolVersion};
    Status status{Status::Ok};
    std::uint32_t payload_size{};
};

}  // namespace pckey::ipc
