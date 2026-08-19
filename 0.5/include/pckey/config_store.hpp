#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pckey/profile.hpp"

namespace pckey {

inline constexpr std::size_t kMaximumProfiles = 128;
inline constexpr std::wstring_view kNormalModeName = L"普通模式";

struct Configuration {
    std::wstring active_profile{std::wstring(kNormalModeName)};
    std::vector<Profile> profiles;

    [[nodiscard]] Profile* FindProfile(
        std::wstring_view name) noexcept;

    [[nodiscard]] const Profile* FindProfile(
        std::wstring_view name) const noexcept;
};

class ConfigStore {
public:
    [[nodiscard]] static std::filesystem::path DefaultPath();
    [[nodiscard]] static std::filesystem::path DraftPath();

    static bool Load(
        const std::filesystem::path& path,
        Configuration& configuration,
        std::wstring& error);

    static bool SaveAtomic(
        const std::filesystem::path& path,
        const Configuration& configuration,
        std::wstring& error);
};

}  // namespace pckey
