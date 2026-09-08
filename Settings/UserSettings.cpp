#include "Settings/UserSettings.h"

#include <windows.h>

#include <algorithm>

namespace artthumb {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\ArtThumb\\Settings";

int ReadSetting(HKEY key, const wchar_t* name, int fallback, int minimum,
                int maximum) noexcept {
    DWORD value = 0;
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(&value), &bytes) != ERROR_SUCCESS ||
        type != REG_DWORD || bytes != sizeof(value)) return fallback;
    return std::clamp(static_cast<int>(value), minimum, maximum);
}

bool WriteSetting(HKEY key, const wchar_t* name, int value) noexcept {
    const DWORD stored = static_cast<DWORD>(value);
    return RegSetValueExW(key, name, 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&stored), sizeof(stored)) == ERROR_SUCCESS;
}

} // namespace

UserSettings LoadUserSettings() noexcept {
    UserSettings settings;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return settings;
    settings.sharpness = ReadSetting(key, L"Sharpness", kDefaultSharpness,
                                     kMinimumSharpness, kMaximumSharpness);
    RegCloseKey(key);
    return settings;
}

bool SaveUserSettings(const UserSettings& settings) noexcept {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &key, &disposition) != ERROR_SUCCESS) return false;
    const int sharpness = std::clamp(settings.sharpness,
                                     kMinimumSharpness, kMaximumSharpness);
    const bool success = WriteSetting(key, L"Sharpness", sharpness);
    RegCloseKey(key);
    return success;
}

} // namespace artthumb
