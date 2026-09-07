#pragma once

namespace artthumb {

inline constexpr int kDefaultBadgePercent = 22;
inline constexpr int kMinimumBadgePercent = 12;
inline constexpr int kMaximumBadgePercent = 40;
inline constexpr int kDefaultSharpness = 0;
inline constexpr int kMinimumSharpness = 0;
inline constexpr int kMaximumSharpness = 100;

struct UserSettings {
    int badgePercent = kDefaultBadgePercent;
    int sharpness = kDefaultSharpness;
};

UserSettings LoadUserSettings() noexcept;
bool SaveUserSettings(const UserSettings& settings) noexcept;

} // namespace artthumb
