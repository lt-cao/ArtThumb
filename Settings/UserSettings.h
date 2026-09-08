#pragma once

namespace artthumb {

inline constexpr int kDefaultSharpness = 0;
inline constexpr int kMinimumSharpness = 0;
inline constexpr int kMaximumSharpness = 100;

struct UserSettings {
    int sharpness = kDefaultSharpness;
};

UserSettings LoadUserSettings() noexcept;
bool SaveUserSettings(const UserSettings& settings) noexcept;

} // namespace artthumb
